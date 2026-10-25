#pragma once
#include "display_capture_core.h"
#include <cstring>
namespace display_capture {
enum class CommandKind { Request, Manifest, Read, Cancel };
struct Command {CommandKind kind=CommandKind::Request;Ticket ticket{};size_t offset=0,length=0;};
inline bool hex(const char* s,size_t n,uint64_t& value){
 if(!s||std::strlen(s)!=n)return false;value=0;
 for(size_t i=0;i<n;++i){unsigned c=static_cast<unsigned char>(s[i]);unsigned v;
  if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10;else return false;
  value=(value<<4)|v;
 }return true;
}
inline bool number(const char* s,size_t& value){
 if(!s||!*s)return false;value=0;
 for(;*s;++s){if(*s<'0'||*s>'9'||value>MaxBytes/10)return false;value=value*10+(*s-'0');if(value>MaxBytes)return false;}return true;
}
inline bool parse(const char* line,Command& out){
 if(!line||std::strlen(line)>=128)return false;
 char text[128];std::strcpy(text,line);char* tokens[6]{};size_t count=0;char* p=text;
 while(*p){if(count==6)return false;tokens[count++]=p;while(*p&&*p!=' ')++p;if(*p){*p++=0;if(!*p||*p==' ')return false;}}
 if(count<2||std::strcmp(tokens[0],"screen"))return false;
 Command c;
 if(!std::strcmp(tokens[1],"request")){if(count!=2)return false;out=c;return true;}
 if(!std::strcmp(tokens[1],"manifest"))c.kind=CommandKind::Manifest;
 else if(!std::strcmp(tokens[1],"read"))c.kind=CommandKind::Read;
 else if(!std::strcmp(tokens[1],"cancel"))c.kind=CommandKind::Cancel;
 else return false;
 if(count!=(c.kind==CommandKind::Read?6u:4u)||std::strlen(tokens[2])!=32)return false;
 for(unsigned i=0;i<16;++i){char pair[3]={tokens[2][2*i],tokens[2][2*i+1],0};uint64_t v;if(!hex(pair,2,v))return false;c.ticket.boot[i]=v;}
 if(!hex(tokens[3],16,c.ticket.sequence)||!c.ticket.sequence)return false;
 if(c.kind==CommandKind::Read&&(!number(tokens[4],c.offset)||!number(tokens[5],c.length)||!c.length||c.length>MaxChunk||c.offset>MaxBytes||c.length>MaxBytes-c.offset))return false;
 out=c;return true;
}
inline const char* result_name(Result r){switch(r){case Result::Ok:return "ok";case Result::Unavailable:return "unavailable";case Result::Busy:return "busy";case Result::WrongFrame:return "wrong_frame";case Result::Expired:return "expired";case Result::NotReady:return "not_ready";default:return "invalid";}}
}
