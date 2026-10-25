// Bounded, closed configuration codec; no persistence or hardware access.
#pragma once
#include "process_bindings_core.h"
#include <ArduinoJson.h>
namespace bindings {
constexpr size_t JsonCapacity=2048;
inline bool network_host(const char* text,uint8_t (&ip)[4]) {
    if(!text)return false;
    for(unsigned i=0;i<4;++i) {
        unsigned n=0,digits=0;const char* start=text;
        while(*text>='0'&&*text<='9') {n=n*10+unsigned(*text++-'0');if(++digits>3||n>255)return false;}
        if(!digits||(digits>1&&*start=='0'))return false;
        ip[i]=uint8_t(n);
        if(i<3){if(*text++!='.')return false;}else if(*text)return false;
    }
    return true;
}
inline bool device_id(const char* text,uint64_t& out) {
    if(!text||strlen(text)!=16)return false;out=0;
    for(unsigned i=0;i<16;++i){char c=text[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
        out=(out<<4)|unsigned(c<='9'?c-'0':c-'a'+10);}
    return out!=0;
}
inline bool decode(const char* data,size_t size,Config& out) {
    if(!size||size>=JsonCapacity)return false;
    // No escapes are needed by this schema's ASCII identifiers. Reject them and
    // NULs explicitly; counting colons below also detects overwritten duplicate keys.
    size_t colons=0;for(size_t i=0;i<size;++i){if(!data[i]||data[i]=='\\')return false;if(data[i]==':')++colons;}
    struct Reader {const char* p;size_t n,pos=0;int read(){return pos<n?uint8_t(p[pos++]):-1;}
        size_t readBytes(char* dst,size_t nread){size_t i=0;while(i<nread&&pos<n)dst[i++]=p[pos++];return i;}} reader{data,size};
    StaticJsonDocument<4096> doc;
    if(deserializeJson(doc,reader))return false;
    while(reader.pos<size){int c=reader.read();if(c!=' '&&c!='\n'&&c!='\r'&&c!='\t')return false;}
    if(!doc.is<JsonObject>()||doc.size()!=2||!doc["schema"].is<unsigned>()||
        (doc["schema"].as<unsigned>()!=1&&doc["schema"].as<unsigned>()!=2)||!doc["inputs"].is<JsonArray>()||doc["inputs"].size()!=Count)return false;
    Config staging;staging.schema=doc["schema"];size_t keys=2;
    for(unsigned i=0;i<Count;++i) {
        JsonVariant v=doc["inputs"][i];if(v.isNull())continue;
        if(!v.is<JsonObject>())return false;auto& a=staging.inputs[i];a.configured=true;
        for(const char* key:{"role","driver","field"})if(!v[key].is<const char*>())return false;
        const char* role=v["role"],*driver=v["driver"],*field=v["field"];
        if(strlen(role)>=sizeof(a.role)||strlen(driver)>=sizeof(a.driver))return false;
        strcpy(a.role,role);strcpy(a.driver,driver);
        unsigned f=0;for(;f<unsigned(Field::Count);++f)if(!strcmp(field,Fields[f].name))break;
        if(f==unsigned(Field::Count))return false;a.field=Field(f);
        if(!strcmp(driver,"modbus-sensor-v1")) {
            if(staging.schema!=2 || v.size()!=11)return false;keys+=11;a.network=true;
            for(const char* key:{"host","device_id"})if(!v[key].is<const char*>())return false;
            for(const char* key:{"port","address"})if(!v[key].is<uint16_t>())return false;
            for(const char* key:{"poll_ms","timeout_ms","max_age_ms"})if(!v[key].is<uint32_t>())return false;
            if(!v["unit"].is<uint8_t>() || !network_host(v["host"],a.remote.host) || !device_id(v["device_id"],a.remote.device))return false;
            a.remote.port=v["port"];a.remote.address=v["address"];a.remote.unit=v["unit"];
            a.remote.poll_ms=v["poll_ms"];a.remote.timeout_ms=v["timeout_ms"];a.remote.max_age_ms=v["max_age_ms"];a.remote.field=f;
            continue;
        }
        if(v.size()!=10)return false;keys+=10;
        for(const char* key:{"bus","mux","address"})if(!v[key].is<uint8_t>())return false;
        for(const char* key:{"part","revision"})if(!v[key].is<uint16_t>())return false;
        if(!v["channel"].is<int8_t>()||!v.containsKey("serial"))return false;
        a.bus=v["bus"];a.mux=v["mux"];a.address=v["address"];a.channel=v["channel"];a.part=v["part"];a.revision=v["revision"];
        if(!v["serial"].isNull()) {
            if(!v["serial"].is<const char*>())return false;const char* serial=v["serial"];
            const unsigned width=!strcmp(a.driver,"mlx90614-candidate")?16:12;
            if(strlen(serial)!=width)return false;a.unique_serial=true;
            for(unsigned j=0;j<width;++j){char c=serial[j];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
                a.serial=(a.serial<<4)|unsigned(c<='9'?c-'0':c-'a'+10);}
        }
    }
    if(colons!=keys||!valid(staging))return false;out=staging;return true;
}
inline void write_config(JsonObject d,const Config& c) {
    d["schema"]=c.schema;auto inputs=d.createNestedArray("inputs");
    for(const auto& a:c.inputs) {
        if(!a.configured){inputs.add(nullptr);continue;}auto v=inputs.createNestedObject();
        if(a.network) {
            v["role"]=a.role;v["driver"]=a.driver;v["field"]=Fields[unsigned(a.field)].name;
            char host[16],id[17];snprintf(host,sizeof(host),"%u.%u.%u.%u",a.remote.host[0],a.remote.host[1],a.remote.host[2],a.remote.host[3]);
            snprintf(id,sizeof(id),"%016llx",(unsigned long long)a.remote.device);
            v["host"]=host;v["port"]=a.remote.port;v["unit"]=a.remote.unit;v["address"]=a.remote.address;v["device_id"]=id;
            v["poll_ms"]=a.remote.poll_ms;v["timeout_ms"]=a.remote.timeout_ms;v["max_age_ms"]=a.remote.max_age_ms;continue;
        }
        v["role"]=a.role;v["bus"]=a.bus;v["mux"]=a.mux;v["channel"]=a.channel;v["address"]=a.address;
        v["driver"]=a.driver;v["part"]=a.part;v["revision"]=a.revision;
        if(a.unique_serial){char serial[17];snprintf(serial,sizeof(serial),!strcmp(a.driver,"mlx90614-candidate")?"%016llx":"%012llx",(unsigned long long)a.serial);v["serial"]=serial;}else v["serial"]=nullptr;
        v["field"]=Fields[unsigned(a.field)].name;
    }
}
inline size_t encode(const Config& c,char* out,size_t cap) {
    if(!valid(c))return 0;
    StaticJsonDocument<4096> d;write_config(d.to<JsonObject>(),c);
    size_t size=measureJson(d);if(d.overflowed()||size+1>cap)return 0;
    return serializeJson(d,out,cap);
}
}
