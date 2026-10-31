// Circuit catalog check — no browser, no twin.   node tools/circuit-catalog-check.js
// For every predesigned Part 5 circuit (web/public/circuits/catalog.js) it rebuilds the connectivity
// from the BENCH DRAWING (part studs, conductor strips, flying leads, controller/module terminals) and
// fails if that differs from the circuit's declared netlist (which the schematic view is drawn from).
// It also enforces the wiring rules of the visual spec:
//   - every lead lands on a real stud or terminal (no dangling clip);
//   - strips are 1–7 grid units, on the 40-unit grid;
//   - no lead passes over a stud it does not land on, or over a module body (except where it lands);
//   - no two leads cross; every lead-over-strip crossing is captioned.
const path=require('path');
const SK=require(path.join(__dirname,'../web/public/circuits/snapkit.js'));
const CAT=require(path.join(__dirname,'../web/public/circuits/catalog.js'));
let failures=0;const fail=(c,m)=>{failures++;console.log(`  ✗ [${c.id}] ${m}`);};
const key=p=>p[0]+','+p[1];

function bodyRect(p){const [x,y]=p.at,S=p.span||2,L=S*SK.P;
  if(p.type==='relay')return{x0:x-22,x1:x+L+22,y0:y-28,y1:y+2*SK.P+18};
  const ext=(p.type==='motor')?44:28;           // motor housing r44 overhangs the 56-wide body
  return p.dir==='v'?{x0:x-ext,x1:x+ext,y0:y-22,y1:y+L+22}:{x0:x-22,x1:x+L+22,y0:y-ext,y1:y+ext};}
function segs(pts){const out=[];for(let i=1;i<pts.length;i++)out.push([pts[i-1],pts[i]]);return out;}
function segHitsRect(a,b,r,pad){const x0=Math.min(a[0],b[0]),x1=Math.max(a[0],b[0]),y0=Math.min(a[1],b[1]),y1=Math.max(a[1],b[1]);
  return x1>=r.x0-pad&&x0<=r.x1+pad&&y1>=r.y0-pad&&y0<=r.y1+pad;}
function distPointSeg(p,a,b){const dx=b[0]-a[0],dy=b[1]-a[1],L2=dx*dx+dy*dy||1;let t=((p[0]-a[0])*dx+(p[1]-a[1])*dy)/L2;t=Math.max(0,Math.min(1,t));return Math.hypot(a[0]+t*dx-p[0],a[1]+t*dy-p[1]);}
function cross(a,b,c,d){ // orthogonal segment intersection (interior)
  const h1=a[1]===b[1],h2=c[1]===d[1];if(h1===h2)return null;const [H,V]=h1?[[a,b],[c,d]]:[[c,d],[a,b]];
  const hx0=Math.min(H[0][0],H[1][0]),hx1=Math.max(H[0][0],H[1][0]),vy0=Math.min(V[0][1],V[1][1]),vy1=Math.max(V[0][1],V[1][1]);
  const x=V[0][0],y=H[0][1];return (x>hx0&&x<hx1&&y>vy0&&y<vy1)?[x,y]:null;}

for(const c of CAT.CIRCUITS){
  console.log(`circuit ${c.id} · ${c.name}`);
  const terms=SK.stampTerms(c.ctl);const exp=c.expansion?SK.expTerms(c.expansion):{};
  const parent={};const find=k=>{while(parent[k]&&parent[k]!==k)k=parent[k];return k;};
  const union=(a,b)=>{parent[a]=parent[a]||a;parent[b]=parent[b]||b;const ra=find(a),rb=find(b);if(ra!==rb)parent[ra]=rb;};
  const studs=new Map();                                  // coordinate key -> what sits there
  const addStud=(p,what)=>{const k=key(p);studs.set(k,(studs.get(k)||[]).concat(what));};
  for(const p of c.parts){const pins=SK.partPins(p);for(const [n,xy] of Object.entries(pins)){union(p.id+'.'+n,'@'+key(xy));addStud(xy,p.id+'.'+n);}
    for(const xy of Object.values(pins))if((xy[0]-620)%40||(xy[1]-100)%40)fail(c,`${p.id} stud ${key(xy)} is off the 40-unit grid`);}
  for(const s of c.strips){union('@'+key(s.a),'@'+key(s.b));addStud(s.a,'strip');addStud(s.b,'strip');
    const span=Math.hypot(s.b[0]-s.a[0],s.b[1]-s.a[1])/SK.P;if(span<1||span>7||span%1)fail(c,`strip ${key(s.a)}→${key(s.b)} span ${span} (must be 1–7 whole units)`);
    if(s.a[0]!==s.b[0]&&s.a[1]!==s.b[1])fail(c,`strip ${key(s.a)}→${key(s.b)} is not straight`);}
  const termName=t=>exp[t]?'X1.'+t:'U1.'+t;
  const leadPts={};
  for(const l of c.leads){const pts=SK.leadPoints(l,Object.assign({},terms,exp));leadPts[l.id]=pts;
    const end=e=>e.term?termName(e.term):'@'+key(e.stud);union(end(l.from),end(l.to));
    for(const e of [l.from,l.to])if(e.stud&&!studs.has(key(e.stud)))fail(c,`lead ${l.id} lands on ${key(e.stud)} where there is no stud`);
    for(const e of [l.from,l.to])if(e.term&&!terms[e.term]&&!exp[e.term])fail(c,`lead ${l.id} names unknown terminal ${e.term}`);
    for(let i=1;i<pts.length;i++)if(pts[i][0]!==pts[i-1][0]&&pts[i][1]!==pts[i-1][1])fail(c,`lead ${l.id} segment ${i} is not orthogonal`);}
  if(c.jumper)union('U1.-','U1.C');
  // groups -> pin sets
  const groups={};for(const k of Object.keys(parent)){if(k.startsWith('@'))continue;const r=find(k);(groups[r]=groups[r]||[]).push(k);}
  const drawn=Object.values(groups).filter(g=>g.length>1).map(g=>g.sort().join(' '));
  const declared=Object.entries(c.nets).map(([n,pins])=>[n,pins.slice().sort().join(' ')]);
  for(const [n,d] of declared)if(!drawn.includes(d))fail(c,`net ${n} {${d}} is not what the drawing connects`);
  for(const d of drawn)if(!declared.some(([,x])=>x===d))fail(c,`drawing connects {${d}} but no declared net matches`);
  // every declared pin exists
  for(const [n,pins] of Object.entries(c.nets))for(const pin of pins){const [id]=pin.split('.');if(id!=='U1'&&id!=='X1'&&!c.parts.find(p=>p.id===id))fail(c,`net ${n} names unknown part ${id}`);}
  // routing rules
  for(const l of c.leads){const pts=leadPts[l.id],ends=[l.from.stud,l.to.stud].filter(Boolean).map(key);
    const S=segs(pts);
    S.forEach(([a,b],si)=>{
      for(const [k,what] of studs){if(ends.includes(k))continue;const xy=k.split(',').map(Number);if(distPointSeg(xy,a,b)<14)fail(c,`lead ${l.id} passes over the stud at ${k} (${what.join(', ')}) without landing`);}
      for(const p of c.parts){const r=bodyRect(p);
        // the only part of a lead allowed over a body is the last 34 units of the segment that lands on that
        // part's stud (the clip sits on the stud at the body's end); anything else is a wire over/under a part
        let A=a,B=b;const pins=Object.values(SK.partPins(p)).map(key);
        const landEnd=(si===S.length-1&&l.to.stud&&pins.includes(key(l.to.stud)))?b:(si===0&&l.from.stud&&pins.includes(key(l.from.stud)))?a:null;
        if(landEnd){const o=landEnd===b?a:b,L=Math.hypot(o[0]-landEnd[0],o[1]-landEnd[1])||1,cut=Math.min(L,34);
          const q=[landEnd[0]+(o[0]-landEnd[0])/L*cut,landEnd[1]+(o[1]-landEnd[1])/L*cut];if(L<=34)continue;A=q;B=o;}
        if(segHitsRect(A,B,r,4))fail(c,`lead ${l.id} passes over/under ${p.id}'s body`);}
    });}
  const ids=Object.keys(leadPts);
  for(let i=0;i<ids.length;i++)for(let j=i+1;j<ids.length;j++)for(const [a,b] of segs(leadPts[ids[i]]))for(const [x,y] of segs(leadPts[ids[j]])){const p=cross(a,b,x,y);if(p)fail(c,`leads ${ids[i]} and ${ids[j]} cross at ${key(p)} (route around: junctions exist only at studs)`);}
  let stripX=0;for(const l of c.leads)for(const [a,b] of segs(leadPts[l.id]))for(const s of c.strips){if(cross(a,b,s.a,s.b))stripX++;}
  // spec R7: a crossing is allowed only over a strip (never lead-over-lead, checked above) and the first one
  // in a drawing is captioned ("lead passes over the − strip · no stud = no joint")
  const captions=(c.crossings||[]).filter(x=>!x.cont).length;
  if(stripX&&!captions)fail(c,`${stripX} lead-over-strip crossing(s) but no caption`);
  if(!stripX&&captions)fail(c,`a crossing caption but no crossing`);
  console.log(`  nets ${declared.length} · drawn groups ${drawn.length} · lead-over-strip crossings ${stripX} (captioned ${captions})`);
}
if(failures){console.log(`circuit-catalog-check: ${failures} problem(s)`);process.exit(1);}
console.log('circuit-catalog-check: all circuits consistent');
