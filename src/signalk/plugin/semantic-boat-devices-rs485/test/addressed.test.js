'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { AddressedBus } = require('../src/addressed-bus');
const { SerialLink } = require('../src/serial-link');
const { Catalog } = require('../../semantic-boat-devices/src/catalog');
const { validateOptions } = require('../src/runtime');

function until(condition) {
  return new Promise((resolve, reject) => {
    const deadline = Date.now() + 5000;
    const timer = setInterval(() => {
      if (condition()) { clearInterval(timer); resolve(); }
      else if (Date.now() > deadline) { clearInterval(timer); reject(new Error('Test condition timed out')); }
    }, 5);
  });
}

test('addressed options accept two displays and reject duplicate addresses or unsafe baud rates', () => {
  const options = { buses: [{ id: 'bus', type: 'rs485-addressed', path: '/dev/fake' }],
    devices: [{ id: 'one', bus: 'bus', address: 1 }, { id: 'two', bus: 'bus', address: 2 }] };
  assert.equal(validateOptions(structuredClone(options)).devices.length, 2);
  const duplicate = structuredClone(options); duplicate.devices[1].address = 1;
  assert.throws(() => validateOptions(duplicate), /addresses must be unique/);
  const slow = structuredClone(options); slow.buses[0].baudRate = 9600;
  assert.throws(() => validateOptions(slow), /115200/);
});

test('one serial master isolates bindings and transfers, replays retries and survives an absent display', async context => {
  const protocol = await import('../../../../tools/disp-sim/src/protocol.js');
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'boat-addressed-'));
  const catalog = new Catalog(directory); await catalog.init();
  for (const name of ['one', 'two']) await catalog.save(`${name}.json`, { schema: 'semantic-display/v1', name,
    sections: [{ id: name, pages: [{ id: 'page', grid: { columns: 1, rows: 1 }, widgets: [] }] }] });
  const states = new Map([1, 2].map(address => [address, { queue: [], applied: [], online: true }]));
  const subscription = address => {
    const provider = Buffer.from('signalk'); const sourcePath = Buffer.from(`test.device${address}`);
    const header = Buffer.alloc(9); header.writeUInt32LE(100, 2);
    header[7] = provider.length; header[8] = sourcePath.length;
    return Buffer.concat([header, provider, sourcePath]);
  };
  class FakePort extends EventEmitter {
    isOpen = false;
    writes = [];
    open(callback) { this.isOpen = true; this.emit('open'); callback(null); }
    drain(callback) { callback(null); }
    close(callback) { this.isOpen = false; this.emit('close'); callback?.(null); }
    write(frame, callback) {
      this.writes.push(frame);
      this.emit('data', frame);
      callback(null);
      const message = new protocol.FrameDecoder(2).push(frame)[0];
      const state = states.get(message.flags);
      if (!state?.online) return;
      let reply;
      if (state.sequence === message.sequence && state.type === message.type && state.payload.equals(message.payload)) reply = state.reply;
      else {
        state.applied.push(message);
        if (message.type === protocol.MessageType.HELLO) {
          const begin = Buffer.alloc(4); begin.writeUInt16LE(1, 2);
          state.queue = [
            { type: protocol.MessageType.HELLO, payload: protocol.helloPayload(1, `display-${message.flags}`) },
            { type: protocol.MessageType.SUBSCRIPTIONS_BEGIN, payload: begin },
            { type: protocol.MessageType.SUBSCRIBE, payload: subscription(message.flags) },
            { type: protocol.MessageType.SUBSCRIPTIONS_END, payload: Buffer.alloc(0) }
          ];
        }
        const response = state.queue.shift() ?? { type: protocol.MessageType.BUS_IDLE, payload: Buffer.alloc(0) };
        reply = protocol.encodeFrame(response.type, message.sequence, response.payload, message.flags, 2, true);
        Object.assign(state, { sequence: message.sequence, type: message.type, payload: message.payload, reply });
      }
      if (state.dropReply && (!state.dropType || state.dropType === message.type)) { state.dropReply = false; return; }
      setImmediate(() => { this.emit('data', reply.subarray(0, 3)); this.emit('data', reply.subarray(3)); });
    }
  }
  const bindings = []; let unsubscribed = 0; const errors = [];
  const app = { getSelfPath: source => ({ value: source === 'test.device1' ? 12.5 : 99 }),
    subscriptionmanager: { subscribe(parameters, unsubscribes) { bindings.push(parameters.subscribe[0].path); unsubscribes.push(() => { unsubscribed += 1; }); } } };
  const bus = { id: 'shared', type: 'rs485-addressed', path: '/dev/fake', baudRate: 115200 };
  const master = new AddressedBus({ bus, protocol, onError: error => errors.push(error), Port: FakePort, gapMs: 1, responseTimeoutMs: 15 });
  const links = [1, 2].map(address => {
    const port = master.devicePort(address);
    return new SerialLink({ app, bus, device: { id: `display-${address}`, address }, protocol, catalog,
      mappings: {}, onError: error => errors.push(error), Port: function () { return port; } });
  });
  context.after(async () => { await Promise.all(links.map(link => link.stop())); await master.stop(); await fs.rm(directory, { recursive: true, force: true }); });
  for (const link of links) link.start(); master.start();
  await until(() => links.every(link => link.session.ready && link.session.valuesSent > 0));
  assert.deepEqual(bindings.sort(), ['test.device1', 'test.device2']);
  for (const address of [1, 2]) {
    const values = states.get(address).applied.filter(message => message.type === protocol.MessageType.VALUE_NUMBER);
    assert.equal(values[0].payload.readFloatLE(2), address === 1 ? 12.5 : 99);
  }
  states.get(1).dropReply = true;
  states.get(1).dropType = protocol.MessageType.PING;
  const marker = Buffer.from([9, 8, 7, 6]);
  await links[0].send(protocol.MessageType.PING, marker);
  assert.equal(states.get(1).applied.filter(message => message.type === protocol.MessageType.PING && message.payload.equals(marker)).length, 1);
  assert.ok(master.devices[0].timeouts >= 1);
  states.get(2).dropReply = true; states.get(2).dropType = protocol.MessageType.CONFIG_END;
  await Promise.all([links[0].install('one.json'), links[1].install('two.json')]);
  assert.equal(links[1].lastTransfer.state, 'sent-awaiting-device-restart');
  for (const address of [1, 2]) {
    const frames = states.get(address).applied;
    const begin = frames.find(message => message.type === protocol.MessageType.CONFIG_BEGIN);
    assert.equal(begin.payload.subarray(11).toString(), address === 1 ? 'one.json' : 'two.json');
    assert.equal(frames.filter(message => message.type === protocol.MessageType.CONFIG_END).length, 1);
  }
  const firstReplies = master.devices[0].receivedFrames;
  states.get(2).online = false;
  links[1].session.lastSeen = Date.now() - 13000;
  await until(() => !links[1].session.ready && master.devices[0].receivedFrames >= firstReplies + 5);
  assert.equal(links[0].session.ready, true);
  assert.ok(unsubscribed >= 1);
  assert.equal(master.decoder.errors, 0);
  assert.equal(master.unexpectedReplies, 0);
  assert.equal(errors.length, 0);
  states.get(2).online = true;
  master.port.close();
  assert.equal(links[0].session.ready, false);
  assert.equal(links[1].session.ready, false);
  master.open();
  for (const link of links) link.open();
  await until(() => links.every(link => link.session.ready));
  assert.equal(master.decoder.errors, 0);
});

test('failed shared-port opening releases virtual sessions so plugin stop cannot hang', async () => {
  const protocol = await import('../../../../tools/disp-sim/src/protocol.js');
  class FailedPort extends EventEmitter {
    isOpen = false;
    open(callback) { callback(new Error('USB adapter absent')); }
  }
  const master = new AddressedBus({ bus: { path: '/dev/missing', baudRate: 115200 }, protocol,
    onError() {}, Port: FailedPort });
  const port = master.devicePort(1);
  const link = new SerialLink({ app: {}, bus: { type: 'rs485-addressed' }, device: { address: 1 },
    protocol, catalog: {}, onError() {}, Port: function () { return port; } });
  link.start(); master.start();
  assert.equal(port.opening, false);
  await link.stop(); await master.stop();
  assert.equal(master.running, false);
});