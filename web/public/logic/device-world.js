/* Shared-state device view. Envelope dimensions are referenced; layout is schematic. */
(function(root){
function inspect(state,id){
 if(!state)return null;
 if(id===state.id)return {title:'Controller',lines:[state.connection,state.identityScope,'Enclosure geometry is schematic. Selection does not send controller commands.']};
 if(id===state.id+':display')return {title:'Controller screen',lines:[state.screen.state==='snapshot'?'Software-submitted frame '+state.screen.manifest.frame:'No captured screen','Pixels do not independently verify the physical LCD.']};
 const port=state.ports?.find(p=>p.id===id);
 if(port)return {title:port.label,lines:[port.observation,port.detail,port.evidence,
  ...port.deviceIds.map(deviceId=>{const d=state.devices.find(d=>d.id===deviceId);return d?d.driver+' · '+d.route:'Route unavailable';}),port.task]};
 const d=state.devices.find(d=>d.id===id);if(!d)return null;
 return {title:d.driver,registers:d.registerBindings||[],lines:[d.route,d.category+' · '+d.connection+' · '+d.quality,
  ...(d.registerBindings?.length?d.registerBindings.map(b=>'%IW'+b.iw+' · '+b.field+' · '+b.evidence):['No current exact process-register association. Read process bindings to check.']),
  ...d.values.map(v=>v.label+': '+(v.value===null?'unavailable':v.value+' '+v.unit)),
  'A reported route does not authenticate a device. Confirm measurement quality and binding before using it in logic.']};
}
function mount(canvas,status,{selector=null,details=null,registerLinks=null,onRegister=()=>{}}={}){
 let disposed=false,current=null,selected=null,choiceKey='',linkKey='';
 function select(id){
  selected=id;const info=inspect(current,id);if(selector)selector.value=info?id:'';
  const nextLinks=JSON.stringify(info?.registers||[]);if(registerLinks&&nextLinks!==linkKey){linkKey=nextLinks;registerLinks.replaceChildren();for(const b of info?.registers||[]){const button=document.createElement('button');button.className='btn';button.textContent='Inspect %IW'+b.iw+' · holding '+b.address+'–'+(b.address+1);button.onclick=()=>onRegister(b.iw,id,b.configSha256);registerLinks.appendChild(button);}}
  if(details)details.textContent=info?info.title+'\n'+info.lines.join('\n'):'Selected device is no longer in this session.';
 }
 function updateInspector(state){if(disposed)return;current=state;
  const choices=[[state.id,'Controller'],[state.id+':display','Controller screen'],...(state.ports||[]).map(p=>[p.id,p.label+' · '+p.observation]),...state.devices.map(d=>[d.id,d.driver+' · '+d.route])];
  const nextChoices=JSON.stringify(choices);if(selector&&choiceKey!==nextChoices){choiceKey=nextChoices;selector.replaceChildren();for(const [id,label] of choices){const option=document.createElement('option');option.value=id;option.textContent=label;selector.appendChild(option);}}
  select(inspect(state,selected)?selected:state.id);
 }
 const choose=()=>select(selector.value);selector?.addEventListener('change',choose);
 const fallback=()=>({update(state){updateInspector(state);},front(){},angle(){},dispose(){disposed=true;selector?.removeEventListener('change',choose);}});
 const T=root.TBDThree;
 if(!T){status.textContent='3D library unavailable; the device list and screen remain available.';return fallback();}
 let renderer;try{renderer=new T.WebGLRenderer({canvas,antialias:true});}catch{status.textContent='3D unavailable; component inspection and register links remain available.';return fallback();}
 renderer.setPixelRatio(Math.min(root.devicePixelRatio||1,2));renderer.outputColorSpace=T.SRGBColorSpace;
 const scene=new T.Scene();scene.background=new T.Color('#17212e');
 const camera=new T.PerspectiveCamera(38,1,1,2000);camera.position.set(145,105,240);camera.lookAt(25,0,0);
 scene.add(new T.HemisphereLight(0xffffff,0x505060,3));const light=new T.DirectionalLight(0xffffff,3);light.position.set(60,110,150);scene.add(light);
 const assembly=new T.Group();scene.add(assembly);
 const material=new T.MeshStandardMaterial({color:0xe9e4cf,roughness:.75});
 const body=new T.Mesh(new T.BoxGeometry(72,80,33.4),material);assembly.add(body);
 const textureCanvas=document.createElement('canvas');textureCanvas.width=240;textureCanvas.height=135;
 const texture=new T.CanvasTexture(textureCanvas);texture.colorSpace=T.SRGBColorSpace;texture.magFilter=T.NearestFilter;
 const display=new T.Mesh(new T.PlaneGeometry(48,27),new T.MeshBasicMaterial({map:texture}));display.position.set(0,8,17);assembly.add(display);
 const modules=new T.Group();scene.add(modules);let routes='',lastFrame='';
 function disposeObject(group){group.traverse(o=>{o.geometry?.dispose();if(o.material)o.material.dispose();});group.clear();}
 function draw(){if(disposed)return;const width=Math.max(1,canvas.clientWidth),height=300;renderer.setSize(width,height,false);camera.aspect=width/height;camera.updateProjectionMatrix();renderer.render(scene,camera);}
 function pick(event){if(!current||disposed)return;const bounds=canvas.getBoundingClientRect();if(!bounds.width||!bounds.height)return;
  const ray=new T.Raycaster();ray.setFromCamera(new T.Vector2((event.clientX-bounds.left)/bounds.width*2-1,1-(event.clientY-bounds.top)/bounds.height*2),camera);
  const hit=ray.intersectObjects([body,display,...modules.children],false)[0];if(hit)select(hit.object.userData.instanceId);
 }
 canvas.addEventListener('click',pick);
 function update(state){
  if(disposed)return;updateInspector(state);assembly.userData.instanceId=state.id;body.userData.instanceId=state.id;display.userData.instanceId=state.id+':display';
  const key=JSON.stringify(state.devices.map(d=>[d.id,d.connection]));
  if(key!==routes){disposeObject(modules);routes=key;state.devices.forEach((d,i)=>{
   const box=new T.Mesh(new T.BoxGeometry(22,14,12),new T.MeshStandardMaterial({color:d.connection==='reported'?0x73b9ba:0x737985}));
   box.position.set(70+(Math.floor(i/5)*30),32-(i%5)*18,0);box.userData={instanceId:d.id,route:d.route,geometry:'generic module; not physical dimensions'};modules.add(box);
  });}
  const screen=state.screen;const frameKey=screen.state==='snapshot'?state.id+screen.manifest.boot+screen.manifest.frame:'unavailable';
  if(frameKey!==lastFrame){lastFrame=frameKey;const ctx=textureCanvas.getContext('2d');
   if(screen.state==='snapshot')ctx.putImageData(new ImageData(screen.rgba,240,135),0,0);
   else{ctx.fillStyle='#101b24';ctx.fillRect(0,0,240,135);ctx.fillStyle='#dce7ed';ctx.font='16px sans-serif';ctx.textAlign='center';ctx.fillText('No captured screen',120,73);}
   texture.needsUpdate=true;
  }
  status.textContent=state.connection+' · '+state.devices.length+' reported modules · '+(screen.state==='snapshot'?(state.evidenceMode==='recorded'?'recorded screen snapshot':'screen snapshot '+Math.floor(screen.ageMs/1000)+'s old'):'screen unavailable');draw();
 }
 const observer=new ResizeObserver(draw);observer.observe(canvas);
 return {update,front(){camera.position.set(25,0,290);camera.lookAt(25,0,0);draw();},angle(){camera.position.set(145,105,240);camera.lookAt(25,0,0);draw();},dispose(){if(disposed)return;disposed=true;canvas.removeEventListener('click',pick);selector?.removeEventListener('change',choose);observer.disconnect();disposeObject(assembly);disposeObject(modules);texture.dispose();renderer.dispose();}};
}
root.StudioDeviceWorld={mount,inspect};
})(globalThis);
