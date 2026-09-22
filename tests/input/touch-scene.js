// Physical input diagnostic; authored ASCII atlas, no external font dependency.
// A/B/D/E are deliberately non-collinear: diagonal taps cannot prove axis order.
const W=ui.__viewport.w,H=ui.__viewport.h;
if(W!==1024||H!==600||!ui.loadFontAtlas(__pak))throw new Error('touch profile');
const rgb=(r,g,b)=>(0xff000000|(b<<16)|(g<<8)|r)>>>0;
function node(type,x,y,w,h){const n=ui.createNode(type);ui.setProp(n,24,1);ui.setProp(n,28,x);ui.setProp(n,25,y);ui.setProp(n,1,w);ui.setProp(n,2,h);ui.insertBefore(1,n,0);return n;}
function box(x,y,w,h,color){const n=node(0,x,y,w,h);ui.setProp(n,64,color);return n;}
function label(x,y,w,s){const n=node(1,x,y,w,24);ui.setProp(n,96,rgb(245,245,245));ui.setProp(n,97,0);ui.setText(n,s);return n;}
ui.setProp(1,64,rgb(16,20,28));
label(32,14,900,'P3 1024X600   A B C D E');
const coords=[[32,55],[792,55],[412,240],[32,395],[792,395]];
const targets=[],owners={},counts=[0,0,0,0,0],pressed={},last={};
const base=rgb(55,78,107),held=rgb(125,98,35),done=rgb(36,130,84);
for(let i=0;i<5;i++){
  const x=coords[i][0],y=coords[i][1];
  const b=box(x,y,200,115,base),t=label(x+84,y+44,60,String.fromCharCode(65+i));
  targets.push(b);owners[b]=i;owners[t]=i;
}
const marks=[];for(let i=0;i<8;i++)marks.push(box(-30,-30,14,14,rgb(250,245,130)));
const status=label(32,530,960,'ID:0 X:0 Y:0');
const summary=label(32,564,960,'A:0 B:0 C:0 D:0 E:0');
let clicks=0,drags=0,cancels=0,turns=0;
function point(word){const v=word>>>0,b=(v&0x80000000)?10:9,m=(1<<b)-1;return{id:(v>>>(b*2))&255,x:v&m,y:(v>>>b)&m};}
function inside(i,x,y){return x>=coords[i][0]&&x<coords[i][0]+200&&y>=coords[i][1]&&y<coords[i][1]+115;}
globalThis.frame=function(buttons,analog,words,hits){
  turns++;words=words||[];const seen={},cancelled={};let marker=0;
  for(let i=0;i<words.length;i++)if((words[i]>>>0)&0x40000000){const c=point(words[i]);cancelled[c.id]=true;if(pressed[c.id])cancels++;delete pressed[c.id];delete last[c.id];}
  for(let i=0;i<words.length;i++){
    if((words[i]>>>0)&0x40000000)continue;
    const c=point(words[i]);seen[c.id]=true;
    if(!pressed[c.id]){
      const owner=owners[(hits&&hits[i])||0];
      pressed[c.id]={target:owner===undefined?-1:owner,x:c.x,y:c.y,moved:false};
    }
    const p=pressed[c.id];if(Math.abs(c.x-p.x)+Math.abs(c.y-p.y)>24)p.moved=true;
    last[c.id]=c;
    if(marker<8){ui.setProp(marks[marker],28,Math.max(0,Math.min(W-14,c.x-7)));ui.setProp(marks[marker++],25,Math.max(0,Math.min(H-14,c.y-7)));}
    ui.replaceText(status,'ID:'+c.id+' X:'+c.x+' Y:'+c.y);
  }
  for(const id of Object.keys(pressed))if(!seen[id]){
    const p=pressed[id],c=last[id];
    if(!cancelled[id]&&p.moved)drags++;
    else if(!cancelled[id]&&p.target>=0&&c&&inside(p.target,c.x,c.y)){
      counts[p.target]++;clicks++;ui.__reportAppAction('touch-target',p.target);
    }
    delete pressed[id];delete last[id];
  }
  for(;marker<8;marker++){ui.setProp(marks[marker],28,-30);ui.setProp(marks[marker],25,-30);}
  for(let i=0;i<5;i++)ui.setProp(targets[i],64,Object.values(pressed).some(p=>p.target===i)?held:(counts[i]?done:base));
  ui.replaceText(summary,counts.map((n,i)=>String.fromCharCode(65+i)+':'+n).join(' '));
};
// Harness-only access is bound by test binaries, not exported by the device CLI.
globalThis.inspect=function(op){if(op===1)return clicks;if(op===2)return drags;if(op===3)return cancels;if(op===4)return Object.keys(pressed).length;if(op>=10&&op<15)return targets[op-10];return turns;};
