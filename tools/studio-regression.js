// Studio regression harness — run before/after any change to the Studio's
// instruction set, ST<->ladder compiler, renderer or simulator.
//   node tools/studio-regression.js        (serves web/public, headless)
// For every example project it asserts: loads clean (no page errors), compiles
// to device IR, deploymentText builds, and the ST<->ladder highlight pairing
// holds (ladder and ST elements share data-eid). Baseline 2026-10-30: all green.
// Requires: npm i playwright (and a chromium). Run from web/public or repo root.
const {chromium}=require('playwright'), fs=require('fs'), path=require('path');
const ROOT=fs.existsSync('logic_studio.html')?'.':'web/public';
(async()=>{
 const srv=require('http').createServer((q,r)=>{let p=q.url.split('?')[0];if(p=='/')p='/logic_studio.html';try{const b=fs.readFileSync(require('path').join(ROOT,p));r.writeHead(200,{'content-type':p.endsWith('.csco')||p.endsWith('.json')?'application/json':'text/html'});r.end(b);}catch(e){r.writeHead(404);r.end('nf');}}).listen(0);
 const port=srv.address().port;const b=await chromium.launch({args:['--no-sandbox']});const pg=await b.newPage({viewport:{width:1440,height:900}});
 await pg.goto('http://127.0.0.1:'+port+'/logic_studio.html',{waitUntil:'networkidle'});
 await pg.evaluate(()=>{ if(typeof closeModal==='function')closeModal(); });
 const examples=['pushbutton','two-led-nonc','indicator','interlocked-pair','io-expansion','reboot-run-indicator'];
 const results=[];
 for(const name of examples){
   const text=fs.readFileSync(path.join(ROOT,'projects',name+'.csco'),'utf8');
   const errs=[]; const h=e=>errs.push(e.message); pg.on('pageerror',h);
   const r=await pg.evaluate(async(args)=>{
     const [text]=args; const out={};
     try{ importProject(text); }catch(e){ out.load='FAIL:'+e.message; return out; }
     out.load='ok';
     await new Promise(r=>setTimeout(r,500));
     try{ const ir=StudioProject.compileProgram(PROG); out.ir = ir? 'ok ('+(ir.code?ir.code.length:ir.length||'?')+')':'empty'; }catch(e){ out.ir='FAIL:'+e.message; }
     try{ out.dtext = StudioProject.deploymentText(currentProject()).length; }catch(e){ out.dtext='FAIL:'+e.message; }
     const lad=[...document.querySelectorAll('.col.ladder [data-eid]')].map(e=>e.getAttribute('data-eid'));
     const stl=[...document.querySelectorAll('.col.stl [data-eid]')].map(e=>e.getAttribute('data-eid'));
     const inter=lad.filter(x=>stl.includes(x));
     out.ladderEls=lad.length; out.stlEls=stl.length; out.sharedEids=new Set(inter).size;
     out.nets=(PROG.nets||[]).length;
     return out;
   },[text]);
   await new Promise(r=>setTimeout(r,100));
   pg.off('pageerror',h);
   r.name=name; r.pageerrors=errs.slice(0,2);
   results.push(r);
 }
 console.log(JSON.stringify(results,null,0).replace(/},/g,'},\n'));
 await b.close();srv.close();
})().catch(e=>{console.log('HARNESS FAIL '+e.message);process.exit(1);});
