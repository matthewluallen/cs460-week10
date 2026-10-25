import {exportStructuredText} from './structured-text.mjs';

// Initial reverse-authoring grammar: the explicit, ordered form produced by our
// exporter. Arbitrary IEC ST needs additional AST/ladder constructs, not guessing.
export function parseStructuredText(source) {
  if(typeof source!=='string'||source.length>131072)throw new Error('ST source limit');
  const fail=message=>{throw new Error(message);};
  // Comments do not participate in semantics. Reject nested or unfinished ones.
  const clean=source.replace(/\(\*[\s\S]*?\*\)|\/\/[^\r\n]*/g,text=>{
    if(text.startsWith('(*')&&text.slice(2,-2).includes('(*'))fail('Nested comments unsupported');
    return ' ';
  }).toUpperCase();
  if(clean.includes('(*')||clean.includes('*)'))fail('Unfinished comment');
  const envelope=/^\s*PROGRAM\s+STUDIOPROGRAM\s+VAR\b([\s\S]*?)\bEND_VAR\b([\s\S]*?)\bEND_PROGRAM\s+CONFIGURATION\s+CONFIG0\s+RESOURCE\s+RES0\s+ON\s+PLC\s+TASK\s+MAIN\s*\(\s*INTERVAL\s*:=\s*T#(\d+)MS\s*,\s*PRIORITY\s*:=\s*0\s*\)\s*;\s*PROGRAM\s+INSTANCE0\s+WITH\s+MAIN\s*:\s*STUDIOPROGRAM\s*;\s*END_RESOURCE\s+END_CONFIGURATION\s*$/;
  const document=envelope.exec(clean);
  if(!document)fail('Unsupported program structure; keep the generated program/task declarations');
  const declarations=document[1],body=document[2],scan_ms=Number(document[3]);
  const refs=new Map(),assigned=new Set(),nets=[];
  let pending=null,serial=0;
  const id=()=>`st_e${++serial}`;
  const take=name=>{
    if(name==='FALSE')return null;
    if(!refs.has(name))fail(`Missing, repeated or cross-network expression reference ${name}`);
    const node=refs.get(name);refs.delete(name);return node;
  };
  const statements=body.split(';');
  if(statements.pop().trim())fail('Missing statement terminator');
  for(const raw of statements) {
    const line=raw.trim();if(!line)fail('Empty statement unsupported');
    let m;
    if((m=/^FB(\d+)\s*\(\s*(IN|CLK)\s*:=\s*(EXPR\d+|FALSE)(?:\s*,\s*PT\s*:=\s*T#(\d+)MS)?\s*\)$/.exec(line))) {
      if(pending)fail('Function block output must be captured before the next call');
      if((m[2]==='IN')!==(m[4]!==undefined))fail('TON requires PT; R_TRIG has no PT');
      pending={t:m[2]==='IN'?'ton':'rtrig',inst:Number(m[1]),in:take(m[3]),id:id()};
      if(m[4]!==undefined)pending.preset=Number(m[4]);
      continue;
    }
    const assignment=/^(EXPR\d+|QX\d+|MX\d+)\s*:=\s*([\s\S]+)$/.exec(line);
    if(!assignment)fail('Unsupported statement; no ladder program was changed');
    const [,target,rhs]=assignment;
    if(!target.startsWith('EXPR')) {
      if(pending)fail('Uncaptured function block output');
      const coil=/^(NOT\s+)?(EXPR\d+|FALSE)$/.exec(rhs);
      if(!coil)fail('Coil assignment requires a generated expression');
      const expr=take(coil[2]);
      if(refs.size)fail('Unused or cross-network expressions cannot be converted losslessly');
      nets.push({id:`N${nets.length+1}`,label:'',expr,coil:{io:target.slice(0,2),idx:Number(target.slice(2)),neg:!!coil[1]}});
      continue;
    }
    if(assigned.has(target))fail('Expression temporaries must be assigned once');
    let node;
    if(pending) {
      if(rhs!==`FB${pending.inst}.Q`)fail('Capture the function block Q immediately after its call');
      node=pending;pending=null;
    } else if((m=/^(NOT\s+)?(IX|QX|MX)(\d+)$/.exec(rhs))) {
      node={t:'c',io:m[2],idx:Number(m[3]),neg:!!m[1],id:id()};
    } else if((m=/^IW(\d+)\s*(>=|<=|>|<|=)\s*(?:DINT#)?(-?\d+)$/.exec(rhs))) {
      node={t:'cmp',idx:Number(m[1]),op:({'>':'GT','<':'LT','>=':'GE','<=':'LE','=':'EQ'})[m[2]],k:Number(m[3]),id:id()};
    } else if(/^(EXPR\d+|FALSE)(\s+(AND|OR)\s+(EXPR\d+|FALSE))*$/.test(rhs)) {
      const parts=rhs.split(/\s+/),ops=parts.filter((_,i)=>i%2);
      if(new Set(ops).size>1)fail('Mixed operators need separate generated expressions');
      node={t:ops[0]==='OR'?'par':'series',kids:parts.filter((_,i)=>!(i%2)).map(take),id:id()};
    } else fail('Unsupported expression; keep the draft to avoid losing logic');
    refs.set(target,node);assigned.add(target);
  }
  if(pending||refs.size)fail('Unconsumed program expressions');
  const program={scan_ms,nets};
  const regenerated=exportStructuredText(program).source.toUpperCase();
  // Comparing lexical tokens rejects changed types/addresses, reordered evaluation,
  // missing declarations and latent semantic changes while accepting formatting.
  const tokens=text=>{
    const pattern=/T#\d+MS|DINT#-?\d+|[A-Z_][A-Z_0-9]*|\d+|:=|>=|<=|[%.,;:()#=<>-]/gy;
    const result=[];let pos=0;
    while(pos<text.length){if(/\s/.test(text[pos])){pos++;continue;}
      pattern.lastIndex=pos;const token=pattern.exec(text);
      if(!token)fail('Unsupported ST token');result.push(token[0]);pos=pattern.lastIndex;
    }return result.join(' ');
  };
  if(tokens(clean)!==tokens(regenerated))fail('Source changes declarations or evaluation order outside the reversible subset');
  return program;
}
