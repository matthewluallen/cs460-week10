#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H
#include <Arduino.h>
#include "net_types.h"
#include "config.h"

// OPC-UA discovery / fingerprint responder — on-device enumeration target (LS-21).
// Implements the OPC-UA binary handshake a scanner (opcua-client, nmap, UaExpert)
// uses to identify a server on opc.tcp TCP 4840: Hello->Acknowledge,
// OpenSecureChannel (SecurityPolicy #None) and GetEndpoints -> a single endpoint
// advertising SecurityMode None + Anonymous user token — the "secure-by-design but
// misconfigured" lesson (no encryption, no auth). The ApplicationDescription
// (ApplicationUri / ProductUri / ApplicationName) is the fingerprint identity;
// teaching strings only (no real-vendor impersonation). Discovery subset only:
// no session, read or subscription services.
class OpcUaServer {
public:
    OpcUaServer();
    bool start();
    void handle();
private:
    NetServer server;
    uint32_t s_seq;
    char     s_endpoint[52];
    char     cur_src_ip[24];
    int build_ack(uint8_t* out);
    int build_opn_resp(uint8_t* out, uint32_t reqid, uint32_t reqhandle, uint32_t seq);
    int build_gep_resp(uint8_t* out, uint32_t reqid, uint32_t reqhandle, uint32_t seq);
};
extern OpcUaServer opcua_server;
#endif // OPCUA_SERVER_H
