// Coffee reference application. HostOps are the pinned PocketJS ABI, not DOM.
// COFFEE_LOCALES is injected by the trusted build; external bundles are data only.
const W=ui.__viewport.w,H=ui.__viewport.h;
if(W!==1024||(H!==600&&H!==800))throw new Error('profile');
if(!ui.loadFontAtlas(__pak))throw new Error('font atlas');
const locales=Object.keys(COFFEE_LOCALES), names=['espresso','americano','latte','cappuccino','flatwhite','mocha','tea','water'];
const color=(r,g,b)=>(0xff000000|(b<<16)|(g<<8)|r)>>>0;
const palette={bg:color(248,246,240),ink:color(48,55,52),muted:color(113,118,111),green:color(52,99,75),paper:color(255,255,255),soft:color(231,236,225)};
let locale=0,page='home',sheet=0,selected=0,progress=0,completed=0,mediaVersion='builtin',mediaCount=0;
let nodes=[],zones=[],imageBindings=[],textures=[],gesture=null,down=false,blockUntilUp=false,generation=0,lastIds={};
let progressBar=0,progressText=0,footer=0,mediaLabel=0;
const textValues=new Map();
function make(type,x,y,w,h){const n=ui.createNode(type);nodes.push(n);ui.setProp(n,24,1);ui.setProp(n,28,x);ui.setProp(n,25,y);ui.setProp(n,1,w);ui.setProp(n,2,h);ui.insertBefore(1,n,0);return n;}
function box(x,y,w,h,c,r=12){const n=make(0,x,y,w,h);ui.setProp(n,64,c);ui.setProp(n,68,r);return n;}
function setText(n,s){if(textValues.get(n)!==s){ui.replaceText(n,s);textValues.set(n,s);}}
function label(x,y,w,s,c=palette.ink){const n=make(1,x,y,w,36);ui.setProp(n,96,c);ui.setProp(n,97,0);ui.setProp(n,99,32);ui.setText(n,s);textValues.set(n,s);return n;}
function imageAt(index,x,y,w=256,h=144){const n=make(2,x,y,w,h);imageBindings.push({n,index});if(textures[index]!==undefined)ui.setImage(n,textures[index]);return n;}
function button(x,y,w,s,action,primary=false){box(x,y,w,48,primary?palette.green:palette.soft);label(x+16,y+8,w-24,s,primary?palette.paper:palette.ink);zones.push({x,y,w,h:48,action});}
function clear(){for(let i=nodes.length-1;i>=0;i--){ui.removeChild(1,nodes[i]);ui.destroyNode(nodes[i]);}nodes=[];zones=[];imageBindings=[];textValues.clear();generation++;gesture=null;}
function lang(){return COFFEE_LOCALES[locales[locale]];}
function rebuild(){
 clear();const l=lang();ui.setProp(1,64,palette.bg);
 label(32,20,620,l.title);label(32,55,640,l.subtitle,palette.muted);
 button(832,24,160,locales[locale],{kind:'locale'});
 const body=H-170,rowH=Math.floor((body-16)/2);
 if(page==='home'){
  for(let k=0;k<6;k++){const i=sheet*6+k;if(i>=names.length)continue;const x=32+(k%3)*328,y=105+Math.floor(k/3)*(rowH+16),h=rowH;
   box(x,y,304,h,palette.paper);imageAt(i,x+24,y+6,256,Math.min(144,h-54));
   label(x+20,y+h-43,270,l.names[i]);zones.push({x,y,w:304,h,action:{kind:'select',index:i}});
  }
  button(784,H-60,208,sheet?l.back:l.next,{kind:'sheet'});
 } else if(page==='confirm'){
  // Full-screen modal scope: no home targets are present underneath.
  box(216,110,592,H-190,palette.paper,18);label(256,132,512,l.confirm,palette.muted);
  imageAt(selected,384,180);label(272,Math.min(340,H-194),480,l.names[selected]);
  button(256,H-152,224,l.cancel,{kind:'cancel'});button(520,H-152,240,l.start,{kind:'start'},true);
 } else {
  label(64,130,890,page==='making'?l.making:l.done);
  imageAt(selected,384,188);label(384,346,470,l.names[selected]);
  box(256,404,512,14,palette.soft,7);progressBar=box(256,404,Math.max(1,512*progress/300),14,palette.green,7);
  progressText=label(454,430,190,Math.floor(progress/3)+'%');
  button(384,H-115,256,page==='making'?l.cancel:l.home,{kind:page==='making'?'cancel':'home'},page==='done');
 }
 footer=label(32,H-52,730,l.demo,palette.muted);
 // Small version marker uses only known ASCII glyphs.
 mediaLabel=label(680,62,312,l.media+': '+mediaVersion,palette.muted);
}
function action(a){if(!a)return;switch(a.kind){
 case 'locale':if(page==='making')return;locale=(locale+1)%locales.length;break;
 case 'sheet':if(page!=='home')return;sheet=1-sheet;break;
 case 'select':if(page!=='home')return;selected=a.index;page='confirm';ui.reportAction('coffee-select',selected);break;
 case 'start':if(page!=='confirm')return;page='making';progress=0;ui.reportAction('coffee-simulate',selected);break;
 case 'cancel':page='home';progress=0;break;
 case 'home':page='home';break;
 }rebuild();}
function hit(x,y){for(let i=zones.length-1;i>=0;i--){const z=zones[i];if(x>=z.x&&x<z.x+z.w&&y>=z.y&&y<z.y+z.h)return z;}return null;}
function decode(v){v>>>=0;if(v&0x40000000)return {cancel:true,id:(v>>>18)&255};const bits=(v&0x80000000)?10:9,mask=(1<<bits)-1;return {id:(v>>>(bits*2))&255,x:v&mask,y:(v>>>bits)&mask};}
// The package is an exact fixed-size data plane; no paths, JS, HTML or SVG.
globalThis.onResourcePack=function(buffer){
 const ready=page==='home'&&!down&&!blockUntilUp;
 if(buffer===null)return ready?1:0;
 if(!ready)return 0;
 if(!(buffer instanceof ArrayBuffer)||buffer.byteLength!==64+8*256*144*4)return -1;
 const v=new DataView(buffer),magic=new Uint8Array(buffer,0,8);
 if(String.fromCharCode(...magic)!=='PUIIMG1\x00'||v.getUint32(8,true)!==1||v.getUint32(12,true)!==8||v.getUint32(16,true)!==256||v.getUint32(20,true)!==144)return -1;
 const next=[];try{for(let i=0;i<8;i++){const bytes=buffer.slice(64+i*256*144*4,64+(i+1)*256*144*4);const t=ui.uploadTexture(bytes,256,144,3);if(t<0)throw new Error('texture budget');next.push(t);}}
 catch(e){for(const t of next)ui.freeTexture(t);return -1;}
 // UI rendering cannot interleave with this callback; swap the complete set.
 for(const b of imageBindings)ui.setImage(b.n,next[b.index]);
 const old=textures;textures=next;for(const t of old)ui.freeTexture(t);
 mediaVersion=Array.from(new Uint8Array(buffer,32,3)).map(n=>n.toString(16).padStart(2,'0')).join('');mediaCount++;
 setText(mediaLabel,lang().media+': '+mediaVersion);return 1;
};
globalThis.frame=function(buttons,analog,packed){
 const touches=[];let cancelled=false;
 for(const v of packed||[]){const p=decode(v);if(p.cancel)cancelled=true;else touches.push(p);}
 if(cancelled||touches.length>1){gesture=null;blockUntilUp=true;}
 down=touches.length>0;
 if(blockUntilUp){if(!down)blockUntilUp=false;}
 else if(touches.length===1){const p=touches[0];
  if(!lastIds[p.id])gesture={id:p.id,x:p.x,y:p.y,endX:p.x,endY:p.y,moved:false,generation,target:hit(p.x,p.y)};
  if(gesture&&gesture.id===p.id){gesture.endX=p.x;gesture.endY=p.y;if(Math.abs(p.x-gesture.x)>12||Math.abs(p.y-gesture.y)>12)gesture.moved=true;}
 } else if(gesture){const g=gesture;gesture=null;if(g.generation===generation){
  if(!g.moved&&g.target&&hit(g.endX,g.endY)===g.target)action(g.target.action);
  else if(page==='home'&&g.y>100&&Math.abs(g.endX-g.x)>100&&Math.abs(g.endY-g.y)<80)action({kind:'sheet'});
 }}
 lastIds={};for(const p of touches)lastIds[p.id]=true;
 if(page==='making'){
  progress=Math.min(300,progress+1);
  if(progress%6===0){ui.setProp(progressBar,1,512*progress/300);setText(progressText,Math.floor(progress/3)+'%');}
  if(progress===300){page='done';completed++;ui.reportAction('coffee-complete',selected);rebuild();}
 }
};
// Inspection is read-only. Tests still drive real contact words, not fake clicks.
globalThis.inspect=function(op){if(op===1)return ['home','confirm','making','done'].indexOf(page);if(op===2)return selected;if(op===3)return locale;if(op===4)return sheet;if(op===5)return completed;if(op===6)return mediaCount;if(op===7)return textures.length;if(op===8)return progress;return -1;};
rebuild();
