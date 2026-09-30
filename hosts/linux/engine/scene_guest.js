// Private adapter for the pinned rendering runtime. Application code never sees
// these property IDs. Text catalog and glyph data are trusted build inputs.
const W=ui.__viewport.w,H=ui.__viewport.h;
if(W!==1024||(H!==600&&H!==800)||!ui.loadFontAtlas(__pak))throw Error('SCENE_BOOT');
let records=new Map(),textures=[],ready=true,locale=0,order='';
function node(type,parent){const n=ui.createNode(type);ui.setProp(n,24,1);ui.insertBefore(parent,n,0);return n;}
function geom(n,x,y,w,h){ui.setProp(n,28,x);ui.setProp(n,25,y);ui.setProp(n,1,w);ui.setProp(n,2,h);}
function rgbaAlpha(c,a){return (((Math.floor((c>>>24)*a/256)&255)<<24)|(c&0xffffff))>>>0;}
function remove(r){for(const n of [r.label,r.image,r.fill,r.box])if(n){ui.removeChild(r.root,n);ui.destroyNode(n);}ui.removeChild(1,r.root);ui.destroyNode(r.root);}
function textFor(ref){const t=POCKET_TEXT_CATALOG[locale][ref];if(typeof t!=='string')throw Error('SCENE_TEXT_REF');return t;}
function validate(s){
 if(!s||s.v!==1||s.w!==W||s.h!==H||!Number.isInteger(s.locale)||s.locale<0||s.locale>=6||
    !Number.isInteger(s.bg)||(s.ready!==0&&s.ready!==1)||!Array.isArray(s.nodes)||s.nodes.length>256)throw Error('SCENE_HEADER');
 const seen=new Set(),allowed=new Set([1,2,3,5,11,13,14,15,16,17,19]);
 for(const r of s.nodes){
  if(!Array.isArray(r)||r.length!==19||r.some(n=>!Number.isSafeInteger(n))||r[0]<1||seen.has(r[0])||!allowed.has(r[1]))throw Error('SCENE_RECORD');
  seen.add(r[0]);
  if(r.slice(2,10).some(n=>Math.abs(n)>65536)||r[4]<0||r[5]<0||r[8]<0||r[9]<0||r[12]<0||r[12]>2048||r[13]<0||r[13]>256||
     r[14]<0||r[14]>65535||r[15]<0||r[15]>8||r[10]<0||r[10]>0xffffffff||r[11]<0||r[11]>0xffffffff)throw Error('SCENE_RANGE');
  if(r[14]&&typeof POCKET_TEXT_CATALOG[s.locale][r[14]]!=='string')throw Error('SCENE_TEXT');
  if(r[15]&&textures[r[15]-1]===undefined)throw Error('SCENE_IMAGE');
  if(r[1]===11&&(r[17]>=r[18]||r[16]<r[17]||r[16]>r[18]))throw Error('SCENE_PROGRESS');
 }
}
function apply(s){
 validate(s);locale=s.locale;
 const keep=new Set(s.nodes.map(r=>r[0]));
 for(const [id,r] of records)if(!keep.has(id)){remove(r);records.delete(id);}
 ui.setProp(1,64,s.bg);
 for(const v of s.nodes){
  const [id,kind,x,y,w,h,cx,cy,cw,ch,bg,fg,radius,alpha,ref,image,value,min,max]=v;
  let r=records.get(id);
  if(r&&(r.kind!==kind||!!r.label!==!!ref||!!r.image!==!!image)){remove(r);records.delete(id);r=null;}
  if(!r){
   r={root:node(0,1),kind};ui.setProp(r.root,30,1);
   r.box=node(0,r.root);
   if(kind===11)r.fill=node(0,r.root);
   if(image)r.image=node(2,r.root);
   if(ref){r.label=node(1,r.root);ui.setProp(r.label,97,0);ui.setProp(r.label,99,36);}
   records.set(id,r);
  }
  const encoded=JSON.stringify(v)+'/'+locale;
  if(r.previous===encoded)continue;
  // Reassert scissor when restyling a retained wrapper. Moving content must
  // never retain a previous un-clipped paint state outside the layout clip.
  ui.setProp(r.root,30,1);
  // Clip each projected object using the layout result, never by cropping text.
  const lx=Math.max(0,cx),ly=Math.max(0,cy),rx=Math.min(W,cx+cw),ry=Math.min(H,cy+ch);
  geom(r.root,lx,ly,Math.max(0,rx-lx),Math.max(0,ry-ly));
  const dx=x-lx,dy=y-ly;
  geom(r.box,dx,dy,w,h);ui.setProp(r.box,64,rgbaAlpha(bg,alpha));ui.setProp(r.box,68,radius);
  if(r.fill){geom(r.fill,dx,dy,w*(value-min)/(max-min),h);ui.setProp(r.fill,64,rgbaAlpha(fg,alpha));ui.setProp(r.fill,68,radius);}
  if(r.image){geom(r.image,dx,dy,w,h);if(r.imageRef!==image)ui.setImage(r.image,textures[image-1]);r.imageRef=image;}
  if(r.label){const tx=kind===5?16:0,ty=kind===5?Math.max(0,(h-36)/2):0;
   geom(r.label,dx+tx,dy+ty,Math.max(0,w-tx),h);ui.setProp(r.label,96,rgbaAlpha(fg,alpha));
   const text=textFor(ref);if(r.text!==text){if(r.text===undefined)ui.setText(r.label,text);else ui.replaceText(r.label,text);r.text=text;}}
  r.previous=encoded;
 }
 const nextOrder=s.nodes.map(r=>r[0]).join(',');
 if(order!==nextOrder){for(const v of s.nodes){const r=records.get(v[0]);ui.removeChild(1,r.root);ui.insertBefore(1,r.root,0);}order=nextOrder;}
 ready=!!s.ready;return 1;
}
function images(buffer){
 if(!ready)return 0;
 if(buffer.byteLength!==64+8*256*128*4)return -1;
 const v=new DataView(buffer),bytes=new Uint8Array(buffer,0,8);
 if(String.fromCharCode(...bytes)!=='PUIIMG1\x00'||v.getUint32(8,true)!==1||v.getUint32(12,true)!==8||v.getUint32(16,true)!==256||v.getUint32(20,true)!==128)return -1;
 const next=[];
 try {for(let i=0;i<8;i++){const t=ui.uploadTexture(buffer.slice(64+i*131072,64+(i+1)*131072),256,128,3);if(t<0)throw Error('IMAGE_BUDGET');next.push(t);}}
 catch(e){for(const t of next)ui.freeTexture(t);return -1;}
 for(const r of records.values())if(r.image)ui.setImage(r.image,next[r.imageRef-1]);
 const old=textures;textures=next;for(const t of old)ui.freeTexture(t);return 1;
}
globalThis.onResourcePack=function(buffer){
 if(buffer===null)return ready?1:0;
 if(!(buffer instanceof ArrayBuffer))return -1;
 const b=new Uint8Array(buffer);
 if(b[0]!==123)return images(buffer);
 if(b.length>65536)return -1;
 // Scene JSON carries numeric refs only; locale strings stay in the build catalog.
 let text='';for(let i=0;i<b.length;i++){if(b[i]>127)return -1;text+=String.fromCharCode(b[i]);}
 let s;try{s=JSON.parse(text);validate(s);}catch(e){return -1;}
 return apply(s);
};
// Input belongs exclusively to Pocket Interaction Runtime. No second hit tester.
globalThis.frame=function(){};
