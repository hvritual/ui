/* A separate interactive counter/settings application. It shares only Pocket
 * operations and resource limits, not Coffee routes, state or native code. */
(function () {
  'use strict';
  const call=__pocketCall;
  let height, home, countLabel, count=0, page=1, modal=0, pending=0, actions=0, resets=0;
  const make=(kind,parent,x,y,w,h,style,text=0)=>call('component.create',kind,parent,x,y,w,h,style,text,0);
  function button(parent,x,y,w,label,action) {
    const c=make(5,parent,x,y,w,56,3,label); call('event.bind',c,action,1); return c;
  }
  function publish() { call('frame.state',count,0,0,0,0,count,resets,actions,0,0,0,0xfffaf8f4,+(page===1 && !modal)); }
  function closeModal() { if (modal) { call('overlay.dismiss',10); modal=0; } }
  globalThis.PocketApplication={
    start(config) {
      height=config.height;
      call('theme.define',1,2,22,1,0xfffaf8f4,2,0xff332b26,3,0xfff0dfc8,4,0xffffffff,5,0x90000000);
      call('style.rule',1,1,2,0); call('style.rule',2,4,2,12); call('style.rule',3,3,2,10); call('style.rule',4,5,2,0);
      call('theme.select',1);
      home=make(1,0,0,0,1024,height,1);
      make(2,home,64,36,840,44,1,5001);
      make(2,home,64,100,880,36,1,5002);
      const panel=make(1,home,64,180,896,200,2);
      make(2,panel,36,20,520,40,2,5003);
      countLabel=make(2,panel,380,95,150,48,2,5200);
      button(home,64,height-152,256,5004,1);
      button(home,384,height-152,256,5005,2);
      button(home,704,height-152,256,5006,3);
      call('navigation.push',1,home); publish(); return true;
    },
    event(event) {
      if (event.type!==1101 || event.phase!==2) return 0;
      if (!pending) { pending=event.token; actions++; }
      return 1;
    },
    tick() {
      const action=pending; pending=0;
      if (action) call('layout.invalidate');
      if (action===1 && page===1) { count=Math.min(99,count+1); call('component.text',countLabel,5200+count); }
      else if (action===2 && page===1) { count=Math.max(0,count-1); call('component.text',countLabel,5200+count); }
      else if (action===3 && page===1) {
        const settings=make(1,0,0,0,1024,height,1);
        make(2,settings,64,36,840,44,1,5006);
        make(2,settings,64,140,860,48,1,5007);
        button(settings,64,height-152,384,5008,4);
        button(settings,576,height-152,384,5009,5);
        page=2; call('navigation.push',2,settings);
      } else if (action===4 && page===2) { closeModal(); call('navigation.pop'); page=1; }
      else if (action===5 && page===2 && !modal) {
        modal=make(16,0,0,0,1024,height,4);
        const panel=make(1,modal,224,160,576,260,2);
        make(2,panel,32,32,512,48,2,5010);
        button(panel,32,160,224,5011,6); button(panel,320,160,224,5009,7);
        call('overlay.present',10,2,modal,3);
      } else if (action===6 && modal) closeModal();
      else if (action===7 && modal) {
        count=0; resets++; call('component.text',countLabel,5200);
        closeModal(); call('navigation.pop'); page=1;
      }
      publish(); return true;
    },
    inspect() { return {page,count,resets}; }
  };
})();
