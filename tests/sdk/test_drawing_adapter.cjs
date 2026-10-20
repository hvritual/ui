"use strict";
// Private adapter contract tests only. Canonical ELF tests own actual raster evidence.
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm"),path=require("node:path");
const uploads=[],freed=[],bindings=[];let next=1;
const sandbox=vm.createContext({ui:{__viewport:{w:1024,h:600},loadFontAtlas:()=>true,
  createNode:()=>++next,setProp(){},insertBefore(){},removeChild(){},destroyNode(){},
  uploadTexture(buffer,w,h,format){
    assert.ok(w<=512&&h<=512&&(w&(w-1))===0&&(h&(h-1))===0);assert.equal(format,3);
    const id=uploads.length;uploads.push({w,h,data:Buffer.from(buffer)});return id;
  },freeTexture(id){freed.push(id);},setImage(node,id){bindings.push({node,id});}},
  POCKET_TEXT_CATALOG:Array.from({length:6},()=>({})),__pak:null});
vm.runInContext(fs.readFileSync(path.resolve(__dirname,"../../hosts/linux/engine/scene_guest.js"),"utf8"),sandbox);
vm.runInContext(`
function scene(w,h,count=1){
 const packet=new ArrayBuffer(32+count*80),d=new DataView(packet);
 const header=[0x31435350,1024,600,count,0,0,1,0];header.forEach((v,i)=>d.setUint32(i*4,v,true));
 if(count){const record=[42,0,1,64,160,w,h,64,160,w,h,0,0,0,256,0,0,0,0,100];
 record.forEach((v,i)=>d.setUint32(32+i*4,v,true));}return packet;
}
function drawing(w,h,revision){
 const packet=new ArrayBuffer(32+w*h*4),d=new DataView(packet);
 [0x31574450,1,42,0,w,h,revision,0].forEach((v,i)=>d.setUint32(i*4,v,true));
 const pixels=new Uint8Array(packet,32);
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){
   const at=(y*w+x)*4;pixels[at]=x&255;pixels[at+1]=y&255;pixels[at+2]=x>>8;pixels[at+3]=255;
 }return packet;
}
if(onResourcePack(scene(896,240))!==1)throw Error('scene');
if(onResourcePack(drawing(896,240,1))!==1)throw Error('drawing');
if(onResourcePack(scene(896,240))!==1)throw Error('bind');
`,sandbox,{timeout:2000});
assert.deepEqual(uploads.map(t=>[t.w,t.h]),[[512,256],[512,256]]);
assert.equal(uploads[1].data[2],2); // original x=512, not a scaled or repeated first tile
assert.equal(uploads[1].data[(383*4)+3],255);
assert.equal(uploads[1].data[(384*4)+3],0); // right-hand zero padding
assert.equal(uploads[0].data[(240*512*4)+3],0); // lower zero padding
assert.equal(bindings.length,2);assert.equal(freed.length,0);
vm.runInContext("onResourcePack(drawing(832,280,2));",sandbox,{timeout:2000});
assert.deepEqual(uploads.slice(2).map(t=>[t.w,t.h]),[[512,512],[512,512]]);
assert.equal(freed.length,0); // old textures survive until the new binding pass
vm.runInContext("onResourcePack(scene(832,280));",sandbox);
assert.equal(bindings.length,4);assert.deepEqual(freed,[0,1]);
vm.runInContext("onResourcePack(scene(832,280,0));",sandbox);
assert.deepEqual(freed,[0,1,2,3]);
console.log("DRAWING_ADAPTER_OK pow2-512-tiles exact-pixels rebind owner-cleanup");
