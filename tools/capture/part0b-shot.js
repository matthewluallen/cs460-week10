// One command, a clean Step-0b screenshot for docs/img/part0b-studio-connected.png:
// the Logic Studio connected to the twin with the Motor start/stop + E-stop program
// loaded and validated. Headless (Playwright) -> no OS window chrome (unlike a macOS
// screencapture, which leaked the Dock into the frame). Run inside the Codespace with
// the twin + Studio up (./start.sh):
//   node tools/capture/part0b-shot.js
// Uses the same .capture playwright as studio-shots.js:
//   (cd .capture && npm i playwright && npx playwright install chromium)
const PW='/workspaces/cs460-week10/.capture/node_modules/playwright';
const {chromium}=require(PW);
const OUT='/workspaces/cs460-week10/docs/img/';
const BASE='http://localhost:8088';
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const b=await chromium.launch({args:['--no-sandbox']});
  const ctx=await b.newContext({viewport:{width:1400,height:900},deviceScaleFactor:2});
  const p=await ctx.newPage();
  const errs=[];
  p.on('pageerror',e=>errs.push(String(e).slice(0,100)));
  p.on('dialog',d=>d.dismiss().catch(()=>{}));
  await p.goto(BASE+'/logic_studio.html',{waitUntil:'domcontentloaded'});
  await sleep(2000);
  await p.keyboard.press('Escape').catch(()=>{});        // dismiss the startup project picker
  await sleep(400);
  await p.evaluate(()=>{try{connectTwin(twinDefaultUrl());}catch(e){}});
  await sleep(3000);
  await p.evaluate(()=>{try{openExampleProjects();}catch(e){}});
  await sleep(1000);
  await p.click('[data-example="motor-start-stop-estop"]',{timeout:4000}).catch(()=>{});
  await sleep(2000);
  await p.locator('button:has-text("Validate")').first().click({timeout:3000}).catch(()=>{});
  await sleep(2000);
  await p.locator('button:has-text("Load application")').first().click({timeout:3000}).catch(()=>{});
  await sleep(3000);
  await p.screenshot({path:OUT+'part0b-studio-connected.png'});
  console.log('OK   part0b -> part0b-studio-connected.png'+(errs.length?('  [errs: '+errs.slice(0,3).join(' | ')+']'):''));
  await b.close();
})().catch(e=>{console.error('FATAL',e);process.exit(1);});
