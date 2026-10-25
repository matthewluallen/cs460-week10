import {exportStructuredText} from './structured-text.mjs';

// Graph of authored evaluation events, not observed controller/physical values.
export function programDependencies(program) {
  exportStructuredText(program,{scanMs:50}); // enforce the same bounded model
  const nodes=[],edges=[],lastWrite=new Map(),lastCall=new Map();
  const add=n=>{nodes.push(n);return n.id;};
  const edge=(from,to,kind)=>edges.push({from,to,kind});
  const incoming=(variable,io)=>{
    if(lastWrite.has(variable))return {from:lastWrite.get(variable),kind:'earlier-write-in-scan'};
    const id='image:'+variable;
    if(!nodes.some(n=>n.id===id))add({id,kind:'image',variable,label:variable,
      timing:io==='IX'||io==='IW'?'input-image-at-scan':'state-entering-scan'});
    return {from:id,kind:io==='IX'||io==='IW'?'input-image-at-scan':'state-entering-scan'};
  };
  function visit(n) {
    if(!n)return null;
    if(n.t==='c'||n.t==='cmp') {
      const io=n.t==='cmp'?'IW':n.io,variable=io+n.idx;
      add({id:n.id,kind:n.t,variable,label:variable,
        detail:n.t==='cmp'?`Compare ${variable} ${n.op} ${n.k} in its configured engineering scale.`:n.neg?'True when the sampled bit is false.':'True when the sampled bit is true.'});
      const source=incoming(variable,io);edge(source.from,n.id,source.kind);
    } else {
      const children=(n.kids||[n.in]).map(visit).filter(Boolean);
      const fb=n.t==='ton'||n.t==='rtrig';
      add({id:n.id,kind:n.t,label:fb?`FB${n.inst} ${n.t==='ton'?'TON':'R_TRIG'}`:n.t==='series'?'AND branch':'OR branch',
        detail:n.t==='ton'?`Output Q becomes true after IN stays true for ${n.preset} ms; false IN resets the non-retained on-delay.`:n.t==='rtrig'?'Output Q marks a false-to-true transition at this call; the instance remembers the preceding input.':n.t==='series'?'Every child condition must be true.':'At least one child condition must be true.'});
      for(const child of children)edge(child,n.id,'expression-input');
      if(fb){const instance='FB'+n.inst;const previous=lastCall.get(instance)||'state:'+instance;
        if(!lastCall.has(instance))add({id:previous,kind:'state',label:instance+' state entering scan'});
        edge(previous,n.id,lastCall.has(instance)?'earlier-call-state':'state-entering-scan');lastCall.set(instance,n.id);
      }
    }return n.id;
  }
  for(const net of program.nets){const value=visit(net.expr),id=net.id+'coil',variable=net.coil.io+net.coil.idx;
    add({id,kind:'assignment',variable,label:variable+' assignment',detail:net.coil.neg?'Assign the inverse of this rung result.':'Assign this rung result. This is a software command, not physical feedback.'});
    if(value)edge(value,id,'assignment');lastWrite.set(variable,id);
  }
  return {nodes,edges};
}

export function describeDependencySelection(graph,id) {
  const selected=graph.nodes.find(n=>n.id===id);if(!selected)return null;
  const closure=reverse=>{const result=new Set(),pending=[id];while(pending.length){const current=pending.pop();
    for(const e of graph.edges){const from=reverse?e.to:e.from,to=reverse?e.from:e.to;
      if(from===current&&!result.has(to)){result.add(to);pending.push(to);}}
  }return result;};
  const before=closure(true),after=closure(false);
  const upstream=graph.nodes.filter(n=>before.has(n.id)),downstream=graph.nodes.filter(n=>after.has(n.id));
  return {selected,upstream,downstream,
    inputs:[...new Set(upstream.filter(n=>n.variable).map(n=>n.variable))],
    uses:[...new Set(downstream.filter(n=>n.variable).map(n=>n.variable))],
    timing:[...new Set(graph.edges.filter(e=>e.to===id||before.has(e.to)).map(e=>e.kind).filter(k=>k!=='expression-input'&&k!=='assignment'))],
    relatedIds:[...new Set([...before,...after])].filter(x=>!x.includes(':'))};
}
