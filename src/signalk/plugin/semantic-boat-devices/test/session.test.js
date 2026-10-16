'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { DeviceSession } = require('../src/session');

async function fixture() {
  const protocol = await import('../../../../tools/disp-sim/src/protocol.js');
  const sent = [];
  let callback;
  let unsubscribed = 0;
  const values = { 'propulsion.port.revolutions': { value: 25 } };
  const app = { getSelfPath: path => values[path], subscriptionmanager: {
    subscribe(command, cleanup, error, handler) {
      callback = handler;
      cleanup.push(() => { unsubscribed += 1; });
    }
  }};
  const session = new DeviceSession({ app, device: { id: 'display-2', address: 2 }, protocol,
    mappings: { 'propulsion.main.revolutions': 'propulsion.port.revolutions' },
    send: (type, payload) => sent.push({ type, payload }) });
  const frame = (type, payload = Buffer.alloc(0)) => session.handle({ type, payload });
  const snapshot = (count = 1) => {
    const begin = Buffer.alloc(4); begin.writeUInt16LE(count, 2);
    frame(protocol.MessageType.SUBSCRIPTIONS_BEGIN, begin);
  };
  const source = (index = 0, path = 'propulsion.main.revolutions', flags = 0, providerName = 'signalk') => {
    const provider = Buffer.from(providerName); const pathBytes = Buffer.from(path);
    const header = Buffer.alloc(9); header.writeUInt16LE(index); header[6] = flags; header[7] = provider.length; header[8] = pathBytes.length;
    frame(protocol.MessageType.SUBSCRIBE, Buffer.concat([header, provider, pathBytes]));
  };
  return { session, protocol, sent, values, frame, snapshot, source,
    emit: delta => callback(delta), cleanups: () => unsubscribed };
}

test('complete snapshot binds real preferred values and coalesces updates', async () => {
  const fixtureValue = await fixture();
  const { session, protocol, sent, values, snapshot, source, frame, emit } = fixtureValue;
  snapshot(); source(); frame(protocol.MessageType.SUBSCRIPTIONS_END);
  assert.equal(session.ready, true);
  session.flush(1000);
  assert.equal(sent[0].type, protocol.MessageType.VALUE_NUMBER);
  assert.equal(sent[0].payload.readFloatLE(2), 25);
  values['propulsion.port.revolutions'].value = 30;
  emit({ updates: [{ values: [{ path: 'propulsion.port.revolutions', value: 999 }] }] });
  session.flush(1200); assert.equal(sent.length, 1);
  session.flush(1500); assert.equal(sent[1].payload.readFloatLE(2), 30);
});

test('missing values are unavailable, snapshots replace subscriptions and Hello cleans up', async () => {
  const { session, protocol, sent, snapshot, source, frame, cleanups } = await fixture();
  snapshot(); source(); frame(protocol.MessageType.SUBSCRIPTIONS_END);
  snapshot(); source(1, 'missing.path'); frame(protocol.MessageType.SUBSCRIPTIONS_END);
  assert.equal(cleanups(), 1);
  session.flush(1000);
  assert.equal(sent[0].type, protocol.MessageType.VALUE_UNAVAILABLE);
  assert.equal(sent[0].payload.readUInt16LE(0), 1);
  frame(protocol.MessageType.HELLO, protocol.helloPayload(1, 'remote-a'));
  assert.equal(cleanups(), 2); assert.equal(session.active.size, 0);
});

test('opt-in Signal K text preserves small SI values and formats boolean status', async () => {
  const { session, protocol, sent, values, snapshot, source, frame, emit } = await fixture();
  values['environment.weather.air.pm10'] = { value: 15 * 1e-9, timestamp: new Date(1000).toISOString() };
  values['environment.weather.status.available'] = { value: true };
  snapshot(4);
  source(0, 'environment.weather.air.pm10', 2, 'signalk-text');
  source(1, 'environment.weather.status.available', 0, 'signalk-text');
  source(2, 'environment.weather.air.pm10');
  source(3, 'missing.path', 0, 'signalk-text');
  frame(protocol.MessageType.SUBSCRIPTIONS_END); session.flush(2000);
  assert.equal(sent[0].type, protocol.MessageType.VALUE_TEXT);
  assert.equal(sent[0].payload.subarray(3, -4).toString('utf8'), '1.5e-8');
  assert.equal(sent[0].payload.readUInt32LE(sent[0].payload.length - 4), 1000);
  const byIndex = new Map(sent.map(message => [message.payload.readUInt16LE(0), message]));
  assert.equal(byIndex.get(1).payload.subarray(3).toString('utf8'), 'YES');
  assert.equal(byIndex.get(2).type, protocol.MessageType.VALUE_NUMBER);
  assert.equal(byIndex.get(3).type, protocol.MessageType.VALUE_UNAVAILABLE);
  values['environment.weather.status.available'].value = false;
  emit({ updates: [{ values: [{ path: 'environment.weather.status.available' }] }] });
  session.flush(2500);
  assert.equal(sent.at(-1).payload.subarray(3).toString('utf8'), 'NO');
  for (const value of [null, NaN, Infinity]) {
    values['environment.weather.air.pm10'].value = value;
    emit({ updates: [{ values: [{ path: 'environment.weather.air.pm10' }] }] });
    session.flush(3000 + sent.length * 500);
    assert.equal(sent.at(-2).type, protocol.MessageType.VALUE_UNAVAILABLE);
    assert.equal(sent.at(-1).type, protocol.MessageType.VALUE_UNAVAILABLE);
  }
});

test('opt-in particulate conversion sends ug/m3 without changing preferred SI values', async () => {
  const { session, protocol, sent, values, snapshot, source, frame, emit } = await fixture();
  const path = 'environment.weather.air.pm10';
  const raw = 15 * 1e-9;
  values[path] = { value: raw, timestamp: new Date(1000).toISOString() };
  snapshot(2); source(0, path, 2, 'signalk-ugm3'); source(1, path);
  frame(protocol.MessageType.SUBSCRIPTIONS_END); session.flush(2000);
  assert.equal(sent[0].type, protocol.MessageType.VALUE_NUMBER);
  assert.equal(sent[0].payload.readFloatLE(2), 15);
  assert.equal(sent[0].payload.readUInt32LE(6), 1000);
  assert.equal(sent[1].payload.readFloatLE(2), Math.fround(raw));
  assert.equal(values[path].value, raw);
  values[path].value = 0;
  emit({ updates: [{ values: [{ path }] }] }); session.flush(2500);
  assert.equal(sent.at(-2).payload.readFloatLE(2), 0);
  for (const value of [null, NaN, Infinity, '15']) {
    values[path].value = value;
    emit({ updates: [{ values: [{ path }] }] }); session.flush(3000 + sent.length * 500);
    assert.equal(sent.at(-2).type, protocol.MessageType.VALUE_UNAVAILABLE);
  }
});

test('position objects become bounded coordinate text and malformed positions are unavailable', async () => {
  const { session, protocol, sent, values, snapshot, source, frame, emit } = await fixture();
  values['navigation.position'] = { value: { latitude: -33.12345, longitude: 151.54321 } };
  snapshot(); source(0, 'navigation.position'); frame(protocol.MessageType.SUBSCRIPTIONS_END);
  session.flush(1000);
  assert.equal(sent[0].type, protocol.MessageType.VALUE_TEXT);
  assert.equal(sent[0].payload.subarray(3).toString('utf8'), '33.123450S 151.543210E');
  values['navigation.position'].value = { latitude: 60.25, longitude: -5.75 };
  emit({ updates: [{ values: [{ path: 'navigation.position' }] }] }); session.flush(1500);
  assert.equal(sent[1].payload.subarray(3).toString('utf8'), '60.250000N 5.750000W');
  values['navigation.position'].value = { latitude: 48.858370, longitude: 2.294481 };
  emit({ updates: [{ values: [{ path: 'navigation.position' }] }] }); session.flush(2000);
  assert.equal(sent[2].payload.subarray(3).toString('utf8'), '48.858370N 2.294481E');
  for (const value of [{ latitude: 91, longitude: 0 }, { latitude: 0, longitude: 181 },
    { latitude: NaN, longitude: 0 }, { latitude: 1 }, null]) {
    values['navigation.position'].value = value;
    emit({ updates: [{ values: [{ path: 'navigation.position' }] }] }); session.flush(2000 + sent.length * 500);
    assert.equal(sent.at(-1).type, protocol.MessageType.VALUE_UNAVAILABLE);
  }
});

test('incomplete or duplicate snapshot cannot replace active subscriptions', async () => {
  const { session, protocol, snapshot, source, frame } = await fixture();
  snapshot(); source(); frame(protocol.MessageType.SUBSCRIPTIONS_END);
  snapshot(2); source(1); frame(protocol.MessageType.SUBSCRIPTIONS_END);
  assert.equal(session.active.has(0), true);
  snapshot(2); source(1); source(1); frame(protocol.MessageType.SUBSCRIPTIONS_END);
  assert.equal(session.active.has(0), true);
});

test('age-capable subscriptions carry preferred sample age for numeric and position values', async () => {
  const { session, protocol, sent, values, snapshot, source, frame, emit } = await fixture();
  values['propulsion.port.revolutions'].timestamp = new Date(1000).toISOString();
  values['navigation.position'] = { value: { latitude: 1, longitude: 2 }, timestamp: new Date(2000).toISOString() };
  snapshot(2); source(0, 'propulsion.main.revolutions', 2); source(1, 'navigation.position', 2);
  frame(protocol.MessageType.SUBSCRIPTIONS_END); session.flush(12000);
  assert.equal(sent[0].payload.length, 10);
  assert.equal(sent[0].payload.readUInt32LE(6), 11000);
  assert.equal(sent[1].type, protocol.MessageType.VALUE_TEXT);
  assert.equal(sent[1].payload.subarray(3, -4).toString('utf8'), '1.000000N 2.000000E');
  assert.equal(sent[1].payload.readUInt32LE(sent[1].payload.length - 4), 10000);
  emit({ updates: [{ values: [{ path: 'navigation.position' }] }] }); session.flush(13000);
  assert.equal(sent.at(-1).payload.readUInt32LE(sent.at(-1).payload.length - 4), 11000);
  values['navigation.position'].timestamp = 'invalid';
  emit({ updates: [{ values: [{ path: 'navigation.position' }] }] }); session.flush(14000);
  assert.equal(sent.at(-1).payload.readUInt32LE(sent.at(-1).payload.length - 4), 0);
});