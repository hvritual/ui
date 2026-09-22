// P3-02 physical touch diagnostic for the locked PocketJS HostOps.
// Uses the real multi-contact frame wire; no browser/DOM input path.
const W = ui.__viewport.w, H = ui.__viewport.h;
if (W !== 1024 || H !== 600) throw new Error('touch diagnostic profile');
if (!ui.loadFontAtlas(__pak)) throw new Error('font');

const rgba = (r,g,b) => (0xff000000 | (b<<16) | (g<<8) | r) >>> 0;
function node(type, parent, x, y, w, h) {
  const n = ui.createNode(type);
  ui.setProp(n,24,1); ui.setProp(n,28,x); ui.setProp(n,25,y);
  ui.setProp(n,1,w); ui.setProp(n,2,h); ui.insertBefore(parent,n,0);
  return n;
}
function box(x,y,w,h,r,g,b) {
  const n=node(0,1,x,y,w,h); ui.setProp(n,64,rgba(r,g,b)); ui.setProp(n,68,12); return n;
}
function label(x,y,w,value) {
  const n=node(1,1,x,y,w,28);
  ui.setProp(n,96,rgba(240,244,248)); ui.setProp(n,97,0); ui.setProp(n,99,28);
  ui.setText(n,value); return n;
}

ui.setProp(1,64,rgba(14,18,24));
label(32,24,900,'P3 TOUCH TEST - TAP TARGETS AND DRAG');
label(32,56,900,'marker must follow finger; no ghost click after release');

const a=box(40,100,240,150,50,80,120);
const b=box(392,225,240,150,50,120,80);
const c=box(744,400,240,150,120,80,50);
label(72,150,180,'TOP LEFT');
label(438,275,160,'CENTER');
label(780,450,170,'BOTTOM RIGHT');

const marker=box(-100,-100,30,30,255,255,255);
ui.setProp(marker,31,1000);
const status=label(32,565,950,'idle');

let lastHit=0, lastId=-1, taps=0;
function decode(value) {
  value >>>= 0;
  if (value & 0x40000000) return null;
  const wide=(value & 0x80000000)!==0;
  const bits=wide?10:9, mask=(1<<bits)-1;
  return {id:(value >>> (bits*2))&255, x:value&mask, y:(value>>>bits)&mask};
}

globalThis.frame=function(buttons,analog,contacts,hits) {
  let index=-1, touch=null;
  if (contacts) {
    for (let i=0;i<contacts.length;i++) {
      const decoded=decode(contacts[i]);
      if (decoded) { index=i; touch=decoded; break; }
    }
  }
  if (!touch) {
    if (lastHit) ui.setActive?.(lastHit,0);
    lastHit=0; lastId=-1;
    ui.setProp(marker,28,-100); ui.setProp(marker,25,-100);
    ui.replaceText(status,'released - marker hidden');
    return;
  }

  const hit=(hits && index>=0 ? hits[index] : 0) || 0;
  if (lastId<0) taps++;
  if (lastHit && lastHit!==hit) ui.setActive?.(lastHit,0);
  if (hit) ui.setActive?.(hit,1);
  lastHit=hit; lastId=touch.id;

  const mx=Math.max(0,Math.min(W-30,touch.x-15));
  const my=Math.max(0,Math.min(H-30,touch.y-15));
  ui.setProp(marker,28,mx); ui.setProp(marker,25,my);
  ui.replaceText(status,'touch id='+touch.id+' x='+touch.x+' y='+touch.y+' taps='+taps+' hit='+hit);
};
