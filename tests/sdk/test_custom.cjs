"use strict";
/* Unit-scope component geometry/budget tests. Real renderer tested separately. */
const assert=require("node:assert/strict");
const fs=require("node:fs");
const vm=require("node:vm");
const path=require("node:path");
const ROOT=path.resolve(__dirname,"../..");
const files=[
 "sdk/ui/js/pocket-ui.js",
 "sdk/ui/js/pocket-custom.js",
 "sdk/ui/widgets/bar-chart.js",
 "apps/custom-chart/application.js"
];
function scene(loadApp=true){
 const operations=[];
 let next=100;
 const scope={__pocketCall(op,...args){operations.push({op,args});return op==="component.create"?++next:1;}};
 scope.globalThis=scope;vm.createContext(scope);
 for(const f of files.slice(0,loadApp?4:3))
  vm.runInContext(fs.readFileSync(path.join(ROOT,f),"utf8"),scope,{filename:f,timeout:1000});
 return {scope,operations};
}
function test(name,fn){
 try{fn();process.stdout.write("F7_CUSTOM_TEST_OK "+name+"\n");}
 catch(err){console.error("F7_CUSTOM_TEST_FAILED "+name,err);process.exitCode=1;}
}
test("public-only-bars-600-800-real-lifecycle",()=>{
 for(const height of [600,800]){
  const s=scene();
  const app=s.scope.PocketApplication;
  assert.equal(app.start({height}),true);
  assert.equal(app.inspect().drawPrimitives,10); // baseline + 8 bars + vertical axis
  const components=s.operations.filter(x=>x.op==="component.create");
  assert.equal(components.length,16); // root,2 text,panel,custom root,10 rects,2 buttons =17? counted below
  assert.ok(components.some(x=>x.args[5]===height));
  const before=s.operations.length;
  assert.equal(app.event({type:1101,phase:2,token:1}),1);
  assert.equal(s.operations.length,before,"F6 event mutated renderer");
  app.tick();
  assert.equal(app.inspect().count,1);
  assert.equal(app.inspect().actions,1);
  const layouts=s.operations.filter(x=>x.op==="component.layout");
  assert.equal(layouts.length,8);
  assert.equal(app.event({type:1101,phase:2,token:2}),1);
  app.tick();
  assert.equal(app.inspect().count,0);
  assert.equal(app.inspect().drawPrimitives,10);
  assert.equal(s.operations.filter(x=>x.op==="component.create").length,components.length);
 }
});
test("registration-fail-closed",()=>{
 const {scope}=scene(false);
 assert.throws(()=>scope.PocketCustom.register("",()=>({update(){}})),/POCKET_CUSTOM_NAME/);
 assert.throws(()=>scope.PocketCustom.register("pocket.bar-chart",()=>({update(){}})),/POCKET_CUSTOM_ALREADY_REGISTERED/);
 assert.throws(()=>scope.PocketCustom.register("example.invalid",null),/POCKET_CUSTOM_FACTORY/);
 assert.throws(()=>scope.PocketCustom.mount("example.missing",{},1,{x:0,y:0,width:64,height:64},{}),/UNKNOWN/);
 assert.equal(scope.PocketCustom.limits.primitives,24);
 assert.equal(typeof scope.PocketCustom.raw,"undefined");
 assert.equal(typeof scope.PocketCustom.__pocketCall,"undefined");
});
test("geometry-denies-out-of-bounds-and-nan-before-native",()=>{
 const {scope,operations}=scene(false);
 let captured;
 scope.PocketCustom.register("vendor.inspect",(surface)=>{captured=surface;return{update(){return true;}}});
 const ui={createView(parent,geometry,style){operations.push({op:"view",geometry});return operations.length;},
          setLayout(handle,geometry){operations.push({op:"layout",geometry});return 1;}};
 assert.throws(()=>scope.PocketCustom.mount("vendor.inspect",ui,1,{x:0,y:0,width:NaN,height:96},{}),/POCKET_CUSTOM_/);
 const w=scope.PocketCustom.mount("vendor.inspect",ui,1,{x:0,y:0,width:144,height:96},{});
 assert.equal(w.root,1);
 let n=operations.length;
 for(const box of [{x:-1,y:0,width:3,height:5},{x:143,y:0,width:2,height:5},
   {x:0,y:0,width:0,height:5},{x:0,y:Infinity,width:5,height:5},
   {x:0,y:0,width:1.2,height:5}])
  assert.throws(()=>captured.rect(box,3),/POCKET_CUSTOM_/);
 assert.equal(operations.length,n);
 const id=captured.rect({x:0,y:0,width:12,height:6},3);
 assert.equal(id,1);
 n=operations.length;
 assert.throws(()=>captured.move(2,{x:0,y:0,width:12,height:6}),/POCKET_CUSTOM_/);
 assert.throws(()=>captured.move(id,{x:141,y:0,width:8,height:6}),/POCKET_CUSTOM_/);
 assert.equal(operations.length,n);
 captured.move(id,{x:1,y:3,width:12,height:6});
 assert.equal(operations.at(-1).op,"layout");
});
test("primitive-budget-stops-before-25th-host-call",()=>{
 const {scope,operations}=scene(false);
 let surface;
 scope.PocketCustom.register("vendor.rectangles",(s)=>{surface=s;return{update(){return true;}}});
 const ui={createView(){operations.push("view");return operations.length;},setLayout(){return 1;}};
 scope.PocketCustom.mount("vendor.rectangles",ui,1,{x:0,y:0,width:100,height:100},{});
 for(let i=0;i<24;i++)surface.rect({x:0,y:0,width:1,height:1},1);
 assert.equal(surface.used(),24);
 const before=operations.length;
 assert.throws(()=>surface.rect({x:0,y:0,width:1,height:1},1),/PRIMITIVE_BUDGET/);
 assert.equal(operations.length,before);
});
test("bar-chart-invalid-update-prevalidates-all-data",()=>{
 const {scope,operations}=scene(false);
 const ui={createView(){operations.push("view");return operations.length;},
           setLayout(){operations.push("layout");return 1;}};
 const chart=scope.PocketCustom.mount("pocket.bar-chart",ui,1,{x:0,y:0,width:836,height:252},
    {samples:[10,20,30,40,50,60,70,80]});
 const before=operations.length;
 assert.throws(()=>chart.update({samples:[10,20,30,40,50,60,70,NaN]}),/CHART_SAMPLE_RANGE/);
 assert.throws(()=>chart.update({samples:[10]}),/CHART_SAMPLES_REQUIRED/);
 assert.equal(operations.length,before);
 chart.update({samples:[11,22,33,44,55,66,77,88]});
 assert.equal(operations.filter(x=>x==="layout").length,8);
});
test("no-device-native-calls",()=>{
 const s=scene();s.scope.PocketApplication.start({height:600});
 const names=s.operations.map(x=>x.op);
 assert.ok(names.every(x=>x.startsWith("component.")||x.startsWith("event.")||x.startsWith("theme.")||
   x.startsWith("style.")||x.startsWith("navigation.")||x==="frame.state"));
 assert.equal(names.filter(x=>x==="component.create").length,17);
});
process.stdout.write(process.exitCode?"F7_CUSTOM_UNIT_FAILED\n":"F7_CUSTOM_UNIT_OK\n");
