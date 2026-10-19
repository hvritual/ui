/* Developer-owned 1000-point dual-chart sample (Line/Scatter + Bubble).
 * No JS access to framebuffer, renderer handles, memory pointers, or native chart code.
 */
(function(){
 "use strict";
 const background=0xfff8f6f0;
 let trend,bubbles,updates=0;
 const trendPoints=[];
 const bubblePoints=[];
 for(let i=0;i<1000;i++){
   const x=Math.round(i*10000/999);
   const phase=i%128,tri=phase<64?phase:127-phase;
   const y=1800+tri*95+(i*37)%470;
   trendPoints.push({x,y:Math.max(0,Math.min(10000,y))});
   const by=1000+((i*1733)%8000);
   bubblePoints.push({x,y:by,size:2+(i*17)%220});
 }
 PocketUI.define({
   start(ui,target){
     ui.defineTheme({id:1,foregroundToken:2,fontSize:22,colors:{
       "1":background,"2":0xff343834,"3":0xffe1e9e2,"4":0xff51945a
     }});
     ui.styleRule(1,1,2,0);
     ui.styleRule(2,3,2,10);
     ui.selectTheme(1);
     const root=ui.createView(0,{x:0,y:0,width:1024,height:target.height},1);
     ui.createText(root,{x:64,y:32,width:860,height:46},5001,1);
     ui.createText(root,{x:64,y:94,width:860,height:32},5002,1);
     const plotH=target.height===600?160:242;
     trend=PocketCharts.mount(ui,root,{x:128,y:138,width:768,height:plotH},{
       type:"line",color:0xff5b843f,data:trendPoints
     });
     bubbles=PocketCharts.mount(ui,root,{x:128,y:target.height===600?312:412,width:768,height:plotH},{
       type:"bubble",color:0xff8c7a4a,data:bubblePoints
     });
     ui.createButton(root,{x:84,y:target.height-81,width:362,height:56},5004,2,1);
     ui.createButton(root,{x:580,y:target.height-81,width:362,height:56},5005,2,2);
     ui.navigation.push(1,root);
     ui.publish({selected:0,itemCount:1000,actions:0,background,mediaReady:1});
     return true;
   },
   action(ui,id){
     if(id!==1&&id!==2)return;
     updates++;
     if(id===1){
       const change=trendPoints.map((p,i)=>({x:p.x,y:Math.max(0,Math.min(10000,p.y+(((i+updates*17)%67)-33)*14))}));
       trend.setOption({type:updates%2?"scatter":"line",data:change,color:0xff6b8b41});
     }else{
       const change=bubblePoints.map((p,i)=>({x:p.x,y:p.y,size:2+(i*17+updates*23)%220}));
       bubbles.setOption({type:"bubble",data:change,color:0xff936744});
     }
     ui.publish({selected:updates,itemCount:1000,actions:updates,background,mediaReady:1});
   },
   tick(){return true;},
   inspect(){return {updates,trendPoints:trend.pointCount(),bubblePoints:bubbles.pointCount()};}
 });
})();
