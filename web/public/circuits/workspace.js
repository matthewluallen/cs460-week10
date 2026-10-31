/* Circuit workspace — Part 1 "Wire & run": the predesigned snap-kit circuits, full width in the Studio's
 * Circuit tab. Bench view and schematic view of the same netlist, a live signal-path strip with honest
 * provenance tags, and keyboard-operable switches.
 *
 * Live behaviour (nothing is ever animated unless a target is connected and reporting):
 *   Twin (Connect twin → WebSocket /ws, served by qemu/twin-bridge.py):
 *     inputs  → POST /field?bit=N&v=0|1 sets the twin's AW9523 input bit AT THE CHIP; the firmware reads
 *               it over the emulated I²C bus on its next scan. Loading the motor circuit sets the whole
 *               byte to its rest state (IN3 = 1: the NC E-stop is closed/healthy).
 *     outputs ← GET /field (~4 Hz) `relays` (the expander's output register) + the console `io` reply
 *               (cmd/act, forced flags) + Modbus DI mirror coils (the logical input image).
 *   Physical board (Connect USB → Web Serial): no /field. Inputs can only be driven with the console
 *     `force IXn`, which writes the input image and BYPASSES the opto, the expander and the I²C read —
 *     the signal-path strip says so. Outputs come from `io`.
 *   Not connected: the circuit is drawn idle with a "not connected" state.
 */
(function(){
'use strict';
const SK=window.SnapKit,CC=window.CircuitCatalog;if(!SK||!CC)return;
const $=id=>document.getElementById(id),esc=SK.esc,txt=SK.txt;
const VBW=1600;

/* ================================================================= state */
const ST={lcdUrl:'',lcdAt:0,cur:'a',view:'bench',trace:true,sub:'wire',phoneSel:'orient',light:'room',ch1Set:false,active:false,
  sw:{},held:{},conn:'none',sock:null,
  field:{ok:null,di:null,relays:null,analog:null,analogNote:'',at:0,err:'',busy:false,next:0},
  io:{di:null,forced:null,cmd:null,act:null,at:0,last:0},
  mirror:{di:null,at:0,next:0},regs:{v:{},at:0,next:0},bus:{r59:0,w59:0},
  ourForce:{},restKey:'',restP:null,epoch:0,renderKey:'',msg:'',msgAt:0,hopPulse:null};
try{const s=JSON.parse(localStorage.getItem('cw-state')||'{}');if(s.cur&&CC.byId(s.cur))ST.cur=s.cur;if(s.view)ST.view=s.view;if(typeof s.trace==='boolean')ST.trace=s.trace;if(s.phoneSel)ST.phoneSel=s.phoneSel;if(s.light)ST.light=s.light;}catch(e){}
function save(){try{localStorage.setItem('cw-state',JSON.stringify({cur:ST.cur,view:ST.view,trace:ST.trace,phoneSel:ST.phoneSel,light:ST.light}));}catch(e){}}
const circuit=()=>CC.byId(ST.cur)||CC.CIRCUITS[0];
function themeName(){const t=document.documentElement.getAttribute('data-theme');if(t==='light'||t==='dark')return t;return matchMedia&&matchMedia('(prefers-color-scheme: light)').matches?'light':'dark';}
function connKind(){try{if(typeof wsSock!=='undefined'&&wsSock&&wsSock.readyState===1)return 'twin';}catch(e){}try{if(typeof port!=='undefined'&&port&&typeof conn!=='undefined'&&conn)return 'usb';}catch(e){}return 'none';}
const bits=(b,n)=>Array.from({length:n},(_,i)=>(b>>i)&1);
function resetSwitches(c){ST.sw={};ST.held={};for(const p of c.parts)if(p.control)ST.sw[p.id]=!!p.rest;}
resetSwitches(circuit());

/* ================================================================= the controller's screen */
/* The Logic view's panel canvas is the one source of the StampLC screen: on the twin it shows the REAL
 * frame the firmware pushed to the ST7789 (GET /lcd); on a board or offline, a labelled mirror of the
 * firmware's screens. The bench drawing shows a copy of it (refreshed in place, no re-render) and opens
 * the same enlarge view; A/B/C on the drawing press the device's panel buttons. */
const lcdSource=()=>{try{return window.heroLcdSource?window.heroLcdSource():{kind:'emulated',short:'—',label:'StampLC screen'};}catch(e){return{kind:'emulated',short:'—',label:'StampLC screen'};}};
let lcdScratch=null;
function refreshLcd(force){const src=document.getElementById('heroLcd');if(!src)return;const now=performance.now();if(!force&&now-ST.lcdAt<500)return;ST.lcdAt=now;
  try{if(!lcdScratch){lcdScratch=document.createElement('canvas');lcdScratch.width=480;lcdScratch.height=270;}
    const g=lcdScratch.getContext('2d');g.imageSmoothingEnabled=false;g.drawImage(src,0,0,480,270);ST.lcdUrl=lcdScratch.toDataURL('image/png');
    document.querySelectorAll('#cwStage [data-lcd-mirror]').forEach(im=>im.setAttribute('href',ST.lcdUrl));}catch(e){}}
function lcdOpen(){try{if(typeof window.enlargeLcd==='function')window.enlargeLcd();}catch(e){}}
async function lcdKey(k){try{if(typeof window.heroPress!=='function')return;const ok=await window.heroPress(k);
  if(ok===false){note('The controller did not take panel button '+k.toUpperCase()+' (the console is busy, or Monitor is read-only) — see the console line.');return;}
  setTimeout(()=>refreshLcd(true),120);setTimeout(()=>refreshLcd(true),1400);}catch(e){}}

/* ================================================================= view-model */
function buildVm(c){const now=performance.now(),k=ST.conn;
  const fieldFresh=k==='twin'&&ST.field.ok&&now-ST.field.at<2000,ioFresh=k!=='none'&&ST.io.at&&now-ST.io.at<3500;
  const live=k!=='none'&&(fieldFresh||ioFresh);
  let inb=Array(8).fill(null),relay=Array(4).fill(null),inSrc=null,relSrc=null;
  if(fieldFresh){inb=bits(ST.field.di,8);relay=bits(ST.field.relays,4);inSrc='chip';relSrc='chip';}
  else if(ioFresh){inb=ST.io.di?ST.io.di.slice():inb;relay=(ST.io.act||ST.io.cmd||relay).slice();inSrc='io';relSrc='io';}
  const v={live,conn:k,in:inb,relay,inSrc,relSrc,inForced:ioFresh&&ST.io.forced?ST.io.forced:Array(8).fill(0),
    cmd:ioFresh?ST.io.cmd:null,act:ioFresh?ST.io.act:null,ioDi:ioFresh?ST.io.di:null,ioFresh,fieldFresh,
    mirror:(k==='twin'&&now-ST.mirror.at<3000)?ST.mirror.di:null,
    mode:(typeof controllerStatus!=='undefined'&&controllerStatus&&controllerStatus.mode)||null,
    sw:ST.sw,held:ST.held,canDrive:k!=='none',on:{},
    busR:now-ST.bus.r59<2500,busW:now-ST.bus.w59<2500,
    regs:(k==='twin'&&now-ST.regs.at<4000)?ST.regs.v:null,monitorIW:monitorWords(),fieldErr:k==='twin'&&ST.field.ok===false?ST.field.err:''};
  if(c.phone){const ph=CC.PHONE_MAP;v.inNet=Array(8).fill(0);ph.forEach(s=>{if(s.bit!=null)v.inNet[s.bit]=1;});}
  const cond=e=>evalCond(e,v,c);v.cond=cond;
  for(const p of c.parts)if(p.on)v.on[p.id]=cond(p.on);
  return v;}
function monitorWords(){try{if(typeof monitorLive!=='undefined'&&monitorLive&&Array.isArray(monitorLive.IW)&&typeof sampleFresh==='function'&&sampleFresh())return monitorLive;}catch(e){}return null;}
function evalCond(expr,v,c){if(!expr||!v.live||expr==='never')return false;
  return String(expr).split('|').some(term=>{term=term.trim();let neg=false;if(term[0]==='!'){neg=true;term=term.slice(1);}let r=false;
    if(/^IN[1-8]$/.test(term))r=v.in[+term.slice(2)-1]===1;
    else if(/^RELAY[1-4]$/.test(term))r=v.relay[+term.slice(5)-1]===1;
    else if(term==='any'||term==='field')r=(c.consumers||[]).some(x=>evalCond(x.on,v,c));
    else if(term==='loads')r=(c.consumers||[]).some(x=>!x.input&&evalCond(x.on,v,c));
    else if(term==='inputs')r=(c.consumers||[]).some(x=>x.input&&evalCond(x.on,v,c));
    return neg?!r:r;});}
function currentFor(ma,v,c){if(typeof ma==='number')return ma;const xs=(c.consumers||[]).filter(x=>(ma==='loads'?!x.input:ma==='inputs'?x.input:1)&&evalCond(x.on,v,c));return xs.reduce((s,x)=>s+x.ma,0)||1;}

/* ================================================================= bench renderer */
function header(c,v,T,kind){let s=txt(40,44,c.title,{size:26,weight:800,fill:T.ink})+
  txt(40,68,(kind==='bench'?'Bench view · snap-kit parts wired to the StampLC · ':'Schematic view · same netlist, designators and live state · ')+c.subtitle,{size:14,fill:T.sub});
  const lbl=v.live?(v.conn==='twin'?'LIVE · twin':'LIVE · USB board'):(v.conn!=='none'?'CONNECTED · waiting for data':'NOT CONNECTED · idle');
  const w=lbl.length*8.6+44,x=VBW-40-w,col=v.live?T.okInk:T.idle;
  s+=`<g transform="translate(${x},22)"><rect width="${w}" height="34" rx="17" fill="${v.live?(T.name==='dark'?'#0f2e26':'#d9f6ee'):T.card}" stroke="${col}"${v.live?'':' stroke-dasharray="5 4"'}/>`+
    `<circle cx="19" cy="17" r="5.5" fill="${v.live?col:'none'}" stroke="${col}" stroke-width="2"/>`+txt(33,22.5,lbl,{size:13.5,weight:800,fill:v.live?col:T.sub})+`</g>`;
  return s;}
function partLabel(p,v,T){const L=p.label;if(!L)return '';const a=L.anchor||'start';let s='';
  if(L.inline){s+=txt(L.x,L.y,L.t,{size:14.5,weight:800,fill:T.ink,anchor:a});s+=txt(L.x+L.t.length*9.6+12,L.y,L.s||'',{size:12.5,fill:T.sub});}
  else{s+=txt(L.x,L.y,L.t,{size:L.small?13:14.5,weight:800,fill:T.ink,anchor:a});if(L.s)s+=txt(L.x,L.y+18,L.s,{size:12.5,fill:T.sub,anchor:a});if(L.s2)s+=txt(L.x,L.y+35,L.s2,{size:12.5,fill:L.warn?T.warnInk:T.sub,anchor:a});}
  if(p.sub2)s+=txt(p.sub2.x,p.sub2.y,p.sub2.t,{size:12.5,fill:T.sub});
  if(p.control&&v.held[p.id]){const x=L.x+L.t.length*9.6+16;s+=`<g transform="translate(${x},${L.y-13})"><rect width="52" height="18" rx="9" fill="${T.accentFill}" stroke="${T.accent}" stroke-width="1.2"/>`+txt(26,13,'HELD',{anchor:'middle',size:12.5,weight:900,fill:T.accentInk})+`</g>`;}
  return s;}
function leadTagSpots(c,leads,pts){ // controller-end sleeves: stagger neighbours on the top row, alternate x on the bottom harness
  const spots={};const top=leads.filter(l=>l.from.term&&pts[l.id][0][1]<pts[l.id][1][1]===false&&pts[l.id][1][1]<pts[l.id][0][1]);
  top.sort((a,b)=>pts[a.id][0][0]-pts[b.id][0][0]).forEach((l,i,arr)=>{const prev=arr[i-1];let d=30;
    if(prev&&pts[l.id][0][0]-pts[prev.id][0][0]<44&&spots[prev.id]&&spots[prev.id].d===30){const w=(l.tag.length+prev.tag.length)*4+24;if(w>pts[l.id][0][0]-pts[prev.id][0][0]-4)d=48;}
    const a=SK.along(pts[l.id],d);spots[l.id]={x:a.x,y:a.y,d};});
  const bottom=leads.filter(l=>l.from.term&&!spots[l.id]&&pts[l.id][1][1]>pts[l.id][0][1]);
  bottom.sort((a,b)=>pts[a.id][1][1]-pts[b.id][1][1]).forEach((l,i)=>{const P=pts[l.id];
    // first long horizontal run
    for(let k=1;k<P.length;k++){const a=P[k-1],b=P[k];if(a[1]===b[1]&&Math.abs(b[0]-a[0])>200){const x=Math.min(a[0],b[0])+120+(i%2)*56+(c.ctl.x-44);spots[l.id]={x,y:a[1]};return;}}
    const a=SK.along(P,36);spots[l.id]={x:a.x,y:a.y};});
  leads.filter(l=>!spots[l.id]).forEach(l=>{const a=SK.along(pts[l.id],Math.min(40,SK.polyLen(pts[l.id])/2));spots[l.id]={x:a.x,y:a.y};});
  return spots;}
function renderBench(c,v,T){
  const terms=Object.assign({},SK.stampTerms(c.ctl),c.expansion?SK.expTerms(c.expansion):{});
  const H=1000;let s=`<svg class="cw-svg" viewBox="0 0 ${VBW} ${H}" xmlns="http://www.w3.org/2000/svg" role="group" aria-label="${esc(c.title)} — bench view">`+`<title>${esc(c.title)} — bench view</title>`+SK.defs(T);
  s+=`<rect width="${VBW}" height="${H}" fill="url(#skMat)"/>`+header(c,v,T,'bench');
  // base board
  s+=`<rect x="600" y="80" width="960" height="880" rx="18" fill="url(#skBoard)" stroke="${T.boardStroke}" stroke-width="2"/><rect x="600" y="80" width="960" height="880" rx="18" fill="url(#skHoles)"/>`;
  if(c.fence){const f=c.fence;s+=`<rect x="${f.x}" y="${f.y}" width="${f.w}" height="${f.h}" rx="16" fill="${T.fenceFill}" fill-opacity="${T.name==='dark'?.35:.7}" stroke="${T.fence}" stroke-opacity=".75" stroke-width="1.6" stroke-dasharray="9 7"/>`+
    txt(f.x+16,f.y+22,f.title,{size:13,weight:800,fill:T.fenceInk,ls:'.4'})+f.lines.map((l,i)=>txt(f.x+16,f.y+40+i*16,l,{size:12.5,fill:T.fenceSub})).join('');}
  // network plane (phone circuit) — drawn on the bench, never copper
  if(c.phone)s+=phoneBench(c,v,T,terms);
  // conductor strips (level 1)
  for(const st of c.strips)s+=SK.strip(st,v,T);
  for(const st of c.strips)if(!st.noflow&&evalCond(st.on,v,c))s+=SK.stripFlow(st,currentFor(st.ma,v,c),st.dir);
  for(const st of c.strips)s+=SK.stripStuds(st);
  // snap modules (level 2)
  for(const p of c.parts)s+=`<g data-part="${p.id}" data-hop-target="part-${p.id}">`+SK.PARTS[p.type](p,v,T)+`</g>`;
  // controller + docked expansion
  const used=c.used||{};let xray='';
  if(ST.trace&&c.path){const P=c.path;
    if(P.kind==='wired')xray=SK.xrayPath(P.in-1,P.relay,{bypass:(v.live&&v.inSrc==='io'&&v.inForced[P.in-1])?[0,1]:null});
    else if(P.kind==='phone')xray=SK.xrayPath(0,P.relay,{network:1});
    else xray=SK.xrayPath(0,P.relay,{analog:1});}
  if(!ST.lcdUrl)refreshLcd(true);s+=SK.stamplc({x:c.ctl.x,y:c.ctl.y,used,xray,wifi:!!c.phone,wifiLabel:c.phone?'Wi-Fi · MQTT client':'',lcd:{href:ST.lcdUrl,label:lcdSource().short}},v,T);
  if(c.expansion)s+=SK.expansion(c.expansion,v,T);
  // trace glow under traced leads
  const pts={};for(const l of c.leads)pts[l.id]=SK.leadPoints(l,terms);
  if(ST.trace)s+=`<g fill="none" stroke="${T.accent}" stroke-linecap="round" stroke-linejoin="round" opacity="${T.name==='dark'?.28:.22}" filter="url(#skBlur)">`+c.leads.filter(l=>l.trace).map(l=>`<path d="${SK.roundPath(pts[l.id],l.r)}" stroke-width="22"/>`).join('')+`</g>`;
  // leads
  s+=`<g fill="none" stroke-linecap="round" stroke-linejoin="round">`;
  for(const l of c.leads)s+=`<g filter="url(#skLeadSh)">${SK.leadBody(l,pts[l.id],T)}</g>`;
  if(c.jumper){const m=terms['-'],cc=terms['C'];const y0=m.wire[1]+1;const d=`M${cc.x},${y0} C${cc.x},${y0-27} ${m.x},${y0-27} ${m.x},${y0}`;const W=SK.wireColours(T).minus;
    s+=`<g filter="url(#skLeadSh)"><path d="${d}" stroke="${W.casing}" stroke-width="8"/><path d="${d}" stroke="${W.core}" stroke-width="5.4"/></g>`;
    if(evalCond('inputs',v,c))s+=`<path d="${d}" stroke="#fff" stroke-width="2.2" stroke-dasharray="0.1 14" class="sk-flowr" style="animation-duration:${SK.flowDur(1.25)}" opacity=".85"/>`;}
  for(const l of c.leads)if(evalCond(l.on,v,c)&&!(l.noflow))s+=SK.leadFlow(l,pts[l.id],currentFor(l.ma,v,c),l.dir||1);
  s+=`</g>`;
  // screw ends, sleeves, clips
  const minusTwin=!!c.jumper;
  for(const l of c.leads){if(l.from.term)s+=SK.screwEnd(terms[l.from.term],l.fn,T,minusTwin&&l.from.term==='-');if(l.to.term)s+=SK.screwEnd(terms[l.to.term],l.fn,T,false);}
  if(c.jumper){const cc=terms['C'];s+=SK.screwEnd(cc,'minus',T,false);}
  const spots=leadTagSpots(c,c.leads,pts);
  for(const l of c.leads){const sp=spots[l.id];if(sp)s+=SK.sleeve(sp.x,sp.y,l.tag,l.fn,T);
    const P=pts[l.id],last=P[P.length-1],prev=P[P.length-2],segL=Math.hypot(last[0]-prev[0],last[1]-prev[1]);
    if(l.to.stud&&segL>=110){const a=SK.along(P,52,true);s+=SK.sleeve(a.x,a.y,l.tag,l.fn,T);}
    if(l.to.stud)s+=SK.clip(l.to.stud[0],l.to.stud[1],l.fn,T);}
  // labels, captions, cards
  for(const p of c.parts)s+=partLabel(p,v,T);
  for(const x of c.crossings||[])s+=txt(x.x,x.y,x.t,{size:12.5,fill:T.sub,anchor:x.anchor||'start'});
  for(const n of c.notes||[])s+=SK.noteCard(n,T);
  s+=readoutCard(c,v,T);
  if(isAnalog(c))s+=lightCard(c,v,T);
  if(c.legend)s+=SK.legend(c.legend.x,c.legend.y,c.legend.fns,T);
  return s+`</svg>`;}

/* ---------------------------------------------------------------- phone block (bench) */
function phoneBench(c,v,T,terms){const ph=c.phone,b=ph.broker,sel=CC.PHONE_MAP.find(x=>x.id===ST.phoneSel)||CC.PHONE_MAP[1];
  const w=terms.wifi,live=v.live,mapped=!!sel.point;let s='';
  const jackY=b.y+b.h/2,jackX=ph.x;
  // link: controller Wi-Fi badge → up → broker → phone jack  (dashed, reddish purple; ends only at network jacks)
  const p1=[[w.x,w.y-19],[w.x,jackY],[b.x,jackY]],p2=[[b.x+b.w,jackY],[jackX-17,jackY]];
  s+=SK.netLink(p1,T,{packets:1,live,dim:!live||!mapped})+SK.netLink(p2,T,{packets:1,live,dim:!live||!mapped});
  s+=`<g transform="translate(${b.x},${b.y})"><rect width="${b.w}" height="${b.h}" rx="12" fill="${T.card}" stroke="${T.net}" stroke-width="1.6"/>`+
     txt(14,22,'MQTT broker (phone bridge)',{size:13,weight:800,fill:T.ink})+txt(14,42,mapped?`phone/<id>/${sel.topic} →`:`phone/<id>/${sel.topic} (not ingested)`,{size:12.5,fill:T.netInk,mono:1})+txt(14,60,`← ${CC.PHONE_SCREEN.topic}`,{size:12.5,fill:T.netInk,mono:1})+`</g>`;
  s+=txt(b.x,b.y-12,'Wi-Fi · keyed WebSocket · MQTT :1883',{size:12.5,fill:T.sub});
  // the card
  const x=ph.x,y=ph.y,W=ph.w,Hh=ph.h;s+=`<g transform="translate(${x},${y})"><rect width="${W}" height="${Hh}" rx="16" fill="${T.card}" stroke="${T.net}" stroke-opacity=".8" stroke-width="1.6"/>`;
  s+=`<rect x="30" y="14" width="22" height="36" rx="5" fill="none" stroke="${T.ink}" stroke-width="2"/><rect x="37" y="44" width="8" height="2" rx="1" fill="${T.ink}"/>`;
  s+=txt(64,30,'PHONE BLOCK',{size:15,weight:900,fill:T.ink})+txt(64,48,'network source · paired by QR · no studs',{size:12.5,fill:T.sub});
  s+=`<line x1="12" y1="62" x2="${W-12}" y2="62" stroke="${T.cardRule}"/>`;
  s+=txt(16,82,'SOURCE · pick any phone sensor',{size:12.5,weight:800,fill:T.mut,ls:'.4'});
  CC.PHONE_MAP.forEach((sn,i)=>{const yy=92+i*31,on=sn.id===sel.id;
    s+=`<g class="sk-ctl" role="radio" tabindex="${on?0:-1}" aria-checked="${on}" data-phone="${sn.id}" aria-label="${esc(sn.name+' — '+(sn.point?'maps to '+sn.point:'not mapped by the firmware yet'))}">`+
      `<rect x="10" y="${yy}" width="${W-20}" height="27" rx="7" fill="${on?(T.name==='dark'?'#2a1830':'#f6e7f0'):'transparent'}" stroke="${on?T.net:'none'}"/><rect x="8" y="${yy-2}" width="${W-16}" height="31" rx="8" class="sk-focus"/>`+
      txt(22,yy+18,sn.name,{size:13,weight:on?800:600,fill:T.ink})+txt(150,yy+18,sn.scalar+(sn.ios?'':' · Android'),{size:12.5,fill:T.sub})+
      txt(W-20,yy+18,sn.point||'not mapped',{anchor:'end',size:12.5,weight:800,fill:sn.point?T.ink:T.mut,mono:1})+`</g>`;});
  const py=92+8*31+8;s+=`<line x1="12" y1="${py}" x2="${W-12}" y2="${py}" stroke="${T.cardRule}"/>`;
  s+=txt(16,py+20,'WHAT THE CONTROLLER SEES',{size:12.5,weight:800,fill:T.mut,ls:'.4'});
  const val=phoneValue(sel,v);
  s+=txt(16,py+56,val.text,{size:30,weight:800,fill:val.tag==='OBSERVED'?T.ink:T.mut,mono:1});
  s+=txt(16,py+78,val.sub,{size:12.5,fill:T.sub});
  if(val.tag)s+=SK.tag(val.tag,W-16-SK.tagWidth(val.tag),py+40,T);
  s+=txt(16,py+100,sel.thr?('alarm rung: '+sel.thr.text):(sel.note||''),{size:12.5,fill:T.cardInk});
  s+=txt(16,py+118,'authentication: none · ages out after 8 s',{size:12.5,fill:T.mut});
  const sy=py+132;s+=`<line x1="12" y1="${sy}" x2="${W-12}" y2="${sy}" stroke="${T.cardRule}"/>`;
  s+=txt(16,sy+22,'SCREEN ← relay0 R · 1 G · 2 B',{size:12.5,weight:800,fill:T.mut,ls:'.3'});
  const rgb=v.live&&v.relay[0]!=null?[v.relay[0],v.relay[1],v.relay[2]]:null;
  const col=rgb?`rgb(${rgb[0]*255},${rgb[1]*255},${rgb[2]*255})`:'none';
  s+=`<rect x="${W-76}" y="${sy+10}" width="60" height="34" rx="8" fill="${col}" stroke="${T.cardStroke}"${rgb?'':' stroke-dasharray="4 3"'}/>`+txt(W-46,sy+32,rgb?rgb.join(' '):'—',{anchor:'middle',size:12.5,weight:800,fill:rgb&&(rgb[1]||(rgb[0]&&rgb[2]))?'#10141a':T.sub,mono:1});
  s+=txt(16,sy+42,rgb?'command image · retained':'idle — no command image',{size:12.5,fill:T.sub});
  s+=`</g>`;
  s+=SK.netJack(jackX,jackY,T,!live||!mapped);
  return s;}
function phoneValue(sel,v){
  if(!sel.point)return{text:'not mapped',sub:sel.note||'',tag:'PENDING'};
  if(!v.live)return{text:sel.point+' = —',sub:'not connected',tag:null};
  if(sel.bit!=null){const b=v.mirror?v.mirror[sel.bit]:(v.monitorIW&&v.monitorIW.IX?(v.monitorIW.IX[sel.bit]?1:0):null);
    return b==null?{text:sel.point+' = —',sub:'input image not read yet',tag:null}:{text:`${sel.point} = ${b}`,sub:v.mirror?'Modbus DI mirror (input image incl. phone OR)':'Studio monitor',tag:'OBSERVED'};}
  let raw=null,src='';
  if(v.regs&&v.regs[sel.reg]!=null){raw=v.regs[sel.reg];src='Modbus reg '+sel.reg;}
  else if(v.monitorIW&&Array.isArray(v.monitorIW.IW)){const i=+sel.point.replace('%IW','');raw=v.monitorIW.IW[i];src='Studio monitor';}
  if(raw==null)return{text:sel.point+' = —',sub:'no reading yet (is a phone paired?)',tag:null};
  const sv=raw>32767?raw-65536:raw;
  return{text:`${sel.point} = ${sv}`,sub:`${src} · ${sel.scaleNote||sel.unit}${sel.scale>1?' → '+(sv/sel.scale).toFixed(1)+' '+sel.unit:''}`,tag:'OBSERVED'};}

/* ---------------------------------------------------------------- light on RP1 (analog circuit) */
/* The twin has no photoresistor. These presets stand in for the light level: the divider
 * arithmetic gives the CH1 voltage, and POST /field?ch1_mv= puts it on the twin's IO-expansion
 * channel at the chip, where the firmware reads it over I2C @0x20 like a wired sensor. */
const LIGHT=[{id:'bright',name:'Bright lamp',r:1000},{id:'room',name:'Room light',r:10000},
  {id:'hover',name:'Hand hovering',r:15000},{id:'covered',name:'Covered',r:60000}];
const lightMv=r=>Math.round(4450*r/(r+10000));          // 4.45 V field supply, 10 kΩ to +
const lightSel=()=>LIGHT.find(x=>x.id===ST.light)||LIGHT[1];
const fmtR=r=>r>=1000?(r/1000)+' kΩ':r+' Ω';
const isAnalog=c=>!!(c.path&&c.path.kind==='analog');
function lightCard(c,v,T){const x=1240,y=318,W=304,sel=lightSel(),a=ST.field.analog,tw=v.conn==='twin';let s=`<g transform="translate(${x},${y})">`;
  s+=`<rect width="${W}" height="312" rx="16" fill="${T.card}" stroke="${T.cardStroke}" stroke-width="1.4"/>`;
  s+=txt(16,28,'LIGHT ON RP1',{size:15,weight:900,fill:T.ink})+txt(16,46,tw?'twin: sets CH1 at the chip (QMP)':'board: shade the real photoresistor',{size:12.5,fill:T.sub});
  s+=`<line x1="12" y1="58" x2="${W-12}" y2="58" stroke="${T.cardRule}"/>`;
  LIGHT.forEach((L,i)=>{const yy=66+i*31,on=L.id===sel.id,mv=lightMv(L.r),alarm=mv>3000;
    s+=`<g class="sk-ctl" role="radio" tabindex="${on?0:-1}" aria-checked="${on}" data-light="${L.id}" aria-label="${esc(L.name+', photoresistor about '+fmtR(L.r)+', '+mv+' millivolts on CH1')}">`+
      `<rect x="10" y="${yy}" width="${W-20}" height="27" rx="7" fill="${on?T.accentFill:'transparent'}" stroke="${on?T.accent:'none'}"/><rect x="8" y="${yy-2}" width="${W-16}" height="31" rx="8" class="sk-focus"/>`+
      txt(22,yy+18,L.name,{size:13,weight:on?800:600,fill:T.ink})+txt(150,yy+18,'≈'+fmtR(L.r),{size:12.5,fill:T.sub})+
      txt(W-20,yy+18,mv+' mV',{anchor:'end',size:12.5,weight:800,fill:alarm?T.warnInk:T.ink,mono:1})+`</g>`;});
  const py=66+4*31+6;s+=`<line x1="12" y1="${py}" x2="${W-12}" y2="${py}" stroke="${T.cardRule}"/>`;
  s+=txt(16,py+20,'CONTROLLER READS',{size:12.5,weight:800,fill:T.mut,ls:'.4'});
  const iw=v.monitorIW&&Array.isArray(v.monitorIW.IW)?v.monitorIW.IW[0]:null;
  let big='CH1 = —',sub2=v.conn==='none'?'not connected':'no reading yet',tag=null;
  if(tw&&v.fieldFresh&&a&&a.writable){big=`CH1 = ${a.ch1_mv} mV`;sub2='chip register · your circuit owns CH1';tag='OBSERVED';}
  else if(tw&&v.fieldFresh&&a){big=`CH1 = ${a.ch1_mv} mV`;sub2='tank model owns CH1 · see the guide line';tag='PENDING';}
  else if(v.conn==='usb'&&iw!=null){big=`%IW0 = ${iw} mV`;sub2='Studio monitor (the real CH1)';tag='OBSERVED';}
  else if(v.conn==='usb'){sub2='start Monitor in Logic to read %IW0';}
  s+=txt(16,py+52,big,{size:26,weight:800,fill:tag==='OBSERVED'?T.ink:T.mut,mono:1})+txt(16,py+72,sub2,{size:12.5,fill:T.sub});
  if(tag)s+=SK.tag(tag,W-16-SK.tagWidth(tag),py+7,T);
  s+=txt(16,py+94,(tw&&iw!=null?`%IW0 = ${iw} mV · `:'')+'alarm sets > 3000 · clears < 2400 mV',{size:12.5,fill:T.cardInk});
  return s+`</g>`;}

/* ---------------------------------------------------------------- live readout card */
function readoutRows(c,v){const rows=[];const L=v.live;
  rows.push({l:'Field supply + / −',val:'4.45 V',tag:'NOMINAL'});
  {const ls=lcdSource();rows.push({l:'StampLC screen',val:ls.kind==='real'?'real frame':ls.kind==='mirror'?'mirror':'—',tag:ls.kind==='real'?'OBSERVED':ls.kind==='mirror'?'MODELLED':null});}
  for(const p of c.parts.filter(p=>p.control)){const n=+p.in.slice(2),b=v.in[n-1],f=v.inForced[n-1];
    const name=(p.label&&p.label.t||p.id).replace(/\s+/g,' ');
    let val='—';if(L&&b!=null)val=String(b)+(f?' forced':'')+(p.type==='slide'&&c.id==='c'?(b?' healthy':' TRIP'):'');
    rows.push({l:`${p.in} (di${n-1}) · ${name.split(' ').slice(1).join(' ')||name}`,val,tag:L&&b!=null?'OBSERVED':null});}
  for(let n=1;n<=4;n++)if(c.used['RELAY'+n]){let val='—';
    if(L){const chip=v.relSrc==='chip'?v.relay[n-1]:null,cm=v.cmd?v.cmd[n-1]:null,ac=v.act?v.act[n-1]:null;
      val=cm!=null?`cmd ${cm}·act ${ac}`:(chip!=null?'chip '+chip:'—');}   // cmd/act from io; else the 0x59 output register
    rows.push({l:`RELAY ${n} (relay${n-1})`,val,tag:L&&val!=='—'?'OBSERVED':null});}
  if(isAnalog(c)){let val='—',tag=null;const a=ST.field.analog;
    if(v.conn==='twin'&&v.fieldFresh&&a&&a.writable){val=a.ch1_mv+' mV';tag='OBSERVED';}
    else if(v.conn==='twin'&&v.fieldFresh&&a){val=a.ch1_mv+' mV';tag='PENDING';}
    else if(L&&v.monitorIW&&Array.isArray(v.monitorIW.IW)){val=v.monitorIW.IW[0]+' mV';tag='OBSERVED';}
    rows.push({l:'CH1 → %IW0 (divider)',val,tag});}
  if(c.phone){const sel=CC.PHONE_MAP.find(x=>x.id===ST.phoneSel)||CC.PHONE_MAP[1];const pv=phoneValue(sel,v);rows.push({l:`phone · ${sel.name}`,val:pv.text.replace(/^.* = /,''),tag:pv.tag});}
  for(const p of c.parts.filter(p=>p.on)){const on=v.on[p.id];const what={led:'LED',motor:'motor',buzzer:'buzzer',lamp:'lamp'}[p.type]||p.type;
    rows.push({l:`${p.id} ${what}`,val:L?(on?(p.type==='motor'?'running · ≈0.32 A':'on · ≈'+p.ma+' mA'):'off'):'—',tag:L?'MODELLED':null});}
  if(c.parts.some(p=>p.type==='battery'))rows.push({l:'B1 motor pack',val:'3.0 V',tag:'NOMINAL'});
  return rows;}
function readoutCard(c,v,T){const rows=readoutRows(c,v);const RH=22,w=304,h=54+rows.length*RH+30,x=1240,y=96;let s=SK.card(x,y,w,h,T,{stroke:T.cardStroke});
  const head=v.live?(v.mode?v.mode+' · ':'')+(v.conn==='twin'?'twin · readback live':'USB board · io readback'):v.conn!=='none'?'connected · waiting for io / field':'NOT CONNECTED · idle';
  s+=`<circle cx="18" cy="24" r="5" fill="${v.live?T.okInk:'none'}" stroke="${v.live?T.okInk:T.idle}" stroke-width="2"/>`+txt(30,29,head,{size:13,weight:800,fill:v.live?T.okInk:T.sub});
  s+=`<line x1="12" y1="42" x2="${w-12}" y2="42" stroke="${T.cardRule}"/>`;
  rows.forEach((r,i)=>{const yy=62+i*RH;s+=txt(14,yy,r.l,{size:12.5,fill:T.cardInk});s+=txt(r.tag?w-16-SK.tagWidth(r.tag)-8:w-16,yy,r.val,{anchor:'end',size:12.5,weight:800,fill:T.ink,mono:1});if(r.tag)s+=SK.tag(r.tag,w-16-SK.tagWidth(r.tag),yy-12.5,T);});
  const fy=62+rows.length*RH;
  s+=txt(14,fy+2,v.live?'observed = read back · modelled = drawn from bits':'Connect twin to drive and read it live.',{size:12.5,fill:T.mut});
  s+=txt(14,fy+19,'4.45 V: USB bench measurement (twin: 24 V)',{size:12.5,fill:T.mut});
  return s+`</g>`;}

/* ================================================================= schematic (generated from the netlist) */
function netFn(c,n,pinNet){const m=c.nets[n]||[];
  if(m.includes('U1.+')||/^B\+$/.test(n)||m.some(p=>/\.p$/.test(p)&&p.startsWith('B')))return 'plus';
  if(m.includes('U1.-')||m.some(p=>/^B\d\.n$/.test(p)))return 'minus';
  if(m.some(p=>/^U1\.IN\d$/.test(p))||m.some(p=>p.startsWith('X1.')))return 'signal';
  return 'load';}
function renderSchematic(c,v,T){
  const pinNet={};for(const [n,pins] of Object.entries(c.nets))pins.forEach(p=>pinNet[p]=n);
  const parts=Object.fromEntries(c.parts.map(p=>[p.id,p]));
  const pinsOf=id=>Object.keys(SK.partPins(parts[id]));const netOf=(id,pin)=>pinNet[id+'.'+pin];
  const FPp=pinNet['U1.+'],FPm=pinNet['U1.-'];const bat=c.parts.find(p=>p.type==='battery');const Bp=bat&&netOf(bat.id,'p'),Bm=bat&&netOf(bat.id,'n');
  const seen=new Set(bat?[bat.id]:[]);
  const partsOn=n=>(c.nets[n]||[]).map(s=>s.split('.')).filter(([id])=>parts[id]&&!seen.has(id)).map(([id,pin])=>({id,pin}));
  const other=(id,pin)=>{const o=pinsOf(id).find(x=>x!==pin);return{pin:o,net:netOf(id,o)};};
  function trace(net,ret){if(net===ret)return [];const items=partsOn(net);const taps=(c.nets[net]||[]).filter(s=>s.startsWith('X1.'));const seq=[];
    if(taps.length)seq.push({tap:taps[0],net});
    if(!items.length)return seq;
    if(items.length===1){const it=items[0];seen.add(it.id);const o=other(it.id,it.pin);return seq.concat([{part:it.id,inNet:net,outNet:o.net,fromPin:it.pin}],trace(o.net,ret));}
    const br=items.map(it=>{seen.add(it.id);const o=other(it.id,it.pin);return [{part:it.id,inNet:net,outNet:o.net,fromPin:it.pin}].concat(trace(o.net,ret));});
    return seq.concat([{par:br,net}]);}
  const col=fn=>fn==='plus'?'#e4572e':fn==='minus'?T.minusCasing:fn==='signal'?'#56b4e9':'#e69f00';
  const ncol=n=>col(netFn(c,n,pinNet));const ink=T.schInk;
  const L=150,R=1450,BX=640,BW=330;const W=(pts,n,w)=>SK.SCH.wire(pts,typeof n==='string'&&n[0]==='#'?n:ncol(n),w);
  let y=150,s='',defsTrace='';const traceNets=new Set();
  // --- input rungs
  const ins=[];for(let k=1;k<=8;k++){const n=pinNet['U1.IN'+k];if(!n)continue;const seq=trace(n,FPp);ins.push({k,n,seq});}
  const boxTop=y-48;let rowY=y+12;
  const elDraw=(e,x,yy)=>{const p=parts[e.part],on=!!v.on[p.id],cc=ink;
    if(p.type==='push'){const closed=v.live&&v.in[+p.in.slice(2)-1]===1;return SK.SCH.push(x,yy,cc,closed,!!p.nc);}
    if(p.type==='slide'){const closed=v.live?v.in[+p.in.slice(2)-1]===1:!!p.rest;return SK.SCH.slide(x,yy,cc,closed);}
    if(p.type==='resistor')return SK.SCH.res(x,yy,cc);if(p.type==='ldr')return SK.SCH.ldr(x,yy,cc);
    if(p.type==='led')return SK.SCH.led(x,yy,cc,on,p.hue);if(p.type==='diode')return SK.SCH.diode(x,yy,cc,e.fromPin==='k');
    if(p.type==='motor')return SK.SCH.motor(x,yy,cc,on);if(p.type==='buzzer')return SK.SCH.buzzer(x,yy,cc,on);if(p.type==='lamp')return SK.SCH.lamp(x,yy,cc,on);return '';};
  const elLabel=(e,x,yy)=>{const p=parts[e.part];const top=p.id+(p.label&&p.label.t?' '+p.label.t.replace(/^\S+\s+/,''):'');
    const sub=p.value||({led:(p.hue||'')+' LED',motor:'3 V DC',buzzer:'active',diode:'freewheel',ldr:'photoresistor',push:p.nc?'NC push':'NO push',slide:'slide · ON'}[p.type]||'');
    return txt(x,yy-30,top,{anchor:'middle',size:12.5,weight:800,fill:ink})+txt(x,yy+34,sub,{anchor:'middle',size:12.5,fill:T.sub});};
  for(const r of ins){const sw=r.seq.slice().reverse();const xsw=420;
    s+=`<g data-hop-target="sch-in${r.k}">`;
    s+=W([[L,rowY],[xsw-30,rowY]],FPp);sw.forEach(e=>{s+=elDraw(e,xsw,rowY)+elLabel(e,xsw,rowY);});
    s+=W([[xsw+30,rowY],[BX,rowY]],r.n)+SK.SCH.dot(L,rowY,col('plus'));
    s+=`<circle cx="${BX}" cy="${rowY}" r="5" fill="${T.schBg}" stroke="${ink}" stroke-width="2"/>`+txt(BX+12,rowY+4.5,`IN${r.k} (di${r.k-1})`,{size:13,weight:800,fill:ink});
    const b=v.live?v.in[r.k-1]:null;s+=txt(BX+120,rowY+4.5,b==null?'opto → 0x59':'opto → 0x59 · '+b,{size:12.5,fill:T.sub,mono:1});
    s+=`</g>`;if(ST.trace&&c.path&&c.path.in===r.k)traceNets.add('in'+r.k);rowY+=92;}
  // C terminal and the controller box
  let yC=null;if(pinNet['U1.C']){yC=rowY;s+=`<circle cx="${BX+BW}" cy="${yC}" r="5" fill="${T.schBg}" stroke="${ink}" stroke-width="2"/>`+txt(BX+BW-12,yC+4.5,'C (input common)',{anchor:'end',size:13,weight:800,fill:ink})+W([[BX+BW,yC],[R,yC]],FPm)+SK.SCH.dot(R,yC,col('minus'))+txt(BX+BW+18,yC-10,'jumper C → −  (sourcing wiring)',{size:12.5,fill:T.sub});rowY+=56;}
  if(c.phone){const py=Math.max(rowY,boxTop+70);const sel=CC.PHONE_MAP.find(x=>x.id===ST.phoneSel)||CC.PHONE_MAP[1];
    s+=`<g transform="translate(200,${py-44})"><rect width="300" height="112" rx="12" fill="${T.card}" stroke="${T.net}" stroke-width="1.6"/>`+txt(14,24,'PHONE (network source)',{size:13.5,weight:800,fill:T.ink})+txt(14,44,`${sel.name} · ${sel.scalar}`,{size:12.5,fill:T.cardInk})+txt(14,64,sel.point?`→ ${sel.point}`:'→ not mapped (firmware)',{size:12.5,weight:800,fill:sel.point?T.ink:T.mut,mono:1})+txt(14,84,'no copper · no terminal · no opto',{size:12.5,fill:T.sub})+txt(14,101,'authentication: none',{size:12.5,fill:T.mut})+`</g>`;
    s+=SK.netLink([[500,py],[BX,py]],T,{packets:1,live:v.live})+txt(530,py-12,'MQTT :1883',{size:12.5,fill:T.netInk,mono:1})+txt(530,py+22,sel.point?sel.topic:'not ingested',{size:12.5,fill:T.netInk,mono:1});
    s+=`<circle cx="${BX}" cy="${py}" r="6" fill="${T.schBg}" stroke="${T.net}" stroke-width="2"/>`+txt(BX+12,py+4.5,'Wi-Fi · MQTT client',{size:13,weight:800,fill:ink})+txt(BX+12,py+22,'→ point map '+(sel.point||''),{size:12.5,fill:T.sub,mono:1});
    rowY=py+90;}
  // coils (detached representation): inside the box, labelled with the firmware name
  const usedR=[1,2,3,4].filter(n=>pinNet[`U1.RELAY${n}.COM`]);
  const coilY=rowY+10;usedR.forEach((n,i)=>{const x=BX+60+i*86,on=v.live&&v.relay[n-1]===1;s+=`<g data-hop-target="sch-coil${n}">`+SK.SCH.coil(x,coilY,ink,on)+txt(x,coilY+30,'relay'+(n-1),{anchor:'middle',size:12.5,weight:800,fill:ink,mono:1})+txt(x,coilY+46,`RELAY ${n} coil`,{anchor:'middle',size:12.5,fill:T.sub})+`</g>`;});
  const boxBot=coilY+60;
  s=`<rect x="${BX}" y="${boxTop}" width="${BW}" height="${boxBot-boxTop}" rx="12" fill="none" stroke="${ink}" stroke-width="2"/>`+txt(BX+BW-12,boxTop+20,'StampLC · controller',{anchor:'end',size:13,weight:800,fill:ink})+
    txt(BX+BW-12,boxTop+37,'scan: read 0x59 → solve → write 0x59',{anchor:'end',size:12.5,fill:T.sub})+s;
  y=boxBot+70;
  // --- output rungs (field domain), contacts drawn detached with the board + firmware name
  const outRows=[],domRows=[];
  for(const n of usedR){const com=pinNet[`U1.RELAY${n}.COM`];for(const pin of ['NO','NC']){const qn=pinNet[`U1.RELAY${n}.${pin}`];if(!qn)continue;
    const ret=com===Bp?Bm:FPm;const seq=trace(qn,ret);(com===Bp?domRows:outRows).push({n,pin,com,qn,ret,seq});}}
  // remaining parts fed from + (e.g. the photoresistor divider)
  const extra=[];for(const it of partsOn(FPp)){seen.add(it.id);const o=other(it.id,it.pin);extra.push({seq:[{part:it.id,inNet:FPp,outNet:o.net,fromPin:it.pin}].concat(trace(o.net,FPm))});}
  function drawSeq(seq,x0,yy,startNet,ret,xR){let x=x0,out='',net=startNet,extraH=0;const pitch=150;
    for(let i=0;i<seq.length;i++){const e=seq[i];
      if(e.tap){out+=SK.SCH.dot(x+20,yy,ncol(e.net));out+=W([[x,yy],[x+20,yy]],e.net);out+=W([[x+20,yy],[x+20,yy+64]],e.net);
        out+=`<g transform="translate(${x-70},${yy+64})"><rect width="250" height="96" rx="10" fill="none" stroke="${ink}" stroke-width="2"/>`+
          `<circle cx="90" cy="0" r="5" fill="${T.schBg}" stroke="${ink}" stroke-width="2"/>`+txt(100,17,'CH1 IN+',{size:12.5,weight:800,fill:ink})+txt(12,40,'X1 · IO expansion @0x20',{size:12.5,weight:800,fill:ink})+txt(12,58,'0–10 V → ADC → I²C → %IW0 (mV)',{size:12.5,fill:T.sub})+
          txt(12,78,v.conn==='twin'?'twin: analog input pending':'needs the process binding',{size:12.5,fill:v.conn==='twin'?T.warnInk:T.sub})+
          `<circle cx="250" cy="80" r="5" fill="${T.schBg}" stroke="${ink}" stroke-width="2"/>`+txt(240,74,'ISO GND',{anchor:'end',size:12.5,weight:800,fill:ink})+`</g>`;
        out+=W([[x+185,yy+144],[xR,yy+144]],FPm)+SK.SCH.dot(xR,yy+144,col('minus'));extraH=Math.max(extraH,178);x+=20;net=e.net;continue;}
      if(e.par){const branches=e.par;out+=SK.SCH.dot(x+24,yy,ncol(e.net));out+=W([[x,yy],[x+24,yy]],e.net);
        branches.forEach((b,bi)=>{const by=yy+bi*100;if(bi)out+=W([[x+24,yy+(bi-1)*100],[x+24,by]],e.net);
          const r=drawSeq(b,x+24,by,e.net,ret,xR);out+=r.svg;extraH=Math.max(extraH,bi*100+r.h);});
        return{svg:out,h:extraH};}
      const ex=x+pitch-30;out+=W([[x,yy],[ex-30,yy]],net)+elDraw(e,ex,yy)+elLabel(e,ex,yy);x=ex+30;net=e.outNet;}
    out+=W([[x,yy],[xR,yy]],net)+SK.SCH.dot(xR,yy,ncol(net));return{svg:out,h:extraH};}
  const contactLabel=(r)=>`RELAY ${r.n} ${r.pin==='NO'?'N.O':'N.C'}`;
  const contactSvg=(r,x,yy)=>{const on=v.live&&v.relay[r.n-1]===1;const closed=r.pin==='NO'?on:(v.live?!on:true);
    return (r.pin==='NO'?SK.SCH.no(x,yy,ink,closed):SK.SCH.nc(x,yy,ink,closed))+txt(x,yy-26,contactLabel(r),{anchor:'middle',size:12.5,weight:800,fill:ink})+txt(x,yy+30,`(relay${r.n-1})`,{anchor:'middle',size:12.5,fill:T.sub,mono:1});};
  for(const r of outRows){const xc=330;s+=`<g data-hop-target="sch-out${r.n}${r.pin}">`+W([[L,y],[xc-30,y]],r.com)+SK.SCH.dot(L,y,col('plus'))+contactSvg(r,xc,y);
    const d=drawSeq(r.seq,xc+30,y,r.qn,r.ret,R);s+=d.svg+`</g>`;if(ST.trace&&c.path&&c.path.relay===r.n&&r.pin===c.path.contact)traceNets.add('out'+r.n);y+=90+d.h;}
  for(const r of extra){s+=W([[L,y],[L+40,y]],FPp)+SK.SCH.dot(L,y,col('plus'));const d=drawSeq(r.seq,L+40,y,FPp,FPm,R);s+=d.svg;y+=90+d.h;}
  const railBot=y-50;
  s=W([[L,130],[L,railBot]],col('plus'),3)+W([[R,130],[R,railBot]],col('minus'),3)+txt(L-14,118,'+  field supply (4.45 V on USB)',{size:13,weight:800,fill:col('plus')})+txt(R,118,'−',{size:16,weight:900,fill:ink,anchor:'middle'})+s;
  // --- separate supply domain
  if(domRows.length){const top=y,dx0=520,dx1=1520,bp=640,bm=1400;let yy=top+104;let body='';
    for(const r of domRows){const xc=bp+180;body+=W([[bp,yy],[xc-30,yy]],'#e4572e')+SK.SCH.dot(bp,yy,'#e4572e')+contactSvg(r,xc,yy);const d=drawSeq(r.seq,xc+30,yy,r.qn,r.ret,bm);body+=d.svg;yy+=90+d.h;}
    const bot=yy-20;s+=`<rect x="${dx0}" y="${top}" width="${dx1-dx0}" height="${bot-top+20}" rx="16" fill="none" stroke="${T.fence}" stroke-width="1.6" stroke-dasharray="9 7"/>`+
      txt(dx0+18,top+24,'MOTOR LOOP · OWN 3 V PACK',{size:13,weight:800,fill:T.fenceInk})+txt(dx0+18,top+42,'no copper path to the controller’s + / − · the RELAY 1 contact is the only link',{size:12.5,fill:T.fenceSub});
    s+=W([[bp,top+60],[bp,bot]],'#e4572e',3)+W([[bm,top+60],[bm,bot]],T.minusCasing,3)+txt(bp,top+56,'B+',{anchor:'middle',size:13,weight:800,fill:'#e4572e'})+txt(bm,top+56,'B−',{anchor:'middle',size:13,weight:800,fill:ink});
    const bx=580,by=(top+60+bot)/2;s+=W([[bx,top+60],[bp,top+60]],'#e4572e')+W([[bx,top+60],[bx,by-30]],'#e4572e')+SK.SCH.battery(bx,by,ink)+txt(bx-14,by+4,'B1',{anchor:'end',size:12.5,weight:800,fill:ink})+txt(bx-14,by+20,'3 V',{anchor:'end',size:12.5,fill:T.sub})+
      W([[bx,by+30],[bx,bot+8],[bm,bot+8],[bm,bot]],T.minusCasing);
    s+=body;y=bot+50;}
  const H=Math.max(560,y+20);
  let out=`<svg class="cw-svg" viewBox="0 0 ${VBW} ${H}" xmlns="http://www.w3.org/2000/svg" role="group" aria-label="${esc(c.title)} — schematic view"><title>${esc(c.title)} — schematic view</title>`+SK.defs(T)+
    `<rect width="${VBW}" height="${H}" fill="${T.schBg}"/><pattern id="skSchGrid" width="20" height="20" patternUnits="userSpaceOnUse"><path d="M20,0 V20 H0" fill="none" stroke="${T.schGrid}" stroke-width="1"/></pattern><rect y="84" width="${VBW}" height="${H-84}" fill="url(#skSchGrid)"/>`+header(c,v,T,'schematic');
  if(ST.trace&&traceNets.size)out+=`<style>.cw-svg [data-hop-target="sch-in${c.path.in}"],.cw-svg [data-hop-target="sch-out${c.path.relay}${c.path.contact}"]{filter:drop-shadow(0 0 5px ${T.accent})}</style>`;
  out+=s+`</svg>`;return out;}

/* ================================================================= parts gallery (the reusable library) */
function renderGallery(T){const v={live:false,sw:{},held:{},on:{}};const H=980;let s=`<svg class="cw-svg" viewBox="0 0 ${VBW} ${H}" role="img" aria-label="Snap-kit parts library"><title>Snap-kit parts library</title>`+SK.defs(T)+`<rect width="${VBW}" height="${H}" fill="url(#skMat)"/>`;
  s+=txt(40,44,'Snap-kit parts library',{size:26,weight:800,fill:T.ink})+txt(40,68,'original snap-block style · one 40-unit grid · printed IEC symbols · colour = category · reused by every circuit and the future designer',{size:14,fill:T.sub});
  s+=`<rect x="20" y="88" width="1560" height="872" rx="18" fill="url(#skBoard)" stroke="${T.boardStroke}" stroke-width="2"/><rect x="20" y="88" width="1560" height="872" rx="18" fill="url(#skHoles)"/>`;
  const items=[
    [{id:'B1',type:'battery',at:[100,180],span:4,cells:2},'Battery block 2×AA','3.0 V · power (dark on purpose)'],
    [{id:'B2',type:'battery',at:[380,180],span:5,cells:3},'Battery block 3×AA','4.5 V'],
    [{id:'S1',type:'push',at:[700,180],span:4,cap:'green',glyph:'I'},'Press switch · NO','momentary'],
    [{id:'S2',type:'push',at:[980,180],span:4,cap:'red',glyph:'O',nc:1},'Press switch · NC','designer only (the kit has none)'],
    [{id:'S3',type:'slide',at:[1260,180],span:4},'Slide switch','maintained · ON / OFF'],
    [{id:'S4',type:'slide',at:[100,340],span:4,estop:1},'E-stop (slide, hazard band)','closed = healthy (fail-safe)'],
    [{id:'S5',type:'reed',at:[380,340],span:4},'Reed switch','NO · magnet · never motor current'],
    [{id:'H1',type:'led',at:[700,340],span:3,hue:'green',glyph:'▶'},'LED green','needs a series resistor'],
    [{id:'H2',type:'led',at:[940,340],span:3,hue:'red',glyph:'!'},'LED red',''],
    [{id:'H3',type:'led',at:[1180,340],span:3,hue:'amber',glyph:'0',builtinR:1},'LED amber · built-in R','variant: resistor printed on it'],
    [{id:'L1',type:'lamp',at:[1420,340],span:3},'Lamp','battery domain only'],
    [{id:'M1',type:'motor',at:[100,520],span:4},'Motor + fan','3 V · own supply + freewheel diode'],
    [{id:'BZ1',type:'buzzer',at:[380,520],span:3},'Buzzer (active)','10–40 mA'],
    [{id:'RP1',type:'ldr',at:[620,520],span:3},'Photoresistor','≈1 kΩ bright · ≈∞ dark'],
    [{id:'R1',type:'resistor',at:[860,520],span:2,value:'100 Ω'},'Resistor','100 Ω … 100 kΩ'],
    [{id:'D1',type:'diode',at:[1060,520],span:4},'Diode','band = cathode (k)'],
    [{id:'K1',type:'relay',at:[1320,480],span:4},'Relay module (SPDT)','add a diode across the coil']];
  for(const [p,t,sub] of items){s+=SK.PARTS[p.type](p,v,T);const pins=SK.partPins(p),xs=Object.values(pins).map(q=>q[0]),ys=Object.values(pins).map(q=>q[1]);
    const cx=(Math.min(...xs)+Math.max(...xs))/2,by=Math.max(...ys)+(p.type==='motor'?62:p.type==='relay'?44:46);s+=txt(cx,by,t,{anchor:'middle',size:13.5,weight:800,fill:T.ink})+txt(cx,by+18,sub,{anchor:'middle',size:12.5,fill:T.sub});}
  s+=txt(100,664,'CONDUCTOR STRIPS · spans 1–7 · span ticks, no numbers · core coloured by net function',{size:12.5,weight:800,fill:T.mut,ls:'.5'});
  const fns=['plus','minus','signal','load','plus','minus','load'];let x=100;
  for(let S=1;S<=7;S++){const y=S<=4?710:790;if(S===5)x=100;const st={a:[x,y],b:[x+S*40,y],fn:fns[S-1],tracer:S===5};s+=SK.strip(st,v,T)+SK.stripStuds(st)+txt(x+S*20,y+36,S+' unit'+(S>1?'s':'')+(S===5?' · second supply (tracer)':''),{anchor:'middle',size:12.5,fill:T.sub});x+=S*40+90;}
  s+=txt(1080,664,'FLYING LEADS · colour = function · sleeve + clip',{size:12.5,weight:800,fill:T.mut,ls:'.5'});
  [['plus','+'],['minus','−'],['signal','IN1'],['load','RUN']].forEach(([fn,tag],i)=>{const x0=1080+(i%2)*250,y0=i<2?710:790,pts=[[x0,y0],[x0+190,y0]];s+=`<g fill="none" stroke-linecap="round"><g filter="url(#skLeadSh)">${SK.leadBody({fn},pts,T)}</g></g>`+SK.sleeve(x0+56,y0,tag,fn,T)+SK.clip(x0+190,y0,fn,T);});
  {const x0=1080,y0=880,pts=[[x0,y0],[x0+330,y0]];s+=SK.netLink(pts,T,{packets:1})+SK.netJack(x0+347,y0,T)+txt(x0,y0-18,'network link (phone): dashed, never copper',{size:12.5,fill:T.sub});}
  return s+`</svg>`;}

/* ================================================================= signal-path strip */
function hops(c,v){const P=c.path;if(!P)return[];const L=v.live,k=v.conn;const out=[];
  const H=(t,val,tag,target,why)=>out.push({t,val,tag:L?tag:null,target,why:L?why:'not connected'});
  const relayBit=v.relay[P.relay-1],cmd=v.cmd?v.cmd[P.relay-1]:null;
  const qx=`%QX${P.relay-1}`,rl=`RELAY ${P.relay} ${P.contact==='NO'?'N.O':'N.C'}`;
  if(P.kind==='wired'){const i=P.in-1,b=v.in[i],forced=v.inForced[i],sw=c.parts.find(p=>p.id===P.sw);const swName=sw&&sw.label?sw.label.t.replace(/\s+/g,' '):P.sw;
    const viaForce=k==='usb'||(k==='twin'&&!v.fieldFresh);const real=k==='usb'&&b===1&&!forced;
    H(swName,L?(ST.sw[P.sw]||ST.held[P.sw]?'closed':'open'):'—',viaForce?(real?'INFERRED':'BYPASSED'):'MODELLED',`part-${P.sw}`,viaForce?(real?'IN reads 1 and is not forced → a real switch closed':'the drawn switch sends force IX'+i+' — a software write, not a wire'):'virtual switch on the drawing (twin)');
    H(`IN${P.in} opto`,L?(b?'≈0.5 mA':'0 mA'):'—',viaForce?(real?'INFERRED':'BYPASSED'):'MODELLED',`in${P.in}`,viaForce?(real?'only a voltage on IN'+P.in+' can light the opto on a board':'force skips the opto'):'the twin has no opto; this current is drawn, not measured');
    H('expander in',L?(v.fieldFresh?`0x59 bit${i} = ${b}`:'—'):'—',viaForce?(real?'INFERRED':'BYPASSED'):'OBSERVED',`in${P.in}`,viaForce?(real?'the firmware read it from the expander':'force skips the AW9523 input register'):'QMP read of the twin’s AW9523 input register');
    const fwRead=v.ioDi?v.ioDi[i]:null;
    H('I²C read 0x59',L?(fwRead!=null?`firmware read ${fwRead}${forced?' (forced)':''}`:'—'):'—',viaForce&&!real?'BYPASSED':(v.busR?'OBSERVED':(fwRead!=null&&!forced?'OBSERVED':'INFERRED')),`in${P.in}`,viaForce&&!real?'force writes the image after the read':(v.busR?'@BUS tap saw R 0x59':'console io = the firmware’s read of 0x59'));
    const img=v.mirror?v.mirror[i]:(v.ioDi?v.ioDi[i]:null);
    H(`%IX${i} = ${img==null?'—':img}`,L?(v.mirror?'DI mirror':'io'):'—','OBSERVED',`in${P.in}`,'the logical input image');
    H('rung',P.rung,(cmd!=null)?'INFERRED':'INFERRED',`relay${P.relay}`,cmd==null?'needs io':(cmd===img?'output followed the input this scan':'output differs from the input: check the program / mode'));
  }else if(P.kind==='phone'){const sel=CC.PHONE_MAP.find(x=>x.id===ST.phoneSel)||CC.PHONE_MAP[1];const pv=phoneValue(sel,v);
    H(`phone · ${sel.name}`,sel.scalar,'INFERRED','wifi','the Studio cannot see the phone; someone published a number');
    H('broker',sel.point?`phone/<id>/${sel.topic}`:'not ingested',sel.point?'INFERRED':'PENDING','wifi','topic on the bridge; payload not visible here');
    H('MQTT → point map',pv.text.replace(/^.* = /,sel.point?sel.point+' = ':''),pv.tag||'PENDING','wifi',pv.sub);
    H('opto · 0x59 in · I²C read','not used','BYPASSED','wifi','a network value never crosses the input bus');
    H('rung',sel.thr?sel.thr.text:'compare rung','INFERRED',`relay${P.relay}`,'deadband compare in your ladder');
  }else{const twin=k==='twin';const iw=v.monitorIW&&Array.isArray(v.monitorIW.IW)?v.monitorIW.IW[0]:null;const a=ST.field.analog,sel=lightSel(),tw=twin&&v.fieldFresh&&a;
    H('light on RP1',twin?`${sel.name} · ≈${fmtR(sel.r)}`:'cover / uncover',twin?'MODELLED':'INFERRED','part-RP1',twin?'a chosen light level: the twin has no photoresistor':'the real photoresistor on your bench');
    H('divider → CH1','V = 4.45·RP1/(RP1+10k)'+(twin?' = '+lightMv(sel.r)+' mV':''),'MODELLED','part-R3','arithmetic, not a reading');
    H('X1 ADC · I²C 0x20',tw?`CH1 = ${a.ch1_mv} mV`:'—',tw&&a.writable?'OBSERVED':(tw?'PENDING':'INFERRED'),'part-R3',tw&&a.writable?'QMP read of the expansion’s CH1 register; the firmware polls it @0x20':(tw?'the twin’s tank model owns CH1: RESTART=1 FIELD_ANALOG=1 ./qemu/studio.sh':'the firmware polls @0x20'));
    H('%IW0 (mV)',iw!=null?String(iw):'—',iw!=null?'OBSERVED':'INFERRED','part-R3','Studio monitor (start Monitor in Logic)');
    H('rung',P.rung,'INFERRED',`relay${P.relay}`,'deadband + on-delay in your ladder');}
  H(`${qx} = ${cmd==null?'—':cmd}`,L?'io cmd':'—',cmd!=null?'OBSERVED':'INFERRED',`relay${P.relay}`,'the commanded output image');
  H('I²C write 0x59',L?(v.relSrc==='chip'?`0x59 out bit${P.relay-1} = ${relayBit}`:(v.act?`act = ${v.act[P.relay-1]}`:'—')):'—',v.busW?'OBSERVED':(relayBit!=null?'OBSERVED':'INFERRED'),`relay${P.relay}`,v.relSrc==='chip'?'QMP read of the expander output register':'expander pin readback (not contact feedback)');
  H(rl,L?(relayBit==null?'—':((P.contact==='NO')===(relayBit===1)?'closed':'open')):'—','MODELLED',`relay${P.relay}`,'drawn from the coil drive; the board has no contact feedback');
  const ld=c.parts.find(p=>p.id===P.load);H(P.loadName,L?(v.on[P.load]?(ld&&ld.type==='motor'?'turning ≈0.32 A':'on ≈'+P.ma+' mA'):'off'):'—','MODELLED',`part-${P.load}`,'drawn from the observed relay bit');
  return out;}
function renderPath(c,v){const el=$('cwPath');if(!el)return;const hs=hops(c,v),T=themeName();
  const forcedPath=c.path&&c.path.kind==='wired'&&(v.conn==='usb'||(v.conn==='twin'&&!v.fieldFresh));
  const title=c.path?(c.path.kind==='phone'?'phone sensor → '+c.path.loadName:c.path.kind==='analog'?'RP1 light → '+c.path.loadName:(c.path.sw+' → '+c.path.loadName)):'';
  let note=!v.live?'Not connected — hops show what each step will prove once the twin (or a board) is connected.':forcedPath?'Driven with force: hops 1–4 read BYPASSED — force writes the input image after the I²C read, so the opto, expander and bus are not exercised. A real switch on the board (IN reads 1, not forced) turns them INFERRED.':c.path.kind==='phone'?'A network value: the opto, expander and I²C read are never on this path.':c.path.kind==='analog'&&v.conn==='twin'?'Analog input on the twin: the light buttons put the divider voltage on CH1 at the chip (QMP); the firmware reads it over I²C @0x20. The photoresistor and divider are modelled, the CH1 register is observed.':'Twin: inputs are set at the chip (QMP), so hops 3–5 are observed; the opto and the contact are drawn, not measured.';
  el.innerHTML=`<div class="cw-path-h"><span class="cw-path-t">Signal path · ${esc(title)}</span><span class="cw-path-n">${esc(note)}</span></div><ol class="cw-hops">`+hs.map((h,i)=>{const t=h.tag;
    return `<li><button type="button" class="cw-hop${t?' t-'+t.toLowerCase():' t-idle'}" data-target="${esc(h.target)}" title="${esc(h.why||'')}"><span class="cw-hop-n">${i+1}</span><span class="cw-hop-t">${esc(h.t)}</span><span class="cw-hop-v">${esc(h.val||'')}</span>${t?`<span class="cw-hop-tag">${esc(t)}</span>`:'<span class="cw-hop-idle">idle</span>'}</button></li>`;}).join('')+`</ol>`;}

/* ================================================================= teaching notes */
function renderNotes(c){const el=$('cwNotes');if(!el)return;const t=c.teach,p=c.project;
  const pairBtn=p.file?`<button type="button" class="btn" id="cwOpenProject" data-file="${esc(p.file)}">Open “${esc(p.name)}” in Logic</button>`:'';
  el.innerHTML=`<section class="cw-note"><h4>What it teaches</h4><p>${esc(t.idea)}</p></section>`+
    `<section class="cw-note"><h4>Predict → Run → Reconcile</h4><p><b>Predict.</b> ${esc(t.predict)}</p><p><b>Run.</b> ${esc(t.run)}</p><p><b>Reconcile.</b> ${esc(t.reconcile)}</p></section>`+
    `<section class="cw-note"><h4>Do not conclude</h4><ul>${t.mustNot.map(x=>`<li>${esc(x)}</li>`).join('')}</ul><h4>Real world</h4><p>${esc(t.consequence)}</p></section>`+
    `<section class="cw-note"><h4>Pairs with</h4><p><b>${esc(p.name)}</b>${p.file?` <code>projects/${esc(p.file)}.csco</code>`:''}</p><p>reads ${p.reads.map(esc).join(', ')} · writes ${p.writes.map(esc).join(', ')}</p><p>${esc(p.note)}</p>${pairBtn}<p class="cw-small" id="cwProjMsg" role="status"></p></section>`;
  const b=$('cwOpenProject');if(b)b.onclick=()=>{if(b.dataset.armed!=='1'){b.dataset.armed='1';b.textContent='Replace the open project? Click again';setTimeout(()=>{if(b.isConnected){b.dataset.armed='';b.textContent=`Open “${p.name}” in Logic`;}},4000);return;}
    b.disabled=true;fetch('projects/'+p.file+'.csco',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error('example unavailable');return r.text();}).then(t=>{if(typeof importProject==='function'&&importProject(t)){const m=$('cwProjMsg');if(m)m.textContent='Opened in the editor (workspace only). Load it from Logic: PROGRAM → Load application → RUN.';}}).catch(e=>{const m=$('cwProjMsg');if(m)m.textContent=e.message;}).finally(()=>{b.disabled=false;b.dataset.armed='';b.textContent=`Open “${p.name}” in Logic`;});};}

/* ================================================================= render orchestration */
function guideText(c,v){
  if(v.conn==='none')return['idle','Not connected — the circuit is drawn idle. Press Connect twin (top bar) to drive it live on the QEMU twin; on a physical board, Connect USB (inputs then go through force).'];
  if(!v.live)return['wait','Connected — waiting for the first readback (field / io).'];
  if(v.conn==='usb')return['force','Physical board over USB: the drawn switches send force IXn (a software override of the input image). Your real snap switches still work — an input that reads 1 without “F” came through the wire. Leaving this view releases every force it set.'];
  if(v.fieldErr)return['force','Twin without the chip endpoint (/field: '+v.fieldErr+'). Falling back to force: hops 1–4 are bypassed.'];
  if(isAnalog(c)&&v.conn==='twin'&&ST.field.analogNote)return['mode','CH1 belongs to the twin’s built-in tank model, so this photoresistor cannot drive it yet. In the terminal run: RESTART=1 FIELD_ANALOG=1 ./qemu/studio.sh — then Connect twin again. (The process model is switched off while you own CH1.)'];
  if(v.mode&&v.mode!=='RUN'&&v.mode!=='REMOTE')return['mode','Twin live. The controller is in '+v.mode+': outputs stay off until you load the paired program and select RUN.'];
  if(isAnalog(c))return['live','Twin live: the light buttons put the divider voltage on CH1 at the chip; the firmware reads it over I²C @0x20. Load light-alarm and RUN, then go from Room light to Covered and back through Hand hovering.'];
  return['live','Twin live: switches set the AW9523 input bit at the chip; the firmware reads it over I²C on its next scan.'];}
function render(force){const c=circuit();ST.conn=connKind();const v=buildVm(c);const T=SK.THEMES[themeName()];
  const key=JSON.stringify([ST.cur,ST.view,ST.trace,ST.sub,ST.phoneSel,T.name,v.live,v.conn,v.in,v.relay,v.inForced,v.cmd,v.act,v.mode,ST.sw,ST.held,v.mirror,v.regs&&[50,51,52].map(r=>v.regs[r]),v.monitorIW&&v.monitorIW.IW,v.busR,v.busW,v.fieldErr,lcdSource().kind,ST.light,ST.field.analog&&[ST.field.analog.ch1_mv,ST.field.analog.ch1_source],ST.field.analogNote]);
  if(!force&&key===ST.renderKey)return;ST.renderKey=key;
  const stage=$('cwStage');if(!stage)return;
  const focusId=document.activeElement&&document.activeElement.closest&&document.activeElement.closest('[data-ctl],[data-phone],[data-light],[data-lcd-key],[data-lcd-open][tabindex="0"]');const fsel=focusId?(focusId.dataset.lcdKey?`[data-lcd-key="${focusId.dataset.lcdKey}"]`:focusId.dataset.lcdOpen?'[data-lcd-open][tabindex="0"]':focusId.dataset.ctl?`[data-ctl="${focusId.dataset.ctl}"]`:focusId.dataset.light?`[data-light="${focusId.dataset.light}"]`:`[data-phone="${focusId.dataset.phone}"]`):null;
  const gal=$('cwParts');
  if(ST.sub==='parts'){stage.innerHTML='';if(gal)gal.innerHTML=renderGallery(T);return;}
  if(gal&&gal.innerHTML)gal.innerHTML='';
  if(ST.sub!=='wire'){stage.innerHTML='';return;}
  stage.innerHTML=ST.view==='schematic'?renderSchematic(c,v,T):renderBench(c,v,T);
  if(fsel){const n=stage.querySelector(fsel);if(n)n.focus({preventScroll:true});}
  renderPath(c,v);
  const g=guideText(c,v),gd=$('cwGuide');if(gd){gd.className='cw-guide g-'+g[0];gd.textContent=(ST.msg&&performance.now()-ST.msgAt<5000?ST.msg+'  ·  ':'')+g[1];}
  const cc=$('cwConn');if(cc){cc.className='cw-conn '+(v.live?'on':'off');cc.innerHTML=`<span class="dot"></span>${v.live?(v.conn==='twin'?'Live · twin':'Live · USB board'):v.conn!=='none'?'Connected · waiting':'Not connected'}`;}
  document.querySelectorAll('#cwPick button').forEach(b=>{const on=b.dataset.cid===ST.cur;b.classList.toggle('on',on);b.setAttribute('aria-checked',on);b.tabIndex=on?0:-1;});
  document.querySelectorAll('#cwView button').forEach(b=>b.classList.toggle('on',b.dataset.view===ST.view));
  const tr=$('cwTrace');if(tr){tr.setAttribute('aria-pressed',ST.trace);tr.classList.toggle('on',ST.trace);}
  if(ST.hopPulse){pulse(ST.hopPulse);ST.hopPulse=null;}}
function pulse(target){const stage=$('cwStage');if(!stage)return;let el=stage.querySelector(`[data-hop-target="${target}"]`);
  if(!el&&ST.view==='schematic'){const m=/^(in|relay)(\d)$/.exec(target);if(m)el=stage.querySelector(m[1]==='in'?`[data-hop-target="sch-in${m[2]}"]`:`[data-hop-target="sch-coil${m[2]}"]`);}
  if(!el)return;el.classList.remove('sk-pulse');void el.getBoundingClientRect();el.classList.add('sk-pulse');try{el.scrollIntoView({block:'nearest',behavior:'smooth'});}catch(e){}}

/* ================================================================= driving inputs */
function note(m){ST.msg=m;ST.msgAt=performance.now();render(true);}
async function postField(q){const r=await fetch('/field?'+q,{method:'POST',cache:'no-store',signal:AbortSignal.timeout(3000)});if(!r.ok)throw Error('http '+r.status);const j=await r.json();if(!j.ok)throw Error(j.error||'refused');ST.field.ok=true;ST.field.di=j.di;ST.field.relays=j.relays;ST.field.analog=j.analog||null;ST.field.analogNote=j.analog_note||'';ST.field.at=performance.now();return j;}
async function setLight(id){if(!LIGHT.some(x=>x.id===id))return;ST.light=id;save();ST.conn=connKind();const sel=lightSel(),mv=lightMv(sel.r);
  if(ST.conn==='twin'){try{const j=await postField('ch1_mv='+mv);ST.ch1Set=true;
      if(!j.analog)note('This twin has no IO-expansion analog frame.');
      else{try{log&&log(`Circuit d: ${sel.name} → RP1 ≈ ${fmtR(sel.r)} → ${mv} mV on CH1 at the chip (QMP); the firmware reads it over I²C @0x20.`,'ok');}catch(e){}}}
    catch(e){note('CH1: '+e.message);}}
  else if(ST.conn==='usb')note('Physical board: shade or light the real photoresistor — these buttons drive only the twin.');
  else note('Not connected — Connect twin to put this light level on CH1.');
  render(true);}
function releaseCh1(){if(!ST.ch1Set)return;ST.ch1Set=false;
  try{fetch('/field?ch1_mv=open',{method:'POST',cache:'no-store',keepalive:true}).catch(()=>{});}catch(e){}}
function busy(){try{return typeof busBusy!=='undefined'&&busBusy;}catch(e){return false;}}
function send(cmd){if(busy())return false;try{return typeof rawSend==='function'&&rawSend(cmd);}catch(e){return false;}}
async function drive(p,val){const c=circuit();const n=+p.in.slice(2)-1;ST.conn=connKind();
  if(ST.conn==='none'){note('Not connected — this switch drives nothing yet. Connect twin to drive it live.');return;}
  if(ST.conn==='twin'&&ST.field.ok!==false){try{await ensureRest(c);await postField(`bit=${n}&v=${val?1:0}`);setTimeout(()=>send('io'),120);render();return;}catch(e){ST.field.ok=false;ST.field.err=e.message;}}
  if(val)send('force IX'+n+' 1');else if(p.mode==='momentary')send('unforce IX'+n);else send('force IX'+n+' 0');
  ST.ourForce[n]=!!(val||p.mode!=='momentary');setTimeout(()=>send('io'),150);render();}
function press(id){const c=circuit(),p=c.parts.find(x=>x.id===id);if(!p||!p.control)return;
  if(connKind()==='none'){note('Not connected — this switch drives nothing yet. Connect twin to drive it live.');return;}   // a drawn switch never moves without a target
  if(p.mode==='momentary'){if(ST.held[id])return;ST.held[id]=true;ST.sw[id]=true;drive(p,1);}
  else{ST.sw[id]=!ST.sw[id];drive(p,ST.sw[id]?1:0);}render(true);}
function release(id){const c=circuit(),p=c.parts.find(x=>x.id===id);if(!p||p.mode!=='momentary'||!ST.held[id])return;ST.held[id]=false;ST.sw[id]=false;drive(p,0);render(true);}
function releaseAll(){releaseCh1();Object.keys(ST.held).forEach(id=>{if(ST.held[id])release(id);});
  Object.keys(ST.ourForce).forEach(n=>{if(ST.ourForce[n]){send('unforce IX'+n);ST.ourForce[n]=false;}});}
/* twin only: put the chip's input byte in this circuit's rest state (e.g. the NC E-stop closed → IN3 = 1)
 * once per connection and circuit. Every drive awaits it, so a press can never be overwritten by it. */
function ensureRest(c){const key=ST.epoch+'|'+c.id;if(ST.restKey===key)return ST.restP||Promise.resolve();
  if(ST.conn!=='twin'||ST.field.ok===false)return Promise.resolve();ST.restKey=key;const di=c.restDi||0;
  ST.restP=postField('di='+di).then(()=>isAnalog(c)&&ST.field.analog&&ST.field.analog.writable?postField('ch1_mv='+lightMv(lightSel().r)).then(j=>{if(j.analog)ST.ch1Set=true;}).catch(()=>{}):null).then(()=>{if(!Object.values(ST.held).some(Boolean))resetSwitches(c);
      try{log&&log(`Circuit ${c.id}: twin field inputs set to this circuit’s rest state at the chip (di = 0x${di.toString(16).padStart(2,'0')}${di?': '+c.parts.filter(p=>p.control&&p.rest).map(p=>p.in+' = 1').join(', '):''}).`,'ok');}catch(e){}})
    .catch(e=>{ST.field.ok=false;ST.field.err=e.message;});
  return ST.restP;}

/* ================================================================= polling (only while the Wire & run view is visible) */
async function pollField(){if(ST.field.busy)return;ST.field.busy=true;try{const r=await fetch('/field',{cache:'no-store',signal:AbortSignal.timeout(2500)});const j=await r.json();
  if(j&&j.ok){ST.field.ok=true;ST.field.err='';ST.field.di=j.di;ST.field.relays=j.relays;ST.field.analog=j.analog||null;ST.field.analogNote=j.analog_note||'';ST.field.at=performance.now();}else{ST.field.ok=false;ST.field.err=(j&&j.error)||'unavailable';ST.field.next=performance.now()+3000;}}
  catch(e){ST.field.ok=false;ST.field.err=e.message||'unreachable';ST.field.next=performance.now()+3000;}finally{ST.field.busy=false;}}
async function pollModbus(){const now=performance.now();if(now<ST.mirror.next)return;ST.mirror.next=now+1000;try{const d=await window.mbFetch('fc=1&addr=100&count=8');if(d&&d.ok){ST.mirror.di=d.values;ST.mirror.at=performance.now();}}catch(e){ST.mirror.next=now+4000;}
  if(circuit().phone){try{const r=await window.mbFetch('fc=3&addr=50&count=3');if(r&&r.ok){ST.regs.v={50:r.values[0],51:r.values[1],52:r.values[2]};ST.regs.at=performance.now();}}catch(e){}}}
function onIo(line){let m=/^DI\s*:\s*(.*)$/.exec(line);
  if(m){const tok=m[1].trim().split(/\s+/);if(tok.length>=8){ST.io.di=tok.slice(0,8).map(t=>t[0]==='1'?1:0);ST.io.forced=tok.slice(0,8).map(t=>/F/.test(t)?1:0);ST.io.at=performance.now();}return;}
  m=/^relay([0-3])\s+cmd=([01])\s+act=([01])/.exec(line);if(m){ST.io.cmd=ST.io.cmd||[0,0,0,0];ST.io.act=ST.io.act||[0,0,0,0];ST.io.cmd[+m[1]]=+m[2];ST.io.act[+m[1]]=+m[3];ST.io.at=performance.now();}}
let ticker=null;
function tick(){if(!ST.active||document.hidden)return;if(ST.sub==='wire'&&ST.view==='bench')refreshLcd(false);const prev=ST.conn;ST.conn=connKind();const now=performance.now();
  if(ST.conn!==prev){ST.epoch++;ST.field={ok:null,di:null,relays:null,analog:null,analogNote:'',at:0,err:'',busy:false,next:0};ST.ch1Set=false;ST.io={di:null,forced:null,cmd:null,act:null,at:0,last:0};ST.ourForce={};ST.restKey='';ST.restP=null;if(!Object.values(ST.held).some(Boolean))resetSwitches(circuit());}
  if(ST.conn==='twin'){const c=circuit();if(ST.restKey!==ST.epoch+'|'+c.id&&ST.field.ok!==false)ensureRest(c);else if(now>=ST.field.next)pollField();if(now-ST.io.last>1000){ST.io.last=now;send('io');}pollModbus();}
  else if(ST.conn==='usb'){if(now-ST.io.last>500){ST.io.last=now;send('io');}}
  render();}

/* ================================================================= DOM wiring */
function selectCircuit(id){if(id===ST.cur||!CC.byId(id))return;releaseAll();ST.cur=id;resetSwitches(circuit());save();renderNotes(circuit());if(ST.active)ensureRest(circuit());render(true);}
function setSub(sub){ST.sub=sub;['wire','paths','parts'].forEach(k=>{const el=$({wire:'cwWire',paths:'cwPaths',parts:'cwParts'}[k]);if(el)el.hidden=k!==sub;});
  document.querySelectorAll('#cwSub button').forEach(b=>{const on=b.dataset.sub===sub;b.classList.toggle('on',on);b.setAttribute('aria-selected',on);});
  window.__circSub=sub;window.dispatchEvent(new Event('circsub'));if(sub!=='wire')releaseAll();render(true);}
function init(){const stage=$('cwStage');if(!stage)return;
  const pick=$('cwPick');pick.innerHTML=CC.CIRCUITS.map(c=>`<button type="button" role="radio" data-cid="${c.id}" aria-checked="false"><b>${c.id}</b>${esc(c.name)}</button>`).join('');
  pick.addEventListener('click',e=>{const b=e.target.closest('button[data-cid]');if(b)selectCircuit(b.dataset.cid);});
  pick.addEventListener('keydown',e=>{const ids=CC.CIRCUITS.map(c=>c.id),i=ids.indexOf(ST.cur);if(e.key==='ArrowRight'||e.key==='ArrowDown'){e.preventDefault();selectCircuit(ids[(i+1)%ids.length]);pick.querySelector(`[data-cid="${ST.cur}"]`).focus();}if(e.key==='ArrowLeft'||e.key==='ArrowUp'){e.preventDefault();selectCircuit(ids[(i+ids.length-1)%ids.length]);pick.querySelector(`[data-cid="${ST.cur}"]`).focus();}});
  $('cwView').addEventListener('click',e=>{const b=e.target.closest('button[data-view]');if(b&&b.dataset.view!==ST.view){ST.view=b.dataset.view;save();stage.classList.add('cw-fade');render(true);setTimeout(()=>stage.classList.remove('cw-fade'),260);}});
  $('cwTrace').addEventListener('click',()=>{ST.trace=!ST.trace;save();render(true);});
  $('cwSub').addEventListener('click',e=>{const b=e.target.closest('button[data-sub]');if(b)setSub(b.dataset.sub);});
  // switches: pointer + keyboard (Space/Enter hold a momentary switch; toggle a maintained one)
  stage.addEventListener('pointerdown',e=>{const g=e.target.closest('[data-ctl]');if(!g)return;e.preventDefault();try{g.focus({preventScroll:true});}catch(_){}press(g.dataset.ctl);});
  window.addEventListener('pointerup',()=>{Object.keys(ST.held).forEach(id=>{if(ST.held[id])release(id);});});
  window.addEventListener('pointercancel',()=>{Object.keys(ST.held).forEach(id=>{if(ST.held[id])release(id);});});
  stage.addEventListener('keydown',e=>{if(e.key===' '||e.key==='Enter'){const k=e.target.closest('[data-lcd-key]'),o=e.target.closest('[data-lcd-open]');
      if(k){e.preventDefault();if(!e.repeat)lcdKey(k.dataset.lcdKey);return;}if(o){e.preventDefault();lcdOpen();return;}}
    const g=e.target.closest('[data-ctl],[data-phone],[data-light]');if(!g)return;
    if(g.dataset.light){const ids=LIGHT.map(x=>x.id),i=ids.indexOf(ST.light);
      const go=id=>{ST.light=id;render(true);const n=stage.querySelector(`[data-light="${id}"]`);if(n)n.focus({preventScroll:true});setLight(id);};
      if(e.key===' '||e.key==='Enter'){e.preventDefault();go(g.dataset.light);}
      else if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();go(ids[(i+(e.key==='ArrowDown'?1:ids.length-1))%ids.length]);}return;}
    if(g.dataset.phone){const ids=CC.PHONE_MAP.map(x=>x.id),i=ids.indexOf(ST.phoneSel);
      if(e.key===' '||e.key==='Enter'){e.preventDefault();ST.phoneSel=g.dataset.phone;save();render(true);}
      else if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();ST.phoneSel=ids[(i+(e.key==='ArrowDown'?1:ids.length-1))%ids.length];save();render(true);}return;}
    if((e.key===' '||e.key==='Enter')&&!e.repeat){e.preventDefault();press(g.dataset.ctl);}});
  stage.addEventListener('keyup',e=>{const g=e.target.closest('[data-ctl]');if(g&&(e.key===' '||e.key==='Enter'))release(g.dataset.ctl);});
  stage.addEventListener('focusout',e=>{const g=e.target.closest&&e.target.closest('[data-ctl]');if(g&&ST.held[g.dataset.ctl]&&!(e.relatedTarget&&e.relatedTarget.closest&&e.relatedTarget.closest(`[data-ctl="${g.dataset.ctl}"]`)))setTimeout(()=>{const a=document.activeElement;if(!(a&&a.closest&&a.closest(`[data-ctl="${g.dataset.ctl}"]`)))release(g.dataset.ctl);},0);});
  stage.addEventListener('click',e=>{const kk=e.target.closest('[data-lcd-key]');if(kk){lcdKey(kk.dataset.lcdKey);return;}if(e.target.closest('[data-lcd-open]')){lcdOpen();return;}
    const r=e.target.closest('[data-phone]');if(r){ST.phoneSel=r.dataset.phone;save();render(true);}
    const l=e.target.closest('[data-light]');if(l)setLight(l.dataset.light);});
  $('cwPath').addEventListener('click',e=>{const b=e.target.closest('[data-target]');if(b)pulse(b.dataset.target);});
  try{if(typeof lineListeners!=='undefined'&&!lineListeners.includes(onIo))lineListeners.push(onIo);}catch(e){}
  const prevBus=window.__flowBusEvent;window.__flowBusEvent=function(line){try{prevBus&&prevBus(line);}catch(e){}const m=/^@BUS\s+\d+\s+([RW])\s+0x59/i.exec(line);if(m){if(m[1].toUpperCase()==='R')ST.bus.r59=performance.now();else ST.bus.w59=performance.now();}};
  new MutationObserver(()=>render(true)).observe(document.documentElement,{attributes:true,attributeFilter:['data-theme']});
  window.addEventListener('beforeunload',releaseAll);
  renderNotes(circuit());setSub('wire');
  if(window.AppView)window.AppView.register(v=>{const act=v==='circuit';if(act&&!ST.active){ST.active=true;ST.restKey='';ST.restP=null;render(true);if(!ticker)ticker=setInterval(tick,250);}
    else if(!act&&ST.active){ST.active=false;releaseAll();if(ticker){clearInterval(ticker);ticker=null;}}});}
window.CircuitWorkspace={select:id=>{selectCircuit(id);},setView:v=>{ST.view=v;render(true);},setTrace:t=>{ST.trace=!!t;render(true);},setSub,render:()=>render(true),state:ST,
  press:id=>press(id),release:id=>release(id),phone:id=>{ST.phoneSel=id;render(true);},light:id=>setLight(id),
  open:id=>{if(window.AppView)window.AppView.set('circuit');setSub('wire');if(id)selectCircuit(id);}};
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();
})();
