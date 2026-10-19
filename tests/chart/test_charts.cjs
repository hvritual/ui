"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm"),path=require("node:path");
const root=path.resolve(__dirname,"../..");
const sources=[
 "sdk/ui/js/pocket-ui.js",
 "sdk/ui/js/pocket-charts.js",
 "apps/chart-1000/application.js"
];
function open(withDemo=true,denied=false){
 const calls=[];let next=110;
 const env={__pocketCall(op,...args){
    calls.push({op,args});
    if(denied&&op==="chart.set")return 0;
    return op==="component.create"?++next:1;
 }};
 env.globalThis=env;vm.createContext(env);
 for(const src of sources.slice(0,withDemo?3:2))
    vm.runInContext(fs.readFileSync(path.join(root,src),"utf8"),env,{filename:src,timeout:5000});
 return {env,calls};
}
function test(name,fn){
 try{fn();console.log("F7_CHART_TEST_OK",name);}
 catch(err){console.error("F7_CHART_TEST_FAILED",name,err);process.exitCode=1;}
}
test("actual-1000-point-two-series-demo-600-800",()=>{
 for(const h of [600,800]){
  const {env,calls}=open();
  const app=env.PocketApplication;
  assert.equal(app.start({height:h}),true);
  assert.equal(env.PocketCharts.limits.pointsPerSeries,1000);
  assert.equal(app.inspect().trendPoints,1000);
  assert.equal(app.inspect().bubblePoints,1000);
  const payloads=calls.filter(x=>x.op==="chart.set");
  assert.equal(payloads.length,2);
  for(const entry of payloads){
   assert.ok([1,4].includes(entry.args[1]));
   assert.equal(entry.args[3].length,6668);
   const decoded=Buffer.from(entry.args[3],"base64");
   assert.equal(decoded.length,5000);
  }
  assert.equal(calls.filter(x=>x.op==="component.create").length,7);
  const before=calls.length;
  assert.equal(app.event({type:1101,phase:2,token:1}),1);
  assert.equal(calls.length,before,"event must not render reentrantly");
  app.tick();
  assert.equal(app.inspect().updates,1);
  assert.equal(calls.filter(x=>x.op==="chart.set").length,3);
  assert.equal(calls.filter(x=>x.op==="component.create").length,7);
  app.event({type:1101,phase:2,token:2});app.tick();
  assert.equal(calls.filter(x=>x.op==="chart.set").length,4);
  assert.equal(app.inspect().bubblePoints,1000);
 }
});
test("four-renderer-modes-and-canonical-packed-data",()=>{
 const {env,calls}=open(false);
 const ui={createChart(){return 330;},setChartData(handle,mode,color,payload){
  calls.push({op:"chart.set",args:[handle,mode,color,payload]});return 1;}};
 for(const [type,mode] of Object.entries({line:1,bar:2,scatter:3,bubble:4})){
  const values=Array.from({length:1000},(_,i)=>({x:i*10,y:i*10,size:i%256}));
  const handle=env.PocketCharts.mount(ui,1,{x:0,y:0,width:768,height:300},
    {type,data:values,color:0xff9a7251});
  assert.equal(handle.pointCount(),1000);
  const cmd=calls.at(-1);
  assert.equal(cmd.args[1],mode);
  assert.equal(cmd.args[3].length,6668);
  const bytes=Buffer.from(cmd.args[3],"base64");
  assert.equal(bytes.length,5000);
  assert.equal(bytes.readUInt16LE(0),0);
  assert.equal(bytes.readUInt16LE(4995),9990);
  if(mode===4)assert.equal(bytes[4999],999%256);
 }
 assert.throws(()=>env.PocketCharts.mount(ui,1,{x:0,y:0,width:768,height:300},
   {type:"line",data:[{x:0,y:0}],color:0}),/INSTANCE_BUDGET/);
});
test("invalid-datasets-never-enter-native",()=>{
 const {env,calls}=open(false);
 const ui={createChart(){calls.push("create");return 3;},setChartData(){calls.push("update");return 1;}};
 const valid=Array.from({length:1000},(_,i)=>({x:i*10,y:i*10,size:5}));
 for(const samples of [
  [],Array.from({length:1001},()=>({x:10,y:10})),
  [{x:1,y:NaN}],[{x:Infinity,y:2}],
  [{x:-1,y:2}],[{x:10001,y:2}],[{x:1,y:0,size:256}],
  [{x:1,y:0,size:-1}], [{x:1,y:2.5}]
 ]){
  const before=calls.length;
  assert.throws(()=>env.PocketCharts.mount(ui,1,{x:0,y:0,width:400,height:240},
    {type:"bubble",data:samples,color:0xff454545}));
  assert.equal(calls.length,before,"malformed point reached Native");
 }
 const c=env.PocketCharts.mount(ui,1,{x:0,y:0,width:400,height:240},
  {type:"line",data:valid,color:0xff343434});
 const before=calls.length;
 assert.throws(()=>c.setOption({type:"scatter",data:[{x:NaN,y:1}],color:0xff343434}));
 assert.equal(calls.length,before);
});
test("axis-policy-extensible-bubble-transformer",()=>{
 const {env,calls}=open(false);
 const ui={createChart(){return 45;},setChartData(...args){calls.push(args);return 1;}};
 env.PocketCharts.registerSeries("vendor.bubble-heat","bubble",array=>array.map((v,i)=>({x:i*10,y:v.value,size:v.radius})));
 const data=Array.from({length:1000},(_,i)=>({value:i*10,radius:(i%200)+1}));
 const chart=env.PocketCharts.mount(ui,12,{x:0,y:0,width:640,height:250},
  {type:"vendor.bubble-heat",data,color:0xff112233});
 assert.equal(chart.pointCount(),1000);
 assert.equal(calls.at(-1)[1],4);
 assert.throws(()=>env.PocketCharts.registerSeries("vendor.bubble-heat","line",()=>[]),/SERIES_NAME/);
 assert.throws(()=>env.PocketCharts.registerSeries("vendor.x","3d",()=>[]),/SERIES_ADAPTER/);
});
test("old-runtime-chart-capability-denied-no-silent-fallback",()=>{
 const {env}=open(false,true);
 assert.throws(()=>env.PocketCharts.mount({createChart(){return 9;},setChartData(){return 0;}},1,
   {x:0,y:0,width:400,height:240},{type:"line",data:[{x:0,y:0}],color:0}),
   /^(?!$)/);
 const {env:direct}=open(false,true);
 assert.throws(()=>direct.PocketUI.define(null),/INVALID_APP/);
 assert.equal(typeof direct.PocketCharts.raw,"undefined");
 assert.equal(typeof direct.PocketCharts.drawBuffer,"undefined");
});
console.log(process.exitCode?"F7_CHART_UNIT_FAILED":"F7_CHART_UNIT_OK");
