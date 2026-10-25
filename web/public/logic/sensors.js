/* Read-only cached Grove telemetry. No discovery, binding or output commands. */
(function(root){
const fields={distance_mm:["Distance","mm"],ambient_centi_c:["Sensor temperature","°C",100],object_centi_c:["Object temperature","°C",100],temperature_c:['Temperature','°C'],humidity_pct:['Humidity','%'],pressure_pa:['Pressure','Pa'],gas_ohm:['Gas resistance','Ω'],eco2_estimate_ppm:['Estimated CO₂','ppm'],tvoc_ppb:['TVOC','ppb'],ir_raw:['Infrared raw','counts'],red_raw:['Red raw','counts'],ch1_mv:['Channel 1 voltage','mV'],ch1_ua:['Channel 1 current','µA'],ch2_mv:['Channel 2 voltage','mV'],ch2_ua:['Channel 2 current','µA']};
function parse(text,kind){
 if(typeof text!=='string'||text.length>65536)throw Error('Sensor reply too large');
 const j=JSON.parse(text);
 if(!j||j.devices!==kind||j.schema!==1||!Array.isArray(j.entries)||j.entries.length>32||j.entry_count!==j.entries.length||!Number.isInteger(j.generation)||j.generation<0)throw Error('Incomplete sensor reply');
 const seen=new Set();
 for(const e of j.entries){
  if(!e||!['porta','internal'].includes(e.bus)||!Number.isInteger(e.mux)||e.mux<0||e.mux>127||!Number.isInteger(e.channel)||e.channel< -1||e.channel>7||!Number.isInteger(e.address)||e.address<0||e.address>127||typeof e.driver!=='string'||e.driver.length>64)throw Error('Invalid sensor route');
  const key=route(e);if(seen.has(key))throw Error('Duplicate sensor route');seen.add(key);
  if(kind==='samples'&&(!Number.isInteger(e.max_age_ms)||e.max_age_ms<=0||e.max_age_ms>60000||!(e.age_ms===null||Number.isInteger(e.age_ms)&&e.age_ms>=0&&e.age_ms<=4294967295)))throw Error('Invalid sensor age');
 }
 return j;
}
function route(e){return `${e.bus}:${e.mux}:${e.channel}:${e.address}`;}
function readings(j,elapsed){
 if(!j||!Number.isFinite(elapsed)||elapsed<0)return [];
 return j.entries.map(e=>{
  const age=e.age_ms===null?null:e.age_ms+elapsed;
  let quality=j.enabled!==true?'paused':j.route_ok!==true?'route unavailable':j.overflow!==false?'overflow':e.quality;
  if(quality==='valid'&&(age===null||age>e.max_age_ms||elapsed>3000))quality='stale';
  const valid=quality==='valid'&&e.valid===true&&e.has_sample===true;
  const values=Object.entries(fields).filter(([k])=>Object.hasOwn(e.values||{},k)).map(([k,[label,unit,scale=1]])=>{
   const permitted=valid&&(k!=='distance_mm'||(e.values.range_status===9&&Number.isInteger(e.values[k])&&e.values[k]>=0&&e.values[k]<=65535))&&(k!=='gas_ohm'||e.values.gas_valid===true)&&(!k.startsWith('ch1_')||e.values.ch1_valid===true)&&(!k.startsWith('ch2_')||e.values.ch2_valid===true);
   return {label,unit,value:permitted&&Number.isFinite(e.values[k])?e.values[k]/scale:null};
  });
  return {route:route(e),driver:e.driver,quality:valid?'valid':quality==='valid'?'unavailable':quality,age,values};
 });
}
root.StudioSensors={parse,readings,route};
})(globalThis);
