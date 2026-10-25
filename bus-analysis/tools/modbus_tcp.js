/* Bounded Modbus/TCP ADU study decoder shared by browser and CLI.
 * One complete application message, not a PCAP/TCP stream or device client.
 * Byte consistency never proves connection identity, freshness or physical state.
 * Modbus Application Protocol v1.1b3 §§4,6.1,6.3,6.5,6.6,7;
 * Messaging Implementation Guide v1.0b §3.1.2 (MBAP). */
(function(root){
 'use strict';
 const word=(b,p)=>b[p]*256+b[p+1];
 const hex=b=>Array.from(b,x=>x.toString(16).padStart(2,'0')).join(' ');
 const names={1:'Read coils',3:'Read holding registers',5:'Write single coil',6:'Write single register'};
 const exceptions={1:'Illegal function',2:'Illegal data address',3:'Illegal data value',4:'Server device failure',5:'Acknowledge',6:'Server device busy',8:'Memory parity error',10:'Gateway path unavailable',11:'Gateway target failed to respond'};
 function fromHex(value){
  if(typeof value!=='string'||value.length>520||value.length%2||!/^[0-9a-f]*$/i.test(value))throw Error('Supply contiguous hexadecimal for one ADU of at most 260 bytes');
  return Uint8Array.from(value.match(/../g)||[],x=>parseInt(x,16));
 }
 function field(b,label,offset,length,value,help,extra={}){
  return {label,offset,length,hex:hex(b.slice(offset,offset+length)),value,help,...extra};
 }
 function envelope(b){
  if(!(b instanceof Uint8Array)||b.length>260)throw Error('Supply one complete ADU of at most 260 bytes');
  const r={status:'invalid',reason:'Incomplete application message',hex:hex(b),fields:[],values:null};
  if(b.length<8)return r;
  r.fields=[
   field(b,'Transaction',0,2,word(b,0),'Pairs a reply with a request on the same TCP connection. It is reused; it is not a global event identity.'),
   field(b,'Protocol identifier',2,2,word(b,2),'Zero identifies Modbus in this header.'),
   field(b,'Remaining length',4,2,word(b,4),'Counts bytes starting at the unit identifier, including function and data. It excludes these first six bytes.'),
   field(b,'Unit identifier',6,1,b[6],'Selects the addressed unit, including through a gateway. It is separate from the IP address and provides no authentication.'),
   field(b,'Function',7,1,b[7],names[b[7]]||((b[7]&128)?'Exception response: high bit set on the requested function.':'Outside this decoder\'s four-function scope.'))
  ];
  if(word(b,2)!==0){r.reason='Nonzero Modbus protocol identifier';return r;}
  if(word(b,4)<2||word(b,4)!==b.length-6){r.reason='Length mismatch: supply exactly one complete ADU, without truncation or trailing bytes';return r;}
  Object.assign(r,{status:'envelope',transaction:word(b,0),unit:b[6],function:b[7]});return r;
 }
 function request(b){
  const r=envelope(b);if(r.status!=='envelope')return r;
  r.status='invalid';
  if(!names[r.function]){r.status='unsupported';r.reason='Only FC01, FC03, FC05 and FC06 are supported';return r;}
  if(b.length!==12){r.reason='This request must contain exactly 12 ADU bytes';return r;}
  const address=word(b,8),value=word(b,10),read=[1,3].includes(r.function);
  r.fields.push(field(b,read?'Start address':'Address',8,2,address,'Zero-based wire address, high byte first. A displayed register label may use another numbering convention.',{address}));
  r.fields.push(field(b,read?'Quantity':'Write value',10,2,value,read?'Number of points requested, not a measurement or an ending address.':r.function===5?'FF00 requests ON; 0000 requests OFF. A request is not a measured contact.':'One raw 16-bit word. Meaning and units require the exact device/program map.'));
  if(read&&(value<1||value>(r.function===1?2000:125)||address+value>65536)){r.reason='Invalid quantity or address range';return r;}
  if(r.function===5&&![0,65280].includes(value)){r.reason='Single-coil write must be FF00 or 0000';return r;}
  Object.assign(r,{status:'valid',reason:'Complete supported request bytes; no network or physical claim',address,...(read?{count:value}:{value})});return r;
 }
 function reply(b,requestBytes){
  const q=request(requestBytes),r=envelope(b);
  if(q.status!=='valid'){r.status='invalid';r.reason='A valid supported request is required';return r;}
  if(!b.length){r.status='unavailable';r.reason='No reply supplied; a missing reply is not a zero measurement';return r;}
  if(r.status!=='envelope')return r;
  r.status='invalid';
  if(r.transaction!==q.transaction||r.unit!==q.unit){r.reason='Transaction or unit differs from the supplied request';return r;}
  if(r.function===(q.function|128)){
   if(b.length!==9){r.reason='Exception response must have exactly 9 ADU bytes';return r;}
   r.fields.push(field(b,'Exception code',8,1,b[8],exceptions[b[8]]||'Unrecognized exception code; do not infer a normal value.'));
   r.status=exceptions[b[8]]?'exception':'unsupported';r.reason=exceptions[b[8]]||'Unknown exception';r.exception=b[8];return r;
  }
  if(r.function!==q.function){r.reason='Function differs from the supplied request';return r;}
  if([5,6].includes(q.function)){
   if(b.length!==12||word(b,8)!==q.address||word(b,10)!==q.value){r.reason='Write echo differs from the request address or value';return r;}
   r.fields.push(field(b,'Echoed address',8,2,q.address,'The acknowledged wire address, not evidence of process movement.',{address:q.address}));
   r.fields.push(field(b,'Echoed value',10,2,q.value,'The server echoed the write. Check controller logic and independent feedback to determine its effect.'));
   r.status='valid';r.reason='Matching write echo; no independent process observation';return r;
  }
  const count=q.function===3?q.count*2:Math.ceil(q.count/8);
  if(b.length!==9+count||b[8]!==count){r.reason='Reply byte count/length differs from the requested quantity';return r;}
  if(q.function===1&&q.count%8&&(b[b.length-1]>>(q.count%8))!==0){r.reason='Unused high coil bits must be zero';return r;}
  r.fields.push(field(b,'Data byte count',8,1,b[8],'Counts only the following data bytes; a register occupies two bytes, while coil states are packed into bits.'));
  r.values=[];
  for(let i=0;i<q.count;i++){
   const register=q.function===3,offset=9+(register?2*i:Math.floor(i/8));
   const value=register?word(b,offset):(b[offset]>>(i%8))&1;r.values.push(value);
   r.fields.push(field(b,`${register?'Register':'Coil'} ${q.address+i}`,offset,register?2:1,value,
    register?'High byte first (big-endian). This is an unsigned raw word; signedness, scale and engineering units come from the selected device map.':`Bit ${i%8} of this byte; coils start at the least-significant bit. This is a reported point, not an independent physical contact.`,
    {address:q.address+i,...(register?{}:{bit:i%8})}));
  }
  r.status='valid';r.reason='Complete reply matches the supplied request bytes; verify TCP connection and source separately';return r;
 }
 const api={request,reply,fromHex};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.TBDModbusTCP=api;
})(typeof globalThis!=='undefined'?globalThis:this);
