'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync('apps/media-scene/runtime.js','utf8');
function fixture(){
 let next=2,uploads=0,failAt=Infinity;const nodes=new Set(),textures=new Set();
 const ui={createNode(){nodes.add(next);return next++;},setProp(){},insertBefore(){},removeChild(){},destroyNode(n){nodes.delete(n);},setImage(){},uploadTexture(){if(++uploads===failAt)return -1;textures.add(next);return next++;},freeTexture(t){textures.delete(t);}};
 const ctx=vm.createContext({ArrayBuffer,Uint8Array,DataView,Map,Set,console});vm.runInContext(source,ctx);
 const scene=ctx.createMediaScene(ui,1024,600);
 const doc={version:'clock',assets:[{id:'a',width:16,height:16,offset:0},{id:'b',width:16,height:16,offset:1024},{id:'c',width:16,height:16,offset:2048}],elements:[{id:'hero',kind:'carousel',x:0,y:0,width:1024,height:416,z:0,visible:true,fit:'contain',loop:true,items:[{asset:'a',hold_ms:1000},{asset:'b',hold_ms:2000},{asset:'c',hold_ms:3000}]}]};
 function packet(d=doc){const text=JSON.stringify(d),b=new ArrayBuffer(32+text.length+3072),v=new DataView(b),a=new Uint8Array(b);a.set(Buffer.from('PUISCNE1'));v.setUint32(8,1,true);v.setUint32(12,text.length,true);v.setUint32(16,3072,true);a.set(Buffer.from(text),32);return b;}
 return {scene,doc,packet,nodes,textures,fail:()=>{failAt=uploads+2;}};
}
const f=fixture(),s=f.scene;
assert.equal(s.apply(f.packet()),1);assert.equal(s.inspect().elements,1);
assert(s.advance(0));assert(s.advance(999));assert.equal(s.inspect().items[0].index,0);
assert(s.advance(1000));assert.equal(s.inspect().items[0].index,1);
assert(s.advance(3000));assert.equal(s.inspect().items[0].index,2);
assert(s.advance(6000));assert.equal(s.inspect().items[0].index,0);
assert(s.advance(6000+6000*1000000+1000));assert.equal(s.inspect().items[0].index,1);
let now=s.inspect().clock,remain=s.inspect().items[0].remaining;
s.pause(true);assert(s.advance(now+500000));assert.equal(s.inspect().items[0].remaining,remain);
s.pause(false);now+=500000;assert(s.advance(now+remain));assert.equal(s.inspect().items[0].index,2);
s.step(-1);assert.equal(s.inspect().items[0].index,1);s.step(1);assert.equal(s.inspect().items[0].index,2);
assert.equal(s.advance(0),false);
console.log('PASS scene-clock-durations-wrap-large-jump-pause-manual-reversed');
now=s.inspect().clock;assert(s.advance(now+10,true));assert.equal(s.apply(f.packet()),0);
assert(s.advance(now+20,false));f.fail();assert.equal(s.apply(f.packet()),-1);assert.equal(s.inspect().elements,1);assert.equal(f.textures.size,3);
console.log('PASS scene-transaction-defer-allocation-failure-retains-old');
const bad=JSON.parse(JSON.stringify(f.doc));bad.elements[0].kind='video';assert.equal(s.apply(f.packet(bad)),-1);assert.equal(s.inspect().elements,1);
s.destroy();assert.equal(f.nodes.size,0);assert.equal(f.textures.size,0);
console.log('PASS scene-invalid-type-destroy-resources');
const g=fixture();g.doc.elements[0].loop=false;assert.equal(g.scene.apply(g.packet()),1);g.scene.advance(0);g.scene.advance(100000);assert.equal(g.scene.inspect().items[0].index,2);assert.equal(g.scene.inspect().items[0].ended,true);
g.scene.step(-1);assert.equal(g.scene.inspect().items[0].ended,false);g.scene.destroy();
console.log('PASS scene-once-holds-last-manual-rearms');
console.log('SCENE_CLOCK_OK cases=4 model_only=true');
