/* Independent public-SDK-only app. No engine, C, renderer or native calls.
 * The internal builder prepends sdk/ui/js/pocket-ui.js and copies existing
 * pinned assets (real glyphs and the standard control-panel text catalog).
 */
(function () {
  "use strict";
  let value=0, totalActions=0, numberLabel=0;
  const COLOR={base:0xfffaf8f4,ink:0xff332b26,card:0xfff0dfc8,white:0xffffffff};
  PocketUI.define({
    start(ui,target) {
      ui.defineTheme({id:1,foregroundToken:2,fontSize:22,colors:{
        "1":COLOR.base,"2":COLOR.ink,"3":COLOR.card,"4":COLOR.white
      }});
      ui.styleRule(1,1,2,0);
      ui.styleRule(2,3,2,12);
      ui.styleRule(3,3,2,10);
      ui.selectTheme(1);
      const root=ui.createView(0,{x:0,y:0,width:1024,height:target.height},1);
      ui.createText(root,{x:64,y:38,width:880,height:44},5001,1);
      ui.createText(root,{x:64,y:112,width:880,height:36},5002,1);
      const panel=ui.createView(root,{x:64,y:192,width:896,height:205},2);
      ui.createText(panel,{x:36,y:32,width:520,height:40},5003,2);
      numberLabel=ui.createText(panel,{x:388,y:92,width:200,height:50},5200,2);
      ui.createButton(root,{x:64,y:target.height-156,width:360,height:64},5004,3,1);
      ui.createButton(root,{x:600,y:target.height-156,width:360,height:64},5005,3,2);
      ui.navigation.push(1,root);
      ui.publish({selected:0,itemCount:2,actions:0,theme:1,mediaReady:1,background:COLOR.base});
      return true;
    },
    action(ui,actionId) {
      if(actionId===1) value=Math.min(99,value+1);
      else if(actionId===2) value=Math.max(0,value-1);
      else return;
      totalActions++;
      ui.setText(numberLabel,5200+value);
      ui.publish({selected:value,itemCount:2,actions:totalActions,theme:1,mediaReady:1,background:COLOR.base});
    },
    tick(){return true;},
    inspect(){return {value,totalActions};}
  });
})();
