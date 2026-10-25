// M5StamPLC generated-runtime binding contract, separate from portable ST export.
import {exportStructuredText} from './structured-text.mjs';
export function targetProgram(project, {analogBindings={}}={}) {
  const result=exportStructuredText(project,{scanMs:20});
  if(result.scanMs!==20)throw Error('M5StamPLC generated runtime requires a 20 ms task');
  const analog=result.bindings.filter(v=>v.io==='IW');
  let processInputMask=0,bindingConfigSha256='';
  for(const v of analog) {
    const binding=analogBindings[v.name];
    if(v.idx===0 && binding==='board-temperature-celsius-x10')continue;
    const match=typeof binding==='string' && /^process-binding-sha256:([0-9a-f]{64})$/.exec(binding);
    if(v.idx<1 || v.idx>3 || !match)throw Error(`Explicit supported analog binding required: ${v.name}`);
    if(bindingConfigSha256 && bindingConfigSha256!==match[1])throw Error('Process bindings must share one configuration identity');
    bindingConfigSha256=match[1];processInputMask|=1<<(v.idx-1);
  }
  for(const name of Object.keys(analogBindings))if(!analog.some(v=>v.name===name))throw Error('Unused analog binding: '+name);
  let source=result.source,mappings=result.mappings;
  if(analog.length) {
    const position=source.indexOf('END_VAR\n');
    let declaration='',assignment='';
    for(const v of analog) {
      if(v.idx===0) {
        declaration+=' PHYSICAL_IW0 AT %IW0 : INT;\n';
        assignment+='IW0 := INT_TO_DINT(PHYSICAL_IW0);\n';
      } else {
        const high=2*v.idx-1;
        declaration+=` PROCESS_${v.idx}_HIGH AT %IW${high} : WORD;\n PROCESS_${v.idx}_LOW AT %IW${high+1} : WORD;\n`;
        assignment+=`${v.name} := DWORD_TO_DINT(SHL(WORD_TO_DWORD(PROCESS_${v.idx}_HIGH), 16) OR WORD_TO_DWORD(PROCESS_${v.idx}_LOW));\n`;
      }
    }
    source=source.slice(0,position)+declaration+'END_VAR\n'+assignment+source.slice(position+8);
    const offset=declaration.length+assignment.length;
    mappings=mappings.map(m=>({...m,start:m.start+offset,end:m.end+offset}));
  }
  return {...result,source,mappings,target:'m5stamplc-openplc-generated-v1',
    analogBindings,processInputMask,bindingConfigSha256,internalState:result.bindings.filter(v=>v.io==='MX').map(v=>v.name),
    bindingRequirement:'IX0..7 physical input image; QX0..3 base relays; MX program-local BOOL state; explicit IW0 signed board temperature in tenths Celsius; logical IW1..3 signed32 process bindings pinned to configuration hash, carried in private high/low words IW1..6. Build and device acceptance remain separate.'};
}
