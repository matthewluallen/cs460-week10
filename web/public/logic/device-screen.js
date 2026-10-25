/* Software-submitted controller frames; CRC detects corruption, not authenticity. */
(function(root){
const completedFrames=new WeakSet();
function require(ok,message){if(!ok)throw Error(message);}
function crc32(bytes){let crc=0xffffffff;for(const byte of bytes){crc^=byte;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return ((crc^0xffffffff)>>>0).toString(16).padStart(8,'0');}
function ticket(value){return value?.screen==='reply'&&value.schema===1&&value.result==='ok'&&/^[0-9a-f]{32}$/.test(value.boot)&&/^[0-9a-f]{16}$/.test(value.frame)&&value.frame!=='0000000000000000';}
function collector(manifest){
 require(ticket(manifest),'Invalid screen identity');
 require(manifest.width===240&&manifest.height===135&&manifest.bytes===64800&&manifest.format==='rgb565le','Unsupported screen layout');
 require(Number.isInteger(manifest.rotation)&&manifest.rotation>=0&&manifest.rotation<=3,'Invalid screen rotation');
 require(manifest.source==='software_submitted_frame'&&manifest.clock==='controller_boot_us'&&/^[0-9]{1,20}$/.test(manifest.rendered_us),'Invalid screen provenance');
 require(/^[0-9a-f]{8}$/.test(manifest.crc32),'Invalid screen checksum');
 const info=Object.freeze({...manifest}),bytes=new Uint8Array(info.bytes);let offset=0,failed=false,finished=false;
 return {
  append(chunk){try{
   require(!failed&&!finished,'Screen transfer is closed');
   require(ticket(chunk)&&chunk.boot===info.boot&&chunk.frame===info.frame,'Screen session/frame changed');
   require(chunk.offset===offset&&Number.isInteger(chunk.length)&&chunk.length>0&&chunk.length<=256&&chunk.length<=bytes.length-offset,'Screen chunk is missing, repeated or out of range');
   require(typeof chunk.hex==='string'&&chunk.hex.length===chunk.length*2&&/^[0-9a-f]+$/.test(chunk.hex),'Invalid pixel encoding');
   for(let i=0;i<chunk.length;i++)bytes[offset+i]=parseInt(chunk.hex.slice(2*i,2*i+2),16);
   offset+=chunk.length;
  }catch(e){failed=true;throw e;}},
  finish(){try{
   require(!failed&&!finished&&offset===bytes.length,'Screen transfer is incomplete or closed');
   require(crc32(bytes)===info.crc32,'Screen checksum mismatch');
   const rgba=new Uint8ClampedArray(info.width*info.height*4);
   for(let i=0;i<bytes.length;i+=2){const v=bytes[i]|(bytes[i+1]<<8),p=i*2;rgba[p]=Math.floor(((v>>>11)&31)*255/31);rgba[p+1]=Math.floor(((v>>>5)&63)*255/63);rgba[p+2]=Math.floor((v&31)*255/31);rgba[p+3]=255;}
   finished=true;const frame={manifest:info,rgba};completedFrames.add(frame);return frame;
  }catch(e){failed=true;throw e;}}
 };
}
root.StudioDeviceScreen={collector,crc32,isComplete:frame=>completedFrames.has(frame)};
})(globalThis);
