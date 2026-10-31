// Studio regression harness — run before/after any change to the Studio's
// instruction set, ST<->ladder compiler, renderer or simulator.
//   node tools/studio-regression.js        (serves web/public, headless)
// For every example project it asserts: loads clean (no page errors), compiles
// to device IR, deploymentText builds, and the ST<->ladder highlight pairing
// holds (ladder and ST elements share data-eid). Baseline 2026-10-30: all green.
// It also checks that sbom/firmware/factory/default-project.json is exactly the Studio's
// compile of projects/motor-start-stop-estop.csco (the factory/twin default program) and
// that the Studio's phone word sources match sbom/firmware/point_map.json.
// Requires: npm i playwright (and a chromium). Run from web/public or repo root.
const {chromium}=require('playwright'), fs=require('fs'), path=require('path');
const ROOT=fs.existsSync('logic_studio.html')?'.':'web/public';
(async()=>{
 const srv=require('http').createServer((q,r)=>{let p=q.url.split('?')[0];if(p=='/')p='/logic_studio.html';try{const b=fs.readFileSync(require('path').join(ROOT,p));r.writeHead(200,{'content-type':p.endsWith('.js')?'application/javascript':(p.endsWith('.csco')||p.endsWith('.json')?'application/json':'text/html')});r.end(b);}catch(e){r.writeHead(404);r.end('nf');}}).listen(0);
 const port=srv.address().port;const b=await chromium.launch({args:['--no-sandbox']});const pg=await b.newPage({viewport:{width:1440,height:900}});
 await pg.goto('http://127.0.0.1:'+port+'/logic_studio.html',{waitUntil:'networkidle'});
 await pg.evaluate(()=>{ if(typeof closeModal==='function')closeModal(); });
 const examples=['motor-start-stop-estop','pushbutton','two-led-nonc','indicator','interlocked-pair','io-expansion','reboot-run-indicator','reservoir-pid'];
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
 // Factory default + phone points: the payload seeded into the factory/twin NVS must be the
 // Studio's own compile of the example, and the Studio's phone word sources must match the
 // firmware point map (sbom/firmware/point_map.json phone_points).
 const repo=path.resolve(ROOT,'..','..');
 const factory=fs.readFileSync(path.join(repo,'sbom','firmware','factory','default-project.json'),'utf8');
 const pointMap=JSON.parse(fs.readFileSync(path.join(repo,'sbom','firmware','point_map.json'),'utf8'));
 const motor=fs.readFileSync(path.join(ROOT,'projects','motor-start-stop-estop.csco'),'utf8');
 const extra=await pg.evaluate(([text,want,phone])=>{
   const {project}=StudioProject.parseProject(text);
   return {factory_payload: StudioProject.deploymentText(project)===want,
           phone_sources: JSON.stringify(StudioProject.PHONE_POINTS)===JSON.stringify(phone)};
 },[motor,factory,pointMap.phone_points.points.map(p=>p.name)]);
 results.push({name:'factory+phone-map',load:'ok',ir:extra.factory_payload?'ok (factory payload matches)':'FAIL:factory payload stale',
   dtext:extra.phone_sources?1:'FAIL:phone sources differ from point_map.json',sharedEids:1,pageerrors:[]});
 console.log(JSON.stringify(results,null,0).replace(/},/g,'},\n'));
 await b.close();srv.close();
 // CI gate: a regression must fail the build, not just print. Each example must load
 // clean (no page errors), compile to IR, build deployment text, and keep the ST<->ladder
 // pairing (at least one shared data-eid between the two columns).
 const bad = results.filter(r =>
   r.load!=='ok' || typeof r.ir!=='string' || r.ir.startsWith('FAIL') || r.ir==='empty' ||
   typeof r.dtext!=='number' || (r.pageerrors&&r.pageerrors.length) || !(r.sharedEids>0));
 if(bad.length){ console.log('REGRESSION in: '+bad.map(r=>r.name).join(', ')); process.exit(1); }
 console.log('studio-regression: all '+results.length+' examples OK');
})().catch(e=>{console.log('HARNESS FAIL '+e.message);process.exit(1);});
