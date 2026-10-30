#include "network_access.h"
#include "proto_common.h"
#include "opcua_server.h"
#include "plc_core.h"          // plc_log / SRC_MODBUS
#include <string.h>
#include "eth_net.h"
#ifndef ENABLE_ETHERNET
  #include <WiFi.h>
#endif

OpcUaServer opcua_server;

static const char SEC_NONE[] = "http://opcfoundation.org/UA/SecurityPolicy#None";
static const char XPORT[]    = "http://opcfoundation.org/UA-Profile/Transport/uatcp-uasc-uabinary";
static const char APP_URI[]  = "urn:CYBATI:StamPLC:OpenPLC";
static const char PROD_URI[] = "urn:cybati.io:StamPLC";
static const char APP_NAME[] = "CYBATI StamPLC OpenPLC Server";

// ---- little-endian OPC-UA binary encoders (write into out at index n) --------
static void e32(uint8_t* b, int& n, uint32_t v) { b[n++]=v&0xFF; b[n++]=(v>>8)&0xFF; b[n++]=(v>>16)&0xFF; b[n++]=(v>>24)&0xFF; }
static void e16(uint8_t* b, int& n, uint16_t v) { b[n++]=v&0xFF; b[n++]=(v>>8)&0xFF; }
static void e64z(uint8_t* b, int& n)            { for (int i=0;i<8;i++) b[n++]=0; }
static void estr(uint8_t* b, int& n, const char* s) {
    if (!s) { e32(b,n,0xFFFFFFFFu); return; }                 // null String
    int L = strlen(s); e32(b,n,(uint32_t)L); memcpy(b+n,s,L); n+=L;
}
static void enull_bs(uint8_t* b, int& n)  { e32(b,n,0xFFFFFFFFu); }   // null ByteString
static void eempty_bs(uint8_t* b, int& n) { e32(b,n,0); }             // empty ByteString
static void enid4(uint8_t* b, int& n, uint8_t ns, uint16_t id) { b[n++]=0x01; b[n++]=ns; e16(b,n,id); }
static void eextobj_null(uint8_t* b, int& n) { b[n++]=0x00; b[n++]=0x00; b[n++]=0x00; }
static void eltext(uint8_t* b, int& n, const char* t) { b[n++]=0x02; estr(b,n,t); }   // mask 0x02 = Text only
static void eresp_header(uint8_t* b, int& n, uint32_t rh) {
    e64z(b,n);            // Timestamp (DateTime)
    e32(b,n,rh);          // RequestHandle
    e32(b,n,0);           // ServiceResult (Good)
    b[n++]=0x00;          // ServiceDiagnostics (none)
    e32(b,n,0);           // StringTable (empty array)
    eextobj_null(b,n);    // AdditionalHeader
}
static void finalize(uint8_t* out, int n) {   // backfill MessageSize at bytes 4-7
    out[4]=n&0xFF; out[5]=(n>>8)&0xFF; out[6]=(n>>16)&0xFF; out[7]=(n>>24)&0xFF;
}
static uint32_t rd32(const uint8_t* b, int p) {
    return (uint32_t)b[p] | ((uint32_t)b[p+1]<<8) | ((uint32_t)b[p+2]<<16) | ((uint32_t)b[p+3]<<24);
}
static int nid_len(const uint8_t* b) {         // length of an encoded NodeId
    switch (b[0]) { case 0x00: return 2; case 0x01: return 4; case 0x02: return 7; default: return 2; }
}

OpcUaServer::OpcUaServer() : server(OPCUA_TCP_PORT) { s_seq=1; s_endpoint[0]=0; cur_src_ip[0]=0; }

bool OpcUaServer::start() {
    net_listen_begin(server);  // (was a copy-pasted 3x begin() retry; see proto_common.h)
#ifdef ENABLE_ETHERNET
    String ips = eth_ip();
#else
    String ips = WiFi.localIP().toString();
#endif
    snprintf(s_endpoint, sizeof(s_endpoint), "opc.tcp://%s:%d", ips.c_str(), OPCUA_TCP_PORT);
    Serial.printf("[OPCUA] discovery responder on TCP %d (%s) - SecurityMode None, Anonymous\n",
                  OPCUA_TCP_PORT, s_endpoint);
    return true;
}

int OpcUaServer::build_ack(uint8_t* out) {
    int n=0; out[n++]='A'; out[n++]='C'; out[n++]='K'; out[n++]='F'; e32(out,n,0);
    e32(out,n,0); e32(out,n,65536); e32(out,n,65536); e32(out,n,0); e32(out,n,0);
    finalize(out,n); return n;
}

int OpcUaServer::build_opn_resp(uint8_t* out, uint32_t reqid, uint32_t reqhandle, uint32_t seq) {
    int n=0; out[n++]='O'; out[n++]='P'; out[n++]='N'; out[n++]='F'; e32(out,n,0);
    e32(out,n,1);                                    // SecureChannelId
    estr(out,n,SEC_NONE); enull_bs(out,n); enull_bs(out,n);   // asymmetric security header
    e32(out,n,seq); e32(out,n,reqid);                // sequence header
    enid4(out,n,0,449);                              // OpenSecureChannelResponse
    eresp_header(out,n,reqhandle);
    e32(out,n,0);                                    // ServerProtocolVersion
    e32(out,n,1); e32(out,n,1); e64z(out,n); e32(out,n,3600000);   // ChannelSecurityToken
    eempty_bs(out,n);                                // ServerNonce
    finalize(out,n); return n;
}

int OpcUaServer::build_gep_resp(uint8_t* out, uint32_t reqid, uint32_t reqhandle, uint32_t seq) {
    int n=0; out[n++]='M'; out[n++]='S'; out[n++]='G'; out[n++]='F'; e32(out,n,0);
    e32(out,n,1); e32(out,n,1);                      // SecureChannelId, TokenId (symmetric header)
    e32(out,n,seq); e32(out,n,reqid);                // sequence header
    enid4(out,n,0,431);                              // GetEndpointsResponse
    eresp_header(out,n,reqhandle);
    e32(out,n,1);                                    // Endpoints: array of 1
    estr(out,n,s_endpoint);                          // EndpointUrl
    // ApplicationDescription
    estr(out,n,APP_URI); estr(out,n,PROD_URI); eltext(out,n,APP_NAME);
    e32(out,n,0);                                    // ApplicationType = Server
    estr(out,n,NULL); estr(out,n,NULL);              // GatewayServerUri, DiscoveryProfileUri
    e32(out,n,1); estr(out,n,s_endpoint);            // DiscoveryUrls: [endpoint]
    enull_bs(out,n);                                 // ServerCertificate
    e32(out,n,1);                                    // MessageSecurityMode = None
    estr(out,n,SEC_NONE);                            // SecurityPolicyUri
    e32(out,n,1);                                    // UserIdentityTokens: array of 1
    estr(out,n,"anonymous"); e32(out,n,0); estr(out,n,NULL); estr(out,n,NULL); estr(out,n,NULL);
    estr(out,n,XPORT);                               // TransportProfileUri
    out[n++]=0x00;                                   // SecurityLevel
    finalize(out,n); return n;
}

void OpcUaServer::handle() {
    NetClient client = server.available();
    if (!client) return;
    if (!plc_project_host_allowed(plc_network_ipv4(client.remoteIP()))) { client.stop(); return; }
    if (!client.connected()) { client.stop(); return; }
    if (!client.available()) return;

    uint8_t buf[1024];
    size_t want = client.available(); if (want > sizeof(buf)) want = sizeof(buf);
    int len = client.readBytes(buf, want);
    if (len < 8) return;
    client.remoteIP().toString().toCharArray(cur_src_ip, sizeof(cur_src_ip));

    uint8_t out[512]; int ol = 0;

    if (buf[0]=='H' && buf[1]=='E' && buf[2]=='L') {
        s_seq = 1;
        ol = build_ack(out);
        plc_log(SRC_OPCUA, cur_src_ip, "OPC-UA Hello (opc.tcp handshake)");
    } else if (buf[0]=='O' && buf[1]=='P' && buf[2]=='N') {
        // Bounds-checked parse: a malformed OPN must never advance p past len (every
        // length below is attacker-controlled). On any overrun we leave ol=0 (no reply).
        int p = 12;                                            // 8 hdr + 4 SecureChannelId
        bool ok = true;
        for (int f = 0; f < 3 && ok; f++) {                    // SecurityPolicyUri, SenderCert, ReceiverThumbprint
            if (p + 4 > len) { ok = false; break; }
            uint32_t l = rd32(buf,p); p += 4;
            if (l != 0xFFFFFFFFu) { if (l > (uint32_t)(len - p)) { ok = false; break; } p += (int)l; }
        }
        if (ok && p + 8 <= len) {                              // SequenceNumber(4) + RequestId(4)
            p += 4;
            uint32_t reqid = rd32(buf,p); p += 4;
            if (p < len) p += nid_len(buf+p);                  // TypeId
            if (p < len) p += nid_len(buf+p);                  // AuthenticationToken
            p += 8;                                            // Timestamp
            uint32_t reqhandle = (p >= 0 && p+4 <= len) ? rd32(buf,p) : 1;
            ol = build_opn_resp(out, reqid, reqhandle, s_seq++);
            plc_log(SRC_OPCUA, cur_src_ip, "OPC-UA OpenSecureChannel (SecurityPolicy None)");
        }
    } else if (buf[0]=='M' && buf[1]=='S' && buf[2]=='G') {
        int p = 16;                                            // 8 hdr + channel + tokenid
        if (p + 8 <= len) {                                    // SequenceNumber(4) + RequestId(4)
            p += 4;
            uint32_t reqid = rd32(buf,p); p += 4;
            uint16_t typeid_num = (p + 4 <= len && buf[p]==0x01) ? (uint16_t)(buf[p+2] | (buf[p+3]<<8)) : 0;
            if (p < len) p += nid_len(buf+p);                  // TypeId
            if (p < len) p += nid_len(buf+p);                  // AuthenticationToken
            p += 8;                                            // Timestamp
            uint32_t reqhandle = (p >= 0 && p+4 <= len) ? rd32(buf,p) : 1;
            if (typeid_num == 428) {                           // GetEndpointsRequest
                ol = build_gep_resp(out, reqid, reqhandle, s_seq++);
                plc_log(SRC_OPCUA, cur_src_ip, "OPC-UA GetEndpoints (server enumeration)");
            }
        }
    } else if (buf[0]=='C' && buf[1]=='L' && buf[2]=='O') {
        client.stop(); return;
    }
    if (ol > 0) client.write(out, ol);
}
