/* Bounded MQTT 3.1.1 teaching decoder. Original bytes remain inspectable. */
(function(root){
  'use strict';
  const TYPES={1:'CONNECT',2:'CONNACK',3:'PUBLISH',4:'PUBACK',5:'PUBREC',6:'PUBREL',7:'PUBCOMP',8:'SUBSCRIBE',9:'SUBACK',10:'UNSUBSCRIBE',11:'UNSUBACK',12:'PINGREQ',13:'PINGRESP',14:'DISCONNECT'};
  const TYPE_HELP={1:'CONNECT starts a client connection and supplies session options.',2:'CONNACK is the connection acknowledgement: the broker accepts or refuses CONNECT.',3:'PUBLISH carries an application message under a topic name.',4:'PUBACK is the publish acknowledgement for QoS 1 on this connection.',5:'PUBREC means publish received, the first acknowledgement in QoS 2 delivery.',6:'PUBREL means publish release, the next step after PUBREC in QoS 2 delivery.',7:'PUBCOMP means publish complete, completing the QoS 2 exchange on this connection.',8:'SUBSCRIBE requests delivery of messages matching one or more topic filters.',9:'SUBACK is the subscription acknowledgement, with a result for each requested filter.',10:'UNSUBSCRIBE requests removal of topic subscriptions.',11:'UNSUBACK acknowledges an unsubscribe request.',12:'PINGREQ is a ping request used to keep an otherwise idle connection alive.',13:'PINGRESP is the ping response; it establishes a broker reply, not sensor health.',14:'DISCONNECT closes a client connection normally and suppresses its configured Will.'};
  const SPEC='https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html';
  const GUIDE={
    'Control packet':['The message category: CONNECT starts a session, PUBLISH carries a message, SUBSCRIBE requests topic delivery, and acknowledgement packets report protocol progress.','398718021'],
    'Remaining Length':['The number of bytes after the fixed header, including the variable header and payload. Each encoded byte contributes seven value bits; its top bit says another length byte follows.','398718023'],
    'Packet identifier':['A nonzero exchange number used for acknowledgement tracking on one client/broker connection. It is not a sensor ID, user identity or global message ID.','398718025'],
    'Protocol name':['The protocol identifier in CONNECT. For this version it must be the four UTF-8 characters MQTT.','398718028'],
    'Protocol level':['The protocol version number. Level 4 means MQTT 3.1.1; it is not a QoS level or security rating.','398718028'],
    'Connection flags':['Options for clean session state, the Will message and credential fields. Clean Session=1 starts without a prior persistent session; it does not clear retained messages.','398718028'],
    'Keep Alive':['The maximum interval, in seconds, between client control packets. An otherwise idle client can send PINGREQ and receive PINGRESP. Zero disables this keep-alive mechanism.','398718028'],
    'Client ID':['The identifier the broker uses to distinguish a client and associate session state. It is separate from the username and is not a password.','398718028'],
    'Will topic':['The destination configured for a broker-published Will after an ungraceful client disconnect. This CONNECT packet declares the Will; it does not show its delivery.','398718028'],
    'Will payload':['The application bytes stored with the Will, such as an offline-status message. Their interpretation is supplied by the application.','398718028'],
    'Username':['A credential field presented to the broker. Its presence does not show that authentication or topic authorization succeeded.','398718028'],
    'Password':['Credential bytes supplied with CONNECT. The field display hides the value; the original captured bytes remain inspectable. MQTT alone does not encrypt them.','398718028'],
    'Session present':['Whether the broker resumed previously stored session state. This is not the same as a retained application message.','398718033'],
    'Connection result':['CONNACK is the broker acknowledgement of CONNECT. Its return code reports connection acceptance or refusal, not authority to operate equipment.','398718033'],
    'Delivery flags':['QoS 0: at most once; QoS 1: at least once, with possible duplicates; QoS 2: exactly once at this protocol-delivery scope. RETAIN controls stored last-message behavior, and DUP marks a retransmission attempt. None proves one physical action.','398718037'],
    'Topic name':['The case-sensitive routing name of this publication, such as plant/tank1/telemetry. It is not an IP address or a file path.','398718106'],
    'Topic filter':['A subscription pattern. + matches one complete level; # matches remaining levels and must be last. plant/+/telemetry matches tank1 and tank2 at that level.','398718106'],
    'Application payload':['The actual application message: text, JSON or arbitrary binary bytes. MQTT does not define its units, sensor quality, command meaning or freshness.','398718037'],
    'Requested maximum QoS':['The highest delivery QoS requested for a subscription. The broker reports the granted result in SUBACK.','398718068'],
    'Granted QoS':['The broker result for one topic filter: 0, 1 or 2 is the granted maximum; 0x80 means failure. Match results to the original subscription order.','398718068']};
  function decode(input){
    const b=Uint8Array.from(input),o={schema:'tbd.mqtt-frame/v1',raw:Array.from(b),fields:[],warnings:[],status:'decoded'};
    const need=(ok,msg)=>{if(!ok)throw Error(msg);};
    const field=(layer,name,value,start,n,explanation)=>{const info=GUIDE[name]||['MQTT protocol field.','398718019'];o.fields.push({layer,name,value,offsets:Array.from({length:n},(_,i)=>start+i),explanation,meaning:info[0],helpTopic:name.includes('Topic')?'mqtt-topics':'mqtt-packets',sources:[{title:'OASIS MQTT 3.1.1 · '+name,url:SPEC+'#_Toc'+info[1]}]});};
    let p=0;
    const take=n=>{need(p+n<=b.length,'Truncated MQTT field');const s=p;p+=n;return s;};
    const word=()=>{const s=take(2);return b[s]*256+b[s+1];};
    const string=(name,layer='variable',binary=false)=>{const s=p,n=word(),start=take(n);const bytes=b.slice(start,start+n);const value=binary?Array.from(bytes):new TextDecoder('utf-8',{fatal:true}).decode(bytes);if(!binary)need(!value.includes('\0'),'Invalid MQTT UTF-8 null');field(layer,name,name==='Password'?'Present ('+n+' bytes)':binary?n+' bytes':value,s,n+2,'Two-byte big-endian length followed by '+(binary?'binary data.':'UTF-8 bytes.'));return value;};
    const packetId=()=>{const s=p;o.packetId=word();need(o.packetId!==0,'Packet identifier must be nonzero');field('variable','Packet identifier',o.packetId,s,2,'Scoped to this client/broker connection and exchange; forwarded delivery may have another identifier.');};
    try{
      need(b.length>=2&&b.length<=65535,'Invalid MQTT message length');
      o.type=b[p]>>4;o.name=TYPES[o.type]||'UNKNOWN';o.flags=b[p++]&15;need(o.type>=1&&o.type<=14,'Reserved MQTT control type');
      if(o.type!==3)need(o.flags===([6,8,10].includes(o.type)?2:0),'Invalid fixed-header flags');
      field('fixed','Control packet',o.name,0,1,TYPE_HELP[o.type]+' High four bits identify the control packet; low four bits carry flags.');
      let remaining=0,mult=1,count=0,digit;
      do{need(count<4,'Remaining Length exceeds four bytes');digit=b[take(1)];remaining+=(digit&127)*mult;mult*=128;count++;}while(digit&128);
      need(count===1||remaining>=128**(count-1),'Nonminimal Remaining Length');
      field('fixed','Remaining Length',remaining,1,count,'Base-128 variable-length integer; excludes the fixed header.');
      need(p+remaining===b.length,'One complete MQTT packet required; TCP reassembly/coalescing is outside this decoder');
      if(o.type===1){
        o.protocol=string('Protocol name');o.level=b[take(1)];need(o.protocol==='MQTT'&&o.level===4,'Only MQTT 3.1.1 protocol level 4 is supported');
        field('variable','Protocol level',o.level,p-1,1,'Level 4 denotes MQTT 3.1.1.');
        const s=take(1),f=b[s];need(!(f&1),'Reserved CONNECT flag');o.cleanSession=!!(f&2);o.will=!!(f&4);o.willQoS=(f>>3)&3;o.willRetain=!!(f&32);o.password=!!(f&64);o.username=!!(f&128);
        need(o.willQoS!==3&&(o.will||(!o.willQoS&&!o.willRetain))&&(!o.password||o.username),'Invalid CONNECT flags');
        field('variable','Connection flags','Clean '+Number(o.cleanSession)+' · Will '+Number(o.will)+' · credentials '+Number(o.username),s,1,'A Will is configured here. This packet does not show that the broker published it.');
        const k=p;o.keepAlive=word();field('variable','Keep Alive',o.keepAlive+' s',k,2,'Maximum client control-packet interval; broker disconnect threshold is 1.5 times this value when nonzero.');
        o.clientId=string('Client ID','payload');if(o.will){o.willTopic=string('Will topic','payload');o.willMessage=string('Will payload','payload',true);}
        if(o.username)string('Username','payload');if(o.password)string('Password','payload',true);
      }else if(o.type===2){const s=take(2);need(!(b[s]&254)&&b[s+1]<=5&&(!b[s+1]||!b[s]),'Invalid CONNACK');o.sessionPresent=!!b[s];o.returnCode=b[s+1];field('variable','Session present',o.sessionPresent?'1 · Session resumed':'0 · No stored session resumed',s,1,'Compare this result with the Clean Session flag in CONNECT.');field('variable','Connection result',o.returnCode===0?'0 · Accepted':'Refused '+o.returnCode,s+1,1,({0:'Connection accepted.',1:'Unacceptable protocol version.',2:'Client identifier rejected.',3:'Server unavailable.',4:'Bad username or password.',5:'Not authorized.'}[o.returnCode])+' Authorization to each topic is a separate policy.');}
      else if(o.type===3){
        o.qos=(o.flags>>1)&3;o.retain=!!(o.flags&1);o.dup=!!(o.flags&8);need(o.qos!==3&&!(o.qos===0&&o.dup),'Invalid PUBLISH QoS/DUP');
        field('fixed','Delivery flags','QoS '+o.qos+' · RETAIN '+Number(o.retain)+' · DUP '+Number(o.dup),0,1,'QoS describes protocol delivery, not physical actuation. RETAIN to a broker requests storage; to a subscriber it marks retained delivery.');
        o.topic=string('Topic name');need(o.topic.length>0&&!/[+#]/.test(o.topic),'Invalid PUBLISH topic');if(o.qos)packetId();
        const s=p;take(b.length-p);try{o.payload=new TextDecoder('utf-8',{fatal:true}).decode(b.slice(s));}catch{o.payload='[binary payload]';}
        field('payload','Application payload',o.payload,s,b.length-s,'Application-defined bytes. JSON units and command meanings come from the device contract, not MQTT.');
        try{o.json=JSON.parse(o.payload);}catch{}
      }else if([4,5,6,7,11].includes(o.type)){packetId();}
      else if(o.type===8||o.type===10){packetId();o.subscriptions=[];while(p<b.length){const topic=string('Topic filter','payload');need(topic.length>0&&topic.split('/').every((part,i,all)=>(!part.includes('#')||(part==='#'&&i===all.length-1))&&(!part.includes('+')||part==='+')),'Invalid topic filter');let qos=null;if(o.type===8){const s=take(1);qos=b[s];need(qos<=2,'Invalid requested QoS');field('payload','Requested maximum QoS',qos,s,1,'The broker returns the granted QoS in SUBACK.');}o.subscriptions.push({topic,qos});}need(o.subscriptions.length,'Empty subscription list');}
      else if(o.type===9){packetId();o.grants=[];while(p<b.length){const s=take(1),v=b[s];need([0,1,2,128].includes(v),'Invalid SUBACK code');o.grants.push(v);field('payload','Granted QoS',v===128?'Failure':v,s,1,'One result per requested subscription.');}need(o.grants.length,'Empty SUBACK');}
      need(p===b.length,'Unexpected trailing MQTT fields');
    }catch(error){o.status='invalid';o.warnings.push(error.message);}
    return o;
  }
  function matches(filter,topic){
    if(topic.startsWith('$')&&!filter.startsWith('$'))return false;
    const f=filter.split('/'),t=topic.split('/');
    for(let i=0;i<f.length;i++){if(f[i]==='#')return i===f.length-1;if(t[i]===undefined||(f[i]!=='+'&&f[i]!==t[i]))return false;}return f.length===t.length;
  }
  const api={decode,matches,TYPES};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.TBDMQTT=api;
})(typeof globalThis!=='undefined'?globalThis:this);
