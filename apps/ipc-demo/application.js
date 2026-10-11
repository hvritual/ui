/* Opt-in developer example. Its provider is an independent process; this
 * package contains no native code, device access or service implementation.
 * The acceptance runner changes service/member names in a second package
 * while keeping exactly the same broker and production UI executable. */
(function () {
  'use strict';
  const service = 'demo.counter.v1';
  const members = {query: 'get', command: 'add', event: 'changed', state: 'counter'};
  const comm = new PocketComm();
  const call = __pocketCall;
  let label, stage = 0, value = 0, completed = 0, failure = 0, started = false;
  let workflowDone = false, events = 0, snapshots = 0, enters = 0, removes = 0;
  let eventHandle, stateHandle;
  const odd = {eq: {key: 'parity', value: 'odd'}};

  function expect(condition, code) {
    if (!condition) { failure = code; throw new Error('IPC example assertion'); }
  }
  async function scenario() {
    eventHandle = comm.events.subscribe(service, members.event, odd, result => {
      if (!result.ok) { failure = 81; return; }
      expect(result.value.value.value === 1, 82);
      events++;
    });
    stateHandle = comm.states.watch(service, members.state, odd, result => {
      if (!result.ok) { failure = 83; return; }
      const update = result.value;
      if (update.type === 'snapshot') {
        expect(update.items.length === 0, 84);
        snapshots++;
      } else if (update.type === 'upsert') {
        expect(update.value.value === 1, 85);
        enters++;
      } else if (update.type === 'remove') {
        expect(update.key === '1', 86);
        removes++;
      } else if (update.type === 'reset') {
        failure = 87;
      }
    });
    expect((await eventHandle.ready).ok, 71);
    expect((await stateHandle.ready).ok, 72);
    let result = await comm.query.call(service, members.query, {});
    expect(result.ok && result.value.value === 0 && result.value.operations === 0, 73);
    stage = 1;
    const key = {idempotencyKey: 'example-add-one', timeoutMs: 2000};
    result = await comm.command.execute(service, members.command, {delta: 1}, key);
    expect(result.ok && result.value.value === 1 && result.value.operationId === 1, 74);
    value = 1;
    const duplicate = await comm.command.execute(service, members.command, {delta: 1}, key);
    expect(duplicate.ok && duplicate.value.value === 1 && duplicate.value.operationId === 1, 75);
    const conflict = await comm.command.execute(service, members.command, {delta: 2}, key);
    expect(!conflict.ok && conflict.error.code === 'REJECTED', 76);
    result = await comm.query.call(service, members.query, {});
    expect(result.ok && result.value.value === 1 && result.value.operations === 1, 77);
    stage = 2;
    result = await comm.command.execute(service, members.command, {delta: 1},
      {idempotencyKey: 'example-add-two', timeoutMs: 2000});
    expect(result.ok && result.value.value === 2 && result.value.operationId === 2, 78);
    result = await comm.query.call(service, members.query, {});
    expect(result.ok && result.value.value === 2 && result.value.operations === 2 && result.value.eventFailures === 0, 79);
    value = 2;
    workflowDone = true;
  }

  function publish() {
    // Native view mutation occurs only during the synchronous application tick.
    // The Promise continuation and subscription callbacks update this JS model.
    call('component.text', label, 5200 + (failure || (completed ? 2 : value)));
    call('frame.state', failure || stage, value, events, 0, 0, value,
      completed, enters + removes + snapshots, failure, 0, 0, 0xfffaf8f4, 1);
  }
  globalThis.PocketApplication = {
    start(config) {
      call('theme.define', 1, 2, 22, 1, 0xfffaf8f4, 2, 0xff332b26, 3, 0xfff0dfc8);
      call('style.rule', 1, 1, 2, 0);
      call('style.rule', 2, 3, 2, 12);
      call('theme.select', 1);
      const root = call('component.create', 1, 0, 0, 0, 1024, config.height, 1, 0, 0);
      call('component.create', 2, root, 64, 48, 860, 44, 1, 5001, 0);
      call('component.create', 2, root, 64, 124, 860, 44, 1, 5003, 0);
      label = call('component.create', 2, root, 64, 216, 220, 64, 2, 5200, 0);
      call('navigation.push', 1, root);
      publish();
      return true;
    },
    event() { return 0; },
    tick() {
      if (!started) {
        started = true;
        scenario().catch(() => { if (!failure) failure = 99; });
      }
      if (workflowDone && !failure && snapshots === 1 && enters === 1 && removes === 1 && events === 1) {
        completed = 1;
        stage = 3;
      }
      publish();
      return true;
    },
    inspect() { return {stage, value, completed, failure, events, snapshots, enters, removes}; }
  };
})();
