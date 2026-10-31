// Circuit workspace live-path self-test — no twin, no QEMU, no lab scripts.
//   node tools/circuit-live-selftest.js
// Runs the Studio against a tiny FAKE bridge (static files, /field, /modbus and a WebSocket console that
// answers `io` / `status`) and a faked USB session, and asserts the PROTOCOL the Circuit tab speaks:
//   twin: loading a circuit POSTs its rest byte (motor: di=4, the NC E-stop closed), a press POSTs
//         /field?bit=N&v=1 and the release v=0, outputs are read from GET /field, hops 3–5 read OBSERVED;
//   USB:  a press sends `force IXn 1`, the release `unforce IXn`, hops 1–4 read BYPASSED, leaving the
//         tab releases every force it set;
//         the light circuit posts /field?ch1_mv=<divider mV> (twin started with FIELD_ANALOG=1) and opens it on leaving;
//   none: nothing is lit and nothing is sent.
// The fake bridge is a test double for these assertions only; nothing it returns is ever shown as live data.
const {chromium}=require('playwright'),http=require('http'),fs=require('fs'),path=require('path'),crypto=require('crypto');
const ROOT=path.join(__dirname,'../web/public');
const zlib=require('zlib');
function pngOf(w,h,rgb){const crcT=new Uint32Array(256).map((_,n)=>{let c=n;for(let k=0;k<8;k++)c=c&1?0xedb88320^(c>>>1):c>>>1;return c>>>0;});
  const crc=b=>{let c=0xffffffff;for(const x of b)c=crcT[(c^x)&255]^(c>>>8);return (c^0xffffffff)>>>0;};
  const chunk=(t,d)=>{const len=Buffer.alloc(4);len.writeUInt32BE(d.length);const td=Buffer.concat([Buffer.from(t),d]);const c=Buffer.alloc(4);c.writeUInt32BE(crc(td));return Buffer.concat([len,td,c]);};
  const ih=Buffer.alloc(13);ih.writeUInt32BE(w,0);ih.writeUInt32BE(h,4);ih[8]=8;ih[9]=2;
  const raw=Buffer.alloc((w*3+1)*h);for(let y=0;y<h;y++){raw[y*(w*3+1)]=0;for(let x=0;x<w;x++)raw.set(rgb,y*(w*3+1)+1+x*3);}
  return Buffer.concat([Buffer.from([0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a]),chunk('IHDR',ih),chunk('IDAT',zlib.deflateSync(raw)),chunk('IEND',Buffer.alloc(0))]);}
const LCD_PNG=pngOf(240,135,[14,17,22]);
const field={di:0,relays:0,ch1:0},log=[],forced={};let sockets=[];
function firmware(){ // stand-in scan: relay1 follows input 1 (the pushbutton project); relay0 seals in on IN1
  const inp=i=>forced[i]!=null?forced[i]:(field.di>>i)&1;let r=field.relays;
  r=(r&~2)|(inp(1)?2:0);if(inp(0)&&inp(2))r|=1;if(!inp(2))r&=~1;field.relays=r;}
function ioReply(){firmware();const toks=[];for(let i=0;i<8;i++)toks.push(String(forced[i]!=null?forced[i]:(field.di>>i)&1)+(forced[i]!=null?'F':''));
  return ['DI  : '+toks.join(' '),'relay commanded/actual (base 0-3):'].concat([0,1,2,3].map(k=>`  relay${k} cmd=${(field.relays>>k)&1} act=${(field.relays>>k)&1}`)).join('\n')+'\n';}
function wsSend(sock,text){const b=Buffer.from(text);let h;if(b.length<126)h=Buffer.from([0x81,b.length]);else{h=Buffer.alloc(4);h[0]=0x81;h[1]=126;h.writeUInt16BE(b.length,2);}sock.write(Buffer.concat([h,b]));}
function wsFrames(buf,cb){let o=0;while(o+2<=buf.length){let len=buf[o+1]&127,p=o+2;if(len===126){len=buf.readUInt16BE(p);p+=2;}const mask=buf.slice(p,p+4);p+=4;if(p+len>buf.length)break;const d=Buffer.alloc(len);for(let i=0;i<len;i++)d[i]=buf[p+i]^mask[i%4];cb(d.toString());o=p+len;}return buf.slice(o);}
const srv=http.createServer((q,r)=>{const u=new URL(q.url,'http://x');
  if(u.pathname==='/field'){if(q.method==='POST'){log.push('POST '+u.search);if(u.searchParams.has('ch1_mv')){const m=u.searchParams.get('ch1_mv');field.ch1=m==='open'?0:+m;}
      else if(u.searchParams.has('di'))field.di=+u.searchParams.get('di')&255;else{const b=+u.searchParams.get('bit'),v=+u.searchParams.get('v');field.di=v?field.di|(1<<b):field.di&~(1<<b);}}
    firmware();r.writeHead(200,{'content-type':'application/json'});return r.end(JSON.stringify({ok:true,di:field.di,relays:field.relays,analog:{ch1_mv:field.ch1,ch2_mv:0,ch1_ua:0,ch2_ua:0,ch1_source:'field',ch2_source:'field',writable:true}}));}
  if(u.pathname==='/lcd'){log.push('GET /lcd');r.writeHead(200,{'content-type':'image/png','x-frame-age-ms':'120'});return r.end(LCD_PNG);}
  if(u.pathname==='/modbus'){const fc=+u.searchParams.get('fc'),addr=+u.searchParams.get('addr'),n=+u.searchParams.get('count');let values=Array(n).fill(0);
    if(fc===1&&addr===100)values=values.map((_,i)=>(field.di>>i)&1);if(fc===3&&addr===50)values=[0,981,12];r.writeHead(200,{'content-type':'application/json'});return r.end(JSON.stringify({ok:true,values}));}
  let p=u.pathname==='/'?'/logic_studio.html':u.pathname;try{const b=fs.readFileSync(path.join(ROOT,p));r.writeHead(200,{'content-type':p.endsWith('.js')?'application/javascript':p.endsWith('.csco')||p.endsWith('.json')?'application/json':'text/html'});r.end(b);}catch(e){r.writeHead(404);r.end('nf');}});
srv.on('upgrade',(q,sock)=>{const k=q.headers['sec-websocket-key'];const acc=crypto.createHash('sha1').update(k+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  sock.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+acc+'\r\n\r\n');sockets.push(sock);let rest=Buffer.alloc(0);
  sock.on('data',d=>{rest=wsFrames(Buffer.concat([rest,d]),txt=>txt.split('\n').filter(Boolean).forEach(line=>{log.push('WS '+line);
    let m;if(line==='io')wsSend(sock,ioReply());else if(line==='status')wsSend(sock,'MODE=RUN  scan last=18000us min=1us max=1us  scans=1  hb=1\n');
    else if((m=/^force IX(\d) ([01])$/.exec(line))){forced[+m[1]]=+m[2];wsSend(sock,`forced %IX${m[1]} = ${m[2]}\n`);}
    else if((m=/^unforce IX(\d)$/.exec(line))){delete forced[+m[1]];wsSend(sock,`unforced %IX${m[1]}\n`);}
    else if((m=/^btn ([abc])$/.exec(line)))wsSend(sock,`panel button ${m[1].toUpperCase()} injected\n`);}));});sock.on('error',()=>{});});
let fails=0;const ok=(c,m)=>{console.log((c?'  ✓ ':'  ✗ ')+m);if(!c)fails++;};
const until=async(fn,ms=6000)=>{const t=Date.now();while(Date.now()-t<ms){if(fn())return true;await new Promise(r=>setTimeout(r,50));}return fn();};
const dom=(pg,fn,arg)=>pg.waitForFunction(fn,arg,{timeout:6000}).then(()=>true,()=>false);
(async()=>{await new Promise(r=>srv.listen(0,'127.0.0.1',r));const port=srv.address().port;
  const b=await chromium.launch({args:['--no-sandbox']});const pg=await b.newPage({viewport:{width:1600,height:1200}});const errs=[];pg.on('pageerror',e=>errs.push(e.message));
  await pg.goto(`http://127.0.0.1:${port}/logic_studio.html`,{waitUntil:'networkidle'});await pg.evaluate(()=>{try{closeModal();}catch(e){}});
  console.log('not connected');
  await pg.evaluate(()=>{AppView.set('circuit');CircuitWorkspace.select('a');});await pg.waitForTimeout(600);
  ok(await pg.evaluate(()=>!document.querySelector('#cwStage [fill^="url(#skLensOn"]')),'idle: no LED is lit');
  ok(await pg.evaluate(()=>!document.querySelector('#cwStage .sk-flow')),'idle: no current animation');
  await pg.evaluate(()=>CircuitWorkspace.press('S1'));await pg.waitForTimeout(200);await pg.evaluate(()=>CircuitWorkspace.release('S1'));
  ok(!log.length,'idle: pressing a switch sends nothing');
  console.log('twin (fake bridge)');
  await pg.evaluate(()=>connectTwin(twinDefaultUrl()));await pg.waitForFunction(()=>conn===true);
  await pg.waitForFunction(()=>/Live/.test(document.getElementById('cwConn').textContent),null,{timeout:6000});
  ok(await until(()=>log.includes('POST ?di=0')),'circuit a: rest byte di=0 posted at the chip');
  await pg.evaluate(()=>CircuitWorkspace.press('S1'));
  ok(await until(()=>log.includes('POST ?bit=1&v=1')),'press S1 → POST /field?bit=1&v=1 (IN2 = di1, set at the chip)');
  ok(await dom(pg,()=>!!document.querySelector('#cwStage [data-part="H1"] [fill="url(#skLensOn_green)"]')),'H1 lit from the relay bit read back over GET /field');
  await dom(pg,()=>[...document.querySelectorAll('#cwPath .cw-hop-tag')].map(e=>e.textContent)[4]==='OBSERVED');
  const tags=await pg.evaluate(()=>[...document.querySelectorAll('#cwPath .cw-hop-tag')].map(e=>e.textContent));
  ok(tags[2]==='OBSERVED'&&tags[4]==='OBSERVED'&&tags[0]==='MODELLED','hops: switch MODELLED, expander/image OBSERVED ('+tags.join(',')+')');
  ok(!log.some(l=>/force/.test(l)),'twin path never uses force');
  await pg.evaluate(()=>CircuitWorkspace.release('S1'));
  ok(await until(()=>log.includes('POST ?bit=1&v=0')),'release S1 → POST /field?bit=1&v=0');if(!log.includes('POST ?bit=1&v=0'))console.log('    log: '+log.filter(l=>/POST/.test(l)).join(' · '));
  await pg.evaluate(()=>CircuitWorkspace.select('c'));
  ok(await until(()=>log.includes('POST ?di=4')),'motor circuit: rest byte di=4 (IN3 = 1: NC E-stop closed / healthy)');
  ok(await dom(pg,()=>/1 healthy/.test(document.getElementById('cwStage').textContent)),'readout shows IN3 (di2) = 1 healthy');
  await pg.evaluate(()=>CircuitWorkspace.press('S3'));
  ok(await until(()=>log.includes('POST ?bit=2&v=0')),'E-stop slide OFF → POST /field?bit=2&v=0 (trip)');
  await pg.evaluate(()=>CircuitWorkspace.setView('schematic'));
  ok(await dom(pg,()=>!!document.querySelector('#cwStage svg [data-hop-target="sch-in3"]')),'schematic renders from the netlist');
  await pg.evaluate(()=>{CircuitWorkspace.setView('bench');CircuitWorkspace.select('e');CircuitWorkspace.phone('motion');});
  ok(await dom(pg,()=>/%IW2 = 981/.test(document.getElementById('cwStage').textContent)),'phone block: %IW2 read from Modbus reg 51');
  const ptags=await pg.evaluate(()=>[...document.querySelectorAll('#cwPath .cw-hop-tag')].map(e=>e.textContent));
  ok(ptags[3]==='BYPASSED','phone path: opto · 0x59 in · I²C read BYPASSED');
  // the controller's screen: on the twin the drawing shows the real frame (GET /lcd), enlarges, and A/B/C press the panel
  await pg.evaluate(()=>CircuitWorkspace.select('a'));
  ok(await until(()=>log.includes('GET /lcd')),'twin: the screen is fetched from GET /lcd');
  ok(await dom(pg,()=>/StampLC screen\s*real frame/.test(document.getElementById('cwStage').textContent)),'bench readout: StampLC screen = real frame (OBSERVED)');
  ok(await pg.evaluate(()=>{const im=document.querySelector('#cwStage [data-lcd-mirror]');return !!(im&&/^data:image\/png/.test(im.getAttribute('href')));}),'bench StampLC shows the screen image');
  await pg.waitForFunction(()=>typeof busBusy==='undefined'||!busBusy,null,{timeout:20000}).catch(()=>{});   // a pending readback owns the console
  await pg.click('#cwStage [data-lcd-key="b"]');
  if(!await until(()=>log.includes('WS btn b'),3000))console.log('    debug:',await pg.evaluate(()=>JSON.stringify({MODE,conn,busBusy,ws:!!wsSock,hp:typeof window.heroPress,log:[...document.querySelectorAll('#log div, .log div')].slice(-4).map(d=>d.textContent)})));
  ok(log.includes('WS btn b'),'drawn key B → console btn b (the device’s panel button)');
  await pg.click('#cwStage [data-lcd-open][tabindex="0"]');
  ok(await dom(pg,()=>!!document.getElementById('lcdBig')&&/Real screen/.test((document.getElementById('lcdBigSrc')||{}).textContent||'')),'clicking the drawn screen opens the enlarged view, labelled as the real screen');
  await pg.evaluate(()=>{try{closeModal();}catch(e){}});
  // analog circuit: the twin has no photoresistor; the light presets put the divider voltage on CH1 at the chip
  await pg.evaluate(()=>CircuitWorkspace.select('d'));
  ok(await until(()=>log.includes('POST ?ch1_mv=2225')),'light alarm: Room light (≈10 kΩ) → POST /field?ch1_mv=2225 at the chip');
  await pg.evaluate(()=>CircuitWorkspace.light('covered'));
  ok(await until(()=>log.includes('POST ?ch1_mv=3814')),'Covered (≈60 kΩ) → POST /field?ch1_mv=3814');
  ok(await dom(pg,()=>/CH1 = 3814 mV/.test(document.getElementById('cwStage').textContent)),'light card shows CH1 = 3814 mV read back from the chip');
  const atags=await pg.evaluate(()=>[...document.querySelectorAll('#cwPath .cw-hop-tag')].map(e=>e.textContent));
  ok(atags[0]==='MODELLED'&&atags[1]==='MODELLED'&&atags[2]==='OBSERVED','analog hops: light + divider MODELLED, CH1 register OBSERVED ('+atags.slice(0,4).join(',')+')');
  await pg.evaluate(()=>CircuitWorkspace.select('a'));
  ok(await until(()=>log.includes('POST ?ch1_mv=open')),'leaving the light circuit opens CH1 again (nothing wired: 0 mV)');
  // race: reconnect and press at once — the press must land AFTER the circuit's rest byte, never be undone by it
  await pg.evaluate(()=>{AppView.set('logic');disconnect();});await pg.waitForTimeout(400);log.length=0;
  await pg.evaluate(()=>{connectTwin(twinDefaultUrl());});await pg.waitForFunction(()=>conn===true);
  await pg.evaluate(()=>{AppView.set('circuit');CircuitWorkspace.select('b');CircuitWorkspace.press('S1');});await until(()=>log.includes('POST ?bit=1&v=1'));await pg.waitForTimeout(200);
  const posts=log.filter(l=>l.startsWith('POST'));
  ok(posts.indexOf('POST ?di=0')>=0&&posts.indexOf('POST ?bit=1&v=1')>posts.indexOf('POST ?di=0')&&((field.di>>1)&1)===1,'press right after connect lands after the rest byte ('+posts.join(' · ')+')');
  await pg.evaluate(()=>CircuitWorkspace.release('S1'));await pg.waitForTimeout(300);
  await pg.evaluate(()=>AppView.set('logic'));await pg.evaluate(()=>disconnect());await pg.waitForTimeout(300);
  console.log('USB (faked Web Serial session)');
  const sent=await pg.evaluate(async()=>{const out=[];window.__usbOut=out;writer={write:async b=>{out.push(new TextDecoder().decode(b).trim());}};port={};conn=true;
    AppView.set('circuit');CircuitWorkspace.select('a');await new Promise(r=>setTimeout(r,700));
    onLine('DI  : 0 0 0 0 0 0 0 0');['relay0 cmd=0 act=0','relay1 cmd=0 act=0','relay2 cmd=0 act=0','relay3 cmd=0 act=0'].forEach(l=>onLine('  '+l));
    CircuitWorkspace.press('S1');await new Promise(r=>setTimeout(r,300));
    onLine('DI  : 0 1F 0 0 0 0 0 0');onLine('  relay1 cmd=1 act=1');await new Promise(r=>setTimeout(r,400));
    const tags=[...document.querySelectorAll('#cwPath .cw-hop-tag')].map(e=>e.textContent);
    const lit=!!document.querySelector('#cwStage [data-part="H1"] [fill="url(#skLensOn_green)"]');
    CircuitWorkspace.release('S1');await new Promise(r=>setTimeout(r,200));
    CircuitWorkspace.select('c');await new Promise(r=>setTimeout(r,300));CircuitWorkspace.press('S3');await new Promise(r=>setTimeout(r,200));
    AppView.set('logic');await new Promise(r=>setTimeout(r,200));
    return {out,tags,lit};});
  ok(sent.out.includes('force IX1 1'),'press S1 → force IX1 1');
  ok(sent.out.includes('unforce IX1'),'release S1 → unforce IX1 (the real wire takes over again)');
  ok(sent.tags.slice(0,4).every(t=>t==='BYPASSED'),'hops 1–4 BYPASSED under force ('+sent.tags.slice(0,5).join(',')+')');
  ok(sent.lit,'H1 drawn lit from io act');
  ok(sent.out.includes('force IX2 0')&&sent.out.includes('unforce IX2'),'E-stop OFF forces IX2 0; leaving the tab unforces it');
  ok(!sent.out.some(l=>/^POST|field/.test(l)),'USB path never calls /field');
  ok(!errs.length,'no page errors'+(errs.length?': '+errs.slice(0,2).join(' | '):''));
  sockets.forEach(s=>s.destroy());await b.close();srv.close();
  console.log(fails?`circuit-live-selftest: ${fails} failure(s)`:'circuit-live-selftest: all checks passed');process.exit(fails?1:0);
})().catch(e=>{console.log('selftest error: '+e.message);process.exit(1);});
