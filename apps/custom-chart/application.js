/* F7-02A independent developer chart UI: consumes only public PocketUI
 * and PocketCustom. All shapes compile into existing admitted View primitives.
 */
(function(){
  "use strict";
  const bg=0xfff8f6f0,ink=0xff303734,base=0xffe7ece1,accent=0xff3b805a;
  let chart,count=0,actions=0;
  const samples=[15,33,58,29,72,55,45,81];
  PocketUI.define({
    start(ui,target){
      ui.defineTheme({id:1,foregroundToken:2,fontSize:22,colors:{"1":bg,"2":ink,"3":base,"4":accent}});
      ui.styleRule(1,1,2,0);
      ui.styleRule(2,3,2,10);
      ui.styleRule(3,2,2,0);
      ui.styleRule(4,4,2,2);
      ui.selectTheme(1);
      const root=ui.createView(0,{x:0,y:0,width:1024,height:target.height},1);
      ui.createText(root,{x:52,y:26,width:840,height:44},5001,1);
      ui.createText(root,{x:52,y:84,width:840,height:38},5002,1);
      const panel=ui.createView(root,{x:64,y:156,width:896,height:target.height-320},2);
      chart=PocketCustom.mount("pocket.bar-chart",ui,panel,
        {x:24,y:14,width:836,height:target.height-348},{samples});
      ui.createButton(root,{x:64,y:target.height-120,width:356,height:60},5004,2,1);
      ui.createButton(root,{x:604,y:target.height-120,width:356,height:60},5005,2,2);
      ui.navigation.push(1,root);
      ui.publish({selected:0,itemCount:8,actions:0,mediaReady:1,background:bg});
      return true;
    },
    action(ui,id){
      if(id===1)count=Math.min(50,count+1);
      else if(id===2)count=Math.max(0,count-1);
      else return;
      const next=samples.map((v,i)=>Math.min(100,Math.max(0,v+(i%2?-1:1)*count)));
      chart.update({samples:next});
      actions++;
      ui.publish({selected:count,itemCount:8,actions,mediaReady:1,background:bg});
    },
    tick(){return true;},
    inspect(){return {count,actions,drawPrimitives:chart.primitives()};}
  });
})();
