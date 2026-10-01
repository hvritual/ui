// Private adapter for the pinned rendering runtime. Application code never sees
// these property IDs. Text catalog and glyph data are trusted build inputs.
const W=ui.__viewport.w,H=ui.__viewport.h;
if(W!==1024||(H!==600&&H!==800)||!ui.loadFontAtlas(__pak))throw Error('SCENE_BOOT');
let records=new Map(),textures=[],ready=true,locale=0,order='',inputGlyphs=null,inputTransfer=null,inputOffset=0;
function node(type,parent){const n=ui.createNode(type);ui.setProp(n,24,1);ui.insertBefore(parent,n,0);return n;}
function geom(n,x,y,w,h){ui.setProp(n,28,x);ui.setProp(n,25,y);ui.setProp(n,1,w);ui.setProp(n,2,h);}
function rgbaAlpha(c,a){return (((Math.floor((c>>>24)*a/256)&255)<<24)|(c&0xffffff))>>>0;}
function remove(r){for(const n of [r.label,r.image,r.fill,r.box])if(n){ui.removeChild(r.root,n);ui.destroyNode(n);}ui.removeChild(1,r.root);ui.destroyNode(r.root);}
function inputRef(ref){return ref>65535&&inputGlyphs&&inputGlyphs.has(ref-65536);}
function textFor(ref){if(inputRef(ref))return String.fromCharCode(ref-65536);const t=POCKET_TEXT_CATALOG[locale][ref];if(typeof t!=='string')throw Error('SCENE_TEXT_REF');return t;}
function inputFont(buffer){
 if(inputGlyphs||buffer.byteLength<16||buffer.byteLength>13*1024*1024)return -1;
 const d=new DataView(buffer),n=d.getUint16(6,true);
 if(d.getUint16(4,true)!==3||!n||n>17000||buffer.byteLength!==16+n*728||
    d.getUint8(8)!==24||d.getUint8(9)!==30||d.getUint8(10)!==24||d.getUint8(11)!==36||
    d.getUint8(12)!==1||d.getUint8(13)!==0||d.getUint8(14)!==1||d.getUint8(15)!==0)return -1;
 const codes=new Set();let prev=0;
 for(let i=0;i<n;i++){const at=16+8*i,c=d.getUint32(at,true);
  if(c<32||c>65535||(c>=0xd800&&c<=0xdfff)||c<=prev||d.getUint16(at+4,true)>=n||d.getUint8(at+6)!==24||d.getUint8(at+7)!==0)return -1;
  prev=c;codes.add(c);
 }
 if(!ui.loadFontAtlas(buffer))return -1;inputGlyphs=codes;return 1;
}
// Native validates the complete immutable atlas before sending bounded chunks.
// Do not enlarge the upstream 2 MiB resource-event limit.
function inputChunk(buffer){
 if(inputGlyphs||buffer.byteLength<16||buffer.byteLength>1048592)return -1;
 const d=new DataView(buffer),total=d.getUint32(4,true),offset=d.getUint32(8,true),length=d.getUint32(12,true);
 if(total<16||total>13*1024*1024||!length||length!==buffer.byteLength-16||offset>total||length>total-offset)return -1;
 if(offset===0){if(inputTransfer)return -1;inputTransfer=new Uint8Array(total);inputOffset=0;}
 if(!inputTransfer||inputTransfer.length!==total||offset!==inputOffset)return -1;
 inputTransfer.set(new Uint8Array(buffer,16),offset);inputOffset+=length;
 if(inputOffset!==total)return 1;
 const complete=inputTransfer.buffer;inputTransfer=null;inputOffset=0;return inputFont(complete);
}
function validate(s){
 if(!s||s.v!==1||s.w!==W||s.h!==H||!Number.isInteger(s.locale)||s.locale<0||s.locale>=6||
    !Number.isInteger(s.bg)||(s.ready!==0&&s.ready!==1)||!Array.isArray(s.nodes)||s.nodes.length>256)throw Error('SCENE_HEADER');
 const seen=new Set(),allowed=new Set([1,2,3,5,11,13,14,15,16,17,19,21]);
 for(const r of s.nodes){
  if(!Array.isArray(r)||r.length!==19||r.some(n=>!Number.isSafeInteger(n))||r[0]<1||seen.has(r[0])||!allowed.has(r[1]))throw Error('SCENE_RECORD');
  seen.add(r[0]);
  if(r.slice(2,10).some(n=>Math.abs(n)>65536)||r[4]<0||r[5]<0||r[8]<0||r[9]<0||r[12]<0||r[12]>2048||r[13]<0||r[13]>256||
     r[14]<0||r[14]>131071||r[15]<0||r[15]>8||r[10]<0||r[10]>0xffffffff||r[11]<0||r[11]>0xffffffff)throw Error('SCENE_RANGE');
  if(r[14]&&!inputRef(r[14])&&typeof POCKET_TEXT_CATALOG[s.locale][r[14]]!=='string')throw Error('SCENE_TEXT');
  if(r[15]&&textures[r[15]-1]===undefined)throw Error('SCENE_IMAGE');
  if(r[1]===11&&(r[17]>=r[18]||r[16]<r[17]||r[16]>r[18]))throw Error('SCENE_PROGRESS');
 }
}
function apply(s){
 locale=s.locale;
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
  const previous=r.previous;
  if(previous&&r.locale===locale&&v.every((value,i)=>previous[i]===value))continue;
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
   ui.setProp(r.label,97,inputRef(ref)?1:0);
   const text=textFor(ref);if(r.text!==text){if(r.text===undefined)ui.setText(r.label,text);else ui.replaceText(r.label,text);r.text=text;}}
  r.previous=v;r.locale=locale;
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
// Decode the private PSC1 numeric wire. Validate the complete packet before
// applying any mutation; malformed data cannot partially replace the scene.
function scenePacket(buffer){
 if(buffer.byteLength<32)throw Error('SCENE_TRUNCATED');
 const d=new DataView(buffer),count=d.getUint32(12,true);
 if(d.getUint32(0,true)!==0x31435350||d.getUint32(28,true)!==0||count>256||
    buffer.byteLength!==32+count*80)throw Error('SCENE_WIRE');
 const s={v:1,w:d.getUint32(4,true),h:d.getUint32(8,true),locale:d.getUint32(16,true),
          bg:d.getUint32(20,true),ready:d.getUint32(24,true),nodes:[]};
 for(let i=0;i<count;i++){
  const o=32+i*80,v=[d.getUint32(o,true)+4294967296*d.getUint32(o+4,true),d.getUint32(o+8,true)];
  for(let j=3;j<11;j++)v.push(d.getInt32(o+j*4,true));
  for(let j=11;j<17;j++)v.push(d.getUint32(o+j*4,true));
  for(let j=17;j<20;j++)v.push(d.getInt32(o+j*4,true));
  s.nodes.push(v);
 }
 validate(s);return s;
}
globalThis.onResourcePack=function(buffer){
 if(buffer===null)return ready?1:0;
 if(!(buffer instanceof ArrayBuffer))return -1;
 if(buffer.byteLength>=4&&new DataView(buffer).getUint32(0,true)===0x31464950)return inputChunk(buffer);
 if(buffer.byteLength>=4&&new DataView(buffer).getUint32(0,true)===0x31435350){
  let s;try{s=scenePacket(buffer);}catch(e){return -1;}
  return apply(s);
 }
 return images(buffer);
};
// Input belongs exclusively to Pocket Interaction Runtime. No second hit tester.
globalThis.frame=function(){};
