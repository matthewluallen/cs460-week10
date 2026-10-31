// Part 5 "Wire & run" circuit screenshots — every predesigned circuit, bench + schematic view.
//
//   node tools/capture/circuit-shots.js [--url http://localhost:8088/logic_studio.html]
//        [--out docs/img] [--no-connect] [--hold] [--theme dark|light] [--scale 1.5] [--only a,c]
//
// Run it in the Codespace against the live Studio (./qemu/studio.sh serves it on :8088 with the twin):
// it presses Connect twin headlessly (connectTwin(twinDefaultUrl()), no prompt dialog), opens the Circuit
// tab, and for each circuit waits for real readback before capturing docs/img/part5-circuit-<id>-<view>.png.
// --hold also holds the circuit's traced switch (e.g. S1 START) while capturing, then releases it.
// --no-connect captures the not-connected (idle) state, e.g. against a static server of web/public.
// The drawing never animates anything that is not reported live, so a capture shows what the twin did.
const {chromium}=require('playwright'),path=require('path'),fs=require('fs');
const arg=(k,d)=>{const i=process.argv.indexOf('--'+k);return i<0?d:(process.argv[i+1]&&!process.argv[i+1].startsWith('--')?process.argv[i+1]:true);};
const URL=arg('url','http://localhost:8088/logic_studio.html'),OUT=path.resolve(arg('out',path.join(__dirname,'../../docs/img')));
const CONNECT=!process.argv.includes('--no-connect'),HOLD=process.argv.includes('--hold'),THEME=arg('theme','dark'),SCALE=+arg('scale',1.5),ONLY=arg('only',null);
(async()=>{
  fs.mkdirSync(OUT,{recursive:true});
  const b=await chromium.launch({args:['--no-sandbox']});
  const pg=await b.newPage({viewport:{width:1600,height:1400},deviceScaleFactor:SCALE});
  const errs=[];pg.on('pageerror',e=>errs.push(e.message));
  await pg.goto(URL,{waitUntil:'networkidle'});
  await pg.evaluate(t=>{try{closeModal();}catch(e){}try{setTheme(t);}catch(e){}},THEME);
  if(CONNECT){
    await pg.evaluate(()=>connectTwin(twinDefaultUrl()));
    const ok=await pg.waitForFunction(()=>typeof conn!=='undefined'&&conn===true,null,{timeout:15000}).then(()=>true,()=>false);
    if(!ok){console.log('circuit-shots: could not connect the twin at '+URL+' (is ./qemu/studio.sh running?). Use --no-connect for idle shots.');await b.close();process.exit(2);}
  }
  await pg.evaluate(()=>{AppView.set('circuit');CircuitWorkspace.setSub('wire');CircuitWorkspace.setTrace(true);});
  const ids=await pg.evaluate(()=>CircuitCatalog.CIRCUITS.map(c=>c.id));
  const done=[];
  for(const id of ids){if(ONLY&&!String(ONLY).split(',').includes(id))continue;
    await pg.evaluate(id=>CircuitWorkspace.select(id),id);
    if(CONNECT){ // wait for live readback (the workspace polls /field at ~4 Hz and io at 1 Hz)
      await pg.waitForFunction(()=>/Live/.test(document.getElementById('cwConn').textContent),null,{timeout:10000}).catch(()=>console.log('  (no live readback for circuit '+id+')'));
      await pg.waitForTimeout(1500);}
    const sw=HOLD?await pg.evaluate(id=>{const c=CircuitCatalog.byId(id);return c.path&&c.path.sw||null;},id):null;
    if(sw){await pg.evaluate(s=>CircuitWorkspace.press(s),sw);await pg.waitForTimeout(1800);}
    for(const view of ['bench','schematic']){
      await pg.evaluate(v=>CircuitWorkspace.setView(v),view);await pg.waitForTimeout(CONNECT?900:250);
      // grow the viewport so the drawing and the signal-path strip are fully on screen, then clip to them
      const box=await pg.evaluate(()=>{const s=document.getElementById('cwStage').getBoundingClientRect(),p=document.getElementById('cwPath').getBoundingClientRect(),w=document.getElementById('cwWire');return{x:s.x,y:s.y,w:s.width,h:p.bottom-s.top+8,scroll:w.scrollTop,top:s.top};});
      const need=Math.ceil(box.top+box.h+40);if(need>1400)await pg.setViewportSize({width:1600,height:need});
      const r=await pg.evaluate(()=>{const s=document.getElementById('cwStage').getBoundingClientRect(),p=document.getElementById('cwPath').getBoundingClientRect();return{x:s.x,y:s.y,w:s.width,h:p.bottom-s.top+8};});
      const file=path.join(OUT,`part5-circuit-${id}-${view}.png`);
      await pg.screenshot({path:file,clip:{x:r.x,y:r.y,width:r.w,height:r.h}});done.push(path.relative(process.cwd(),file));
      if(need>1400)await pg.setViewportSize({width:1600,height:1400});
    }
    if(sw)await pg.evaluate(s=>CircuitWorkspace.release(s),sw);
  }
  await pg.evaluate(()=>AppView.set('logic'));   // leaving the tab releases any input it drove
  await pg.waitForTimeout(400);
  await b.close();
  console.log(done.join('\n'));
  if(errs.length){console.log('page errors: '+errs.slice(0,3).join(' | '));process.exit(1);}
})().catch(e=>{console.log('circuit-shots failed: '+e.message);process.exit(1);});
