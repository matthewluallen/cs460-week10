/* Shared observed assembly state for Studio and future world renderers. No control writes. */
(function(root){
// K141 manufacturer reference, reviewed2026-10-03. This is not a detected revision.
const board={id:'m5stamplc',name:'TBD',reference:'https://docs.m5stack.com/en/core/StamPLC',ports:[
 {id:'display',label:'Screen',detail:'1.14-inch display. Supported firmware provides read-only pixel snapshots; a register value is not a screen pixel and a snapshot is not live video.',task:'Compare a controller reading with the physical display before asserting that both agree.'},
 {id:'inputs',label:'IN0–7',detail:'Eight isolated digital inputs mapped by this firmware to IX0–7. COM and field wiring are not discovered by reading the input bits.',task:'Trace one input through its register and authored logic. An electrical input level does not authenticate its source.'},
 {id:'outputs',label:'RLY0–3',detail:'Four relay outputs mapped to QX0–3. Register readback is not independent confirmation of contact position or connected load.',task:'Identify the safe state and independent feedback needed if a contact sticks.'},
 {id:'porta',label:'Grove A',detail:'External I²C route. The inventory can report direct devices and PaHub channels; it does not establish cable geometry.',task:'Check driver, address, channel and sample quality before binding a measurement. Shared connector shape does not guarantee electrical compatibility.'},
 {id:'portc',label:'Grove C',detail:'Firmware-selected Port C profile. A configured UART or HX711 profile does not establish the identity of attached hardware.',task:'Check the selected profile and received data before using this source in logic.'},
 {id:'usb',label:'USB',detail:'Console transport used by Studio. Connection alone does not identify Ethernet or field wiring.',task:'Compare the executing firmware and program identity with the intended project.'},
 {id:'expansion',label:'GPIO.EXT',detail:'Expansion interface. Modules appear only when reported by the controller inventory; unknown devices remain unknown.',task:'Trace an expansion route to its registers. Address responses are not authenticated module identities.'},
 {id:'rs485',label:'PWR-485',detail:'RS-485 A/B plus power. Manufacturer documentation states that power pins follow the main input supply.',task:'Check device voltage compatibility and protocol configuration. Do not infer authenticated commands from successful decoding.'},
 {id:'can',label:'PWR-CAN',detail:'CAN H/L plus power. Manufacturer documentation states that power pins follow the main input supply.',task:'Check termination, voltage compatibility and message origin separately.'},
 {id:'ethernet',label:'Ethernet expansion',detail:'Optional expansion; USB connectivity does not establish its presence, link state or network reachability.',task:'Check the installed hardware and observed network path before linking it to the simulated city.'}
]};
function snapshot({inventory,samples,elapsed=Infinity,inventoryElapsed=elapsed,connected=false,session='unknown',screenFrame=null,screenSession=null,screenElapsed=Infinity,bindings=null,bindingElapsed=Infinity}={}){
 const current=connected&&Number.isFinite(elapsed)&&elapsed>=0&&elapsed<=3000;
 const inventoryCurrent=connected&&Number.isFinite(inventoryElapsed)&&inventoryElapsed>=0&&inventoryElapsed<=3000;
 const inventoryAgeMs=inventoryCurrent&&Number.isInteger(inventory?.scan_age_ms)&&inventory.scan_age_ms>=0&&inventory.scan_age_ms<=4294967295?inventory.scan_age_ms+inventoryElapsed:null;
 const complete=inventory?.complete===true&&inventory.route_ok===true&&inventory.overflow===false&&inventory.timed_out===false;
 const readings=current&&inventoryCurrent&&complete&&samples?.generation===inventory?.generation?root.StudioSensors.readings(samples,elapsed):[];
 const hasScreen=connected&&screenSession===session&&Number.isFinite(screenElapsed)&&screenElapsed>=0&&root.StudioDeviceScreen?.isComplete(screenFrame);
 const screen=hasScreen?{state:'snapshot',source:'software_submitted_frame',ageMs:screenElapsed,
  ageBasis:'host elapsed since capture request; not synchronized controller time',
  manifest:{...screenFrame.manifest},rgba:new Uint8ClampedArray(screenFrame.rgba)}:
  {state:'unavailable',reason:!connected?'Disconnected':screenSession!==session?'No capture for this transport session':'No complete verified display capture'};
 const prefix='session:'+encodeURIComponent(String(session));
 const devices=(inventory?.entries||[]).map(e=>{
  const route=root.StudioSensors.route(e),reading=readings.find(r=>r.route===route&&r.driver===e.driver);
  return {id:prefix+':'+route,route,driver:e.driver,bus:e.bus,address:e.address,mux:e.mux,channel:e.channel,
   category:e.bus==='internal'&&e.driver==='stamplc-io-candidate'?'expansion':e.bus==='porta'?'grove':'internal',
   registerBindings:current&&inventoryCurrent&&complete?(root.StudioBindings?.deviceLinks(bindings,inventory,route,bindingElapsed)||[]):[],
   identification:e.state,connection:inventoryCurrent?'reported':'stale',values:reading?.values||[],sampleAgeMs:reading?.age??null,quality:reading?.quality||'unavailable'};
 });
 const ports=board.ports.filter(p=>p.id!=='display').map(p=>{
  const members=devices.filter(d=>p.id==='porta'?d.bus==='porta':p.id==='expansion'?d.category==='expansion':false);
  const observation=p.id==='usb'?(connected?'transport connected':'disconnected'):
   members.length?(!inventoryCurrent?'stale inventory routes':!complete?'incomplete inventory routes':'inventory routes reported'):'not observed';
  return {id:prefix+':port:'+p.id,portId:p.id,label:p.label,observation,deviceIds:members.map(d=>d.id),
   detail:p.detail,task:p.task,evidence:p.id==='usb'?'Studio transport state':'Controller inventory route; not cable continuity or authenticated identity'};
 });
 return {schema:'studio-device-assembly/v1',id:prefix,board:'m5stamplc',boardRevision:null,
  identityScope:'transport-session; not authenticated hardware identity',connection:connected?'USB connected':'disconnected',
  inventoryAgeMs,inventoryAgeBasis:'controller scan age plus host elapsed since inventory request; not synchronized wall time',
  inventoryState:!inventory?'not read':!inventoryCurrent?'stale':complete?'reported complete':'incomplete',
  screen,devices,ports};
}
function registerLink(state,id,iw,configSha256){
 const device=state?.devices.find(d=>d.id===id);
 return device?.registerBindings.find(b=>b.iw===iw&&b.configSha256===configSha256)||null;
}
root.StudioDeviceAssembly={snapshot,board,registerLink};
})(globalThis);
