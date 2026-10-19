"use strict";
/* F7-01 unit semantics only; the Coffee workflow tests the production renderer. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const ROOT = path.resolve(__dirname, "../..");
const sdk = fs.readFileSync(path.join(ROOT, "sdk/ui/js/pocket-ui.js"), "utf8");
const example = fs.readFileSync(path.join(ROOT, "apps/sdk-demo/application.js"), "utf8");

function context(bridge) {
  const sandbox = {__pocketCall:bridge};
  sandbox.globalThis = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(sdk, sandbox, {filename:"pocket-ui.js", timeout:1000});
  return sandbox;
}
function test(name, fn) {
  try {fn(); process.stdout.write("F7_SDK_TEST_OK " + name + "\n");}
  catch (error) {console.error("F7_SDK_TEST_FAIL", name, error); process.exitCode=1;}
}
function mockCalls() {
  const operations=[];
  function bridge(op, ...args) {
    operations.push({op,args});
    return op==="component.create" ? 123 : 1;
  }
  return {operations,bridge};
}

test("real-public-demo-600-800",()=>{
  for (const height of [600,800]) {
    const {operations,bridge}=mockCalls();
    const sandbox=context(bridge);
    vm.runInContext(example,sandbox,{filename:"sdk-demo.js",timeout:1000});
    const app=sandbox.PocketApplication;
    assert.equal(sandbox.PocketUI.version,"0.1-experimental");
    assert.equal(app.start({height}),true);
    const views=operations.filter(x=>x.op==="component.create");
    assert.equal(views.length,8);
    assert.equal(views[0].args[5],height);
    assert.equal(operations.filter(x=>x.op==="event.bind").length,2);
    const frames=operations.filter(x=>x.op==="frame.state");
    assert.equal(frames.length,1);
    assert.equal(frames[0].args.length,13);
    assert.equal(app.event({type:1101,phase:1,token:1}),0);
    assert.equal(app.event({type:1101,phase:2,token:1}),1);
    assert.equal(app.inspect().value,0);
    assert.equal(operations.filter(x=>x.op==="component.text").length,0);
    assert.equal(app.tick(),true);
    assert.equal(app.inspect().value,1);
    assert.equal(operations.filter(x=>x.op==="component.text").length,1);
    assert.equal(app.event({type:1101,phase:2,token:2}),1);
    assert.equal(app.tick(),true);
    assert.equal(app.inspect().value,0);
    assert.equal(app.inspect().totalActions,2);
    assert.equal(operations.filter(x=>x.op==="frame.state").length,3);
  }
});
test("queued-events-never-mutate-in-F6",()=>{
  const {operations,bridge}=mockCalls();
  const s=context(bridge);
  vm.runInContext(example,s);
  s.PocketApplication.start({height:600});
  const n=operations.length;
  for(let i=0;i<20;i++) assert.equal(s.PocketApplication.event({type:1101,phase:2,token:1}),1);
  assert.equal(operations.length,n);
  s.PocketApplication.tick();
  assert.equal(s.PocketApplication.inspect().value,20);
});
test("invalid-events-rejected",()=>{
  const {bridge}=mockCalls();
  const s=context(bridge);
  vm.runInContext(example,s);
  s.PocketApplication.start({height:600});
  for(const e of [null,{}, {type:1101,phase:1,token:1},
    {type:1101,phase:2,token:0},{type:1101,phase:2,token:4096},
    {type:1101,phase:2,token:"1"}, {type:12,phase:2,token:1}])
    assert.equal(s.PocketApplication.event(e),0);
  assert.equal(s.PocketApplication.inspect().value,0);
  assert.throws(()=>{for(let i=0;i<33;i++)s.PocketApplication.event({type:1101,phase:2,token:1});},
                /POCKET_UI_ACTION_OVERFLOW/);
});
test("no-raw-operation-capability-escape",()=>{
  const {operations,bridge}=mockCalls();
  const s=context(bridge);
  vm.runInContext(example,s);
  s.PocketApplication.start({height:600});
  assert.equal(typeof s.PocketUI["__pocketCall"],"undefined");
  assert.equal(typeof s.PocketUI.raw,"undefined");
  assert.throws(()=>s.PocketUI.define({start(){},action(){}}),/ALREADY_DEFINED/);
  assert.throws(()=>s.PocketApplication.start({height:480}),/INVALID_TARGET/);
  assert.equal(operations.some(x=>x.op.startsWith("native.")||x.op.startsWith("ipc.")||x.op.startsWith("keyboard.")),false);
});
test("fail-closed-input-and-command-denial",()=>{
  const calls=mockCalls();
  const s=context(calls.bridge);
  vm.runInContext(example,s);
  s.PocketApplication.start({height:800});
  const ui=s.PocketUI;
  assert.throws(()=>vm.runInContext("PocketUI.define(null)",s),/ALREADY_DEFINED/);
  const prior=calls.operations.length;
  vm.runInContext("globalThis._ctx = null",s);
  assert.equal(prior,calls.operations.length);
  const unavailable=context((op)=>op==="component.create"?123:0);
  assert.throws(()=>vm.runInContext(example+"; PocketApplication.start({height:600})",unavailable),
                /POCKET_UI_HOST_REJECTED/);
});
test("missing-host-and-duplicate-sdk",()=>{
  assert.throws(()=>context(undefined),/POCKET_UI_HOST_REQUIRED/);
  const s=context(()=>1);
  assert.throws(()=>vm.runInContext(sdk,s,{timeout:1000}),/POCKET_UI_DUPLICATE_SDK/);
});
process.stdout.write(process.exitCode?"F7_SDK_FAILURE\n":"F7_SDK_UNIT_OK\n");
