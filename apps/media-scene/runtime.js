// Trusted dynamic media component. External packets describe data, never code.
// Layout coordinates are within a protected 1024x416 design region.
function createMediaScene(ui, width, height) {
  const MAX_PIXELS = 1792 * 1024;
  const idPattern = /^[a-z][a-z0-9_-]{0,31}$/;
  let current = null, clock = null, paused = false, blocked = false;
  let stats = {applied:0, rejected:0, switches:0, uploads:0};
  const integer = (n, lo, hi) => Number.isSafeInteger(n) && n >= lo && n <= hi;
  const keys = (o, required) => o && !Array.isArray(o) &&
    Object.keys(o).sort().join(',') === required.split(',').sort().join(',');
  function reject() { throw new Error('scene contract'); }
  function decode(buffer) {
    if (!(buffer instanceof ArrayBuffer) || buffer.byteLength < 32 || buffer.byteLength > 2097152) reject();
    const v = new DataView(buffer), bytes = new Uint8Array(buffer);
    if (String.fromCharCode(...bytes.subarray(0,8)) !== 'PUISCNE1' || v.getUint32(8,true)!==1 || v.getUint32(24,true)!==0 || v.getUint32(28,true)!==0) reject();
    const n = v.getUint32(12,true), size = v.getUint32(16,true);
    if (!n || n > 32768 || size > MAX_PIXELS || n + size + 32 !== bytes.length) reject();
    // CRC is checked by the native store before dispatch. Local store is trusted.
    let text = ''; for (let i=32;i<32+n;i++) { if(bytes[i]>127)reject(); text+=String.fromCharCode(bytes[i]); }
    const m=JSON.parse(text);
    if(JSON.stringify(m)!==text)reject(); // Canonical installer JSON also rejects duplicate fields.
    if (!keys(m,'version,assets,elements') || typeof m.version!=='string' || !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,47}$/.test(m.version) ||
        !Array.isArray(m.assets) || m.assets.length<1 || m.assets.length>16 || !Array.isArray(m.elements) || m.elements.length>8) reject();
    const assets=new Map(), ids=new Set(); let offset=0;
    for (const a of m.assets) {
      if(!keys(a,'id,width,height,offset') || typeof a.id!=='string' || !idPattern.test(a.id) || assets.has(a.id) ||
         !integer(a.width,16,512) || !integer(a.height,16,512) || (a.width&(a.width-1)) || (a.height&(a.height-1))) reject();
      const length=a.width*a.height*4;
      if(a.offset!==offset || offset+length>size)reject();
      assets.set(a.id,a);offset+=length;
    }
    if(offset!==size)reject();
    for (const e of m.elements) {
      if(!keys(e,'id,kind,x,y,width,height,z,visible,fit,loop,items') || typeof e.id!=='string' || !idPattern.test(e.id) || ids.has(e.id) ||
         !['image','carousel'].includes(e.kind) || !integer(e.z,0,100) || typeof e.visible!=='boolean' || typeof e.loop!=='boolean' ||
         !integer(e.x,0,1023) || !integer(e.y,0,415) || !integer(e.width,1,1024-e.x) || !integer(e.height,1,416-e.y) ||
         !['contain','stretch'].includes(e.fit) || !Array.isArray(e.items) || e.items.length<1 || e.items.length>16 ||
         (e.kind==='image' && (e.items.length!==1 || e.loop))) reject();
      for(const item of e.items) if(!keys(item,'asset,hold_ms') || !assets.has(item.asset) || !integer(item.hold_ms,500,60000)) reject();
      ids.add(e.id);
    }
    return {m,assets,pixelOffset:32+n};
  }
  function position(n,x,y,w,h) {
    ui.setProp(n,24,1);ui.setProp(n,28,x);ui.setProp(n,25,y);ui.setProp(n,1,w);ui.setProp(n,2,h);
  }
  function release(s) {
    if(!s)return;
    for(const e of s.items)if(e.node){ui.removeChild(s.root,e.node);ui.destroyNode(e.node);}
    if(s.root){if(s.attached)ui.removeChild(1,s.root);ui.destroyNode(s.root);}
    for(const t of s.textures.values())ui.freeTexture(t);
  }
  function bind(s,e) {
    if(!e.node)return;
    const a=s.assets.get(e.spec.items[e.index].asset), r=e.spec;
    let x=r.x*width/1024,y=96+r.y*(height-184)/416,w=r.width*width/1024,h=r.height*(height-184)/416;
    if(r.fit==='contain'){const scale=Math.min(w/a.width,h/a.height),nw=a.width*scale,nh=a.height*scale;x+=(w-nw)/2;y+=(h-nh)/2;w=nw;h=nh;}
    // Root fills the full viewport; children stay inside the protected region.
    position(e.node,Math.round(x),Math.round(y),Math.max(1,Math.round(w)),Math.max(1,Math.round(h)));
    ui.setImage(e.node,s.textures.get(a.id));
  }
  function apply(buffer) {
    if(blocked)return 0;
    let next=null;
    try {
      const p=decode(buffer);
      next={version:p.m.version,assets:p.assets,textures:new Map(),items:[],root:0,attached:false};
      // Bounded eager upload is deliberate in this first scene protocol.
      // Whole-generation pixels <=1.75MiB, not an unbounded photo library.
      for(const a of p.m.assets){
        const raw=buffer.slice(p.pixelOffset+a.offset,p.pixelOffset+a.offset+a.width*a.height*4);
        const t=ui.uploadTexture(raw,a.width,a.height,3);if(t<0)reject();next.textures.set(a.id,t);
      }
      next.root=ui.createNode(0);if(next.root<=0)reject();position(next.root,0,0,width,height);
      const ordered=p.m.elements.map((spec,order)=>({spec,order})).sort((a,b)=>a.spec.z-b.spec.z||a.order-b.order);
      for(const {spec} of ordered){
        const e={spec,node:0,index:0,remaining:spec.items[0].hold_ms,ended:false};next.items.push(e);
        if(spec.visible){e.node=ui.createNode(2);if(e.node<=0)reject();ui.insertBefore(next.root,e.node,0);bind(next,e);}
      }
      // All allocations are complete before the old scene is detached.
      ui.insertBefore(1,next.root,0);next.attached=true;
    } catch (_) {release(next);stats.rejected++;return -1;}
    const old=current;current=next;release(old);
    stats.applied++;stats.uploads+=next.textures.size;return 1;
  }
  function advance(now, isBlocked=false) {
    if(!Number.isSafeInteger(now)||now<0||(clock!==null&&now<clock))return false;
    const elapsed=clock===null?0:now-clock;clock=now;blocked=isBlocked;
    if(!current || paused || blocked)return true;
    for(const e of current.items){
      if(!e.node || e.spec.kind!=='carousel' || e.ended)continue;
      const items=e.spec.items;let delta=elapsed;
      if(e.spec.loop)delta%=items.reduce((n,p)=>n+p.hold_ms,0);
      const original=e.index;
      // At most one playlist traversal after modular reduction; no catch-up spin.
      for(let budget=0;delta>=e.remaining && !e.ended && budget<=items.length;budget++){
        delta-=e.remaining;
        if(!e.spec.loop&&e.index===items.length-1){e.ended=true;e.remaining=0;break;}
        e.index=(e.index+1)%items.length;e.remaining=items[e.index].hold_ms;
      }
      if(!e.ended)e.remaining-=delta;
      if(e.index!==original){bind(current,e);stats.switches++;}
    }
    return true;
  }
  function step(direction) {
    if(!current || (direction!==1 && direction!==-1))return;
    for(const e of current.items)if(e.node&&e.spec.kind==='carousel'){
      const count=e.spec.items.length;
      const i=e.spec.loop?(e.index+direction+count)%count:Math.max(0,Math.min(count-1,e.index+direction));
      if(i!==e.index){e.index=i;bind(current,e);stats.switches++;}
      e.remaining=e.spec.items[i].hold_ms;e.ended=false;
    }
  }
  return {
    apply,advance,step,
    ready:()=>!blocked,
    pause:value=>{paused=!!value;},
    paused:()=>paused,
    version:()=>current?current.version:'empty',
    inspect:()=>({version:current?current.version:'empty',paused,clock,...stats,assets:current?current.assets.size:0,
      elements:current?current.items.length:0,visible:current?current.items.filter(e=>e.node).length:0,
      items:current?current.items.map(e=>({id:e.spec.id,index:e.index,remaining:e.remaining,ended:e.ended})):[]}),
    destroy:()=>{release(current);current=null;}
  };
}
