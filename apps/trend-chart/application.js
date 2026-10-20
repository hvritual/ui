/* Developer application. 1024 distinct samples, 1023 rendered segments per update. */
(function(){
  "use strict";
  // Public theme colors use the pinned Core's packed 0xAABBGGRR representation.
  const count=1024,base=0xfff4f8fa;
  let chart,ticks=0,updates=0,targetHeight;
  function option(phase) {
    const data=Array.from({length:count},(_,i)=>{
      // Integer deterministic data; identical on Native/ARM and the physical host.
      const ramp=(i*7+phase*173)%256;
      return 20+Math.floor((ramp<=128?ramp:256-ramp)*60/128)+(i%17);
    });
    return {series:[{type:"line",data}],yAxis:{min:0,max:100},dataZoom:{start:0,end:100}};
  }
  PocketUI.define({
    start(ui,target){
      targetHeight=target.height;
      ui.defineTheme({id:1,foregroundToken:2,fontSize:22,colors:{
        "1":base,"2":0xff262b33,"3":0xffc8dff0,"4":0xff987f2a,"5":0xff4c5665
      }});
      ui.styleRule(1,1,2,0);ui.styleRule(2,3,2,0);ui.styleRule(4,4,2,0);ui.styleRule(5,5,2,0);
      ui.selectTheme(1);
      const page=ui.createView(0,{x:0,y:0,width:1024,height:target.height},1);
      ui.createText(page,{x:64,y:38,width:880,height:44},5001,1);
      // Real catalog labels: 10 and 24 display the 1024 input sample count.
      ui.createText(page,{x:64,y:96,width:60,height:40},5210,1);
      ui.createText(page,{x:120,y:96,width:60,height:40},5224,1);
      const registry=PocketCustom.install(ui);PocketCharts.register(registry);
      chart=PocketCharts.init(registry,page,{x:64,y:160,width:896,height:target.height===600?240:280});
      chart.setOption(option(0));
      ui.navigation.push(1,page);
      ui.publish({selected:0,itemCount:count,actions:0,theme:1,background:base});
      return true;
    },
    action(){},
    tick(ui){
      ticks++;
      if(ticks===12){
        chart.resize({x:96,y:176,width:832,height:targetHeight===600?240:280},option(1));
        updates=1;
        ui.publish({selected:1,itemCount:count,actions:updates,theme:1,background:base});
      }
      return true;
    },
    inspect(){return {samples:count,segments:count-1,updates};}
  });
})();
