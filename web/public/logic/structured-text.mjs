/* Independently authored adapter for Logic Studio's existing ladder model.
 * No upstream compiler/editor implementation is embedded here. */
export function exportStructuredText(program, {scanMs} = {}) {
  const integer=(v,min,max,what)=>{
    if(!Number.isInteger(v)||v<min||v>max)throw new Error(`Invalid ${what}`);
    return v;
  };
  if(!program || !Array.isArray(program.nets) || !program.nets.length)
    throw new Error('Program needs networks');
  integer(program.scan_ms,0,1000,'program scan period');
  const period=program.scan_ms || scanMs;
  integer(period,1,1000,'explicit target scan period');
  const vars=new Map(), blocks=new Map(), ids=new Set(), body=[], spans=[], temporaries=[];
  const claim=id=>{
    if(typeof id!=='string'||!id.length||ids.has(id))throw new Error('Missing or duplicate element ID');
    ids.add(id);
  };
  const emit=(text,id)=>{const start=body.join('').length;body.push(text+'\n');spans.push({id,start,end:start+text.length});};
  const variable=(io,idx,write=false)=>{
    const count={IX:8,QX:4,MX:32,IW:8}[io];
    if(!count || (write&&!['QX','MX'].includes(io)))throw new Error('Unsupported I/O class');
    integer(idx,0,count-1,'I/O index');
    const name=io+idx;
    if(!vars.has(name))vars.set(name,{name,io,idx,type:io==='IW'?'DINT':'BOOL',
      address:['IX','QX'].includes(io)?`%${io}0.${idx}`:null});
    return name;
  };
  const neg=v=>{if(typeof v!=='boolean')throw new Error('Invalid negation flag');return v;};
  function expression(n,depth=0) {
    if(!n)return 'FALSE';
    if(depth>64||temporaries.length>=256)throw new Error('Program expression limit');
    claim(n.id);
    let value;
    if(n.t==='c') {
      if(n.io==='IW')throw new Error('Analog contact requires an explicit comparison');
      value=(neg(n.neg)?'NOT ':'')+variable(n.io,n.idx);
    } else if(n.t==='cmp') {
      const op={GT:'>',LT:'<',GE:'>=',LE:'<=',EQ:'='}[n.op];
      if(!op)throw new Error('Unsupported comparison');
      integer(n.k,-2147483648,2147483647,'DINT constant');
      value=`${variable('IW',n.idx)} ${op} ${n.k===-2147483648?'DINT#-2147483648':n.k}`;
    } else if(n.t==='series'||n.t==='par') {
      if(!Array.isArray(n.kids)||!n.kids.length)throw new Error('Empty branch');
      // Materialize children in evaluation order; do not defer a repeated FB's Q read.
      const parts=n.kids.map(k=>expression(k,depth+1));
      value=parts.join(n.t==='series'?' AND ':' OR ');
    } else if(n.t==='ton'||n.t==='rtrig') {
      integer(n.inst,0,15,'function block instance');
      const type=n.t==='ton'?'TON':'R_TRIG',name='FB'+n.inst;
      if(blocks.has(name)&&blocks.get(name)!==type)throw new Error('Function block type conflict');
      blocks.set(name,type);
      const input=expression(n.in,depth+1);
      if(type==='TON')integer(n.preset,0,2147483647,'timer preset');
      emit(`${name}(${type==='TON'?'IN':'CLK'} := ${input}${type==='TON'?`, PT := T#${n.preset}ms`:''});`,n.id);
      value=name+'.Q';
    } else throw new Error(`Unsupported node type: ${n.t}`);
    if(temporaries.length>=256)throw new Error('Program expression limit');
    const temp='EXPR'+temporaries.length;temporaries.push(temp);
    emit(`${temp} := ${value};`,n.id);
    return temp;
  }
  integer(program.nets.length,1,64,'network count');
  for(const network of program.nets) {
    claim(network.id);claim(network.id+'coil');
    const start=body.join('').length;
    const result=expression(network.expr);
    const coil=network.coil;
    if(!coil)throw new Error('Missing coil');
    emit(`${variable(coil.io,coil.idx,true)} := ${neg(coil.neg)?'NOT ':''}${result};`,network.id+'coil');
    spans.push({id:network.id,start,end:body.join('').length});
  }
  const declarations=[...vars.values()].map(v=>` ${v.name}${v.address?' AT '+v.address:''} : ${v.type};`);
  declarations.push(...[...blocks].map(([n,t])=>` ${n} : ${t};`),...temporaries.map(n=>` ${n} : BOOL;`));
  const header='PROGRAM StudioProgram\nVAR\n'+declarations.join('\n')+'\nEND_VAR\n';
  const source=header+body.join('')+'END_PROGRAM\nCONFIGURATION Config0\nRESOURCE Res0 ON PLC\n'+
    `TASK Main(INTERVAL := T#${period}ms, PRIORITY := 0);\nPROGRAM Instance0 WITH Main : StudioProgram;\nEND_RESOURCE\nEND_CONFIGURATION\n`;
  return {source,scanMs:period,bindings:[...vars.values()],
    mappings:spans.map(s=>({...s,start:s.start+header.length,end:s.end+header.length})),
    bindingRequirement:'IX/QX located; IW DINT and MX BOOL require explicit firmware adapter bindings. No target deployment admission.'};
}
