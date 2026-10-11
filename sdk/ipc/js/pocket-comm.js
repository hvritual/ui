/* PocketComm v1. Bundle this file before application.js, then call
 * PocketComm.install(PocketApplication). Hooks stay synchronous. Network work
 * and external callbacks run only in the installed owner tick, never in F6.
 * Four instances share one native session: 32 requests, 16 subscriptions,
 * 32 cached items per watch, 16 outgoing operations per tick. No blocking API.
 * A command needs a caller-owned idempotency key; no accepted command is replayed.
 * The only selector is null or {eq:{key:"label",value:"text"}}. Changing a
 * selector means close the old watch and create a new one with a fresh snapshot.
 */
(function (global) {
    "use strict";
    const requests = [], subscriptions = [], instances = [], controls = [];
    const REQUESTS = 32, SUBSCRIPTIONS = 16, INSTANCES = 4, ITEMS = 32;
    const BEGIN = 256, END = 512, UNKNOWN = 1024;
    let installed = null, pumping = false, recovery = null, subscriptionFirst = false, controlOverflow = false;

    function success(value) { return {ok: true, value: value}; }
    function failure(code, effect) { return {ok: false, error: {code: code, effect: effect || "none"}}; }
    function completedRequest(result) {
        const promise = Promise.resolve(result);
        promise.cancel = function () { return false; };
        return promise;
    }
    function name(value, maximum) {
        return typeof value === "string" && value.length > 0 && value.length <= maximum && /^[A-Za-z0-9_.-]+$/.test(value);
    }
    function bytes(value) {
        let length = 0;
        for (let i = 0; i < value.length; ++i) {
            const c = value.charCodeAt(i);
            if (!c) return Infinity;
            if (c < 128) ++length;
            else if (c < 2048) length += 2;
            else if (c >= 0xd800 && c <= 0xdbff) {
                const next = value.charCodeAt(++i);
                if (!(next >= 0xdc00 && next <= 0xdfff)) return Infinity;
                length += 4;
            } else if (c >= 0xdc00 && c <= 0xdfff) return Infinity;
            else length += 3;
        }
        return length;
    }
    function object(value) { return value !== null && typeof value === "object" && !Array.isArray(value); }
    function options(value, command) {
        const opts = value === undefined || value === null ? {} : value;
        if (!object(opts) || Object.keys(opts).some(k => k !== "timeoutMs" && !(command && k === "idempotencyKey"))) return null;
        const timeout = opts.timeoutMs === undefined ? 2000 : opts.timeoutMs;
        if (!Number.isInteger(timeout) || timeout < 1 || timeout > 120000) return null;
        if (command && !name(opts.idempotencyKey, 63)) return null;
        return {timeout: timeout, key: command ? opts.idempotencyKey : ""};
    }
    function selector(value) {
        if (value === undefined || value === null) return {key: "", value: ""};
        if (!object(value) || Object.keys(value).length !== 1 || !Object.prototype.hasOwnProperty.call(value, "eq") || !object(value.eq)) return null;
        const eq = value.eq;
        if (Object.keys(eq).length !== 2 || !Object.prototype.hasOwnProperty.call(eq, "key") ||
            !Object.prototype.hasOwnProperty.call(eq, "value") || !name(eq.key, 31) ||
            typeof eq.value !== "string" || bytes(eq.value) > 95) return null;
        return {key: eq.key, value: eq.value};
    }
    function remove(list, entry) { const at = list.indexOf(entry); if (at >= 0) list.splice(at, 1); }
    function decimalCompare(a, b) { return a.length !== b.length ? a.length - b.length : (a < b ? -1 : a === b ? 0 : 1); }
    function native(op, args) {
        const result = global.__pocketCall.apply(undefined, [op].concat(args || []));
        return result && typeof result === "object" ? result : {ok: false, code: "PROTOCOL"};
    }
    function diagnostic(owner, code) {
        if (owner._closed) return;
        owner._lastError = failure(code).error;
        ++owner._callbackErrors;
        if (owner._onError) {
            try {
                const returned = owner._onError(owner._lastError);
                if (returned && typeof returned.then === "function")
                    Promise.resolve(returned).catch(() => { if (!owner._closed) owner._lastError = failure("ERROR_HANDLER_ERROR").error; });
            } catch (_) { if (!owner._closed) owner._lastError = failure("ERROR_HANDLER_ERROR").error; }
        }
    }
    function callback(entry, result) {
        if (entry.owner._closed || typeof entry.callback !== "function") return;
        try {
            const returned = entry.callback(result);
            if (returned && typeof returned.then === "function")
                Promise.resolve(returned).catch(() => { if (!entry.closed) diagnostic(entry.owner, "CALLBACK_ERROR"); });
        } catch (_) { diagnostic(entry.owner, "CALLBACK_ERROR"); }
    }
    function control(op, id) {
        if (!id || controls.some(c => c.op === op && c.id === id)) return;
        /* Native may already have timed out a request while its cleanup is
         * queued here. If pressure exceeds the fixed bound, retire the session
         * at this tick's safe point instead of poisoning the Application VM. */
        if (controls.length >= REQUESTS + SUBSCRIPTIONS) { controlOverflow = true; return; }
        controls.push({op: op, id: id});
    }
    function finishRequest(entry, result, cancel) {
        remove(requests, entry);
        if (cancel) control("ipc.cancel", entry.id);
        entry.done = true;
        entry.resolve(result);
    }
    function ready(entry, result) {
        if (!entry.readyDone) { entry.readyDone = true; entry.resolve(result); }
    }
    function finishSubscription(entry, result, notify, unsubscribe) {
        remove(subscriptions, entry);
        entry.closed = true; entry.live = false; entry.cache.clear(); entry.stage = null;
        if (unsubscribe) control("ipc.unsubscribe", entry.id);
        ready(entry, result);
        if (notify) callback(entry, result);
    }
    function commandEffect(entry) { return entry.kind === "command" && entry.id ? "unknown" : "none"; }
    function unavailable(owner) { return owner._limit ? "BUSY" : owner._closed ? "CANCELLED" : null; }
    function request(owner, kind, service, member, payload, opts) {
        if (requests.length >= REQUESTS) return completedRequest(failure("BUSY"));
        const entry = {owner: owner, kind: kind, service: service, member: member, id: null, deadline: null, done: false};
        let promise = new Promise(resolve => { entry.resolve = resolve; });
        promise.cancel = function () {
            if (entry.done || entry.stop) return false;
            entry.stop = "CANCELLED";
            return true;
        };
        entry.stop = unavailable(owner);
        try {
            const config = options(opts, kind === "command");
            if (!config || !name(service, 63) || !name(member, 63)) entry.stop = entry.stop || "INVALID";
            else { entry.timeout = config.timeout; entry.key = config.key; }
            entry.payload = JSON.stringify(payload === undefined ? null : payload);
            if (typeof entry.payload !== "string") entry.stop = entry.stop || "INVALID";
            else if (bytes(entry.payload) > 768) entry.stop = entry.stop || "TOO_LARGE";
        } catch (_) { entry.stop = entry.stop || "INVALID"; }
        requests.push(entry);
        return promise;
    }
    function current(entry) {
        return Array.from(entry.cache.keys()).sort(decimalCompare).map(key => ({key: key, value: JSON.parse(entry.cache.get(key))}));
    }
    function subscription(owner, kind, service, member, filter, fn, opts) {
        const entry = {owner: owner, kind: kind, service: service, member: member, callback: fn,
            id: null, deadline: null, closed: false, live: false, readyDone: false, cache: new Map(), stage: null,
            revision: "0", epoch: "0", cursor: "0"};
        const promise = new Promise(resolve => { entry.resolve = resolve; });
        const handle = {
            ready: promise,
            close: function () {
                if (entry.closed) return false;
                entry.closed = true; entry.stop = "CANCELLED"; entry.cache.clear(); entry.stage = null;
                return true;
            },
            get active() { return !entry.closed && !entry.owner._closed; }
        };
        if (kind === "state") handle.current = function () { return current(entry); };
        entry.stop = unavailable(owner);
        try {
            const config = options(opts, false), chosen = selector(filter);
            if (!config || !chosen || typeof fn !== "function" || !name(service, 63) || !name(member, 63))
                entry.stop = entry.stop || "INVALID";
            else { entry.timeout = config.timeout; entry.filter = chosen; }
        } catch (_) { entry.stop = entry.stop || "INVALID"; }
        if (subscriptions.length >= SUBSCRIPTIONS) {
            entry.closed = true; ready(entry, failure("BUSY"));
        } else subscriptions.push(entry);
        return Object.freeze(handle);
    }
    function parse(text) {
        try { return success(JSON.parse(text)); } catch (_) { return failure("BAD_PAYLOAD"); }
    }
    function stateBody(entry, type, message) {
        const value = {type: type, items: current(entry), revision: entry.revision, serviceEpoch: entry.epoch};
        if (message && message.cursor !== "0") value.key = message.cursor;
        if (type === "upsert") value.value = JSON.parse(entry.cache.get(message.cursor));
        return value;
    }
    function stateMessage(entry, message, now) {
        const stop = code => finishSubscription(entry, failure(code), true, true);
        if (message.kind === 15) {
            /* Every v1 RESET terminates the native watch, including status OK.
             * Recovery explicitly creates a new subscription and snapshot. */
            entry.revision = message.revision; entry.epoch = message.serviceEpoch;
            finishSubscription(entry, failure("STALE"), false, false);
            callback(entry, success(stateBody(entry, "reset")));
            return;
        }
        if (entry.epoch !== "0" && entry.epoch !== message.serviceEpoch) { stop("STALE"); return; }
        entry.epoch = message.serviceEpoch;
        if (message.kind === 12) {
            if (message.flags & BEGIN) {
                entry.stage = new Map(); entry.stageRevision = message.revision;
                entry.live = false;
                if (!entry.deadline) entry.deadline = now + entry.timeout;
            }
            if (!entry.stage || entry.stageRevision !== message.revision) { stop("PROTOCOL"); return; }
            if (message.cursor !== "0") {
                const result = parse(message.payload);
                if (!result.ok) { stop(result.error.code); return; }
                if (!entry.stage.has(message.cursor) && entry.stage.size >= ITEMS) { stop("TOO_LARGE"); return; }
                entry.stage.set(message.cursor, message.payload);
            }
            if (message.flags & END) {
                /* Replace the entire membership atomically. This purges items
                 * that disappeared or ceased matching; it never merges a reset. */
                entry.cache = entry.stage; entry.stage = null; entry.revision = message.revision;
                entry.live = true; entry.deadline = 0;
                const result = success(stateBody(entry, "snapshot"));
                ready(entry, result); callback(entry, result);
            }
            return;
        }
        if (message.kind !== 13 && message.kind !== 14) { stop("PROTOCOL"); return; }
        if (!entry.live || entry.stage || message.cursor === "0") { stop("STALE"); return; }
        if (decimalCompare(message.revision, entry.revision) <= 0) return;
        if (message.kind === 13) {
            const result = parse(message.payload);
            if (!result.ok) { stop(result.error.code); return; }
            if (!entry.cache.has(message.cursor) && entry.cache.size >= ITEMS) { stop("TOO_LARGE"); return; }
            entry.cache.set(message.cursor, message.payload);
        } else entry.cache.delete(message.cursor); // includes matching -> nonmatching
        entry.revision = message.revision;
        callback(entry, success(stateBody(entry, message.kind === 13 ? "upsert" : "remove", message)));
    }
    function messageReceived(message, now) {
        const sameRoute = entry => entry.id === message.id && entry.brokerEpoch === message.brokerEpoch && entry.connectionEpoch === message.connectionEpoch;
        const entry = requests.find(sameRoute);
        if (entry) {
            if (entry.stop) return;
            if (message.code !== "OK" || message.kind === 18 || message.kind === 19) {
                finishRequest(entry, failure(message.code === "OK" ? "UNAVAILABLE" : message.code,
                    message.flags & UNKNOWN ? "unknown" : "none"), false);
            } else if (message.kind === 6) {
                const parsed = parse(message.payload);
                finishRequest(entry, parsed.ok ? parsed : failure(parsed.error.code, commandEffect(entry)), false);
            } else finishRequest(entry, failure("PROTOCOL", commandEffect(entry)), true);
            return;
        }
        const sub = subscriptions.find(sameRoute);
        if (!sub || sub.stop || sub.closed) return;
        if (message.code !== "OK" || message.kind === 18 || message.kind === 19) {
            const nativeTerminal = message.kind === 18 || message.kind === 19 || message.kind === 15;
            finishSubscription(sub, failure(message.code === "OK" ? "UNAVAILABLE" : message.code), true, !nativeTerminal);
        } else if (message.kind === 17) {
            sub.epoch = message.serviceEpoch;
            if (sub.kind === "events") { sub.live = true; sub.deadline = 0; ready(sub, success({subscriptionId: sub.id})); }
        } else if (sub.kind === "state") stateMessage(sub, message, now);
        else if (message.kind === 9) {
            if (sub.epoch !== "0" && sub.epoch !== message.serviceEpoch) {
                finishSubscription(sub, failure("STALE"), true, true); return;
            }
            if (decimalCompare(message.cursor, sub.cursor) <= 0) return;
            sub.cursor = message.cursor; sub.epoch = message.serviceEpoch;
            const parsed = parse(message.payload);
            callback(sub, parsed.ok ? success({value: parsed.value, cursor: message.cursor, serviceEpoch: message.serviceEpoch}) : parsed);
        } else finishSubscription(sub, failure("PROTOCOL"), true, true);
    }
    function expire(now) {
        for (const entry of requests.slice()) {
            if (entry.deadline === null && !entry.stop) entry.deadline = now + entry.timeout;
            if (entry.stop || (entry.deadline !== null && now >= entry.deadline))
                finishRequest(entry, failure(entry.stop || "TIMEOUT", commandEffect(entry)), true);
        }
        for (const entry of subscriptions.slice()) {
            if (entry.deadline === null && !entry.stop) entry.deadline = now + entry.timeout;
            if (entry.stop || (entry.deadline && now >= entry.deadline))
                finishSubscription(entry, failure(entry.stop || "TIMEOUT"), !entry.closed, true);
        }
    }
    function recoverControlPressure() {
        if (!controlOverflow) return;
        for (const entry of requests.slice()) finishRequest(entry, failure("BUSY", commandEffect(entry)), false);
        for (const entry of subscriptions.slice()) finishSubscription(entry, failure("BUSY"), !entry.closed, false);
        controls.length = 0; controlOverflow = false;
        /* This aborts cleanup pressure. It does not replay accepted commands
         * or restore subscriptions. Any newly queued intention still handshakes. */
        native("ipc.reconnect");
    }
    function pump(now) {
        if (pumping) throw new Error("IPC_REENTRANT_TICK");
        pumping = true;
        try {
            if (recovery) {
                for (const entry of requests.slice()) finishRequest(entry, failure("DISCONNECTED", commandEffect(entry)), false);
                for (const entry of subscriptions.slice()) finishSubscription(entry, failure("DISCONNECTED"), !entry.closed, false);
                controls.length = 0; controlOverflow = false;
                const pending = recovery; recovery = null;
                const result = native("ipc.reconnect");
                pending.resolve(result.ok || result.code === "AGAIN" ? success({ready: false}) : failure(result.code));
            }
            expire(now);
            recoverControlPressure();
            for (const entry of controls.slice()) {
                const result = native(entry.op, [entry.id]);
                if (result.code !== "BUSY" && result.code !== "AGAIN") remove(controls, entry);
            }
            const batch = native("ipc.take");
            if (batch.ok && Array.isArray(batch.messages)) {
                for (const message of batch.messages) messageReceived(message, now);
            }
            recoverControlPressure();
            let budget = 16;
            const outgoing = subscriptionFirst ? subscriptions.concat(requests) : requests.concat(subscriptions);
            subscriptionFirst = !subscriptionFirst;
            for (const entry of outgoing) {
                if (entry.id || entry.stop || entry.closed || !budget) continue;
                if (entry.deadline === null) entry.deadline = now + entry.timeout;
                const remaining = entry.deadline - now;
                if (remaining <= 0) continue;
                --budget;
                let result;
                if (entry.kind === "query" || entry.kind === "command") {
                    const args = [entry.service, entry.member, entry.payload, remaining];
                    if (entry.kind === "command") args.push(entry.key);
                    result = native("ipc." + entry.kind, args);
                } else result = native("ipc." + entry.kind, [entry.service, entry.member, entry.filter.key, entry.filter.value, remaining]);
                if (result.ok) { entry.id = result.id; entry.brokerEpoch = result.brokerEpoch; entry.connectionEpoch = result.connectionEpoch; }
                else if (result.code !== "AGAIN") {
                    if (entry.kind === "query" || entry.kind === "command") finishRequest(entry, failure(result.code), false);
                    else finishSubscription(entry, failure(result.code), true, false);
                }
            }
            expire(now);
            recoverControlPressure();
        } finally { pumping = false; }
    }
    class PocketComm {
        constructor(opts) {
            this._closed = false; this._lastError = null; this._callbackErrors = 0; this._onError = null;
            this._limit = instances.length >= INSTANCES;
            if (opts !== undefined) {
                if (!object(opts) || Object.keys(opts).some(k => k !== "onError") ||
                    (opts.onError !== undefined && typeof opts.onError !== "function")) this._closed = true;
                else this._onError = opts.onError || null;
            }
            if (!this._limit && !this._closed) instances.push(this);
            this.query = Object.freeze({call: (service, member, payload, opts) => request(this, "query", service, member, payload, opts)});
            this.command = Object.freeze({execute: (service, member, payload, opts) => request(this, "command", service, member, payload, opts)});
            this.events = Object.freeze({subscribe: (service, member, filter, fn, opts) => subscription(this, "events", service, member, filter, fn, opts)});
            this.states = Object.freeze({watch: (service, member, filter, fn, opts) => subscription(this, "state", service, member, filter, fn, opts)});
        }
        get lastError() { return this._lastError && {code: this._lastError.code, effect: this._lastError.effect}; }
        get callbackErrors() { return this._callbackErrors; }
        close() {
            if (this._closed) return false;
            this._closed = true; remove(instances, this);
            for (const entry of requests) if (entry.owner === this) entry.stop = "CANCELLED";
            for (const entry of subscriptions) if (entry.owner === this) {
                entry.closed = true; entry.stop = "CANCELLED"; entry.cache.clear(); entry.stage = null;
            }
            return true;
        }
        static install(application) {
            if (installed === application) return application;
            if (installed || !application || typeof application.tick !== "function") throw new Error("IPC_INSTALL_INVALID");
            const original = application.tick;
            application.tick = function (input) { pump(input.time); return original.call(this, input); };
            installed = application;
            return application;
        }
        /* Recovery affects the one application session and all instances. It
         * invalidates old work; callers explicitly create new subscriptions. */
        static reconnect() {
            if (recovery) return Promise.resolve(failure("BUSY"));
            return new Promise(resolve => { recovery = {resolve: resolve}; });
        }
    }
    Object.defineProperty(global, "PocketComm", {value: PocketComm, enumerable: true});
})(globalThis);
