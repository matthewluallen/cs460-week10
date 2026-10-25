#!/usr/bin/env node
// Identical decoder to the offline classroom UI; no broker or device control.
const fs=require('node:fs'),D=require('./dnp3'),M=require('./mqtt');
const [protocol,file,requestFile]=process.argv.slice(2);
if(!['dnp3','mqtt','rtu-request','rtu-reply','modbus-request','modbus-reply'].includes(protocol)||!file||(['rtu-reply','modbus-reply'].includes(protocol)&&!requestFile)){console.error('Usage: node student/protocols/decode.js dnp3|mqtt capture.pcap OR rtu-request|modbus-request message.bin OR rtu-reply|modbus-reply reply.bin request.bin');process.exit(2);}
try{
 if(protocol.startsWith('rtu-')||protocol.startsWith('modbus-')){
  const tcp=protocol.startsWith('modbus-'),R=require(tcp?'./modbus_tcp':'./modbus_rtu'),limit=tcp?260:256;
  const read=p=>{if(fs.statSync(p).size>limit)throw Error(`Message exceeds ${limit} bytes`);return fs.readFileSync(p);};
  const result=protocol.endsWith('-request')?R.request(read(file)):R.reply(read(file),read(requestFile));
  console.log(JSON.stringify(result,null,2));
  if(!['valid','exception'].includes(result.status))process.exitCode=1;
 }else{
  if(fs.statSync(file).size>2*1024*1024)throw Error('Capture exceeds 2 MiB');
  console.log(JSON.stringify(D.parsePcap(fs.readFileSync(file),protocol==='mqtt'?M.decode:D.decode,protocol),null,2));
 }
}catch(e){console.error(e.message);process.exit(1);}
