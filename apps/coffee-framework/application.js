/* Coffee reference application. Only experimental Pocket application operations
 * are used; no renderer handles, native plugins or machine commands. */
(function () {
  'use strict';
  const call = __pocketCall;
  const VIEW=1, TEXT=2, IMAGE=3, BUTTON=5, PROGRESS=11, SCROLL=13, MODAL=16;
  const HOME=1, DETAIL=2, MAKING=3, SUCCESS=4;
  const SELECT=1, NEXT=2, BACK=3, START=4, CANCEL=5, CONFIRM=6, THEME=7, LOCALE=8, EDIT=9;
  const PAGE=1, LABEL=2, MUTED=3, CARD=4, KEY=5, PRIMARY=6, SHADE=7, BAR=8, CLEAR=9;
  const PAGE_WIDTH=984, PAGE_ITEMS=6, PAGE_POOL=12, SNAP_MS=180, SCROLL_TOKEN=1;
  const trunc = Math.trunc;
  const clamp = (n, lo, hi) => Math.max(lo, Math.min(n, hi));
  const rgba = (r,g,b) => (0xff000000 | (b<<16) | (g<<8) | r) >>> 0;
  class Pager {
    constructor(count) {
      this.pages=Math.max(1,Math.floor((count+5)/6)); this.settled=0;
      this.position=this.downPosition=this.downX=0;
      this.active=this.dragging=this.settling=this.blockTap=0;
      this.pointer=this.from=this.to=this.since=this.lastMove=this.lastX=this.velocity=0;
    }
    limit() { return (this.pages-1)*PAGE_WIDTH; }
    tick(ms) {
      if (!this.settling || ms<this.since) return;
      const elapsed=ms-this.since;
      if (elapsed>=SNAP_MS) { this.position=this.to; this.settling=0; this.settled=Math.floor(this.to/PAGE_WIDTH); return; }
      const left=SNAP_MS-elapsed;
      this.position=this.to+trunc((this.from-this.to)*left*left*left/(SNAP_MS*SNAP_MS*SNAP_MS));
    }
    press(id,x,ms) {
      if (this.active) return;
      this.tick(ms); this.blockTap=this.settling; this.settling=0; this.active=1; this.dragging=0;
      this.pointer=id; this.downX=this.lastX=x; this.downPosition=this.position;
      this.lastMove=ms; this.velocity=0;
    }
    move(id,x,ms) {
      if (!this.active || this.pointer!==id) return;
      if (x!==this.lastX && ms>this.lastMove) {
        this.velocity=clamp(trunc((x-this.lastX)*1000/Math.min(ms-this.lastMove,10000)),-5000,5000);
        this.lastX=x; this.lastMove=ms;
      }
      this.dragging=this.blockTap=1;
      let pos=this.downPosition+this.downX-x, maximum=this.limit();
      if (pos<0) pos=pos < -240 ? -80 : trunc(pos/3);
      else if (pos>maximum) pos=maximum+(pos-maximum>240?80:trunc((pos-maximum)/3));
      this.position=pos;
    }
    release(id,x,ms) {
      if (!this.active || this.pointer!==id) return;
      if (!this.dragging && !this.blockTap) { this.active=0; return; }
      if (this.dragging) this.move(id,x,ms);
      const pos=clamp(this.position,0,this.limit()), displacement=this.downX-x;
      let target=Math.floor((pos+PAGE_WIDTH/2)/PAGE_WIDTH);
      const recent=ms>=this.lastMove && ms-this.lastMove<=100;
      const origin=clamp(trunc((this.downPosition+PAGE_WIDTH/2)/PAGE_WIDTH),0,this.pages-1);
      if (displacement>=PAGE_WIDTH/4 || (recent && this.velocity<=-600 && displacement>=48)) target=origin+1;
      else if (displacement<=-PAGE_WIDTH/4 || (recent && this.velocity>=600 && displacement<=-48)) target=origin-1;
      this.from=this.position; this.to=clamp(target,0,this.pages-1)*PAGE_WIDTH; this.since=ms;
      this.settling=+(this.from!==this.to); this.active=this.dragging=0;
      if (!this.settling) this.settled=Math.floor(this.to/PAGE_WIDTH);
    }
    cancel() { this.position=this.settled*PAGE_WIDTH; this.active=this.dragging=this.settling=0; this.blockTap=1; }
    jump(page) { this.settled=Math.min(page,this.pages-1); this.position=this.settled*PAGE_WIDTH; this.active=this.dragging=this.settling=this.blockTap=0; }
  }
  let a;
  function make(kind,parent,x,y,w,h,style,text=0,image=0) {
    return call('component.create',kind,parent,x,y,w,h,style,text,image);
  }
  function layout(c,x,y,w,h) { call('component.layout',c,x,y,w,h); }
  function bind(c,action,index) { call('event.bind',c,action*4096+index,1); }
  function button(parent,x,y,w,text,action,index=0,primary=false) {
    const c=make(BUTTON,parent,x,y,w,48,primary?PRIMARY:KEY,text);
    bind(c,action,index); return c;
  }
  function themes() {
    for (let i=0;i<2;i++) {
      call('theme.define',i+1,2,22,
        1,i?rgba(23,30,29):rgba(248,246,240),
        2,i?rgba(235,242,236):rgba(48,55,52),
        3,i?rgba(38,49,45):rgba(255,255,255),
        4,i?rgba(60,80,67):rgba(231,236,225),
        5,rgba(52,99,75),6,rgba(255,255,255),7,0x90000000,8,0,
        9,i?rgba(181,196,185):rgba(113,118,111));
    }
    for (const r of [[PAGE,1,2,0],[LABEL,8,2,0],[MUTED,8,9,0],[CARD,3,2,12],
      [KEY,4,2,10],[PRIMARY,5,6,10],[SHADE,7,2,0],[BAR,4,5,7],[CLEAR,8,2,0]]) call('style.rule',...r);
    call('theme.select',1);
  }
  function page(heading) {
    const c=make(VIEW,0,0,0,1024,a.height,PAGE);
    make(TEXT,c,32,20,620,36,LABEL,heading);
    make(TEXT,c,32,a.height-52,560,36,MUTED,3);
    return c;
  }
  function card(index,owner) {
    const pos=index%PAGE_ITEMS, rh=trunc((a.height-186)/2);
    layout(owner,Math.floor(index/6)*PAGE_WIDTH-a.pager.position+(pos%3)*328,Math.floor(pos/3)*(rh+16),304,rh);
    call('component.style',owner,CARD);
    let item=a.cards.get(owner);
    if (!item) {
      if (a.cards.size>=PAGE_POOL) throw Error('APP_POOL_LIMIT');
      item={owner,image:make(IMAGE,owner,24,8,256,128,CLEAR,0,index%8+1),
        text:make(TEXT,owner,20,rh-42,270,36,LABEL,100+index%8),index};
      a.cards.set(owner,item);
    }
    item.index=index;
    call('component.image',item.image,index%8+1);
    call('component.text',item.text,100+index%8);
    bind(owner,SELECT,index);
  }
  function viewport() {
    const pos=a.pager.position, last=Math.floor((a.items-1)/PAGE_ITEMS);
    let p=pos>0?Math.floor(pos/PAGE_WIDTH):0;
    if (p>=last) p=last?last-1:0;
    const first=p*PAGE_ITEMS;
    if (first!==a.materialized) {
      const rows=call('list.window',a.list,first,Math.min(a.items-first,PAGE_POOL));
      for (const [i,c] of rows) card(i,c);
      a.materialized=first;
    }
    const rh=trunc((a.height-186)/2);
    for (const c of a.cards.values()) {
      const cell=c.index%PAGE_ITEMS;
      layout(c.owner,Math.floor(c.index/PAGE_ITEMS)*PAGE_WIDTH-pos+(cell%3)*328,Math.floor(cell/3)*(rh+16),304,rh);
    }
    a.viewportDirty=0;
  }
  function window(first) { call('input.cancel'); a.pager.jump(Math.floor(first/PAGE_ITEMS)); a.first=first; viewport(); }
  function dismiss() { if (a.modal) { call('overlay.dismiss',1); a.modal=0; } }
  function modal() {
    if (a.modal) throw Error('APP_MODAL_EXISTS');
    a.modal=make(MODAL,0,0,0,1024,a.height,SHADE);
    const panel=make(VIEW,a.modal,240,160,544,260,CARD);
    make(TEXT,panel,32,24,480,36,LABEL,8);
    make(TEXT,panel,32,80,480,36,LABEL,100+a.selected%8);
    button(panel,32,180,220,6,CANCEL);
    button(panel,280,180,232,7,START,0,true);
    call('overlay.present',1,DETAIL,a.modal,3);
  }
  function detail() {
    const c=page(8);
    make(IMAGE,c,384,156,256,128,CLEAR,0,a.selected%8+1);
    make(TEXT,c,384,318,512,36,LABEL,100+a.selected%8);
    button(c,256,a.height-136,224,5,BACK);
    button(c,520,a.height-136,240,7,CONFIRM,0,true);
    a.page=DETAIL; call('navigation.push',DETAIL,c);
  }
  function making() {
    dismiss();
    const c=page(9);
    make(IMAGE,c,384,156,256,128,CLEAR,0,a.selected%8+1);
    make(TEXT,c,384,318,512,36,LABEL,100+a.selected%8);
    const progress=make(PROGRESS,c,256,384,512,16,BAR);
    call('signal.bind',a.signal,progress,6);
    button(c,384,a.height-115,256,6,CANCEL);
    a.progress=0; call('signal.set',a.signal,0);
    a.started=a.now; a.page=MAKING; call('navigation.replace',MAKING,c);
  }
  function success() {
    const c=page(10);
    make(IMAGE,c,384,156,256,128,CLEAR,0,a.selected%8+1);
    make(TEXT,c,384,318,512,36,LABEL,100+a.selected%8);
    button(c,384,a.height-115,256,11,BACK,0,true);
    a.page=SUCCESS; a.completed++; call('navigation.replace',SUCCESS,c);
  }
  function edit() {
    if (a.page!==HOME || a.editing) throw Error('APP_EDITOR_STATE');
    call('input.cancel'); a.pager.cancel(); a.viewportDirty=1;
    call('keyboard.begin',2,HOME,PAGE,LABEL,MUTED,CARD,KEY,PRIMARY,4);
    call('keyboard.field',0,501,205,a.inputLocales?4:0,64,1,0,a.name);
    call('keyboard.field',1,502,206,1,16,1,0,a.number);
    call('keyboard.field',2,503,207,2,64,1,0,'');
    call('keyboard.field',3,504,208,3,8,1,0,'');
    if(a.inputLocales) call('keyboard.languages',a.inputLocales,1);
    call('keyboard.show'); a.editing=1;
  }
  function keyboardStep() {
    if (!a.editing) return;
    const result=call('keyboard.step');
    if (result!==0) {
      if (result===1) { a.name=call('keyboard.result',0); a.number=call('keyboard.result',1); }
      call('keyboard.close'); a.editing=0;
    }
  }
  function act(action,index) {
    call('layout.invalidate');
    switch (action) {
      case EDIT: edit(); break;
      case SELECT:
        if (a.page!==HOME || index>=a.items) throw Error('APP_SELECTION');
        a.selected=index; detail(); break;
      case NEXT: if (a.page===HOME) window(a.first+6<a.items?a.first+6:0); break;
      case BACK:
        if (a.page===HOME) window(a.first>=6?a.first-6:0);
        else { dismiss(); call('navigation.pop'); a.page=HOME; }
        break;
      case CONFIRM: if (a.page!==DETAIL) throw Error('APP_CONFIRM'); modal(); break;
      case START: if (a.page!==DETAIL) throw Error('APP_START'); making(); break;
      case CANCEL:
        if (a.modal) dismiss();
        else if (a.page===MAKING) { call('navigation.pop'); a.page=HOME; }
        break;
      case THEME: a.theme=1-a.theme; call('theme.select',a.theme+1); break;
      case LOCALE: a.locale=(a.locale+1)%6; break;
      default: throw Error('APP_ACTION');
    }
  }
  function state() {
    call('frame.state',a.selected,a.first,a.items,a.locale,a.theme,a.progress,a.completed,a.actions,
      a.pager.position,a.pager.dragging,a.pager.settling,a.theme?rgba(23,30,29):rgba(248,246,240),
      +(a.page===HOME && !a.pager.dragging && !a.pager.settling));
  }
  globalThis.PocketApplication = {
    start(config) {
      config.items=config.items||8;
      if ((config.height!==600 && config.height!==800) || (config.items!==8 && config.items!==100)) throw Error('APP_CONFIG');
      a={height:config.height,items:config.items,inputLocales:config.inputLocales||0,page:HOME,first:0,selected:0,locale:0,theme:0,completed:0,actions:0,
        pending:0,pendingIndex:0,modal:0,progress:0,now:0,started:0,name:'Coffee',number:'',editing:0,
        cards:new Map(),pager:new Pager(config.items),materialized:-1,viewportDirty:0};
      themes(); a.signal=call('signal.create',0);
      a.home=page(1);
      make(TEXT,a.home,32,55,600,36,MUTED,2);
      button(a.home,672,24,144,14,THEME);
      button(a.home,832,24,160,13,LOCALE);
      button(a.home,600,a.height-60,168,204,EDIT);
      a.grid=make(SCROLL,a.home,32,105,960,a.height-170,CLEAR);
      button(a.home,784,a.height-60,208,4,NEXT);
      a.list=call('list.create',a.grid,a.items,PAGE_POOL,BUTTON);
      window(0); call('navigation.push',HOME,a.home);
      call('event.bind',a.grid,SCROLL_TOKEN,2); call('gesture.set',a.grid,32);
      state(); return true;
    },
    event(e) {
      if (e.token===SCROLL_TOKEN) {
        if (a.page!==HOME) return 0;
        if (e.type===1001 && e.phase!==3) a.pager.press(e.pointer,e.x,e.time);
        else if (e.type===1003 && e.phase!==3 && !a.pager.dragging) a.pager.release(e.pointer,e.x,e.time);
        else if (e.phase===2) {
          if (e.type===1130 || e.type===1131) a.pager.move(e.pointer,e.x,e.time);
          else if (e.type===1132) a.pager.release(e.pointer,e.x,e.time);
          else if ((e.type===1004 || e.type===1141) && a.pager.pointer===e.pointer) a.pager.cancel();
        }
        a.viewportDirty=1; return 0;
      }
      if (e.phase!==2 || e.type!==1101) return 0;
      const action=Math.floor(e.token/4096), index=e.token%4096;
      if (a.page===HOME && (a.pager.dragging || a.pager.settling || (action===SELECT && a.pager.blockTap))) return 1;
      if (!a.pending) { a.pending=action; a.pendingIndex=index; a.actions++; }
      return 1;
    },
    tick(input) {
      if (input.time<a.now) throw Error('APP_CLOCK');
      a.now=input.time;
      if (a.page===HOME) {
        if (!call('input.active') && a.pager.active) { a.pager.cancel(); a.viewportDirty=1; }
        const previous=a.pager.position; a.pager.tick(a.now);
        if (previous!==a.pager.position) a.viewportDirty=1;
        if (a.viewportDirty) viewport();
        a.first=a.pager.settled*PAGE_ITEMS;
      }
      if (a.pending) { const action=a.pending,index=a.pendingIndex; a.pending=0; act(action,index); }
      keyboardStep();
      if (a.page===MAKING) {
        a.progress=Math.min(100,Math.floor((a.now-a.started)/50));
        call('signal.set',a.signal,a.progress);
        if (a.progress===100) success();
      }
      state(); return true;
    },
    inspect() { return {page:a.page,selected:a.selected,first:a.first,completed:a.completed,progress:a.progress}; }
  };
})();
