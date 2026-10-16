'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const { SerialLink } = require('../src/serial-link');

class FakePort extends EventEmitter {
  isOpen = false;
  writes = [];
  open(callback) { this.isOpen = true; this.emit('open'); callback(null); }
  write(frame, callback) { this.writes.push(frame); callback(null); }
  drain(callback) { callback(null); }
  close(callback) { this.isOpen = false; this.emit('close'); callback?.(); }
}

test('serial adapter announces, binds values, paces writes and releases resources', async () => {
  const protocol = await import('../../../../tools/disp-sim/src/protocol.js');
  const errors = [];
  const link = new SerialLink({ app: { getSelfPath: () => undefined, subscriptionmanager: { subscribe() {} } },
    bus: { id: 'test', path: '/dev/fake', baudRate: 115200 }, device: { id: 'test', address: 2 },
    protocol, catalog: {}, mappings: {}, onError: error => errors.push(error), Port: FakePort });
  link.start();
  assert.equal(new protocol.FrameDecoder().push(link.port.writes[0])[0].type, protocol.MessageType.HELLO);
  const begin = Buffer.alloc(4);
  link.port.emit('data', Buffer.concat([
    protocol.encodeFrame(protocol.MessageType.HELLO, 0, protocol.helloPayload(1, 'remote-a')),
    protocol.encodeFrame(protocol.MessageType.SUBSCRIPTIONS_BEGIN, 1, begin),
    protocol.encodeFrame(protocol.MessageType.SUBSCRIPTIONS_END, 2)
  ]));
  assert.equal(link.snapshot().ready, true);
  const pending = link.send(protocol.MessageType.PING, Buffer.alloc(4));
  const obsolete = link.send(protocol.MessageType.VALUE_NUMBER, protocol.numberValuePayload(0, 1));
  const latest = link.send(protocol.MessageType.VALUE_NUMBER, protocol.numberValuePayload(0, 2));
  assert.equal(await obsolete, false);
  assert.equal(link.queue.filter(entry => entry.valueFrame).length, 1);
  assert.equal(link.port.writes.length, 1);
  await pending;
  assert.equal(link.port.writes.length, 2);
  await latest;
  const decoded = new protocol.FrameDecoder().push(link.port.writes[2])[0];
  assert.equal(decoded.payload.readFloatLE(2), 2);
  await link.stop(); assert.equal(link.port.isOpen, false); assert.equal(link.session.ready, false);
  assert.equal(errors.length, 0);
});

test('repeated control announcements coalesce while a device is waiting', async () => {
  const protocol = await import('../../../../tools/disp-sim/src/protocol.js');
  const link = new SerialLink({ app: {}, bus: { path: '/dev/fake', baudRate: 115200 },
    device: { id: 'waiting', address: 2 }, protocol, catalog: {}, mappings: {}, onError() {}, Port: FakePort });
  link.start();
  const oldHello = link.send(protocol.MessageType.HELLO, protocol.helloPayload(2, 'old'));
  const newHello = link.send(protocol.MessageType.HELLO, protocol.helloPayload(2, 'new'));
  const oldPing = link.send(protocol.MessageType.PING, Buffer.alloc(4));
  const newPing = link.send(protocol.MessageType.PING, Buffer.from([1, 0, 0, 0]));
  assert.equal(await oldHello, false); assert.equal(await oldPing, false);
  assert.equal(link.queue.length, 2);
  await Promise.all([newHello, newPing]);
  await link.stop();
});

test('gateway waits for the complete subscription snapshot before transmitting', async () => {
  const protocol = await import('../../../../tools/disp-sim/src/protocol.js');
  const link = new SerialLink({ app: { getSelfPath: () => undefined, subscriptionmanager: { subscribe() {} } },
    bus: { id: 'test', path: '/dev/fake', baudRate: 115200 }, device: { id: 'test', address: 2 },
    protocol, catalog: {}, mappings: {}, onError: () => {}, Port: FakePort });
  link.start();
  link.port.emit('data', protocol.encodeFrame(protocol.MessageType.SUBSCRIPTIONS_BEGIN, 1, Buffer.alloc(4)));
  const pending = link.send(protocol.MessageType.PING, Buffer.alloc(4));
  link.hello();
  assert.equal(link.queue.length, 1);
  await new Promise((resolve, reject) => setTimeout(() => {
    try {
      assert.equal(link.port.writes.length, 1);
      link.port.emit('data', protocol.encodeFrame(protocol.MessageType.SUBSCRIPTIONS_END, 2));
      resolve();
    } catch (error) { reject(error); }
  }, 120));
  await pending;
  assert.equal(link.port.writes.length, 2);
  await link.stop();
});