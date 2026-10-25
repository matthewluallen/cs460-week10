/* Controller-reported process bindings. Configuration identity is not authentication. */
(function(root){
const legacyMapSha='91d4006593918573b48ecfa0fb01cfd08da592110123a0ec656ec586a837a456';
const previousMapSha='183d68eba8d6436401da54a66d826b464f1e3b98fc16071c2ee90c53fbf529f9';
const mapSha='e3db6c9c4673c3bd2d7ff03f25b36734f650907fd6bdca727f711ed5ff4cc1b3';
const uint=v=>Number.isInteger(v)&&v>=0&&v<=4294967295;
function parse(text){
 if(typeof text!=='string'||text.length>16384)throw Error('Binding reply too large');
 const j=JSON.parse(text);
 if(!j||j.bindings!=='snapshot'||j.schema!==1||j.map_version!==3||![mapSha,previousMapSha,legacyMapSha].includes(j.map_sha256)||!/^([0-9a-f]{64})$/.test(j.config_sha256)||!uint(j.config_generation)||!uint(j.status)||!j.config||![1,2].includes(j.config.schema)||!Array.isArray(j.config.inputs)||j.config.inputs.length!==3||!Array.isArray(j.inputs)||j.inputs.length!==3)throw Error('Incomplete binding snapshot');
 if(j.boot_id!==undefined&&(typeof j.boot_id!=='string'||!/^[0-9a-f]{32}$/.test(j.boot_id)))throw Error('Invalid controller boot identity');
 for(let i=0;i<3;i++){
  const e=j.inputs[i],c=j.config.inputs[i];
  if(!e||e.iw!==i+1||typeof e.valid!=='boolean'||typeof e.has_sample!=='boolean'||typeof e.quality!=='string'||!uint(e.max_age_ms)||!(e.age_ms===null||uint(e.age_ms))||!(e.value===null||Number.isInteger(e.value)&&e.value>=-2147483648&&e.value<=2147483647))throw Error('Invalid binding value');
  if(c!==null&&(!c||typeof c.role!=='string'||c.role.length>64||typeof c.driver!=='string'||typeof c.field!=='string'||c.field!==e.field||c.role!==e.role||typeof e.unit!=='string'||e.unit.length>32||!Number.isFinite(e.scale)||e.scale<=0))throw Error('Invalid binding description');
  if(c&&['ambient_centi_c','object_centi_c'].includes(c.field)&&(![mapSha,previousMapSha].includes(j.map_sha256)||e.unit!=='C'||e.scale!==100))throw Error('Unsupported NCIR field semantics');
  if(c&&c.field==='distance_mm'&&(j.map_sha256!==mapSha||e.unit!=='mm'||e.scale!==1))throw Error('Unsupported ToF field semantics');
  if(e.valid&&(c===null||!e.has_sample||e.value===null||e.quality!=='valid'||e.age_ms===null||e.max_age_ms===0||j.status!==0))throw Error('Inconsistent binding quality');
 }
 return j;
}
function readings(j,elapsed){
 if(!j)return [];
 return j.inputs.map((e,i)=>{
  const c=j.config.inputs[i],age=e.age_ms===null?null:e.age_ms+elapsed;
  const fresh=Number.isFinite(elapsed)&&elapsed>=0&&elapsed<=3000&&age!==null&&age<=e.max_age_ms;
  const valid=c!==null&&j.status===0&&e.valid&&e.has_sample&&e.quality==='valid'&&fresh;
  return {iw:e.iw,label:c?c.role+' · '+c.field:'Not assigned',unit:c?e.unit:'',scale:c?e.scale:1,age,value:valid?e.value:null,quality:!c?'unbound':!fresh?'stale':j.status?'configuration error':e.quality};
 });
}
function source(j,idx){
 if(!j||idx<1||idx>3||!j.config.inputs[idx-1])return null;
 const c=j.config.inputs[idx-1],e=j.inputs[idx-1];
 const route=c.driver==='modbus-sensor-v1'?'Network endpoint '+c.host+':'+c.port+' / unit '+c.unit:(c.bus===0?'Grove A':'Internal')+(c.mux?' / hub '+c.mux+' channel '+c.channel:'')+' / address '+c.address;
 return {label:c.role+' · '+c.field,route,unit:e.unit,scale:e.scale,address:64+(idx-1)*16};
}
// Mirrors the admitted driver families in process_bindings_core.h; discovery is
// evidence of a route/family, not calibration or permission to control an output.
const driverFields={'vl53l1x-candidate':['distance_mm'],'mlx90614-candidate':['ambient_centi_c','object_centi_c'],bme680:['temperature_x10','humidity_x1000','pressure_pa','gas_ohm'],bme688:['temperature_x10','humidity_x1000','pressure_pa','gas_ohm'],sgp30:['eco2_estimate_ppm','tvoc_ppb'],max30100:['ir_raw','red_raw'],'stamplc-io-candidate':['ch1_mv','ch1_ua','ch2_mv','ch2_ua']};
function candidates(inventory){
 if(!inventory||inventory.devices!=='inventory'||inventory.schema!==1||!uint(inventory.generation)||inventory.complete!==true||inventory.route_ok!==true||inventory.overflow!==false||inventory.timed_out!==false||!Array.isArray(inventory.entries)||inventory.entries.length!==inventory.entry_count||inventory.entries.length>32)return [];
 const found=[],routes=new Set();
 for(const e of inventory.entries){
  if(!e||!['porta','internal'].includes(e.bus)||!uint(e.mux)||!Number.isInteger(e.channel)||!uint(e.address))return [];
  const route=[e.bus,e.mux,e.channel,e.address].join(':');if(routes.has(route))return [];routes.add(route);
  const fields=Object.hasOwn(driverFields,e.driver)?driverFields[e.driver]:null;
  if(!fields||e.state!=='identified'||!uint(e.part)||!uint(e.revision)||e.part>65535||e.revision>65535||typeof e.unique_serial!=='boolean')continue;
  if(e.address<8||e.address>119||(e.mux?(e.bus!=='porta'||e.mux<112||e.mux>119||e.channel<0||e.channel>5||e.address===e.mux):e.channel!==-1))continue;
  const env=e.driver==='bme680'||e.driver==='bme688',sgp=e.driver==='sgp30',heart=e.driver==='max30100',ncir=e.driver==='mlx90614-candidate',tof=e.driver==='vl53l1x-candidate';
  if(env&&(e.bus!=='porta'||e.address!==119||e.part!==97||e.revision!==(e.driver==='bme688'?1:0)))continue;
  if(sgp&&(e.bus!=='porta'||e.address!==88||(e.part>>12)!==0||(e.part&255)<32||e.revision!==0))continue;
  if(heart&&(e.bus!=='porta'||e.address!==87||e.part!==17||e.revision>255))continue;
  if(e.driver==='stamplc-io-candidate'&&(e.bus!=='internal'||e.mux||e.address<32||e.address>47||e.part!==e.address||e.revision>=255))continue;
  if(tof&&(e.bus!=='porta'||!e.mux||e.address!==41||e.part!==60108||e.revision!==0))continue;
  if(ncir&&(e.bus!=='porta'||e.address!==90||e.part!==0||e.revision!==0))continue;
  if(ncir&&(typeof e.serial!=='string'||!/^[0-9a-f]{16}$/.test(e.serial)))continue;
  if(e.unique_serial!==(sgp||ncir)||(sgp&&(typeof e.serial!=='string'||!/^[0-9a-f]{12}$/.test(e.serial))))continue;
  for(const field of fields)found.push({key:route+':'+e.driver+':'+field,driver:e.driver,field,route,config:{bus:e.bus==='porta'?0:1,mux:e.mux,channel:e.channel,address:e.address,driver:e.driver,part:e.part,revision:e.revision,serial:(sgp||ncir)?e.serial:null,field}});
 }
 return found;
}
// Read-only associations, requiring complete admitted inventory and exact identity.
function deviceLinks(snapshot,inventory,route,elapsed){
 if(!snapshot||!Number.isFinite(elapsed)||elapsed<0||elapsed>3000)return [];
 let j;try{j=parse(JSON.stringify(snapshot));}catch{return [];}
 if(j.status!==0)return [];
 const admitted=candidates(inventory).filter(c=>c.route===route),links=[];
 j.config.inputs.forEach((config,i)=>{
  if(!config||config.driver==='modbus-sensor-v1')return;
  if(!admitted.some(c=>['bus','mux','channel','address','driver','part','revision','serial','field'].every(k=>c.config[k]===config[k])))return;
  links.push({iw:i+1,address:64+i*16,field:config.field,role:config.role,configSha256:j.config_sha256,
   evidence:'controller-reported configuration; not proof of scan execution'});
 });return links;
}
function stageAssignment(snapshot,inventory,idx,key,role){
 if(!snapshot||!Number.isInteger(idx)||idx<1||idx>3||typeof role!=='string'||!/^([a-z0-9_-]{1,24})$/.test(role))throw Error('Choose an input and a role of 1–24 lowercase letters, digits, underscores or hyphens.');
 snapshot=parse(JSON.stringify(snapshot));if(snapshot.status!==0)throw Error('Controller binding configuration is not healthy.');
 const selected=candidates(inventory).find(c=>c.key===key);if(!selected)throw Error('Source is not in a complete supported inventory.');
 if(selected.driver==='mlx90614-candidate'&&![mapSha,previousMapSha].includes(snapshot.map_sha256))throw Error('Controller firmware does not support NCIR assignments.');
 if(selected.driver==='vl53l1x-candidate'&&snapshot.map_sha256!==mapSha)throw Error('Controller firmware does not support ToF assignments.');
 const config=JSON.parse(JSON.stringify(snapshot.config));
 for(let i=0;i<3;i++){
  const old=config.inputs[i];if(i===idx-1||!old)continue;
  if(old.role===role)throw Error('Role is already assigned.');
  const c=selected.config;
  if(old.driver!=='modbus-sensor-v1'&&['bus','mux','channel','address'].every(k=>old[k]===c[k])){
   if(['driver','part','revision','serial'].some(k=>old[k]!==c[k]))throw Error('Source identity conflicts with another assignment.');
   if(old.field===c.field)throw Error('This measurement is already assigned.');
  }
 }
 config.inputs[idx-1]={role,...selected.config};
 return {config,baseMapSha256:snapshot.map_sha256,baseBootId:snapshot.boot_id||null,baseConfigSha256:snapshot.config_sha256,baseGeneration:snapshot.config_generation,inventoryGeneration:inventory.generation};
}
// Preflight is advisory; firmware atomically checks this token when applying.
function assignmentCommand(staged,latest,inventory){
 latest=parse(JSON.stringify(latest));
 if(!staged||typeof staged.baseBootId!=='string'||!/^[0-9a-f]{32}$/.test(staged.baseBootId))throw Error('This controller needs firmware with protected assignment support.');
 if(latest.map_sha256!==staged.baseMapSha256||latest.status!==0||latest.boot_id!==staged.baseBootId||latest.config_generation!==staged.baseGeneration||latest.config_sha256!==staged.baseConfigSha256)throw Error('Controller changed. Read its signals again before applying.');
 if(!inventory||inventory.generation!==staged.inventoryGeneration||!candidates(inventory).length)throw Error('Device discovery changed. Read its signals again before applying.');
 const config=JSON.stringify(staged.config);
 if(!config||config.length>4096||/[\r\n]/.test(config))throw Error('Invalid assignment payload.');
 return 'bindings apply '+staged.baseBootId+' '+staged.baseGeneration+' '+staged.baseConfigSha256+' '+config;
}
function buildBindings(indices,board,j){
 const out={};
 for(const idx of new Set(indices)){
  if(idx===0){if(board!=='board-temperature-celsius-x10')throw Error('Choose the IW0 source before building.');out.IW0=board;continue;}
  if(!Number.isInteger(idx)||idx<1||idx>3||!j||j.status!==0||!j.config.inputs[idx-1])throw Error('Read and select the configured process source for IW'+idx+'.');
  out['IW'+idx]='process-binding-sha256:'+j.config_sha256;
 }
 if(!indices.includes(0)&&board)throw Error('This program does not use IW0; select Not bound.');
 return out;
}
root.StudioBindings={deviceLinks,parse,readings,buildBindings,source,candidates,stageAssignment,assignmentCommand};
})(globalThis);
