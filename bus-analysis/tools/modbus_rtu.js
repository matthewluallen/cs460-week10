/* Bounded FC03 study decoder, shared by the notebook and CLI. No device I/O.
 * A whole supplied frame is checked; never search for a CRC-valid substring.
 * This is a byte-level interpretation, not UART timing or sender authentication. */
(function(root){
 'use strict';
 const hex=b=>Array.from(b,x=>x.toString(16).padStart(2,'0')).join(' ');
 function crc16(b){let c=0xffff;for(const v of b){c^=v;for(let i=0;i<8;i++)c=c&1?(c>>>1)^0xa001:c>>>1;}return c;}
 function bytes(v){if(!(v instanceof Uint8Array)||v.length>256)throw Error('Supply one frame of at most 256 bytes');return v;}
 function field(b,label,offset,length,value,help){return {label,offset,length,hex:hex(b.slice(offset,offset+length)),value,help};}
 function initial(v){const b=bytes(v);return {status:'invalid',reason:'Incomplete frame',hex:hex(b),fields:[],values:null,crc_ok:false};}
 function envelope(b,r){
  if(b.length<5)return false;
  r.fields.push(field(b,'Unit address',0,1,b[0],'Identifies a server on this serial bus, not an IP address or authenticated identity.'));
  r.fields.push(field(b,'Function',1,1,b[1],'03 reads holding registers; 83 is the exception response to 03.'));
  const got=b[b.length-2]|b[b.length-1]<<8;
  r.crc_ok=got===crc16(b.slice(0,-2));
  r.crc_field=field(b,'CRC',b.length-2,2,got,'Cyclic redundancy check: low byte first. Detects many transmission errors; provides no authentication or freshness.');
  return true;
 }
 function finish(r){if(r.crc_field){r.fields.push(r.crc_field);delete r.crc_field;}return r;}
 function request(v){
  const b=bytes(v),r=initial(b);if(!envelope(b,r))return r;
  if(!r.crc_ok){r.reason='CRC mismatch';return finish(r);}
  if(b[1]!==3){r.status='unsupported';r.reason='This study decoder supports FC03 only';return finish(r);}
  if(b.length!==8){r.reason='FC03 request must be exactly 8 bytes';return finish(r);}
  const start=b[2]*256+b[3],count=b[4]*256+b[5];
  r.fields.push(field(b,'Start register',2,2,start,'Zero-based protocol address, high byte first. 00 19 is hexadecimal 25, not a value of 19 decimal.'));
  r.fields.push(field(b,'Register count',4,2,count,'How many 16-bit holding registers to read; each register occupies two reply data bytes.'));
  if(b[0]<1||b[0]>247||count<1||count>125||start+count>65536){r.reason='Invalid unicast address or register range';return finish(r);}
  Object.assign(r,{status:'valid',reason:'Valid FC03 request bytes',unit:b[0],function:3,start,count});return finish(r);
 }
 function reply(v,requestBytes){
  const b=bytes(v),r=initial(b),q=request(requestBytes);
  if(q.status!=='valid'){r.reason='A valid matching request is required';return r;}
  if(!b.length){r.status='unavailable';r.reason='No reply bytes supplied';return r;}
  if(!envelope(b,r))return r;
  if(!r.crc_ok){r.reason='CRC mismatch';return finish(r);}
  if(b[0]!==q.unit){r.reason='Reply unit does not match the request';return finish(r);}
  if(b[1]===0x83){
   if(b.length!==5||![1,2,3,4,5,6,8,10,11].includes(b[2])){r.reason='Malformed exception';return finish(r);}
   r.fields.push(field(b,'Exception code',2,1,b[2],'The server rejected or could not complete the operation. This is not a register value.'));
   r.status='exception';r.reason='Matched exception response';return finish(r);
  }
  if(b[1]!==3){r.reason='Reply function does not match the request';return finish(r);}
  if(b[2]!==q.count*2||b.length!==5+q.count*2){r.reason='Byte count or complete-frame length does not match the request';return finish(r);}
  r.fields.push(field(b,'Data byte count',2,1,b[2],'Counts data bytes only; excludes unit, function, this count and CRC.'));
  r.values=[];
  for(let i=0;i<q.count;i++){
   const val=b[3+2*i]*256+b[4+2*i];r.values.push(val);
   r.fields.push(field(b,`Register ${q.start+i}`,3+2*i,2,val,'High byte first. Meaning, signedness and scale require the map for this exact firmware; a number is not independent process truth.'));
  }
  r.status='valid';r.reason='Reply matches request, length and CRC; no independent process observation';return finish(r);
 }
 function timing(baud,bits){
  if(!Number.isInteger(baud)||baud<1||baud>1000000||![10,11].includes(bits))throw Error('Declared baud and 10/11 bits per character required');
  return {assumed_baud:baud,assumed_bits_per_character:bits,character_ms:1000*bits/baud,
   eight_byte_wire_ms:8000*bits/baud,three_point_five_char_ms:3500*bits/baud,measured:false};
 }
 const api={request,reply,crc16,timing};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.TBDRTU=api;
})(typeof globalThis!=='undefined'?globalThis:this);
