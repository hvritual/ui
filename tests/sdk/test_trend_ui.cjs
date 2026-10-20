"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm"),path=require("node:path");
const root=path.resolve(__dirname,"../..");
const read=p=>fs.readFileSync(path.join(root,p),"utf8");
const sdk=read("sdk/ui/js/pocket-ui.js"),custom=read("sdk/ui/js/pocket-custom.js"),
  moduleSource=read("apps/trend-chart/charts.js"),example=read("apps/trend-chart/application.js");
function setup(app=example){
  const calls=[];let handle=0;
  const s=vm.createContext({__pocketCall(op,...args){calls.push({op,args});return op==="component.create"?++handle:1;}});
  vm.runInContext([sdk,custom,moduleSource,app].join("\n"),s,{timeout:1000});
  return {s,calls};
}
function test(name,fn){try{fn();console.log("TREND_OK",name);}catch(e){console.error("TREND_FAIL",name,e);process.exitCode=1;}}
test("1024-real-vertices-both-targets-no-component-rebuild",()=>{
  for(const height of [600,800]){
    const {s,calls}=setup();s.PocketApplication.start({height});
    let uploads=calls.filter(c=>c.op==="draw.begin");
    assert.equal(uploads.length,1);assert.equal(uploads[0].args[4],1024);
    const creates=calls.filter(c=>c.op==="component.create").length;
    assert.equal(creates,8);
    const payload=calls.filter(c=>c.op==="draw.points");
    assert.equal(payload.length,2);assert.equal(payload[0].args[1],0);assert.equal(payload[1].args[1],512);
    assert.equal(payload.reduce((n,c)=>n+c.args[2].length/8,0),1024);
    const first=payload.map(c=>c.args[2]).join("");
    for(let i=0;i<12;i++)s.PocketApplication.tick();
    assert.equal(s.PocketApplication.inspect().samples,1024);assert.equal(s.PocketApplication.inspect().segments,1023);
    assert.equal(s.PocketApplication.inspect().updates,1);
    assert.equal(calls.filter(c=>c.op==="component.create").length,creates);
    assert.equal(calls.filter(c=>c.op==="draw.begin").length,2);
    assert.equal(calls.filter(c=>c.op==="draw.commit").length,2);
    const second=calls.filter(c=>c.op==="draw.points").slice(2).map(c=>c.args[2]).join("");
    assert.notEqual(first,second);assert.equal(second.length,8192);
    assert.equal(calls.some(c=>/^(ipc|keyboard|native)\./.test(c.op)),false);
  }
});
const capture="PocketUI.define({start(ui){globalThis.context=ui;globalThis.registry=PocketCustom.install(ui);PocketCharts.register(registry);},action(){}});";
test("data-axis-zoom-and-point-budget-negative-before-native",()=>{
  const {s,calls}=setup(capture);s.PocketApplication.start({height:600});
  const chart=s.PocketCharts.init(s.registry,1,{x:0,y:0,width:800,height:250});
  const option=data=>({series:[{type:"line",data}],yAxis:{min:0,max:100}});
  for(const data of [[NaN,1],[Infinity,2],new Array(1000),[1],new Array(4097).fill(1)])
    assert.throws(()=>chart.setOption(option(data)),/CHART_DATA/);
  for(const minmax of [[1,1],[NaN,2],[0,Infinity]])
    assert.throws(()=>chart.setOption({...option([1,2]),yAxis:{min:minmax[0],max:minmax[1]}}),/CHART_AXIS/);
  for(const zoom of [{start:50,end:50},{start:-1,end:100},{start:0,end:101},{start:NaN,end:100}])
    assert.throws(()=>chart.setOption({...option([1,2]),dataZoom:zoom}),/CHART_ZOOM/);
  assert.equal(calls.length,0);
  chart.setOption(option(new Array(4096).fill(50)));
  assert.equal(calls.find(c=>c.op==="draw.begin").args[4],4096);
  assert.equal(calls.filter(c=>c.op==="draw.points").length,8);
  chart.setOption({...option(new Array(1024).fill(40)),dataZoom:{start:20,end:80}});
  assert.equal(calls.filter(c=>c.op==="draw.begin").at(-1).args[4],616);
});
test("public-drawing-validates-every-point-before-begin",()=>{
  const {s,calls}=setup(capture);s.PocketApplication.start({height:600});
  const ui=s.context,size={width:800,height:250};
  for(const points of [[],new Array(4097).fill({x:1,y:1}),[{x:0,y:0},{x:800,y:1}],
    [{x:1,y:1},{x:NaN,y:1}],[{x:1,y:1},{x:1,y:250}],[{x:1,y:1},{x:1,y:1,color:0}],new Array(2)])
    assert.throws(()=>ui.setPolyline(1,size,points,4),/POCKET_UI_/);
  assert.throws(()=>ui.setPolyline(1,{width:1024,height:800},[{x:0,y:0},{x:1,y:1}],4),/BUDGET/);
  assert.equal(calls.length,0);
});
test("surface-aggregate-point-budget-and-style-topology",()=>{
  const {s,calls}=setup(capture);s.PocketApplication.start({height:600});
  const shape={kind:"polyline",points:new Array(3000).fill({x:1,y:1}),styleRef:4};
  s.registry.register("test.points",(_,props)=>props);
  const rect={x:0,y:0,width:800,height:250};
  assert.throws(()=>s.registry.mount("test.points",1,rect,[shape,shape],2),/POINT_BUDGET/);
  assert.equal(calls.length,0);
  const instance=s.registry.mount("test.points",1,rect,[shape],2),n=calls.length;
  assert.throws(()=>instance.update([{...shape,styleRef:5}]),/TOPOLOGY/);
  assert.equal(calls.length,n);
});
if(!process.exitCode)console.log("TREND_UNIT_OK");
