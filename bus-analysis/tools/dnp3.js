/* TBD Protocol Studio: shared browser/CLI decoder. No network or device I/O. */
(function (root) {
  'use strict';
  const FUNCTIONS = {0:'CONFIRM',1:'READ',2:'WRITE',3:'SELECT',4:'OPERATE',5:'DIRECT OPERATE',6:'DIRECT OPERATE NR',13:'COLD RESTART',129:'RESPONSE',130:'UNSOLICITED RESPONSE'};
  const hex = n => Number(n).toString(16).padStart(2,'0').toUpperCase();
  const PRIMER='https://www.dnp.org/Portals/0/AboutUs/DNP3%20Primer%20Rev%20A.pdf';
  const VALIDATION='https://www.dnp.org/Portals/0/Public%20Documents/DNP3%20AN2013-004b%20Validation%20of%20Incoming%20DNP3%20Data.pdf';
  const VARIATIONS='https://docs.stepfunc.io/dnp3/1.6.0/java/io/stepfunc/dnp3/Variation.html';
  const GROUPS={1:'Binary input snapshot',2:'Binary input event',10:'Binary output status',12:'Binary output command',20:'Counter snapshot',30:'Analog input snapshot',52:'Time delay',60:'Class data request'};
  const FORMATS={'1:2':'One flag byte contains the on/off state and quality indicators.',
    '2:1':'Binary change/event with flags and no event timestamp in this variation.',
    '10:2':'Reported binary output state with flags; this is separate from an input contact proving equipment position.',
    '12:1':'Control Relay Output Block (CROB): action, repetition count, on/off durations and command status.',
    '20:1':'A quality byte followed by a 32-bit unsigned counter.',
    '30:1':'A quality byte followed by a 32-bit signed integer measurement.',
    '30:2':'A quality byte followed by a 16-bit signed integer measurement.',
    '52:2':'A 16-bit unsigned time delay in milliseconds; interpretation depends on the function.'};
  const QUALIFIERS={0:'8-bit start and stop indexes, inclusive; no index repeated before each value.',
    1:'16-bit start and stop indexes, inclusive; no per-value index prefix.',
    6:'All objects of the specified type: no range bytes. A request may ask for every point or an entire data class.',
    7:'8-bit object count; no per-object index prefix.',8:'16-bit object count; no per-object index prefix.',
    23:'8-bit object count, then an 8-bit point index before each object.',
    40:'16-bit object count, then a 16-bit point index before each object.'};
  const FIELD_MEANING={
    'Start':'A recognizable marker helps locate the beginning of a DNP3 link frame.',
    'Length':'A byte count tells the receiver how much link data belongs to this frame.',
    'Link control':'Link-layer direction, primary/secondary role and operation. These describe delivery between DNP3 endpoints, not the plant command.',
    'Destination address':'The addressed DNP3 endpoint, using the device link-address configuration.',
    'Source address':'The claimed DNP3 sender. DNP3 link addresses and IP addresses are separate numbering systems.',
    'Transport control':'FIR means first segment and FIN means final segment; the six-bit sequence helps assemble application data carried across link frames.',
    'Application control':'FIR/FIN mark application fragments; CON requests confirmation; UNS marks unsolicited traffic. Its four-bit sequence belongs to the application exchange.',
    'Function':'The requested operation, such as READ, SELECT or OPERATE, or a RESPONSE/CONFIRM message.',
    'Internal indications':'IIN is Internal Indications: device and protocol conditions such as pending events, restart or parameter errors.',
    'Object group / variation':'Group identifies the category of data; variation selects its representation within that group. The two bytes must be interpreted together.',
    'Qualifier':'Describes the range or count of objects and any point-index prefixes. It tells the decoder how to locate the points in this message.',
    'Point range':'The first and last point indexes included by this header. Both endpoints count.',
    'Point count':'How many objects follow this header. A count is a quantity, not a point address.',
    'Point index':'A number identifying a point within its data type. It is not automatically a Modbus register or engineering unit.',
    'Quality flags':'Bits describing the condition of a reported value, such as ONLINE, RESTART or COMM_LOST. These are different from the qualifier, which describes message layout.',
    'CROB control':'CROB means Control Relay Output Block. It carries the requested binary control operation; command acceptance and physical feedback are separate evidence.',
    'Operation count':'How many repetitions of the output operation are requested. This is separate from the number of points in an object header.',
    'On time':'Requested duration of the ON part of a pulse, in milliseconds.',
    'Off time':'Requested duration of the OFF part of a pulse, in milliseconds.',
    'Command status':'The outstation result for a command request. A success response does not by itself prove a breaker or valve moved.',
    'Restart delay':'A reported restart delay. A delay value is not a measured completion timestamp.',
    'Fine time delay':'A delay expressed in milliseconds, carried as a 16-bit unsigned integer. Read the application function to determine what the delay describes.'};
  function guide(layer,name){
    const meaning=FIELD_MEANING[name]||(name.includes('CRC')?'CRC means Cyclic Redundancy Check: a calculated check value for accidental transmission errors.':
      /^Binary /.test(name)?'A reported on/off state with quality information. Use the point map to identify the actual switch, contact or alarm.':
      /^Analog /.test(name)?'A numeric measurement. A signed number can be negative; its engineering meaning still requires the point map.':
      /^Counter /.test(name)?'A nonnegative accumulated count; scaling and reset behavior are device-specific.':'');
    const helpTopic=['Quality flags','Internal indications'].includes(name)?'dnp3-quality':
      ['CROB control','Operation count','On time','Off time','Command status'].includes(name)?'dnp3-control':
      layer==='objects'?'dnp3-objects':layer==='crc'?'capture-basics':'dnp3-layers';
    return {meaning,helpTopic,
      sources:[{title:layer==='objects'?'DNP Users Group · object headers and qualifiers':'DNP Users Group · protocol primer',url:layer==='objects'?VALIDATION:PRIMER},
               {title:'Wireshark · DNP3 field reference',url:'https://www.wireshark.org/docs/dfref/d/dnp3.html'}]};
  }
  const need = (ok, message) => { if (!ok) throw Error(message); };
  function flagDetails(group,value){
    const names=['ONLINE','RESTART','COMM_LOST','REMOTE_FORCED','LOCAL_FORCED',
      group===30?'OVER_RANGE':group===20?'ROLLOVER':group===10?'RESERVED':'CHATTER_FILTER',
      group===30?'REFERENCE_ERR':group===20?'DISCONTINUITY':'RESERVED',
      [1,2,10].includes(group)?'STATE':'RESERVED'];
    return names.map((name,bit)=>'bit '+bit+' '+name+'='+Number(!!(value&(1<<bit)))).join('; ')+'.';
  }
  // Deliberately conservative classroom display policy, not a DNP3 standard.
  function qualifiedFlags(group,flags){
    return [1,2,10,20,30].includes(group)&&Number.isInteger(flags)&&flags>=0&&flags<=255&&
      !!(flags&1)&&!(flags&([1,2,10].includes(group)?0x7e:0xfe));
  }
  function crc(bytes) {
    let c=0; for (const b of bytes) { c^=b; for(let i=0;i<8;i++) c=c&1?(c>>>1)^0xA6BC:c>>>1; }
    return (~c)&65535;
  }
  function decode(input) {
    const raw=Uint8Array.from(input), out={schema:'tbd.dnp3-frame/v1',status:'decoded',raw:Array.from(raw),fields:[],objects:[],checks:[],warnings:[]};
    const field=(layer,name,value,offsets,explanation)=>out.fields.push({layer,name,value,offsets,explanation,...guide(layer,name)});
    const range=(s,n)=>Array.from({length:n},(_,i)=>s+i);
    const u16=p=>raw[p]|raw[p+1]<<8;
    try {
      need(raw.length>=10,'Truncated DNP3 link header');
      need(raw[0]===5&&raw[1]===100,'Missing DNP3 start bytes 05 64');
      const count=raw[2]-5;
      need(count>=0,'Invalid link length');
      need(raw.length===10+count+Math.ceil(count/16)*2,'Link length does not match these bytes; TCP reassembly may be required');
      out.source=u16(6);out.destination=u16(4);
      field('link','Start','05 64',[0,1],'DNP3 link-frame synchronization bytes.');
      field('link','Length',raw[2],[2],'Counts control/address bytes (5) plus user data. It excludes start, length and all CRC bytes.');
      field('link','Link control','0x'+hex(raw[3]),[3],'DIR and PRM flags plus the link function. This is separate from the application function.');
      field('link','Destination address',out.destination,[4,5],'16-bit little-endian DNP3 address. It is not an IP address or authenticated identity.');
      field('link','Source address',out.source,[6,7],'16-bit little-endian DNP3 address. Compare it with the IP endpoint and assigned asset inventory.');
      function checksum(start,length,pos,label) {
        const expected=crc(raw.slice(start,start+length)),observed=u16(pos),valid=expected===observed;
        out.checks.push({label,expected,observed,valid,offsets:[pos,pos+1]});
        field('crc',label,valid?'Valid':'FAILED',[pos,pos+1],'CRC detects accidental byte corruption; it does not authenticate the sender. Expected 0x'+expected.toString(16).padStart(4,'0').toUpperCase()+'.');
      }
      checksum(0,8,8,'Header CRC');
      const user=[],map=[];
      for(let p=10,left=count,block=1;left>0;block++) {
        const n=Math.min(16,left); for(let k=0;k<n;k++){user.push(raw[p+k]);map.push(p+k);}
        checksum(p,n,p+n,'Data CRC '+block);p+=n+2;left-=n;
      }
      out.crcValid=out.checks.every(c=>c.valid);
      if(!out.crcValid){out.status='invalid';out.warnings.push('CRC failure: decoded values are inspection only and cannot update reported state.');}
      if(!count){out.status=out.crcValid?'unsupported':'invalid';out.warnings.push('Link-only frame; no application message.');return out;}
      need(user.length>=3,'Truncated transport/application header');
      out.transport={first:!!(user[0]&64),final:!!(user[0]&128),sequence:user[0]&63};
      field('transport','Transport control','0x'+hex(user[0]),[map[0]],'FIR='+Number(out.transport.first)+' FIN='+Number(out.transport.final)+'; six-bit sequence '+out.transport.sequence+'. Segments reassemble an application fragment.');
      if(!out.transport.first||!out.transport.final){out.status=out.crcValid?'unsupported':'invalid';out.warnings.push('Fragmented transport is outside this bounded decoder.');return out;}
      out.control={first:!!(user[1]&128),final:!!(user[1]&64),confirm:!!(user[1]&32),unsolicited:!!(user[1]&16),sequence:user[1]&15};
      out.function=user[2];out.functionName=FUNCTIONS[user[2]]||'UNKNOWN 0x'+hex(user[2]);
      field('application','Application control','0x'+hex(user[1]),[map[1]],'FIR='+Number(out.control.first)+' FIN='+Number(out.control.final)+' CON='+Number(out.control.confirm)+' UNS='+Number(out.control.unsolicited)+'; four-bit sequence '+out.control.sequence+'.');
      field('application','Function',out.functionName+' · 0x'+hex(user[2]),[map[2]],'The requested application action or response type. Sequence numbers correlate exchanges; they are not credentials.');
      if(!out.control.first||!out.control.final){out.status=out.crcValid?'unsupported':'invalid';out.warnings.push('Multi-fragment application message is outside this decoder.');return out;}
      let p=3;
      if(user[2]===129||user[2]===130) {
        need(user.length>=5,'Truncated internal indications');
        const meanings=[];
        for(const [mask,name] of [[1,'Broadcast'],[2,'Class 1 events'],[4,'Class 2 events'],[8,'Class 3 events'],[16,'Need time'],[32,'Local control'],[64,'Device trouble'],[128,'Device restart']]) if(user[3]&mask) meanings.push(name);
        for(const [mask,name] of [[1,'Function unsupported'],[2,'Object unknown'],[4,'Parameter error'],[8,'Event buffer overflow'],[16,'Already executing'],[32,'Configuration corrupt']]) if(user[4]&mask) meanings.push(name);
        out.iin={first:user[3],second:user[4],meanings};
        field('application','Internal indications','IIN1 '+hex(user[3])+' / IIN2 '+hex(user[4]),map.slice(3,5),meanings.join(', ')||'No IIN flags set. This does not prove the plant is healthy.');p=5;
      }
      const take=n=>{need(p+n<=user.length,'Truncated object data');const s=p;p+=n;return s;};
      const uf=(name,value,s,n,explanation)=>field('objects',name,value,map.slice(s,s+n),explanation);
      const integer=(s,n,signed=false)=>{let v=0;for(let i=0;i<n;i++)v+=user[s+i]*2**(8*i);return signed&&v>=2**(n*8-1)?v-2**(n*8):v;};
      while(p<user.length) {
        const start=take(3),g=user[start],v=user[start+1],q=user[start+2];
        const category=GROUPS[g]||'Unrecognized group';
        const format=FORMATS[g+':'+v]||(g===60&&v>=1&&v<=4?(v===1?'Request Class 0 static data.':'Request event Class '+(v-1)+'. Event-class priority is configured for the device.'):'This variation is not described by this bounded teaching decoder.');
        uf('Object group / variation','g'+g+'v'+v+' · '+category,start,2,'Group '+g+' identifies '+category.toLowerCase()+'. Variation '+v+' specifies the format or selection within that group. '+format+' A group number does not identify a particular breaker or sensor.');
        out.fields.at(-1).sources.unshift({title:'DNP3 object formats · Step Function implementation reference',url:VARIATIONS});
        uf('Qualifier','0x'+hex(q)+(q===6?' · All objects':q===0||q===1?' · Start / stop range':[7,8,23,40].includes(q)?' · Object count':''),start+2,1,
          (QUALIFIERS[q]||'Qualifier not supported by this teaching decoder; consult the object header specification.')+' The qualifier says which points are included and how their indexes are encoded. It is not a quality or trust score.');
        const obj={group:g,variation:v,qualifier:q,points:[]};out.objects.push(obj);
        if(q===6){
          obj.all=true; if(g===60&&v>=1&&v<=4){obj.class=v-1;continue;}
          if(out.function===1)continue;
          out.status=out.crcValid?'unsupported':'invalid';out.warnings.push('Unsupported all-objects response');break;
        }
        let n=0,first=0,prefix=0;
        if(q===0||q===1){const w=q===0?1:2,s=take(w*2);first=integer(s,w);const last=integer(s+w,w);need(last>=first,'Reversed object range');n=last-first+1;uf('Point range',first+'–'+last,s,w*2,'Inclusive point indexes in this object group.');}
        else if([7,8,0x17,0x28].includes(q)){const w=(q===8||q===0x28)?2:1,s=take(w);n=integer(s,w);prefix=q===0x17?1:q===0x28?2:0;uf('Point count',n,s,w,'Number of points; qualifier determines index prefixes.');}
        else {out.status=out.crcValid?'unsupported':'invalid';out.warnings.push('Unsupported qualifier 0x'+hex(q));break;}
        need(n<=256,'Object count exceeds bounded decoder');
        const key=g+':'+v;
        const sizes={'1:2':1,'2:1':1,'10:2':1,'30:1':5,'30:2':3,'20:1':5,'12:1':11,'52:2':2};
        if(!(key in sizes)){out.status=out.crcValid?'unsupported':'invalid';out.warnings.push('Unsupported object g'+g+'v'+v);break;}
        for(let k=0;k<n;k++) {
          let index=first+k;
          if(prefix){const s=take(prefix);index=integer(s,prefix);uf('Point index',index,s,prefix,'Index within this group; a separate point map supplies engineering meaning.');}
          const s=take(sizes[key]),point={index,offsets:map.slice(s,p),kind:'unknown'};
          if(['1:2','2:1','10:2'].includes(key)){
            point.kind=g===10?'binary-output':'binary-input';point.flags=user[s];point.value=!!(user[s]&128);point.online=!!(user[s]&1);
            point.qualified=qualifiedFlags(g,point.flags);
            uf('Binary '+index,Number(point.value)+' · flags 0x'+hex(point.flags),s,1,flagDetails(g,point.flags)+' State and quality occupy the same byte. A reported bit is not independent physical feedback.');
            out.fields.at(-1).helpTopic='dnp3-quality';
          }else if(['30:1','30:2','20:1'].includes(key)) {
            point.kind=g===20?'counter':'analog-input';point.flags=user[s];point.online=!!(user[s]&1);point.qualified=qualifiedFlags(g,point.flags);point.value=integer(s+1,sizes[key]-1,g===30);
            uf('Quality flags','0x'+hex(point.flags),s,1,flagDetails(g,point.flags)+' These flags qualify this '+(g===20?'counter':'analog')+' value. ONLINE does not establish calibration or freshness.');
            uf((g===20?'Counter ':'Analog ')+index,point.value,s+1,sizes[key]-1,(g===20?'Unsigned':'Signed')+' little-endian value. Units and scaling require an external point map.');
          }else if(key==='12:1') {
            point.kind='control';point.code=user[s];point.count=user[s+1];point.onMs=integer(s+2,4);point.offMs=integer(s+6,4);point.status=user[s+10];
            const ops={1:'Pulse on',2:'Pulse off',3:'Latch on',4:'Latch off'},trip={0:'No trip/close field',1:'Close',2:'Trip',3:'Reserved'};
            point.action=trip[user[s]>>6]+' / '+(ops[user[s]&15]||'Unknown operation');
            uf('CROB control',point.action+' · 0x'+hex(point.code),s,1,'Trip/close bits and operation bits describe the requested action. SELECT checks a command; it must not actuate.');
            uf('Operation count',point.count,s+1,1,'Requested number of pulse operations.');
            uf('On time',point.onMs+' ms',s+2,4,'32-bit little-endian pulse duration.');uf('Off time',point.offMs+' ms',s+6,4,'32-bit little-endian interval.');
            uf('Command status',point.status===0?'0 · Success':String(point.status),s+10,1,out.function===129?'Success means accepted, initiated or queued. It does not prove the physical position.':'The request carries a status octet; it is not a response acknowledgment.');
          }else {point.kind='delay';point.value=integer(s,2);uf('Fine time delay',point.value+' ms',s,2,'g52v2 reports a delay in milliseconds. This alone does not prove a restart completed.');}
          obj.points.push(point);
        }
      }
    }catch(error){out.status='invalid';out.warnings.push(error.message);}
    return out;
  }
  function parsePcap(input, decoder=decode, protocol='dnp3') {
    need(['dnp3','mqtt'].includes(protocol),'Choose DNP3 or MQTT');
    const b=Uint8Array.from(input);need(b.length>=24&&b.length<=2*1024*1024,'Use a classic PCAP file up to 2 MiB.');
    const signature=Array.from(b.slice(0,4),hex).join('');
    need(['D4C3B2A1','A1B2C3D4','4D3CB2A1','A1B23C4D'].includes(signature),'Classic PCAP required; convert PCAPNG with editcap -F pcap.');
    const le=signature==='D4C3B2A1'||signature==='4D3CB2A1',nano=signature==='4D3CB2A1'||signature==='A1B23C4D';
    const dv=new DataView(b.buffer,b.byteOffset,b.byteLength),u32=p=>dv.getUint32(p,le);
    need(dv.getUint16(4,le)===2&&dv.getUint16(6,le)===4,'Unsupported PCAP version');
    need(u32(20)===1,'Ethernet PCAP required.');
    let p=24,first=null;const packets=[];
    while(p<b.length){
      need(packets.length<4096&&p+16<=b.length,'Too many records or truncated PCAP record');
      const sec=u32(p),fraction=u32(p+4),cap=u32(p+8),original=u32(p+12);p+=16;
      need(fraction<(nano?1e9:1e6),'Invalid PCAP timestamp fraction');
      need(cap<=65535&&cap<=original&&p+cap<=b.length,'Invalid/truncated PCAP record length');
      const bytes=b.slice(p,p+cap);p+=cap;const timestamp=sec+fraction/(nano?1e9:1e6);if(first===null)first=timestamp;
      const record={number:packets.length+1,time:timestamp-first,captured:cap,original,status:'other',warnings:[]};packets.push(record);
      try{
        need(cap===original,'Capture record is truncated');need(cap>=14,'Truncated Ethernet header');
        let ip=14,ether=bytes[12]*256+bytes[13];
        if(ether===0x8100||ether===0x88a8){need(cap>=18,'Truncated VLAN');ether=bytes[16]*256+bytes[17];ip=18;}
        if(ether!==0x800){record.warnings.push('Non-IPv4 packet not decoded');continue;}
        need(cap>=ip+20&&(bytes[ip]>>4)===4,'Truncated IPv4 header');
        const ih=(bytes[ip]&15)*4,total=bytes[ip+2]*256+bytes[ip+3];need(ih>=20&&total>=ih&&ip+total<=cap,'Invalid IPv4 length');
        record.source=Array.from(bytes.slice(ip+12,ip+16)).join('.');record.destination=Array.from(bytes.slice(ip+16,ip+20)).join('.');
        need(!(bytes[ip+6]&0x3f)&&bytes[ip+7]===0,'IPv4 fragment: reassembly required');
        if(bytes[ip+9]!==6){record.warnings.push('Non-TCP packet not decoded');continue;}
        const tcp=ip+ih;need(total>=ih+20,'Truncated TCP header');const th=(bytes[tcp+12]>>4)*4;need(th>=20&&tcp+th<=ip+total,'Invalid TCP header length');
        record.sourcePort=bytes[tcp]*256+bytes[tcp+1];record.destinationPort=bytes[tcp+2]*256+bytes[tcp+3];
        record.tcpFlags=bytes[tcp+13];record.payloadOffset=tcp+th;
        const payload=bytes.slice(tcp+th,ip+total);record.payload=Array.from(payload);
        if(!payload.length){record.status='tcp';continue;}
        // Port/signature selection is a decoding hint, never device identity.
        // Unrelated Modbus/HTTP bytes on a shared capture are not invalid DNP3.
        const port=protocol==='dnp3'?20000:1883;
        const selected=record.sourcePort===port||record.destinationPort===port||
          (protocol==='dnp3'&&payload[0]===5&&payload[1]===100);
        if(!selected){record.status='other-protocol';continue;}
        record[protocol]=decoder(payload);record.status=record[protocol].status;
        if(record[protocol].status!=='decoded') record.warnings.push(...record[protocol].warnings);
      }catch(error){record.status='invalid';record.warnings.push(error.message);}
    }
    return {schema:'tbd.protocol-capture/v1',packetCount:packets.length,packets,
      limits:['Ethernet/one VLAN, IPv4, TCP, one complete selected-protocol message per segment',
        'Selection: DNP3 port 20000 or 05 64 prefix; MQTT port 1883. A port/signature is not trusted identity.',
        'No TCP/IP/application reassembly, TLS, PCAPNG or general object-library support','IP/TCP checksums and physical authenticity are not verified']};
  }
  function reportAt(packets,position,binding) {
    const result={binary:null,analog:null,counter:null,lastControl:null};
    if(!binding)return result;
    const pending=[];
    for(const packet of packets.slice(0,position+1)){
      const d=packet.dnp3;if(!d||d.status!=='decoded')continue;
      const controls=d.objects.flatMap(o=>o.points.filter(p=>p.kind==='control'));
      if([3,4,5,6].includes(d.function)&&controls.length&&packet.destination===binding.outstationIP&&d.destination===binding.outstationLink){
        const point=controls[0];
        pending.push({packet,d,point});
        result.lastControl={function:d.functionName,action:point.action,requestFrame:packet.number,responseFrame:null,status:null};
      }
      if(packet.source!==binding.outstationIP||d.source!==binding.outstationLink)continue;
      if([129,130].includes(d.function)){
        for(const o of d.objects)for(const point of o.points){
          const observation={...point,frame:packet.number,time:packet.time};
          if(point.index===0&&point.kind==='binary-input')result.binary=observation;
          if(point.index===0&&point.kind==='analog-input')result.analog=observation;
          if(point.index===0&&point.kind==='counter')result.counter=observation;
        }
      }
      if(d.function===129&&controls.length){
        const same=(a,b)=>['index','code','count','onMs','offMs'].every(k=>a[k]===b[k]);
        const request=[...pending].reverse().find(r=>r.packet.source===packet.destination&&r.packet.destination===packet.source&&r.packet.sourcePort===packet.destinationPort&&r.packet.destinationPort===packet.sourcePort&&r.d.source===d.destination&&r.d.destination===d.source&&r.d.control.sequence===d.control.sequence&&same(r.point,controls[0]));
        if(request&&result.lastControl?.requestFrame===request.packet.number){result.lastControl.status=controls[0].status;result.lastControl.responseFrame=packet.number;}
      }
    }
    return result;
  }
  // Receipt age is neither sensor sample age nor model time. Negative ages fail.
  function replyAge(snapshot,elapsedMs=0){
    const now=snapshot?.response_monotonic_ns,received=snapshot?.dnp3?.received_ns;
    if(![now,received,elapsedMs].every(Number.isFinite)||received<0||now<received||elapsedMs<0)return null;
    const age=(now-received)/1e6+elapsedMs;
    return age<=2500?age:null;
  }
  function livePointReport(snapshot,elapsedMs=0){
    const age=replyAge(snapshot,elapsedMs),p=snapshot?.dnp3;
    const result={age,feedback:null,busKV:null,currentA:null};
    if(age===null)return result;
    const binary=i=>typeof p.binary?.[i]==='boolean'&&qualifiedFlags(1,p.binary_flags?.[i]);
    const analog=i=>Number.isInteger(p.analog?.[i])&&p.analog[i]>=-32768&&p.analog[i]<=32767&&qualifiedFlags(30,p.analog_flags?.[i]);
    if(binary(2)&&binary(3))result.feedback=p.binary[2]&&!p.binary[3]?'Closed':!p.binary[2]&&p.binary[3]?'Open':'Inconsistent';
    if(analog(2))result.busKV=p.analog[2]*2/1000;
    if(analog(3))result.currentA=p.analog[3]*.02;
    return result;
  }
  const api={decode,parsePcap,crc,reportAt,replyAge,livePointReport,qualifiedFlags,FUNCTIONS,hex};
  if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.TBDDNP3=api;
})(typeof globalThis!=='undefined'?globalThis:this);
