"use strict";
// SDK semantics only. Actual pixels require the canonical production ELF job.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const root = path.resolve(__dirname, "../..");
function source(file) { return fs.readFileSync(path.join(root, file), "utf8"); }
const sdk = source("sdk/ui/js/pocket-ui.js");
const custom = source("sdk/ui/js/pocket-custom.js");
const moduleSource = source("apps/custom-chart/bar-chart.js");
const appSource = source("apps/custom-chart/application.js");
const rect = {x:10,y:20,width:200,height:100};
function rectangle(extra = {}) {
  return {kind:"rectangle",x:1,y:2,width:10,height:20,styleRef:2,...extra};
}
function setup() {
  const calls = [];
  let next = 0, rejectHost = false;
  const context = vm.createContext({__pocketCall(op, ...args) {
    calls.push({op,args});
    if (rejectHost) return 0;
    return op === "component.create" ? ++next : 1;
  }});
  vm.runInContext(sdk + "\n" + custom, context, {timeout:1000});
  vm.runInContext("PocketUI.define({start(ui){globalThis.ui=ui;},action(){}}); PocketApplication.start({height:600});", context);
  const registry = context.PocketCustom.install(context.ui);
  return {context, calls, registry, failHost() { rejectHost = true; }};
}
function test(name, fn) {
  try { fn(); process.stdout.write("F7_CUSTOM_OK " + name + "\n"); }
  catch (error) { console.error("F7_CUSTOM_FAIL", name, error); process.exitCode = 1; }
}
test("registry-and-installation", () => {
  const s = setup();
  s.registry.register("vendor.chart", () => [rectangle()]);
  assert.throws(() => s.registry.register("vendor.chart", () => []), /DUPLICATE_COMPONENT/);
  assert.throws(() => s.registry.mount("missing.chart",1,rect,{},1), /UNKNOWN_COMPONENT/);
  for (const name of ["chart", "__proto__", "vendor..chart", "Vendor.chart", "a." + "b".repeat(96)])
    assert.throws(() => s.registry.register(name, () => []), /NAMESPACE/);
  assert.throws(() => s.registry.register("bad.factory", {}), /FACTORY/);
  assert.throws(() => s.context.PocketCustom.install(s.context.ui), /DUPLICATE_INSTALL/);
  assert.throws(() => s.context.PocketCustom.install({}), /CONTEXT/);
  assert.throws(() => vm.runInContext(custom, s.context), /DUPLICATE_SDK/);
  assert.throws(() => vm.runInNewContext(custom, {}), /UI_REQUIRED/);
  for (let i=1; i<32; i++) s.registry.register("vendor.c"+i, () => [rectangle()]);
  assert.throws(() => s.registry.register("vendor.overflow", () => []), /REGISTRY_BUDGET/);
  assert.equal(s.calls.length, 0);
});
test("bounded-budget-and-coordinates-before-any-mutation", () => {
  const s = setup();
  s.registry.register("vendor.chart", (_, shapes) => shapes);
  const mount = shapes => s.registry.mount("vendor.chart",1,rect,shapes,1);
  for (const shapes of [[], Array.from({length:33}, () => rectangle()), new Array(2),
    Promise.resolve([]), [null], [rectangle({kind:"path"})], [rectangle({color:123})]])
    assert.throws(() => mount(shapes), /POCKET_CUSTOM_/);
  for (const key of ["x","y","width","height","styleRef"]) {
    for (const value of [NaN,Infinity,-1,0.5,"1",null,65537])
      assert.throws(() => mount([rectangle({[key]:value})]), /POCKET_CUSTOM_/);
  }
  assert.throws(() => mount([rectangle({x:195})]), /OUT_OF_BOUNDS/);
  assert.throws(() => mount([rectangle({y:95})]), /OUT_OF_BOUNDS/);
  assert.equal(s.calls.length,0);
  const instance = mount(Array.from({length:32}, () => rectangle()));
  assert.equal(instance.primitiveCount,32);
  assert.equal(s.calls.filter(c => c.op === "component.create").length,33);
});
test("axis-lines-have-bounded-positive-side-thickness", () => {
  const s = setup();
  s.registry.register("vendor.lines", (_, shapes) => shapes);
  const line = {kind:"line",x1:10,y1:5,x2:40,y2:5,thickness:2,styleRef:2};
  for (const bad of [{y2:6}, {x2:10}, {x1:-1}, {thickness:NaN}, {y1:100,y2:100},
    {x1:200,x2:200,y2:50}, {thickness:0}, {color:0}])
    assert.throws(() => s.registry.mount("vendor.lines",1,rect,[{...line,...bad}],1), /POCKET_CUSTOM_/);
  assert.equal(s.calls.length,0);
  s.registry.mount("vendor.lines",1,rect,[line,{...line,x1:12,x2:12,y1:90,y2:10}],1);
  assert.deepEqual(Array.from(s.calls[1].args.slice(2,6)),[10,5,30,2]);
  assert.deepEqual(Array.from(s.calls[2].args.slice(2,6)),[12,10,2,80]);
});
test("updates-keep-topology-and-copy-factory-output", () => {
  const s = setup();
  const shared = [rectangle()];
  s.registry.register("vendor.chart", (_, props) => props || shared);
  const instance = s.registry.mount("vendor.chart",1,rect,null,1);
  const prior = s.calls.length;
  shared[0].styleRef = 3;
  assert.throws(() => instance.update(shared), /TOPOLOGY/);
  assert.throws(() => instance.update([rectangle(),rectangle()]), /TOPOLOGY/);
  assert.throws(() => instance.update([rectangle({x:300})]), /COORDINATE/);
  assert.throws(() => instance.update([rectangle()],{...rect,width:0}), /COORDINATE/);
  assert.equal(s.calls.length,prior);
  instance.update([rectangle({x:30,height:0})],{...rect,x:30});
  assert.equal(s.calls.filter(c => c.op === "component.create").length,2);
  assert.equal(s.calls.filter(c => c.op === "component.layout").length,2);
  assert.equal(s.calls.at(-1).op,"layout.invalidate");
  assert.equal(Object.isFrozen(instance),true);
  assert.deepEqual(Object.keys(instance),["primitiveCount","update"]);
});
test("host-denial-and-stale-owner-poison-instance", () => {
  const s = setup();
  s.registry.register("vendor.chart", () => [rectangle()]);
  const instance = s.registry.mount("vendor.chart",1,rect,{},1);
  s.failHost();
  assert.throws(() => instance.update({}), /POCKET_UI_HOST_REJECTED/);
  const count = s.calls.length;
  assert.throws(() => instance.update({}), /FAILED_INSTANCE/);
  assert.equal(s.calls.length,count);
  assert.throws(() => s.registry.mount("vendor.chart",1,rect,{},1), /POCKET_UI_HOST_REJECTED/);
});
test("reentrant-factory-and-privileged-commands-denied", () => {
  const s = setup();
  s.registry.register("vendor.chart", () => {
    s.registry.mount("vendor.chart",1,rect,{},1);
    return [rectangle()];
  });
  assert.throws(() => s.registry.mount("vendor.chart",1,rect,{},1), /REENTRANT/);
  assert.equal(s.calls.length,0);
  for (const name of ["raw","call","createText","destroy","framebuffer","ipc","__pocketCall"])
    assert.equal(s.registry[name],undefined);
  s.registry.register("vendor.command", (_, props) => props);
  for (const kind of ["native.shell","ipc.command","canvas","arc","text","image"])
    assert.throws(() => s.registry.mount("vendor.command",1,rect,[rectangle({kind})],1), /PRIMITIVE/);
  assert.equal(s.calls.length,0);
});
test("two-isolated-instances-and-retained-size", () => {
  const s = setup();
  let size;
  s.registry.register("vendor.chart", (bounds, props) => { size = bounds; return [rectangle({x:props})]; });
  const a = s.registry.mount("vendor.chart",1,rect,1,1);
  const b = s.registry.mount("vendor.chart",1,rect,2,1);
  assert.equal(Object.isFrozen(size),true);
  a.update(3); b.update(4);
  const handles = s.calls.filter(c => c.op === "component.layout").map(c => c.args[0]);
  assert.deepEqual(handles,[1,2,3,4]);
});
test("real-independent-module-and-demo-dual-target-update", () => {
  for (const height of [600,800]) {
    let handle = 0;
    const calls = [];
    const s = vm.createContext({__pocketCall(op,...args) {
      calls.push({op,args}); return op === "component.create" ? ++handle : 1;
    }});
    vm.runInContext([sdk,custom,moduleSource,appSource].join("\n"),s,{timeout:1000});
    assert.equal(s.PocketApplication.start({height}),true);
    assert.equal(s.PocketApplication.inspect().primitives,8);
    for(let i=0;i<8;i++) s.PocketApplication.tick();
    assert.equal(s.PocketApplication.inspect().updates,1);
    for(let i=0;i<60;i++) s.PocketApplication.tick();
    assert.equal(s.PocketApplication.inspect().updates,1);
    assert.equal(calls.filter(c => c.op === "component.create").length,11);
    assert.equal(calls.filter(c => c.op === "component.layout").length,9);
    assert.deepEqual(Array.from(calls.find(c => c.op === "component.layout").args.slice(1)),
      [96,176,832,height-272]);
    assert.equal(calls.some(c => /^(native|ipc|keyboard|image)\./.test(c.op)),false);
  }
});
test("bar-module-invalid-and-overlong-values", () => {
  const s = setup();
  vm.runInContext(moduleSource,s.context);
  s.context.PocketBarChart.register(s.registry);
  for (const values of [[],new Array(31).fill(1),[NaN],[Infinity],[-1],[101],["1"]])
    assert.throws(() => s.registry.mount("example.chart.bars",1,rect,{values},1), /BAR_CHART_VALUES/);
  assert.equal(s.calls.length,0);
  s.registry.mount("example.chart.bars",1,rect,{values:new Array(30).fill(50)},1);
  assert.equal(s.calls.length,33);
});
process.stdout.write(process.exitCode ? "F7_CUSTOM_FAILURE\n" : "F7_CUSTOM_UNIT_OK\n");
