// Standalone reference application; it does not replace Coffee business UI.
const W=ui.__viewport.w,H=ui.__viewport.h;
if(W!==1024||(H!==600&&H!==800)||!ui.loadFontAtlas(__pak))throw new Error('media profile/font');
const media=createMediaScene(ui,W,H), controls=[];
const rgba=(r,g,b)=>(0xff000000|(b<<16)|(g<<8)|r)>>>0;
function node(type,x,y,w,h){const n=ui.createNode(type);ui.setProp(n,24,1);ui.setProp(n,28,x);ui.setProp(n,25,y);ui.setProp(n,1,w);ui.setProp(n,2,h);ui.insertBefore(1,n,0);return n;}
function text(x,y,w,s){const n=node(1,x,y,w,36);ui.setProp(n,96,rgba(48,55,52));ui.setProp(n,97,0);ui.setProp(n,99,32);ui.setText(n,s);return n;}
function button(x,w,s,action){const n=node(0,x,H-68,w,48);ui.setProp(n,64,rgba(215,229,218));ui.setProp(n,68,10);text(x+16,H-61,w-24,s);controls.push({x,y:H-68,w,h:48,action});}
ui.setProp(1,64,rgba(248,246,240));text(32,20,600,'Dynamic images / Carousel');
const status=text(32,56,960,'Waiting for signed scene');let previousStatus='Waiting for signed scene';
button(32,200,'Pause / Resume','pause');button(252,160,'Previous','previous');button(432,160,'Next','next');
text(620,H-61,390,'DATA ONLY - no video');
let now=0,held=false,blockedUntilUp=false,gesture=null,lastSwitches=0;
function stateLabel(){const s=media.inspect();return s.version+' | elements '+s.elements+' | '+(s.paused?'PAUSED':'PLAYING');}
function updateLabel(){const value=stateLabel();if(value!==previousStatus){ui.replaceText(status,value);previousStatus=value;}}
function hit(x,y){return controls.find(c=>x>=c.x&&x<c.x+c.w&&y>=c.y&&y<c.y+c.h)||null;}
function decode(v){v>>>=0;if(v&0x40000000)return {cancel:true};const b=(v&0x80000000)?10:9,m=(1<<b)-1;return {id:(v>>>(b*2))&255,x:v&m,y:(v>>>b)&m};}
globalThis.onResourcePack=function(buffer){
 if(buffer===null)return !held&&!blockedUntilUp?1:0;
 if(buffer instanceof ArrayBuffer && buffer.byteLength===16){
  const v=new DataView(buffer),b=new Uint8Array(buffer);
  if(String.fromCharCode(...b.subarray(0,8))!=='PUITICK1')return -1;
  const value=v.getUint32(8,true)+v.getUint32(12,true)*4294967296;
  if(!Number.isSafeInteger(value)||value<now)return -1;now=value;return 1;
 }
 if(held||blockedUntilUp)return 0;
 const result=media.apply(buffer);updateLabel();return result;
};
globalThis.frame=function(buttons,analog,packed){
 const touches=[];let cancelled=false;
 for(const v of packed||[]){const p=decode(v);if(p.cancel)cancelled=true;else touches.push(p);}
 if(cancelled||touches.length>1){gesture=null;blockedUntilUp=true;}
 const wasHeld=held;held=touches.length>0;
 if(blockedUntilUp){if(!held)blockedUntilUp=false;}
 else if(touches.length===1){const p=touches[0];
  if(!wasHeld)gesture={id:p.id,target:hit(p.x,p.y),x:p.x,y:p.y,moved:false};
  if(gesture&&(gesture.id!==p.id||Math.abs(p.x-gesture.x)>12||Math.abs(p.y-gesture.y)>12))gesture.moved=true;
 }else if(gesture){const g=gesture;gesture=null;if(!g.moved&&g.target){
  if(g.target.action==='pause')media.pause(!media.paused());else media.step(g.target.action==='next'?1:-1);
 }}
 if(!media.advance(now,held||blockedUntilUp))throw new Error('media clock reversed');
 const switches=media.inspect().switches;if(switches!==lastSwitches){ui.__reportAppAction('carousel-switch',switches);lastSwitches=switches;}
 updateLabel();
};
globalThis.inspect=function(op){const s=media.inspect();if(op===1)return s.elements;if(op===2)return s.visible;if(op===3)return s.assets;if(op===4)return s.applied;if(op===5)return s.switches;if(op===6)return s.paused?1:0;if(op===7)return s.items.length?s.items[0].index:-1;if(op===8)return s.rejected;return -1;};
