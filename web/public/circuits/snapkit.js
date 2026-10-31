/* SnapKit — original snap-together-block style SVG parts for the Part 1 "Wire & run" circuit views.
 *
 * ORIGINAL ARTWORK. A generic snap-block *style* (modules on a 40-unit base grid, snap studs, printed
 * IEC-style symbols). No product's branding, logos, part numbering, artwork or trade dress is copied.
 * Student-facing text says "snap-kit parts".
 *
 * Everything is a pure function from data (+ a small view-model of live state) to an SVG string, so the
 * same library draws the predesigned circuits, the parts gallery and, later, the circuit designer.
 * Geometry helpers (partPins, stampTerms, expTerms) are also used headless by tools/circuit-catalog-check.js.
 *
 * Visual spec: /home/claude/work/circuit-board/ee-technologist.md §2–§4 (grid P=40, body = S·P+44 × 56,
 * category palette, colour = function, lead casing/core/sheen, clips, ferrules, sleeves, live-state rules).
 */
(function(root){
'use strict';
const P=40;                                   // base-grid pitch (SVG user units)
const esc=s=>String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const r1=v=>Math.round(v*10)/10;
const MONO="'JetBrains Mono',ui-monospace,Menlo,'DejaVu Sans Mono',monospace";

/* ---------------------------------------------------------------- themes */
const THEMES={
  dark:{name:'dark',mat0:'#161d28',mat1:'#0b0f15',board0:'#1f2733',board1:'#19202a',boardStroke:'#2c3646',hole:'#0b0f14',holeRim:'#2c3644',
    ink:'#eef2f8',sub:'#aab8cc',mut:'#8a97ab',card:'#0d121b',cardStroke:'#2a3650',cardInk:'#cfd8e6',cardRule:'#1e2a38',
    minusCasing:'#aeb8c5',minusStripCasing:'#b9c3cf',net:'#cc79a7',netInk:'#f0c7de',fence:'#e7b6d2',fenceFill:'#2a1f2a',fenceInk:'#f0c7de',fenceSub:'#d9aac4',
    accent:'#4dd8ff',accentInk:'#bff1ff',accentFill:'#0e2630',warnInk:'#ffd28a',okInk:'#5ff0b0',idle:'#8b96a8',schBg:'#0f141c',schGrid:'#18202b',schInk:'#dfe6f0',schWire:'#c3ccd8',
    plcBody0:'#f1f3f6',plcBody1:'#d9dee4',plcStroke:'#a9b1bb',focus:'#4dd8ff'},
  light:{name:'light',mat0:'#f6f8fc',mat1:'#e7edf5',board0:'#dfe5ee',board1:'#d3dae5',boardStroke:'#b8c2d0',hole:'#9aa5b5',holeRim:'#f4f7fb',
    ink:'#0d1726',sub:'#3c4a60',mut:'#55637a',card:'#ffffff',cardStroke:'#c3cfdf',cardInk:'#26324a',cardRule:'#e2e9f2',
    minusCasing:'#3a3f47',minusStripCasing:'#3a3f47',net:'#a2477c',netInk:'#7a2d5a',fence:'#a8507f',fenceFill:'#f6e7f0',fenceInk:'#7a2d5a',fenceSub:'#7d4566',
    accent:'#0a86ad',accentInk:'#05506a',accentFill:'#dff4fb',warnInk:'#8a5200',okInk:'#08795a',idle:'#6b778a',schBg:'#fbfcfe',schGrid:'#eef2f7',schInk:'#17202e',schWire:'#4a5568',
    plcBody0:'#f7f8fa',plcBody1:'#e1e5ea',plcStroke:'#9aa3ad',focus:'#0a86ad'}
};
const TAGS={
  dark:{OBSERVED:['#0f2e26','#5ff0b0','#5ff0b0'],INFERRED:['#241433','#c98bf0','#d9b0f5'],MODELLED:['#332511','#f3a53a','#ffd28a'],
        NOMINAL:['none','#8b96a8','#cfd8e6',1],BYPASSED:['#20242b','#6b7484','#a3acba',1],PENDING:['none','#8b96a8','#cfd8e6',1]},
  light:{OBSERVED:['#d9f6ee','#0c8a63','#065c43'],INFERRED:['#efe6fb','#8b55d6','#5b2aa6'],MODELLED:['#fbeed9','#c77a12','#734400'],
        NOMINAL:['none','#6b778a','#2c3646',1],BYPASSED:['#eceff3','#8b95a5','#4a5568',1],PENDING:['none','#6b778a','#2c3646',1]}
};
/* Colour = function (Okabe-Ito-derived, colour-blind safe; never coloured by the load's colour). */
function wireColours(T){return{
  plus:{core:'#e4572e',casing:'#8e2a12',sheen:'#ffb49e',name:'+ supply'},
  minus:{core:'#1d2127',casing:T.minusCasing,sheen:'#59616d',name:'− return'},
  signal:{core:'#56b4e9',casing:'#1f5f86',sheen:'#d6efff',name:'signal into an input'},
  load:{core:'#e69f00',casing:'#7a5200',sheen:'#ffe09a',name:'switched load'},
  bus:{core:'#9aa3ae',casing:'#4a525d',sheen:'#e8edf5',name:'module bus'}
};}
/* Category palette for module bodies: [top, bottom, skirt, edge, ink] */
const CAT={
  switch:['#e3e9f2','#c9d3e0','#9aa7b8','#8d9aab','#3a4350'],
  light:['#f1ece0','#dcd4c2','#b0a790','#a69d86','#4a4234'],
  motion:['#e0ebe3','#c5d6ca','#97ab9d','#8fa596','#36463b'],
  sensor:['#ebe3f0','#d6c9e0','#a99bb6','#9f90ad','#3e3448'],
  passive:['#ececec','#d2d2d2','#ababab','#a3a3a3','#3d3d3d'],
  relay:['#e2e6ea','#c8ced5','#9aa3ae','#8d969f','#2f353d'],
  power:['#454b55','#30353d','#1c2026','#1a1d22','#f1f4f8']
};
const LED_HUE={green:['#7dffc0','#18a868','#2ee88a','#0f5a37','#a7c9b4','#3d5a48'],
  red:['#ff9a8a','#d9263a','#ff5a4a','#6a1018','#b9817a','#5a2a26'],
  amber:['#ffd27a','#e08a00','#ffb52e','#6b4300','#c9ab78','#5e4a26'],
  blue:['#9fd2ff','#1e6fd9','#4da3ff','#0b3266','#8aa3bf','#2b3b52'],
  white:['#ffffff','#c9d6e6','#eef4ff','#5b6878','#c4ccd6','#5b6470']};

/* ---------------------------------------------------------------- defs */
function defs(T){
  const t=T.name;
  return `<defs>
<style>
.sk-t{font-family:Inter,'Segoe UI',system-ui,-apple-system,'DejaVu Sans',sans-serif}
.sk-mono{font-family:${MONO}}
.sk-flow{animation:skflow linear infinite}.sk-flowr{animation:skflowr linear infinite}
@keyframes skflow{to{stroke-dashoffset:-14}}@keyframes skflowr{to{stroke-dashoffset:14}}
@keyframes skflown{to{stroke-dashoffset:-20}}@keyframes skflownr{to{stroke-dashoffset:20}}
.sk-spin{transform-box:fill-box;transform-origin:center;animation:skspin linear infinite}
@keyframes skspin{to{transform:rotate(360deg)}}
.sk-buzz{animation:skbuzz .25s ease-out infinite}@keyframes skbuzz{0%{opacity:.95}100%{opacity:.15}}
.sk-ctl{cursor:pointer;outline:none}.sk-ctl[aria-disabled="true"]{cursor:not-allowed}
.sk-ctl:focus-visible .sk-focus{stroke:${T.focus};stroke-width:3;stroke-dasharray:6 4;opacity:1}
.sk-focus{opacity:0;fill:none}
.sk-pulse{animation:skpulse .3s ease-in-out 4 alternate}@keyframes skpulse{to{opacity:.25}}
@media (prefers-reduced-motion:reduce){.sk-flow,.sk-flowr,.sk-spin,.sk-buzz,.sk-net{animation:none!important}}
</style>
<radialGradient id="skMat" cx="55%" cy="40%" r="80%"><stop offset="0" stop-color="${T.mat0}"/><stop offset="1" stop-color="${T.mat1}"/></radialGradient>
<linearGradient id="skBoard" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${T.board0}"/><stop offset="1" stop-color="${T.board1}"/></linearGradient>
<pattern id="skHoles" x="0" y="0" width="40" height="40" patternUnits="userSpaceOnUse"><circle cx="20" cy="20.8" r="2.6" fill="${T.holeRim}"/><circle cx="20" cy="20" r="2.4" fill="${T.hole}"/></pattern>
<radialGradient id="skStudG" cx="38%" cy="32%" r="75%"><stop offset="0" stop-color="#ffffff"/><stop offset=".5" stop-color="#c9d0d8"/><stop offset="1" stop-color="#7a8491"/></radialGradient>
<radialGradient id="skScrewG" cx="38%" cy="32%" r="80%"><stop offset="0" stop-color="#fbfcfd"/><stop offset=".55" stop-color="#bcc4cd"/><stop offset="1" stop-color="#7d8794"/></radialGradient>
<linearGradient id="skFerr" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#8d96a2"/><stop offset=".45" stop-color="#f2f4f6"/><stop offset="1" stop-color="#7f8894"/></linearGradient>
<linearGradient id="skPlc" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${T.plcBody0}"/><stop offset="1" stop-color="${T.plcBody1}"/></linearGradient>
<linearGradient id="skTstrip" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#d8dde3"/><stop offset="1" stop-color="#c4cbd3"/></linearGradient>
${Object.entries(CAT).map(([k,c])=>`<linearGradient id="skC_${k}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${c[0]}"/><stop offset="1" stop-color="${c[1]}"/></linearGradient>`).join('')}
<linearGradient id="skStripV" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#3b4452"/><stop offset=".35" stop-color="#566173"/><stop offset="1" stop-color="#2f3743"/></linearGradient>
<linearGradient id="skStripH" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#566173"/><stop offset=".4" stop-color="#46505f"/><stop offset="1" stop-color="#2f3743"/></linearGradient>
<pattern id="skHaz" width="12" height="12" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="12" height="12" fill="#f2c230"/><rect width="6" height="12" fill="#262b33"/></pattern>
<radialGradient id="skCap_green" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#4fc48a"/><stop offset="1" stop-color="#1d7a4c"/></radialGradient>
<radialGradient id="skCap_black" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#5b626d"/><stop offset="1" stop-color="#22262c"/></radialGradient>
<radialGradient id="skCap_blue" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#5aa0f0"/><stop offset="1" stop-color="#1d4f99"/></radialGradient>
<radialGradient id="skCap_red" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#ff7a6a"/><stop offset="1" stop-color="#a3182a"/></radialGradient>
<radialGradient id="skCapDn" cx="50%" cy="58%" r="70%"><stop offset="0" stop-color="#000" stop-opacity=".05"/><stop offset="1" stop-color="#000" stop-opacity=".38"/></radialGradient>
${Object.entries(LED_HUE).map(([k,h])=>`<radialGradient id="skLensOn_${k}" cx="42%" cy="36%" r="70%"><stop offset="0" stop-color="#f4fff8"/><stop offset=".35" stop-color="${h[0]}"/><stop offset="1" stop-color="${h[1]}"/></radialGradient>
<radialGradient id="skLensOff_${k}" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="${h[4]}"/><stop offset="1" stop-color="${h[5]}"/></radialGradient>
<radialGradient id="skGlow_${k}" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="${h[0]}" stop-opacity=".85"/><stop offset=".45" stop-color="${h[2]}" stop-opacity=".35"/><stop offset="1" stop-color="${h[2]}" stop-opacity="0"/></radialGradient>`).join('')}
<radialGradient id="skMotor" cx="38%" cy="32%" r="78%"><stop offset="0" stop-color="#eef1f4"/><stop offset=".6" stop-color="#b3bbc5"/><stop offset="1" stop-color="#6f7884"/></radialGradient>
<linearGradient id="skCell" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#dfe3e8"/><stop offset="1" stop-color="#9aa3ae"/></linearGradient>
<radialGradient id="skBulbOn" cx="50%" cy="45%" r="60%"><stop offset="0" stop-color="#fff3c4"/><stop offset=".6" stop-color="#ffd27a" stop-opacity=".8"/><stop offset="1" stop-color="#ffb347" stop-opacity=".25"/></radialGradient>
<filter id="skSh" x="-15%" y="-30%" width="130%" height="180%"><feGaussianBlur in="SourceAlpha" stdDeviation="4"/><feOffset dy="6"/><feComponentTransfer><feFuncA type="linear" slope="${t==='dark'?.5:.28}"/></feComponentTransfer><feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<filter id="skLeadSh" filterUnits="userSpaceOnUse" x="-100" y="-100" width="1800" height="1600"><feGaussianBlur in="SourceAlpha" stdDeviation="2.4"/><feOffset dx="2" dy="6"/><feComponentTransfer><feFuncA type="linear" slope="${t==='dark'?.55:.3}"/></feComponentTransfer><feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<filter id="skBlur" filterUnits="userSpaceOnUse" x="-100" y="-100" width="1800" height="1600"><feGaussianBlur stdDeviation="7"/></filter>
<g id="skStud"><circle r="10" fill="url(#skStudG)" stroke="#5b636e" stroke-width="1"/><circle r="4.2" fill="#3a414b"/><path d="M-6.5,-6 A9,9 0 0 1 3,-8.6" fill="none" stroke="#fff" stroke-width="1.6" stroke-linecap="round" opacity=".8"/></g>
<g id="skScrew"><circle r="11.5" fill="url(#skScrewG)" stroke="#7d8794" stroke-width="1"/><path d="M-7,-3.2 L7,3.2" stroke="#59616c" stroke-width="2.8" stroke-linecap="round"/></g>
<g id="skScrewS"><circle r="10.5" fill="url(#skScrewG)" stroke="#7d8794" stroke-width="1"/><path d="M-6.4,-3 L6.4,3" stroke="#59616c" stroke-width="2.6" stroke-linecap="round"/></g>
</defs>`;
}

/* ---------------------------------------------------------------- small primitives */
function txt(x,y,s,o){o=o||{};
  return `<text x="${r1(x)}" y="${r1(y)}"${o.anchor?` text-anchor="${o.anchor}"`:''} font-size="${o.size||12.5}"${o.weight?` font-weight="${o.weight}"`:''} fill="${o.fill||'currentColor'}"${o.mono?` font-family="${MONO}"`:''}${o.ls?` letter-spacing="${o.ls}"`:''}${o.op!=null?` opacity="${o.op}"`:''}${o.tr?` transform="${o.tr}"`:''}${o.cls?` class="${o.cls}"`:''}>${o.raw?s:esc(s)}</text>`;
}
const stud=(x,y)=>`<use href="#skStud" x="${x}" y="${y}"/>`;
function tag(kind,x,y,T,o){o=o||{};const c=(TAGS[T.name]||TAGS.dark)[kind]||TAGS.dark.PENDING;const w=o.w||(kind.length*7.6+14),h=18;
  return `<g transform="translate(${r1(x)},${r1(y)})"><rect width="${w}" height="${h}" rx="8.5" fill="${c[0]}" stroke="${c[1]}"${c[3]?' stroke-dasharray="3 2"':''}/>`+
    txt(w/2,13.2,kind,{anchor:'middle',size:12.5,weight:800,fill:c[2],mono:1})+(kind==='BYPASSED'?`<path d="M6,${h/2} H${w-6}" stroke="${c[2]}" stroke-width="1.2"/>`:'')+`</g>`;
}
function tagWidth(kind){return kind.length*7.6+14;}
/* rounded orthogonal path through points */
function roundPath(pts,R){R=R==null?16:R;
  let d=`M${pts[0][0]},${pts[0][1]}`;
  for(let i=1;i<pts.length-1;i++){
    const [x0,y0]=pts[i-1],[x1,y1]=pts[i],[x2,y2]=pts[i+1];
    const l1=Math.hypot(x1-x0,y1-y0)||1,l2=Math.hypot(x2-x1,y2-y1)||1;
    const r=Math.min(R,i===1?l1:l1/2,i===pts.length-2?l2:l2/2);
    const ax=x1-(x1-x0)/l1*r,ay=y1-(y1-y0)/l1*r,bx=x1+(x2-x1)/l2*r,by=y1+(y2-y1)/l2*r;
    d+=` L${r1(ax)},${r1(ay)} Q${x1},${y1} ${r1(bx)},${r1(by)}`;
  }
  const L=pts[pts.length-1];d+=` L${L[0]},${L[1]}`;return d;
}
/* point at distance along a polyline (from the start, or from the end if fromEnd) */
function along(pts,dist,fromEnd){const p=fromEnd?pts.slice().reverse():pts;let left=dist;
  for(let i=1;i<p.length;i++){const [x0,y0]=p[i-1],[x1,y1]=p[i],l=Math.hypot(x1-x0,y1-y0);
    if(left<=l||i===p.length-1){const f=Math.min(1,left/(l||1));return{x:x0+(x1-x0)*f,y:y0+(y1-y0)*f,vertical:Math.abs(x1-x0)<Math.abs(y1-y0),segLen:l};}left-=l;}
  return{x:p[0][0],y:p[0][1],vertical:true,segLen:0};
}
function polyLen(pts){let s=0;for(let i=1;i<pts.length;i++)s+=Math.hypot(pts[i][0]-pts[i-1][0],pts[i][1]-pts[i-1][1]);return s;}
/* current → dot speed (spec §2.6): v = clamp(12·log2(1 + I/0.5 mA), 6, 120) px/s, dash period 14 */
function flowDur(ma){const v=Math.max(6,Math.min(120,12*Math.log2(1+(ma||1)/0.5)));return (14/v).toFixed(3)+'s';}
function flowWidth(ma){return Math.max(2.2,Math.min(3.8,2.2+0.5*Math.log10(1+(ma||1)))).toFixed(2);}

/* ---------------------------------------------------------------- geometry */
/* Two-terminal snap parts: pin a at `at`, pin b at at + span·P along `dir` ('h' right, 'v' down). */
const PIN_NAMES={led:['a','k'],diode:['k','a'],battery:['p','n'],motor:['p','n'],buzzer:['p','n'],lamp:['a','b']};
function partPins(p){
  const [x,y]=p.at,S=p.span||2,dx=p.dir==='v'?0:S*P,dy=p.dir==='v'?S*P:0;
  if(p.type==='relay'){ // snap SPDT relay: top row coil+ … coil−, bottom row COM · N.O · N.C (one grid row below)
    return{coilp:[x,y],coiln:[x+S*P,y],com:[x,y+P*2],no:[x+S*P/2,y+P*2],nc:[x+S*P,y+P*2]};}
  const n=PIN_NAMES[p.type]||['a','b'];return{[n[0]]:[x,y],[n[1]]:[x+dx,y+dy]};
}

/* ---------------------------------------------------------------- module body */
function body(p,cat,o){o=o||{};const S=p.span||2,L=S*P+44,W=o.w||56,c=CAT[cat];const rx=W>=72?14:12;
  return `<g filter="url(#skSh)"><rect x="-22" y="${-W/2+5}" width="${L}" height="${W}" rx="${rx}" fill="${c[2]}"/><rect x="-22" y="${-W/2}" width="${L}" height="${W}" rx="${rx}" fill="url(#skC_${cat})" stroke="${c[3]}"/></g>`;
}
function frame(p){const [x,y]=p.at;return p.dir==='v'?`translate(${x},${y}) rotate(90)`:`translate(${x},${y})`;}
/* upright text inside a (possibly rotated) part frame */
function ptxt(p,x,y,s,o){o=Object.assign({},o||{});if(p.dir==='v'){o.tr=`rotate(-90 ${r1(x)} ${r1(y)})`;}return txt(x,y,s,o);}
function controlWrap(p,inner,vm){
  if(!p.control)return inner;
  const dis=!(vm&&vm.canDrive);
  const lbl=p.aria||p.label||p.id;
  const pressed=p.type==='slide'?(vm&&vm.sw&&vm.sw[p.id]?'true':'false'):(vm&&vm.held&&vm.held[p.id]?'true':'false');
  return `<g class="sk-ctl" role="button" tabindex="0" data-ctl="${esc(p.id)}" aria-label="${esc(lbl)}" aria-pressed="${pressed}"${dis?' aria-disabled="true"':''}>${inner}</g>`;
}

/* ---------------------------------------------------------------- parts */
const PARTS={};
PARTS.push=function(p,vm){const S=p.span||4,L=S*P,ink=CAT.switch[4];
  const pressed=!!(vm&&vm.sw&&vm.sw[p.id]),held=!!(vm&&vm.held&&vm.held[p.id]);
  const cx=L/2,cap=p.cap||'blue',dy=pressed?1.5:0;
  let s=body(p,'switch');
  // printed NO (or NC) pushbutton symbol
  const sx=20;
  s+=`<g stroke="${ink}" stroke-width="1.8" fill="none" stroke-linecap="round"><circle cx="${sx}" cy="8" r="2.4" fill="${ink}"/><circle cx="${sx+28}" cy="8" r="2.4" fill="${ink}"/>`;
  s+=p.nc?`<path d="M${sx-3},10.5 H${sx+31} M${sx+14},10.5 V-3 M${sx+7},-3 H${sx+21}"/>`:(pressed?`<path d="M${sx-3},5.5 H${sx+31} M${sx+14},5.5 V-6 M${sx+7},-6 H${sx+21}"/>`:`<path d="M${sx-3},-1 H${sx+31} M${sx+14},-1 V-10 M${sx+7},-10 H${sx+21}"/>`);
  s+=`</g>`;
  s+=`<rect x="${cx-26}" y="-26" width="52" height="52" rx="26" class="sk-focus"/>`;
  s+=`<circle cx="${cx}" cy="0" r="22.5" fill="#2b3038"/><circle cx="${cx}" cy="${dy}" r="${pressed?16.2:17}" fill="url(#skCap_${cap})" stroke="#0f1115" stroke-width=".8"/>`;
  if(pressed)s+=`<circle cx="${cx}" cy="${dy}" r="16.2" fill="url(#skCapDn)"/>`;
  else s+=`<path d="M${cx-9},${-8} A11,11 0 0 1 ${cx+3},${-12}" stroke="#fff" stroke-width="2" fill="none" stroke-linecap="round" opacity=".45"/>`;
  if(p.glyph)s+=ptxt(p,cx,6+dy,p.glyph,{anchor:'middle',size:16,weight:900,fill:'#f1f4f8'});
  s+=ptxt(p,L*0.78,5,p.nc?'NC':'NO',{anchor:'middle',size:13,weight:800,fill:ink});
  if(held)s+=`<circle cx="${cx}" cy="0" r="28" fill="none" stroke="#4dd8ff" stroke-width="2" stroke-dasharray="5 3.5"/>`;
  s+=stud(0,0)+stud(L,0);
  return controlWrap(p,`<g transform="${frame(p)}">${s}</g>`,vm);
};
PARTS.slide=function(p,vm){const S=p.span||4,L=S*P,ink=CAT.switch[4],on=!!(vm&&vm.sw&&vm.sw[p.id]);
  let s=body(p,'switch');
  if(p.estop)s+=`<rect x="-22" y="-28" width="${L+44}" height="11" rx="5.5" fill="url(#skHaz)"/>`;
  const sx=18;
  s+=`<g stroke="${ink}" stroke-width="1.8" fill="none" stroke-linecap="round"><circle cx="${sx}" cy="7" r="2.4" fill="${ink}"/><circle cx="${sx+26}" cy="7" r="2.4" fill="${ink}"/>`+
     (on?`<path d="M${sx},7 L${sx+26},5"/>`:`<path d="M${sx},7 L${sx+22},-6"/>`)+`</g>`;
  s+=ptxt(p,sx+13,22,on?'closed':'open',{anchor:'middle',size:12.5,weight:800,fill:'#4a5463'});
  const tx=L*0.56-6,kx=on?tx+3:tx+41;
  s+=`<rect x="${tx-6}" y="-16" width="84" height="32" rx="10" class="sk-focus"/>`;
  s+=`<rect x="${tx}" y="-9" width="72" height="18" rx="9" fill="#2b3038" stroke="#1b1e23"/>`;
  s+=`<rect x="${kx}" y="-12" width="28" height="24" rx="6" fill="#f2f4f7" stroke="#7d8794"/><g stroke="#9aa3ae" stroke-width="1.4"><line x1="${kx+8}" y1="-7" x2="${kx+8}" y2="7"/><line x1="${kx+14}" y1="-7" x2="${kx+14}" y2="7"/><line x1="${kx+20}" y1="-7" x2="${kx+20}" y2="7"/></g>`;
  s+=ptxt(p,tx+14,24,'ON',{anchor:'middle',size:12.5,weight:900,fill:on?'#13663f':'#4a5463'})+ptxt(p,tx+58,24,'OFF',{anchor:'middle',size:12.5,weight:800,fill:on?'#4a5463':'#8a1f1f'});
  s+=stud(0,0)+stud(L,0);
  return controlWrap(p,`<g transform="${frame(p)}">${s}</g>`,vm);
};
PARTS.reed=function(p,vm){const S=p.span||4,L=S*P,ink=CAT.switch[4];let s=body(p,'switch');
  s+=`<rect x="${L/2-44}" y="-11" width="88" height="22" rx="11" fill="#e9f4fb" fill-opacity=".7" stroke="#7d93a8"/>`+
    `<path d="M${L/2-40},0 H${L/2-4} M${L/2+4},${-3} H${L/2+40}" stroke="${ink}" stroke-width="2" stroke-linecap="round"/>`+
    `<g transform="translate(${L*0.82},0)"><path d="M-8,-9 h16 v6 h-5 v6 h5 v6 h-16 v-6 h5 v-6 h-5z" fill="#c4473e" stroke="#7d221c"/></g>`;
  s+=ptxt(p,L/2,24,'NO',{anchor:'middle',size:12.5,weight:800,fill:ink})+stud(0,0)+stud(L,0);
  return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.led=function(p,vm){const S=p.span||3,L=S*P,c=CAT.light,ink=c[4],hue=p.hue||'green',on=!!(vm&&vm.on&&vm.on[p.id]);
  let s='';const cx=L*0.5;
  if(on)s+=`<circle cx="${cx}" cy="0" r="52" fill="url(#skGlow_${hue})"/>`;
  s+=body(p,'light');
  s+=`<g stroke="${ink}" stroke-width="1.6" fill="none" stroke-linecap="round" stroke-linejoin="round"><path d="M14,0 h3 M17,-7 v14 l11,-7 z M28,-7 v14" fill="${ink}"/><path d="M23,-10 l5,-5 m-3,0 h3 v3"/></g>`;
  s+=ptxt(p,16,-13,'+',{anchor:'middle',size:13,weight:900,fill:ink})+ptxt(p,L-16,-12,'−',{anchor:'middle',size:13,weight:900,fill:ink});
  s+=`<circle cx="${cx}" cy="0" r="19.5" fill="#cfc6b2" stroke="#9c9380"/>`;
  if(on)s+=`<circle cx="${cx}" cy="0" r="26" fill="${LED_HUE[hue][2]}" opacity=".45" filter="url(#skBlur)"/>`;
  s+=`<circle cx="${cx}" cy="0" r="15" fill="url(#skLens${on?'On':'Off'}_${hue})"/>`;
  if(!on)s+=`<path d="M${cx-8},-7 A9,9 0 0 1 ${cx+2},-10" fill="none" stroke="#fff" stroke-width="2" stroke-linecap="round" opacity=".45"/>`;
  if(p.glyph)s+=ptxt(p,cx,5.5,p.glyph,{anchor:'middle',size:13,weight:900,fill:on?LED_HUE[hue][3]:'#f3e8e4',op:on?.85:.75});
  if(p.builtinR)s+=`<path d="M${L*0.66},16 h3 l2,-4 l3,8 l3,-8 l3,8 l2,-4 h3" fill="none" stroke="${ink}" stroke-width="1.3"/>`+ptxt(p,L*0.82,22,'R',{anchor:'middle',size:12.5,weight:800,fill:ink});
  s+=stud(0,0)+stud(L,0);
  return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.lamp=function(p,vm){const S=p.span||3,L=S*P,c=CAT.light,ink=c[4],on=!!(vm&&vm.on&&vm.on[p.id]);const cx=L/2;
  let s='';if(on)s+=`<circle cx="${cx}" cy="0" r="58" fill="#ffd27a" opacity=".28" filter="url(#skBlur)"/>`;
  s+=body(p,'light')+`<circle cx="${cx}" cy="0" r="20" fill="${on?'url(#skBulbOn)':'#e9e3d3'}" stroke="#9c9380"/>`+
    `<path d="M${cx-11},6 l4,-10 l4,10 l4,-10 l4,10 l4,-10" fill="none" stroke="${on?'#fff3c4':'#6b4a2a'}" stroke-width="1.8" stroke-linejoin="round"/>`+
    `<g stroke="${ink}" stroke-width="1.6" fill="none"><circle cx="20" cy="0" r="7"/><path d="M15,-5 l10,10 M25,-5 l-10,10"/></g>`+stud(0,0)+stud(L,0);
  return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.resistor=function(p,vm){const S=p.span||2,L=S*P,ink=CAT.passive[4];let s=body(p,'passive');const cx=L/2;
  const tx=p.dir==='v'?cx:cx-9;   // the right end is usually covered by a stacked module
  s+=`<path d="M${tx-16},0 h4 l3,-6 l5,12 l5,-12 l5,12 l5,-6 h4" fill="none" stroke="${ink}" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>`;
  s+=ptxt(p,tx,-12,p.id,{anchor:'middle',size:12.5,weight:800,fill:'#5d5d5d'})+ptxt(p,tx,22,p.value||'',{anchor:'middle',size:12.5,weight:800,fill:ink});
  s+=stud(0,0)+stud(L,0);return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.diode=function(p,vm){const S=p.span||4,L=S*P,ink=CAT.passive[4];let s=body(p,'passive');const cx=L/2;
  // pin a of the part frame is the cathode (k): band on the k side
  s+=`<g stroke="${ink}" stroke-width="1.8" fill="none" stroke-linecap="round" stroke-linejoin="round"><path d="M30,0 H${cx-14} M${cx+14},0 H${L-30}"/><path d="M${cx+14},-10 V10 L${cx-10},0 Z" fill="${ink}"/><path d="M${cx-12},-11 V11" stroke-width="2.8"/></g>`;
  s+=ptxt(p,16,20,'k',{anchor:'middle',size:12.5,weight:900,fill:ink})+ptxt(p,L-16,20,'a',{anchor:'middle',size:12.5,weight:900,fill:ink});
  s+=stud(0,0)+stud(L,0);return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.battery=function(p,vm){const S=p.span||4,L=S*P,n=p.cells||2;let s=body(p,'power');const cw=54,gap=8,tot=n*cw+(n-1)*gap,x0=(L-tot)/2;
  for(let i=0;i<n;i++){const x=x0+i*(cw+gap);s+=`<rect x="${x}" y="-12" width="${cw}" height="24" rx="6" fill="url(#skCell)" stroke="#5d6672"/><rect x="${x+cw}" y="-6" width="4" height="12" rx="1.5" fill="#9aa3ae"/>`+ptxt(p,x+cw/2,5,'1.5 V',{anchor:'middle',size:12.5,weight:800,fill:'#2a3038'});}
  s+=ptxt(p,0,27,'+',{anchor:'middle',size:14,weight:900,fill:'#ffb49e'})+ptxt(p,L,27,'−',{anchor:'middle',size:14,weight:900,fill:'#d7dde6'});
  s+=stud(0,0)+stud(L,0);return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.motor=function(p,vm){const S=p.span||4,L=S*P,ink=CAT.motion[4],on=!!(vm&&vm.on&&vm.on[p.id]);const cx=L/2;
  let s=body(p,'motion')+ptxt(p,16,17,'+',{anchor:'middle',size:13,weight:900,fill:ink})+ptxt(p,L-16,17,'−',{anchor:'middle',size:13,weight:900,fill:ink});
  s+=`<g filter="url(#skSh)"><circle cx="${cx}" cy="0" r="44" fill="url(#skMotor)" stroke="#59626e" stroke-width="1.2"/></g><circle cx="${cx}" cy="0" r="36" fill="#232830"/>`;
  const blade=`M${cx},0 C${cx-8},-16 ${cx-6},-32 ${cx+4},-34 C${cx+12},-32 ${cx+10},-14 ${cx},0 Z`;
  const fan=`<g fill="#e8edf5" fill-opacity=".9" stroke="#9aa3ae" stroke-width=".8"><path d="${blade}"/><path d="${blade}" transform="rotate(120 ${cx} 0)"/><path d="${blade}" transform="rotate(240 ${cx} 0)"/></g>`;
  s+=on?`<g class="sk-spin" style="animation-duration:.25s">${fan}<circle cx="${cx}" cy="0" r="36" fill="none" stroke="none"/></g>`+
        `<g fill="none" stroke="#e8edf5" stroke-linecap="round" opacity=".35" stroke-width="3"><path d="M${cx-28},-14 A31,31 0 0 1 ${cx-4},-31"/><path d="M${cx+26},-16 A31,31 0 0 1 ${cx+29},12"/><path d="M${cx+12},29 A31,31 0 0 1 ${cx-23},21"/></g>`:fan;
  s+=`<circle cx="${cx}" cy="0" r="6" fill="url(#skStudG)" stroke="#59626e"/>`;
  s+=stud(0,0)+stud(L,0);return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.buzzer=function(p,vm){const S=p.span||3,L=S*P,ink=CAT.motion[4],on=!!(vm&&vm.on&&vm.on[p.id]);const cx=L/2+6;
  let s=body(p,'motion')+ptxt(p,16,-12,'+',{anchor:'middle',size:13,weight:900,fill:ink})+ptxt(p,L-16,-12,'−',{anchor:'middle',size:13,weight:900,fill:ink});
  s+=`<circle cx="${cx}" cy="0" r="19" fill="#2b3038" stroke="#151820"/><circle cx="${cx}" cy="0" r="6" fill="#151820"/><circle cx="${cx}" cy="0" r="13" fill="none" stroke="#454b55"/>`;
  s+=`<g stroke="${ink}" stroke-width="1.6" fill="none"><rect x="${cx-48}" y="-8" width="14" height="16"/><path d="M${cx-34},-8 l8,-6 v28 l-8,-6"/></g>`;
  if(on)s+=`<g class="sk-buzz" fill="none" stroke="#e69f00" stroke-width="2.4" stroke-linecap="round"><path d="M${cx+24},-14 A20,20 0 0 1 ${cx+24},14"/><path d="M${cx+31},-21 A30,30 0 0 1 ${cx+31},21"/></g>`;
  s+=stud(0,0)+stud(L,0);return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.ldr=function(p,vm){const S=p.span||3,L=S*P,ink=CAT.sensor[4];let s=body(p,'sensor');const cx=L/2;
  s+=`<circle cx="${cx}" cy="0" r="17" fill="#f6efe2" stroke="#b39e73" stroke-width="1.2"/><path d="M${cx-10},-8 h20 M${cx+10},-8 v4 h-20 v4 h20 v4 h-20 v4 h20" fill="none" stroke="#c0392b" stroke-width="1.5" stroke-linejoin="round"/>`;
  s+=`<g stroke="${ink}" stroke-width="1.6" fill="none" stroke-linecap="round"><path d="M${cx-40},-19 l12,10 m-5,0 h5 v-5"/><path d="M${cx-46},-9 l12,10 m-5,0 h5 v-5"/></g>`;
  s+=stud(0,0)+stud(L,0);return `<g transform="${frame(p)}">${s}</g>`;
};
PARTS.relay=function(p,vm){const S=p.span||4,L=S*P,ink=CAT.relay[4],on=!!(vm&&vm.on&&vm.on[p.id]),c=CAT.relay;
  let s=`<g filter="url(#skSh)"><rect x="-22" y="-23" width="${L+44}" height="${2*P+46}" rx="14" fill="${c[2]}"/><rect x="-22" y="-28" width="${L+44}" height="${2*P+46}" rx="14" fill="url(#skC_relay)" stroke="${c[3]}"/></g>`;
  s+=`<rect x="${L/2-30}" y="-12" width="60" height="24" rx="3" fill="${on?'#f3a53a':'none'}" stroke="${ink}" stroke-width="1.6"/>`+txt(L/2,5,'coil',{anchor:'middle',size:12.5,weight:800,fill:ink});
  s+=`<path d="M0,0 H${L/2-30} M${L/2+30},0 H${L}" stroke="${ink}" stroke-width="1.4" stroke-dasharray="3 3"/>`;
  const y2=2*P;s+=`<g stroke="${ink}" stroke-width="1.8" fill="none" stroke-linecap="round"><path d="M0,${y2} H${L/2-34}"/>${on?`<path d="M${L/2-34},${y2} L${L/2},${y2-2}"/>`:`<path d="M${L/2-34},${y2} L${L-30},${y2-6}"/>`}<path d="M${L/2},${y2-2} V${y2}"/><path d="M${L-30},${y2-6} H${L}"/></g>`;
  s+=`<path d="M${L/2},12 V${y2-10}" stroke="${ink}" stroke-width="1.2" stroke-dasharray="2 3"/>`;
  [['coil +',16,24,'start'],['coil −',L-16,24,'end'],['COM',16,y2-14,'start'],['N.O',L/2+8,y2-14,'start'],['N.C',L-16,y2-14,'end']].forEach(([t,x,y,a])=>s+=txt(x,y,t,{anchor:a,size:12.5,weight:800,fill:ink}));
  s+=stud(0,0)+stud(L,0)+stud(0,y2)+stud(L/2,y2)+stud(L,y2);
  return `<g transform="translate(${p.at[0]},${p.at[1]})">${s}</g>`;
};
/* conductor strip (snap-to-snap link), span 1–7, lies on the board: no shadow */
function strip(st,vm,T){const [x0,y0]=st.a,[x1,y1]=st.b,vert=x0===x1,len=Math.hypot(x1-x0,y1-y0),S=Math.round(len/P);
  const W=wireColours(T)[st.fn]||wireColours(T).plus;const casing=st.fn==='minus'?T.minusStripCasing:W.casing;
  const bx=Math.min(x0,x1)-12,by=Math.min(y0,y1)-12;
  let s=vert?`<rect x="${x0-12}" y="${by}" width="24" height="${len+24}" rx="12" fill="url(#skStripV)" stroke="#7a8597" stroke-width="1.1"/>`
            :`<rect x="${bx}" y="${y0-12}" width="${len+24}" height="24" rx="12" fill="url(#skStripH)" stroke="#7a8597" stroke-width="1.1"/>`;
  s+=`<path d="M${x0},${y0} L${x1},${y1}" stroke="${casing}" stroke-width="5.6"/><path d="M${x0},${y0} L${x1},${y1}" stroke="${st.fn==='minus'?'#17191e':W.core}" stroke-width="3.4"/>`;
  if(st.tracer)s+=`<path d="M${x0-(vert?2:0)},${y0-(vert?0:2)} L${x1-(vert?2:0)},${y1-(vert?0:2)}" stroke="#fff" stroke-width="1.1" opacity=".8"/>`;
  for(let i=1;i<S;i++)s+=`<circle cx="${x0+(x1-x0)*i/S}" cy="${y0+(y1-y0)*i/S}" r="1.8" fill="#c3ccd8"/>`;
  return s;
}
function stripFlow(st,ma,dir){const [x0,y0]=st.a,[x1,y1]=st.b;const d=dir<0?`M${x1},${y1} L${x0},${y0}`:`M${x0},${y0} L${x1},${y1}`;
  return `<path d="${d}" stroke="#fff" stroke-width="2.4" stroke-linecap="round" stroke-dasharray="0.1 14" opacity=".85" fill="none" class="sk-flow" style="animation-duration:${flowDur(ma)}"/>`;}
function stripStuds(st){return stud(st.a[0],st.a[1])+stud(st.b[0],st.b[1]);}

/* ---------------------------------------------------------------- StampLC (functional depiction; no logo, wordmark or label art) */
const TOPX=k=>46+38*k;               // + − C IN1..IN8 screw x (relative)
const RLYX=(n,pin)=>27+118*(n-1)+({NO:0,COM:32,NC:64}[pin]);
function stampTerms(o){const ox=o.x,oy=o.y,t={};
  ['+','-','C','IN1','IN2','IN3','IN4','IN5','IN6','IN7','IN8'].forEach((n,k)=>t[n]={x:ox+TOPX(k),y:oy+28,wire:[ox+TOPX(k),oy-20],side:'top'});
  for(let n=1;n<=4;n++)['NO','COM','NC'].forEach(pin=>t[`RELAY${n}.${pin}`]={x:ox+RLYX(n,pin),y:oy+346,wire:[ox+RLYX(n,pin),oy+394],side:'bottom'});
  t.wifi={x:ox+470,y:oy};return t;
}
function stamplc(o,vm,T){const ox=o.x,oy=o.y,live=vm&&vm.live;
  const used=o.used||{};let s=`<g transform="translate(${ox},${oy})">`;
  s+=`<g filter="url(#skSh)"><rect x="0" y="0" width="470" height="372" rx="18" fill="url(#skPlc)" stroke="${T.plcStroke}" stroke-width="1.5"/></g>`;
  // top strip
  s+=`<rect x="16" y="4" width="440" height="48" rx="8" fill="url(#skTstrip)" stroke="#b2bac4"/><line x1="141" y1="8" x2="141" y2="82" stroke="#8a939e" stroke-width="1.4"/>`;
  for(let k=0;k<11;k++)s+=`<rect x="${TOPX(k)-9}" y="-6" width="18" height="11" rx="2.5" fill="#22272e"/><use href="#skScrew" x="${TOPX(k)}" y="28"/>`;
  const top=['+','−','C'];top.forEach((t,k)=>s+=txt(TOPX(k),77,t,{anchor:'middle',size:k===2?18:20,weight:800,fill:'#262b33'}));
  for(let i=0;i<8;i++){const x=TOPX(i+3);
    s+=`<text x="${x}" y="77" text-anchor="middle" font-weight="800" fill="#262b33"><tspan font-size="12.5" font-weight="700">IN</tspan><tspan font-size="18">${i+1}</tspan></text>`;
    s+=txt(x,93,'di'+i,{anchor:'middle',size:12.5,weight:700,fill:'#4f5865',mono:1});
    // live input chip (overlay: never mistaken for hardware)
    const b=vm&&vm.in?vm.in[i]:null,show=live&&b!=null;
    const f=vm&&vm.inForced&&vm.inForced[i],net=vm&&vm.inNet&&vm.inNet[i];
    s+=`<g data-hop-target="in${i+1}"><rect x="${x-14}" y="99" width="28" height="17" rx="8.5" fill="${show&&b?'#56b4e9':'#1b2130'}" fill-opacity="${show?1:.55}" stroke="${show&&b?'#1f5f86':'#56708a'}"${show&&b?'':' stroke-dasharray="3 2"'}/>`+
       txt(x,112,show?(b?'1':'0')+(f?'F':''):'–',{anchor:'middle',size:12.5,weight:800,fill:show&&b?'#06101e':'#9fb3c8',mono:1})+`</g>`;
    if(net)s+=`<circle cx="${x+15}" cy="100" r="5.5" fill="#1b2130" stroke="${T.net}"/><path d="M${x+12},101 a4,4 0 0 1 6,0" fill="none" stroke="${T.net}" stroke-width="1.3"/>`;
  }
  // face: display, keys, ports, name
  s+=`<rect x="18" y="124" width="172" height="80" rx="9" fill="#0b0e13" stroke="#59626e" stroke-width="2"/>`;
  if(live){const md=vm.mode||'—',mc=md==='RUN'?'#5ff0b0':md==='REMOTE'?'#f3a53a':'#ff8a7a';
    s+=`<circle cx="33" cy="142" r="4.5" fill="${mc}"/>`+txt(43,147,md,{size:14,weight:900,fill:mc})+txt(182,147,vm.conn==='twin'?'twin':'board',{anchor:'end',size:12.5,fill:'#aab8cc',mono:1});
    s+=txt(28,170,'IN '+(vm.in||[]).map(b=>b==null?'·':b).join(' '),{size:12.5,fill:'#cfe0f5',mono:1});
    s+=txt(28,192,'RLY '+(vm.relay||[]).map(b=>b==null?'·':b).join(' '),{size:12.5,fill:'#cfe0f5',mono:1});
  }else{s+=txt(104,160,'not connected',{anchor:'middle',size:13,weight:800,fill:'#7f8ca1'})+txt(104,182,'display idle',{anchor:'middle',size:12.5,fill:'#59626e',mono:1});}
  ['A','B','C'].forEach((k,i)=>s+=`<rect x="${18+i*50}" y="210" width="44" height="22" rx="6" fill="#2b3038"/>`+txt(40+i*50,226,k,{anchor:'middle',size:12.5,weight:800,fill:'#e8edf5'}));
  s+=`<rect x="290" y="124" width="72" height="26" rx="4" fill="#c4473e" stroke="#8a2c26"/><rect x="374" y="124" width="72" height="26" rx="4" fill="#3a78cf" stroke="#244f8c"/>`;
  for(let i=0;i<4;i++)s+=`<rect x="${300+i*13}" y="132" width="9" height="10" rx="1.5" fill="#3a1512"/><rect x="${384+i*13}" y="132" width="9" height="10" rx="1.5" fill="#10213c"/>`;
  s+=txt(326,167,'PORT.A',{anchor:'middle',size:12.5,weight:800,fill:'#262b33'})+txt(410,167,'PORT.C',{anchor:'middle',size:12.5,weight:800,fill:'#262b33'});
  s+=txt(290,198,'StampLC',{size:21,weight:900,fill:'#262b33'})+txt(290,217,'controller · 8 IN · 4 RELAY',{size:12.5,weight:600,fill:'#4f5865'});
  // relay contact windows (overlay): N.O–COM–N.C order like the screws beneath; blade at N.O when energised
  for(let n=1;n<=4;n++){const bx=11+118*(n-1),u=used['RELAY'+n],b=vm&&vm.relay?vm.relay[n-1]:null,show=live&&b!=null;const on=show&&b===1;
    const op=u?(show?.92:.6):.4;
    s+=`<g data-hop-target="relay${n}" opacity="${u?1:.6}"><rect x="${bx}" y="236" width="96" height="44" rx="8" fill="#0d1017" fill-opacity="${op}" stroke="${on?'#4dd8ff':'#56708a'}" stroke-width="1.3" stroke-dasharray="4 3"/>`;
    s+=`<rect x="${bx+39}" y="241" width="18" height="8" rx="2" fill="${on?'#f3a53a':'none'}" stroke="${on?'#f3a53a':'#56708a'}"/>`;
    const lit=show?'#e8edf5':'#8b96a8',dim='#4b5563';
    s+=`<circle cx="${bx+18}" cy="258" r="3.4" fill="${on?lit:dim}"/><circle cx="${bx+78}" cy="258" r="3.4" fill="${show&&!on?lit:dim}"/><circle cx="${bx+48}" cy="273" r="3.6" fill="${show?lit:'#8b96a8'}"/>`;
    s+=on?`<path d="M${bx+48},273 L${bx+20},260" stroke="#ffd28a" stroke-width="3" stroke-linecap="round"/>`:`<path d="M${bx+48},273 L${bx+76},260" stroke="${show?'#cfd6de':'#8b96a8'}" stroke-width="2.6" stroke-linecap="round"/>`;
    s+=`</g>`;
  }
  // bottom labels + screws
  for(let n=1;n<=4;n++){const cx=59+118*(n-1);
    s+=`<text x="${cx-50}" y="299" font-weight="800" fill="#262b33"><tspan font-size="12.5" font-weight="700">RELAY </tspan><tspan font-size="17">${n}</tspan><tspan dx="6" font-size="12.5" font-weight="700" fill="#4f5865" font-family="${MONO}">relay${n-1}</tspan></text>`;
    s+=txt(cx-32,317,'N.O',{anchor:'middle',size:12.5,weight:800,fill:'#262b33'})+`<rect x="${cx-17}" y="305" width="34" height="16" rx="2" fill="#262b33"/>`+txt(cx,318,'COM',{anchor:'middle',size:12.5,weight:800,fill:'#f1f4f8'})+txt(cx+32,317,'N.C',{anchor:'middle',size:12.5,weight:800,fill:'#262b33'});
  }
  s+=`<rect x="6" y="326" width="460" height="42" rx="8" fill="url(#skTstrip)" stroke="#b2bac4"/>`;
  [118,236,354].forEach(x=>s+=`<line x1="${x}" y1="288" x2="${x}" y2="364" stroke="#8a939e" stroke-width="1.4"/>`);
  for(let n=1;n<=4;n++)['NO','COM','NC'].forEach(pin=>{const x=RLYX(n,pin);s+=`<use href="#skScrewS" x="${x}" y="346"/><rect x="${x-8}" y="366" width="16" height="11" rx="2.5" fill="#22272e"/>`;});
  // x-ray trace path inside the controller face (only when tracing)
  if(o.xray)s+=o.xray;
  s+=`</g>`;
  if(o.wifi)s+=wifiBadge(ox+470,oy,T,o.wifiLabel);
  return s;
}
function wifiBadge(x,y,T,label){return `<g data-hop-target="wifi"><circle cx="${x}" cy="${y}" r="19" fill="${T.card}" stroke="${T.net}" stroke-width="2.4"/>`+
  `<g fill="none" stroke="${T.net}" stroke-width="2.2" stroke-linecap="round"><path d="M${x-9},${y-2} a13,13 0 0 1 18,0"/><path d="M${x-5},${y+2.5} a7,7 0 0 1 10,0"/></g><circle cx="${x}" cy="${y+7}" r="2.2" fill="${T.net}"/>`+
  (label?txt(x,y-26,label,{anchor:'middle',size:12.5,weight:800,fill:T.netInk}):'')+`</g>`;}
/* the IN → opto → 0x59 → scan → 0x59 → relay x-ray path, in controller-local coordinates */
function xrayPath(inIdx,relayN,opts){opts=opts||{};const lane=206,rx=59+118*(relayN-1),yb=233;
  let pts,nodes;
  if(opts.network){pts=[[458,14],[458,121],[lane,121],[lane,yb],[rx,yb],[rx,236]];nodes=[['MQTT in',134],['point map',158],['scan',182],['0x59 out',206]];}
  else if(opts.analog){pts=[[470,121],[lane,121],[lane,yb],[rx,yb],[rx,236]];nodes=[['0x20 read',134],['point map',158],['scan',182],['0x59 out',206]];}
  else{const ix=TOPX(inIdx+3);pts=[[ix,116],[ix,121],[lane,121],[lane,yb],[rx,yb],[rx,236]];nodes=[['opto',134],['0x59 in',158],['scan',182],['0x59 out',206]];}
  const d='M'+pts.map(p=>p.join(',')).join(' L');
  let s=`<path d="${d}" fill="none" stroke="#4dd8ff" stroke-width="7" opacity=".22"/><path d="${d}" fill="none" stroke="#0aa4cf" stroke-width="2.2" stroke-dasharray="5 4"/>`;
  nodes.forEach(([t,y],i)=>{const byp=opts.bypass&&opts.bypass.includes(i);
    s+=`<circle cx="${lane}" cy="${y}" r="4.2" fill="${byp?'#7a8597':'#0aa4cf'}" stroke="#e9fbff" stroke-width="1.2"/>`+txt(lane+9,y+4.5,t,{size:12.5,weight:800,fill:byp?'#6b7484':'#066683'});
    if(byp)s+=`<path d="M${lane+8},${y} h${t.length*7.2+4}" stroke="#6b7484" stroke-width="1.3"/>`;});
  return s;
}

/* ---------------------------------------------------------------- IO expansion module (functional depiction, docked on the controller's side) */
const EXP_PINS=[['CH1.IN+','IN+',1],['CH1.IN-','IN−',1],['CH1.ISO','ISO GND',1],['CH2.IN+','IN+',2],['CH2.IN-','IN−',2],['CH2.ISO','ISO GND',2]];
function expTerms(o){const t={};EXP_PINS.forEach(([id],i)=>{const y=o.y+50+i*40+(i>=3?40:0);t[id]={x:o.x+o.w-16,y,wire:[o.x+o.w+22,y],side:'right'};});return t;}
function expansion(o,vm,T){const w=o.w,h=o.h,used=o.used||{};let s=`<g transform="translate(${o.x},${o.y})">`;
  s+=txt(4,-26,'X1 · IO EXPANSION',{size:13.5,weight:800,fill:T.ink})+txt(4,-9,'analog 0–10 V · I²C @0x20',{size:12.5,fill:T.sub});
  s+=`<g filter="url(#skSh)"><rect x="0" y="0" width="${w}" height="${h}" rx="12" fill="url(#skPlc)" stroke="${T.plcStroke}" stroke-width="1.4"/></g>`;
  s+=`<rect x="-7" y="18" width="10" height="${h-36}" rx="3" fill="#8a939e"/>`;            // docking connector to the controller
  s+=`<rect x="${w-31}" y="12" width="28" height="${h-24}" rx="6" fill="url(#skTstrip)" stroke="#b2bac4"/>`;
  EXP_PINS.forEach(([id,lbl,ch],i)=>{const y=50+i*40+(i>=3?40:0),u=used[id];
    s+=`<g opacity="${u?1:.45}"><use href="#skScrewS" x="${w-16}" y="${y}"/><rect x="${w+2}" y="${y-8}" width="11" height="16" rx="2.5" fill="#22272e"/>`+txt(w-34,y+4.5,lbl,{anchor:'end',size:12.5,weight:800,fill:'#262b33'})+`</g>`;});
  s+=txt(10,30,'CH1',{size:13,weight:900,fill:'#262b33'})+txt(10,190,'CH2',{size:13,weight:900,fill:'#262b33',op:.5});
  s+=`<line x1="8" y1="168" x2="${w-36}" y2="168" stroke="#b2bac4"/>`;
  s+=`</g>`;return s;
}

/* ---------------------------------------------------------------- leads (flying jumper wires) */
/* A lead: casing 10 / core 7 / sheen 2 (α .55) with a drop shadow; a second, separate supply gets a white
 * tracer instead of the sheen. Screw end: ferrule + insulation collar + marker sleeve. Snap end: clip. */
function leadPoints(l,terms){const pts=[];const f=l.from,t=l.to;
  const end=e=>e.term?terms[e.term].wire:e.stud;
  pts.push(end(f));(l.via||[]).forEach(v=>pts.push(v));pts.push(end(t));return pts;}
function leadBody(l,pts,T){const W=wireColours(T)[l.fn];const d=roundPath(pts,l.r);
  let s=`<path d="${d}" stroke="${W.casing}" stroke-width="10"/><path d="${d}" stroke="${W.core}" stroke-width="7"/>`;
  s+=l.tracer?`<path d="${d}" transform="translate(-2.3,-2.3)" stroke="#ffffff" stroke-width="1.9"/>`:`<path d="${d}" stroke="${W.sheen}" stroke-width="2" opacity="${l.fn==='minus'?.7:.55}"/>`;
  return s;}
function leadFlow(l,pts,ma,dir){const P2=dir<0?pts.slice().reverse():pts;const d=roundPath(P2,l.r);
  return `<path d="${d}" stroke="#fff" stroke-width="${flowWidth(ma)}" stroke-dasharray="0.1 14" opacity=".9" fill="none" class="sk-flow" style="animation-duration:${flowDur(ma)}"/>`;}
function sleeve(x,y,label,fn,T,vertical){const W=wireColours(T)[fn];const w=Math.max(24,label.length*8+12);
  return `<g transform="translate(${r1(x)},${r1(y)})"><rect x="${-w/2}" y="-9" width="${w}" height="18" rx="3" fill="#f4f6f8" stroke="${fn==='minus'?'#59616d':W.casing}" stroke-width="1.5"/>`+txt(0,4.8,label,{anchor:'middle',size:12.5,weight:800,fill:'#1d222a',mono:1})+`</g>`;}
function clip(x,y,fn,T){const W=wireColours(T)[fn];return `<g transform="translate(${x},${y})"><circle r="13.5" fill="${W.casing}"/><circle r="10.5" fill="${W.core}"/><circle r="4.5" fill="url(#skStudG)"/></g>`;}
function screwEnd(term,fn,T,twin){const W=wireColours(T)[fn];const x=term.x;let s='';const fw=twin?10:6;
  if(term.side==='top'){const y0=term.wire[1];s+=`<rect x="${x-fw/2}" y="${y0+4}" width="${fw}" height="15" fill="url(#skFerr)" stroke="#6b7480" stroke-width=".8"/><rect x="${x-6}" y="${y0-5}" width="12" height="11" rx="2" fill="${W.core}" stroke="${W.casing}"/>`;}
  else if(term.side==='bottom'){const y0=term.wire[1];s+=`<rect x="${x-fw/2}" y="${y0-19}" width="${fw}" height="14" fill="url(#skFerr)" stroke="#6b7480" stroke-width=".8"/><rect x="${x-6}" y="${y0-6}" width="12" height="11" rx="2" fill="${W.core}" stroke="${W.casing}"/>`;}
  else{const x0=term.wire[0],y=term.y;s+=`<rect x="${x0-21}" y="${y-fw/2}" width="15" height="${fw}" fill="url(#skFerr)" stroke="#6b7480" stroke-width=".8"/><rect x="${x0-7}" y="${y-6}" width="11" height="12" rx="2" fill="${W.core}" stroke="${W.casing}"/>`;}
  return s;}

/* ---------------------------------------------------------------- network link (never copper) */
function netLink(pts,T,o){o=o||{};const d='M'+pts.map(p=>p.join(',')).join(' L');const c=o.dim?T.idle:T.net;
  let s=`<path d="${d}" fill="none" stroke="${c}" stroke-width="2.6" stroke-dasharray="2 8" stroke-linecap="round"${o.dim?' opacity=".7"':''}/>`;
  if(o.packets){const L=polyLen(pts);const n=Math.max(1,Math.floor(L/70));for(let i=0;i<n;i++){const a=along(pts,(i+.5)*L/n);s+=`<rect x="${r1(a.x-5)}" y="${r1(a.y-5)}" width="10" height="10" transform="rotate(45 ${r1(a.x)} ${r1(a.y)})" fill="${o.live?'#f0b3d3':T.card}" stroke="${c}" stroke-width="1.5"/>`;}}
  return s;}
function netJack(x,y,T,dim){const c=dim?T.idle:T.net;return `<circle cx="${x}" cy="${y}" r="17" fill="${T.card}" stroke="${c}" stroke-width="2.2"/><g fill="none" stroke="${c}" stroke-width="2" stroke-linecap="round"><path d="M${x-8},${y-1} a11,11 0 0 1 16,0"/><path d="M${x-4.5},${y+3} a6,6 0 0 1 9,0"/></g><circle cx="${x}" cy="${y+8}" r="2" fill="${c}"/>`;}

/* ---------------------------------------------------------------- cards */
function card(x,y,w,h,T,o){o=o||{};return `<g transform="translate(${x},${y})"><rect width="${w}" height="${h}" rx="${o.rx||12}" fill="${T.card}" fill-opacity="${o.op||.96}" stroke="${o.stroke||T.cardStroke}"${o.dash?' stroke-dasharray="6 5"':''}/>`;}
function noteCard(n,T){const lh=n.lh||18,h=36+n.lines.length*lh+(n.foot?lh:0)+4;let s=card(n.x,n.y,n.w,h,T,{stroke:n.accent?T.warnInk:T.cardStroke});
  s+=txt(14,24,n.title,{size:13,weight:800,fill:n.accent?T.warnInk:T.ink});
  n.lines.forEach((l,i)=>s+=txt(14,48+i*lh,l,{size:12.5,fill:T.cardInk}));
  if(n.foot)s+=txt(14,48+n.lines.length*lh+4,n.foot,{size:12.5,fill:T.mut});
  return s+`</g>`;}
function legend(x,y,fns,T,extra){const W=wireColours(T);let s=`<g transform="translate(${x},${y})">`+txt(0,0,'WIRE COLOUR = FUNCTION',{size:12.5,weight:800,fill:T.mut,ls:'.6'});
  const rows=[];fns.forEach(f=>{if(f==='tracer')rows.push(['tracer','white tracer = second, separate supply']);else if(f==='net')rows.push(['net','network link — never drawn as copper']);else if(f==='flow')rows.push(['flow','moving dots = current; faster = more current']);else rows.push([f,{plus:'+ supply (controller "+", 4.45 V on USB)',minus:'− return',signal:'signal into an input',load:'switched load (relay contact → load)',bus:'module bus (expansion connector)'}[f]]);});
  (extra||[]).forEach(r=>rows.push(r));
  rows.forEach(([f,label],i)=>{const yy=16+i*17;
    if(f==='net')s+=`<path d="M0,${yy} H34" stroke="${T.net}" stroke-width="2.6" stroke-dasharray="2 6" stroke-linecap="round"/>`;
    else if(f==='flow')s+=`<path d="M0,${yy} H34" stroke="#4b5563" stroke-width="6" stroke-linecap="round"/><path d="M0,${yy} H34" stroke="#fff" stroke-width="3" stroke-dasharray="0.1 11" stroke-linecap="round"/>`;
    else if(f==='tracer')s+=`<path d="M0,${yy} H34" stroke="#8e2a12" stroke-width="8" stroke-linecap="round"/><path d="M0,${yy} H34" stroke="#e4572e" stroke-width="5.5" stroke-linecap="round"/><path d="M0,${yy-2} H34" stroke="#fff" stroke-width="1.4"/>`;
    else if(W[f])s+=`<path d="M0,${yy} H34" stroke="${W[f].casing}" stroke-width="8" stroke-linecap="round"/><path d="M0,${yy} H34" stroke="${W[f].core}" stroke-width="5.5" stroke-linecap="round"/>`;
    s+=txt(46,yy+4.5,label,{size:12.5,fill:T.cardInk});});
  return s+`</g>`;}

/* ---------------------------------------------------------------- schematic symbols (IEC 60617 style, drawn at a point, horizontal) */
const SCH={};
SCH.wire=(pts,col,w)=>`<path d="M${pts.map(p=>p.join(',')).join(' L')}" fill="none" stroke="${col}" stroke-width="${w||2}" stroke-linejoin="round"/>`;
SCH.dot=(x,y,col)=>`<circle cx="${x}" cy="${y}" r="3.5" fill="${col}"/>`;
/* each symbol occupies [x-30, x+30] on a horizontal rung at y; returns svg */
SCH.no=(x,y,c,closed)=>`<path d="M${x-30},${y} H${x-14} M${x+14},${y} H${x+30}" stroke="${c}" stroke-width="2" fill="none"/>`+(closed?`<path d="M${x-14},${y} L${x+16},${y-2}" stroke="${c}" stroke-width="2.4"/>`:`<path d="M${x-14},${y} L${x+14},${y-14}" stroke="${c}" stroke-width="2.4"/>`)+`<path d="M${x+14},${y} v-6" stroke="${c}" stroke-width="2"/>`;
SCH.nc=(x,y,c,closed)=>`<path d="M${x-30},${y} H${x-14} M${x+14},${y} H${x+30}" stroke="${c}" stroke-width="2" fill="none"/>`+(closed?`<path d="M${x-14},${y} L${x+16},${y+6}" stroke="${c}" stroke-width="2.4"/>`:`<path d="M${x-14},${y} L${x+12},${y-12}" stroke="${c}" stroke-width="2.4"/>`)+`<path d="M${x+14},${y} v8" stroke="${c}" stroke-width="2"/>`;
SCH.push=(x,y,c,closed,nc)=>(nc?SCH.nc(x,y,c,!closed):SCH.no(x,y,c,closed))+`<path d="M${x},${y-(closed?2:8)} V${y-24} M${x-8},${y-24} H${x+8}" stroke="${c}" stroke-width="1.8" stroke-dasharray="${closed?'0':'3 2'}" fill="none"/>`;
SCH.slide=(x,y,c,closed)=>SCH.no(x,y,c,closed)+`<path d="M${x-4},${y-(closed?2:8)} V${y-22} H${x+6} V${y-26}" stroke="${c}" stroke-width="1.6" fill="none" stroke-dasharray="3 2"/>`;
SCH.res=(x,y,c)=>`<path d="M${x-30},${y} H${x-18} M${x+18},${y} H${x+30}" stroke="${c}" stroke-width="2"/><rect x="${x-18}" y="${y-7}" width="36" height="14" fill="none" stroke="${c}" stroke-width="2"/>`;
SCH.ldr=(x,y,c)=>SCH.res(x,y,c)+`<g stroke="${c}" stroke-width="1.6" fill="none"><path d="M${x-14},${y-26} l10,12 m-5,0 h5 v-5"/><path d="M${x-2},${y-28} l10,12 m-5,0 h5 v-5"/></g>`;
SCH.led=(x,y,c,on,hue)=>{const f=on?(LED_HUE[hue]||LED_HUE.green)[2]:'none';return `<path d="M${x-30},${y} H${x-9} M${x+9},${y} H${x+30}" stroke="${c}" stroke-width="2"/><path d="M${x-9},${y-10} V${y+10} L${x+9},${y} Z" fill="${f}" stroke="${c}" stroke-width="2" stroke-linejoin="round"/><path d="M${x+9},${y-10} V${y+10}" stroke="${c}" stroke-width="2.4"/><g stroke="${c}" stroke-width="1.5" fill="none"><path d="M${x+2},${y-14} l7,-8 m-4,0 h4 v4"/><path d="M${x+9},${y-11} l7,-8 m-4,0 h4 v4"/></g>`+(on?`<circle cx="${x}" cy="${y}" r="20" fill="${f}" opacity=".25"/>`:'');};
SCH.diode=(x,y,c,rev)=>{const s=rev?-1:1;return `<path d="M${x-30},${y} H${x+30}" stroke="${c}" stroke-width="2"/><path d="M${x-9*s},${y-10} V${y+10} L${x+9*s},${y} Z" fill="${c}" stroke="${c}" stroke-width="2" stroke-linejoin="round"/><path d="M${x+9*s},${y-10} V${y+10}" stroke="${c}" stroke-width="2.6"/>`;};
SCH.motor=(x,y,c,on)=>`<path d="M${x-30},${y} H${x-16} M${x+16},${y} H${x+30}" stroke="${c}" stroke-width="2"/><circle cx="${x}" cy="${y}" r="16" fill="${on?'rgba(95,240,176,.18)':'none'}" stroke="${c}" stroke-width="2"/>`+txt(x,y+5.5,'M',{anchor:'middle',size:15,weight:800,fill:c});
SCH.buzzer=(x,y,c,on)=>`<path d="M${x-30},${y} H${x-12} M${x+12},${y} H${x+30}" stroke="${c}" stroke-width="2"/><path d="M${x-12},${y+8} V${y-4} A12,12 0 0 1 ${x+12},${y-4} V${y+8} Z" fill="${on?'rgba(230,159,0,.3)':'none'}" stroke="${c}" stroke-width="2"/>`;
SCH.lamp=(x,y,c,on)=>`<path d="M${x-30},${y} H${x-14} M${x+14},${y} H${x+30}" stroke="${c}" stroke-width="2"/><circle cx="${x}" cy="${y}" r="14" fill="${on?'rgba(255,210,122,.4)':'none'}" stroke="${c}" stroke-width="2"/><path d="M${x-10},${y-10} L${x+10},${y+10} M${x+10},${y-10} L${x-10},${y+10}" stroke="${c}" stroke-width="1.6"/>`;
SCH.battery=(x,y,c)=>`<path d="M${x},${y-30} V${y-6} M${x},${y+6} V${y+30}" stroke="${c}" stroke-width="2"/><path d="M${x-16},${y-6} H${x+16}" stroke="${c}" stroke-width="2.4"/><path d="M${x-8},${y+6} H${x+8}" stroke="${c}" stroke-width="4"/>`+txt(x+22,y-6,'+',{size:14,weight:900,fill:c});
SCH.coil=(x,y,c,on)=>`<rect x="${x-20}" y="${y-11}" width="40" height="22" fill="${on?'#f3a53a':'none'}" stroke="${c}" stroke-width="2"/><path d="M${x-12},${y+11} L${x+12},${y-11}" stroke="${on?'#5a3a00':c}" stroke-width="1.4"/>`;

/* ---------------------------------------------------------------- export */
const api={P,THEMES,TAGS,CAT,LED_HUE,MONO,esc,txt,tag,tagWidth,stud,roundPath,along,polyLen,flowDur,flowWidth,wireColours,defs,partPins,PARTS,strip,stripFlow,stripStuds,
  stampTerms,stamplc,wifiBadge,xrayPath,expTerms,expansion,EXP_PINS,leadPoints,leadBody,leadFlow,sleeve,clip,screwEnd,netLink,netJack,card,noteCard,legend,SCH};
if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.SnapKit=api;
})(typeof window!=='undefined'?window:globalThis);
