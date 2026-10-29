// One command, one consistent screenshot set from the unified Studio (:8080) against the
// live twin. Run inside the Codespace:   node tools/capture/studio-shots.js
// Captures: bus analyzer, ladder (live), circuit designer (live), protocol studio.
const PW='/workspaces/cs460-week10/.capture/node_modules/playwright';
const {chromium}=require(PW);
const OUT='/workspaces/cs460-week10/docs/img/';
const BASE='http://localhost:8080';

async function shot(ctx, name, fn){
  const p=await ctx.newPage();
  const errs=[];
  p.on('console',m=>{if(m.type()==='error')errs.push(m.text().slice(0,120));});
  p.on('pageerror',e=>errs.push('PE '+String(e).slice(0,120)));
  p.on('dialog',d=>d.dismiss().catch(()=>{}));
  try{ await fn(p); console.log('OK   '+name+(errs.length?('  [errs: '+errs.slice(0,3).join(' | ')+']'):'')); }
  catch(e){ console.log('FAIL '+name+': '+e.message.slice(0,140)); }
  await p.close();
}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));

(async()=>{
 const b=await chromium.launch({args:['--no-sandbox']});
 const ctx=await b.newContext({viewport:{width:1400,height:900},deviceScaleFactor:2});

 // 1) Bus analyzer (now served from the Studio port)
 await shot(ctx,'bus-analyzer → part1-i2c-analyzer.png', async p=>{
   await p.goto(BASE+'/i2c-analyzer.html',{waitUntil:'domcontentloaded'});
   await sleep(1500);
   await p.screenshot({path:OUT+'part1-i2c-analyzer.png'});
 });

 // Shared Logic Studio session: connect twin, load an example, drive it
 const drive=async(p)=>{
   await p.goto(BASE+'/logic_studio.html',{waitUntil:'domcontentloaded'});
   await sleep(1500);
   await p.evaluate(()=>{try{connectTwin(twinDefaultUrl());}catch(e){}});
   await sleep(2500);
   await p.click('text=Open Pushbutton').catch(()=>{});
   await sleep(1500);
   for(const c of ['mode run','force %IX0 1','force QX0 1','force QX1 1']){
     await p.evaluate(cmd=>{try{send(cmd);}catch(e){}},c); await sleep(400);
   }
   // also write the coils directly so the live read reflects energized outputs
   await p.evaluate(async()=>{ try{ await fetch('/modbus?fc=5&addr=1&value=1'); await fetch('/modbus?fc=5&addr=2&value=1'); }catch(e){} });
   await sleep(2000);
 };

 // 2) Ladder (Logic tab, live)
 await shot(ctx,'ladder-live → part5-02-logic-live.png', async p=>{
   await drive(p);
   await p.click('text=Logic').catch(()=>{});
   await sleep(1500);
   await p.screenshot({path:OUT+'part5-02-logic-live.png'});
 });

 // 3) Circuit designer (Circuit tab, live)
 await shot(ctx,'circuit-live → part5-06-circuit-live.png', async p=>{
   await drive(p);
   await p.click('text=Circuit').catch(()=>{});
   await sleep(2000);
   await p.screenshot({path:OUT+'part5-06-circuit-live.png'});
 });

 // 4) Protocol Studio
 await shot(ctx,'protocol → part5-05-protocol-live.png', async p=>{
   await p.goto(BASE+'/protocol_studio.html',{waitUntil:'domcontentloaded'});
   await sleep(2000);
   await p.screenshot({path:OUT+'part5-05-protocol-live.png'});
 });

 await b.close();
 console.log('STUDIO_SHOTS_DONE');
})().catch(e=>{console.error('FATAL',e);process.exit(1);});
