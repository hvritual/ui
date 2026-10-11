#!/usr/bin/env node
"use strict";
/* SDK semantic unit tests. This controlled native fixture does not count as
 * PocketJS/runtime/transport acceptance; those gates use the real C client. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../../sdk/ipc/js/pocket-comm.js"), "utf8");
const plain = value => JSON.parse(JSON.stringify(value));
let cases = 0;
function harness() {
    const h = {ready: true, epoch: "1", next: 0, calls: [], messages: [], deny: null};
    const accepted = id => ({ok: true, code: "OK", id: String(id), brokerEpoch: h.epoch, connectionEpoch: h.epoch});
    h.context = vm.createContext({__pocketCall(op, ...args) {
        h.calls.push({op, args});
        if (op === "ipc.take") return {ok: true, ready: h.ready, code: "OK", messages: h.messages.splice(0, 16)};
        if (op === "ipc.reconnect") { h.epoch = String(Number(h.epoch) + 1); h.next = 0; return accepted(0); }
        if (op === "ipc.cancel" || op === "ipc.unsubscribe") return accepted(args[0]);
        if (h.deny) return {ok: false, code: h.deny};
        if (!h.ready) return {ok: false, code: "AGAIN"};
        return accepted(++h.next);
    }});
    vm.runInContext(source, h.context, {filename: "pocket-comm.js"});
    h.Comm = h.context.PocketComm;
    h.app = {tick() { return true; }};
    h.Comm.install(h.app);
    h.tick = async time => { assert.equal(h.app.tick({time}), true); await Promise.resolve(); await Promise.resolve(); };
    h.push = (id, kind, payload = "null", extra = {}) => h.messages.push({id: String(id), kind, payload,
        flags: 0, code: "OK", brokerEpoch: h.epoch, connectionEpoch: h.epoch,
        serviceEpoch: "8", revision: "0", cursor: "0", ...extra});
    h.sent = op => h.calls.filter(c => c.op === "ipc." + op);
    return h;
}
async function run(name, fn) { await fn(); ++cases; process.stdout.write("SDK_CASE_OK " + name + "\n"); }
(async () => {
    await run("queued-query-and-opaque-json", async () => {
        const h = harness(), comm = new h.Comm(); h.ready = false;
        let received;
        comm.query.call("demo.counter.v1", "get", {text: "汉字\\\"\n"}).then(r => { received = r; });
        assert.equal(h.calls.length, 0);
        await h.tick(100); assert.equal(received, undefined);
        h.ready = true; await h.tick(101);
        assert.deepEqual(JSON.parse(h.sent("query")[1].args[2]), {text: "汉字\\\"\n"});
        h.push(1, 6, '{"value":7}'); assert.equal(received, undefined);
        await h.tick(102); assert.deepEqual(plain(received), {ok: true, value: {value: 7}});
    });
    await run("invalid-options-and-idempotency-key", async () => {
        const h = harness(), comm = new h.Comm();
        const missing = comm.command.execute("demo", "add", {delta: 1});
        const unsupported = comm.query.call("demo", "get", null, {idempotencyKey: "wrong"});
        const huge = comm.query.call("demo", "get", {text: "汉".repeat(300)});
        await h.tick(0);
        assert.equal((await missing).error.code, "INVALID");
        assert.equal((await unsupported).error.code, "INVALID");
        assert.equal((await huge).error.code, "TOO_LARGE");
        assert.equal(h.sent("command").length, 0);
        const valid = comm.command.execute("demo", "add", {delta: 1}, {idempotencyKey: "operation-1", timeoutMs: 10});
        await h.tick(1);
        assert.equal(h.sent("command")[0].args[4], "operation-1");
        assert.equal(h.sent("command")[0].args[2], '{"delta":1}');
        h.push(1, 6, '{"value":1}'); await h.tick(2); assert.equal((await valid).ok, true);
    });
    await run("handshake-timeout-covers-unaccepted-work", async () => {
        const h = harness(), comm = new h.Comm(); h.ready = false;
        const pending = comm.query.call("demo", "get", null, {timeoutMs: 5});
        await h.tick(1000); await h.tick(1004); await h.tick(1005);
        assert.deepEqual(plain(await pending), {ok: false, error: {code: "TIMEOUT", effect: "none"}});
        assert.equal(h.sent("cancel").length, 0);
    });
    await run("accepted-command-timeout-is-unknown-and-not-replayed", async () => {
        const h = harness(), comm = new h.Comm();
        const pending = comm.command.execute("demo", "add", null, {timeoutMs: 2, idempotencyKey: "one"});
        await h.tick(10); await h.tick(12); await h.tick(13);
        assert.deepEqual(plain(await pending), {ok: false, error: {code: "TIMEOUT", effect: "unknown"}});
        assert.equal(h.sent("command").length, 1); assert.equal(h.sent("cancel").length, 1);
    });
    await run("recoverable-native-denial-and-malformed-payload", async () => {
        const h = harness(), comm = new h.Comm(); h.deny = "UNAUTHORIZED";
        const denied = comm.query.call("demo", "get"); await h.tick(0);
        assert.equal((await denied).error.code, "UNAUTHORIZED");
        h.deny = null;
        const bad = comm.query.call("demo", "get"); await h.tick(1);
        h.push(1, 6, '"]};globalThis.exploited=true;//'); await h.tick(2);
        assert.equal((await bad).error.code, "BAD_PAYLOAD"); assert.equal(h.context.exploited, undefined);
        const good = comm.query.call("demo", "get"); await h.tick(3);
        h.push(2, 6, "42"); await h.tick(4); assert.equal((await good).value, 42);
    });
    await run("events-ready-callback-diagnostics-and-close", async () => {
        const h = harness(); let calls = 0, diagnostic;
        const comm = new h.Comm({onError(error) { diagnostic = error; }});
        const sub = comm.events.subscribe("demo", "changed", {eq: {key: "parity", value: "odd"}}, r => {
            assert.equal(r.value.value.value, 1); ++calls; throw new Error("deliberate application callback exception");
        });
        await h.tick(0); h.push(1, 17); await h.tick(1); assert.equal((await sub.ready).ok, true);
        h.push(1, 9, '{"value":1}', {cursor: "9007199254740993"}); await h.tick(2);
        assert.equal(calls, 1); assert.equal(comm.lastError.code, "CALLBACK_ERROR"); assert.equal(diagnostic.code, "CALLBACK_ERROR");
        sub.close(); h.push(1, 9, '{"value":1}', {cursor: "9007199254740994"}); await h.tick(3);
        assert.equal(calls, 1); assert.equal(sub.active, false); assert.equal(h.sent("unsubscribe").length, 1);
    });
    await run("unsupported-filters-fail-before-native", async () => {
        const h = harness(), comm = new h.Comm();
        const invalid = [{in: {key: "status", values: ["new"]}}, {eq: {key: "status", value: "new", second: "x"}},
            {eq: {key: "status", value: "new"}, second: {}}, Object.assign(Object.create({eq: {key: "a", value: "b"}}), {foo: 1})];
        const handles = invalid.map(filter => comm.states.watch("demo", "counter", filter, () => {}));
        await h.tick(0);
        for (const handle of handles) assert.equal((await handle.ready).error.code, "INVALID");
        assert.equal(h.sent("state").length, 0);
    });
    await run("state-snapshot-membership-removal-and-copy", async () => {
        const h = harness(), comm = new h.Comm(), updates = [];
        const sub = comm.states.watch("demo", "counter", {eq: {key: "parity", value: "odd"}}, r => updates.push(plain(r)));
        await h.tick(0);
        h.push(1, 17); h.push(1, 12, "", {flags: 256}); h.push(1, 12, "", {flags: 512}); await h.tick(1);
        assert.equal((await sub.ready).value.type, "snapshot"); assert.deepEqual(plain(sub.current()), []);
        const key = "9007199254740993";
        h.push(1, 13, '{"value":1}', {cursor: key, revision: "1"}); await h.tick(2);
        assert.deepEqual(plain(sub.current()), [{key, value: {value: 1}}]);
        const copy = sub.current(); copy[0].value.value = 999;
        assert.equal(sub.current()[0].value.value, 1);
        h.push(1, 14, "null", {cursor: key, revision: "2"}); await h.tick(3);
        assert.deepEqual(plain(sub.current()), []); assert.equal(updates.at(-1).value.type, "remove");
        h.push(1, 13, '{"value":3}', {cursor: key, revision: "3"}); await h.tick(4);
        h.push(1, 12, "", {flags: 256, revision: "4"}); await h.tick(5);
        assert.equal(sub.current().length, 1); // old snapshot remains atomic until END
        h.push(1, 12, "", {flags: 512, revision: "4"}); await h.tick(6);
        assert.deepEqual(plain(sub.current()), []); assert.equal(updates.at(-1).value.type, "snapshot");
    });
    await run("state-reset-invalidates-and-incomplete-snapshot-times-out", async () => {
        const h = harness(), comm = new h.Comm(), updates = [];
        const sub = comm.states.watch("demo", "counter", null, r => updates.push(plain(r)), {timeoutMs: 4});
        await h.tick(0); h.push(1, 17); h.push(1, 12, "", {flags: 256}); await h.tick(1);
        await h.tick(4); assert.equal((await sub.ready).error.code, "TIMEOUT"); assert.equal(sub.active, false);
        const second = comm.states.watch("demo", "counter", null, r => updates.push(plain(r)), {timeoutMs: 4});
        await h.tick(5); h.push(2, 12, "", {flags: 256}); h.push(2, 12, "1", {cursor: "1"}); h.push(2, 12, "", {flags: 512}); await h.tick(6);
        assert.equal(second.current().length, 1);
        h.push(2, 15); await h.tick(7); assert.deepEqual(plain(second.current()), []); assert.equal(updates.at(-1).value.type, "reset");
        assert.equal(second.active, false);
        await h.tick(11); assert.equal(updates.at(-1).value.type, "reset");
        const pending = comm.states.watch("demo", "counter", null, () => {});
        await h.tick(12); h.push(3, 15); await h.tick(13);
        assert.equal((await pending.ready).error.code, "STALE"); assert.equal(pending.active, false);
    });
    await run("state-cache-is-bounded", async () => {
        const h = harness(), comm = new h.Comm(); let failure;
        const sub = comm.states.watch("demo", "counter", null, r => { if (!r.ok) failure = r; });
        await h.tick(0); h.push(1, 12, "", {flags: 256});
        for (let i = 1; i <= 33; ++i) h.push(1, 12, String(i), {cursor: String(i)});
        h.push(1, 12, "", {flags: 512});
        await h.tick(1); await h.tick(2); await h.tick(3);
        assert.equal((await sub.ready).error.code, "TOO_LARGE"); assert.equal(failure.error.code, "TOO_LARGE");
        assert.deepEqual(plain(sub.current()), []);
    });
    await run("explicit-recovery-invalidates-and-rejects-old-route", async () => {
        const h = harness(), comm = new h.Comm();
        const old = comm.command.execute("demo", "add", null, {idempotencyKey: "one"}); await h.tick(0);
        const reconnect = h.Comm.reconnect(); await h.tick(1);
        assert.equal((await old).error.effect, "unknown"); assert.equal((await reconnect).ok, true);
        let received;
        comm.query.call("demo", "get").then(r => { received = r; }); await h.tick(2);
        h.push(1, 6, "99", {brokerEpoch: "1", connectionEpoch: "1"}); await h.tick(3);
        assert.equal(received, undefined);
        h.push(1, 6, "2"); await h.tick(4); assert.equal(received.value, 2);
    });
    await run("request-cancel-and-instance-close-are-deferred", async () => {
        const h = harness(), comm = new h.Comm();
        const queued = comm.query.call("demo", "get"); assert.equal(queued.cancel(), true);
        const command = comm.command.execute("demo", "add", null, {idempotencyKey: "one"}); await h.tick(0);
        assert.equal((await queued).error.effect, "none");
        const before = h.calls.length; comm.close(); assert.equal(h.calls.length, before);
        await h.tick(1); assert.equal((await command).error.effect, "unknown"); assert.equal(comm.close(), false);
    });
    await run("bounded-admission-and-query-subscription-fairness", async () => {
        const h = harness(), comm = new h.Comm();
        for (let i = 0; i < 32; ++i) comm.query.call("demo", "get");
        assert.equal((await comm.query.call("demo", "get")).error.code, "BUSY");
        comm.events.subscribe("demo", "changed", null, () => {});
        await h.tick(0); assert.equal(h.sent("events").length, 0);
        await h.tick(1); assert.equal(h.sent("events").length, 1);
        new h.Comm(); new h.Comm(); new h.Comm();
        const overflow = new h.Comm();
        assert.equal((await overflow.query.call("demo", "get")).error.code, "BUSY");
    });
    process.stdout.write("IPC_JS_SDK_OK cases=" + cases + " runtime_integration=false\n");
})().catch(error => { console.error(error); process.exitCode = 1; });
