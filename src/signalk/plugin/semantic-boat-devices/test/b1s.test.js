'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { FromPgn } = require('@canboat/canboatjs');
const { loadRegistry } = require('../src/b1s/registry');
const { canboatDefinitions } = require('../src/b1s/canboat');
const { B1sState } = require('../src/b1s/state');

const registry = loadRegistry();

function parser() {
  let register;
  const fromPgn = new FromPgn({ onPropertyValues: (name, callback) => { register = callback; } });
  register([{ value: canboatDefinitions(registry), setter: 'test' }]);
  return line => fromPgn.parseString(line);
}

// Frames recorded from LT1 on 2026-10-18 (candump -L).
const frames = {
  measurement: '(1.0) can1 14FF0026#FE9F010070950400',
  notAvailable: '(1.0) can1 14FF0026#FE9F0C01FFFFFF7F',
  description: [
    '(1.0) can1 1DFF0326#A015FE9F0001F0FF',
    '(1.0) can1 1DFF0326#A1C80002010B014C',
    '(1.0) can1 1DFF0326#A2543120626F6172',
    '(1.0) can1 1DFF0326#A364FFFFFFFFFFFF'
  ]
};

test('canboat decodes B1S measurements', () => {
  const parse = parser();
  const measurement = parse(frames.measurement);
  assert.strictEqual(measurement.description, 'B1S: Measurement');
  assert.strictEqual(measurement.fields.quantity, 1);
  assert.strictEqual(measurement.fields.channel, 0);
  assert.strictEqual(measurement.fields.value, 300400);
  // canboat omits "not available" fields; the plugin treats them as null.
  assert.strictEqual(parse(frames.notAvailable).fields.value, undefined);
});

test('canboat decodes a Channel Description fast packet with its strings', () => {
  const parse = parser();
  let result;
  for (const frame of frames.description) result = parse(frame);
  assert.strictEqual(result.description, 'B1S: Channel Description');
  assert.strictEqual(result.fields.channel, 0);
  assert.strictEqual(result.fields.kind, 1);
  assert.strictEqual(result.fields.encoding, 0);
  assert.strictEqual(result.fields.period, 2);
  assert.strictEqual(result.fields.location, undefined); // empty: unassigned
  assert.strictEqual(result.fields.label, 'LT1 board');
});

function state(now = 0) {
  const deltas = [];
  const requests = [];
  const clock = { now };
  const b1s = new B1sState(registry, {
    onDelta: delta => deltas.push(delta),
    request: (address, pgn) => requests.push([address, pgn]),
    now: () => clock.now
  });
  return { b1s, deltas, requests, clock };
}

const claim = { pgn: 60928, src: 38, fields: { uniqueNumber: 1082470, manufacturerCode: 2046 } };
const measurement = (channel, value, quantity = 1) =>
  ({ pgn: 65280, src: 38, fields: { manufacturerCode: 2046, quantity, channel, value } });
const description = (channel, location, label) => ({
  pgn: 130819, src: 38,
  fields: { manufacturerCode: 2046, channel, kind: 1, encoding: 0, period: 2, location, label }
});

test('an unknown device is asked for its address claim, then its channels', () => {
  const { b1s, deltas, requests } = state();
  b1s.handle(measurement(0, 300400));
  assert.deepStrictEqual(requests, [[38, 60928]]);
  assert.strictEqual(deltas.length, 0);
  b1s.handle(claim);
  b1s.handle(measurement(0, 300400));
  assert.deepStrictEqual(requests, [[38, 60928], [38, 126996], [38, 130819]]);
  assert.strictEqual(deltas[0].updates[0].values[0].path, 'b1s.unassigned.1082470.0.temperature');
  assert.strictEqual(deltas[0].updates[0].values[0].value, 300.4);
});

test('requests are rate limited per device and PGN', () => {
  const { b1s, requests, clock } = state();
  b1s.handle(measurement(0, 1));
  b1s.handle(measurement(0, 1));
  clock.now = 10000;
  b1s.handle(measurement(0, 1));
  assert.deepStrictEqual(requests, [[38, 60928], [38, 60928]]);
});

test('a located channel uses the quantity template, with units and label', () => {
  const { b1s, deltas } = state();
  b1s.handle(claim);
  b1s.handle(description(0, 'inside.refrigerator', 'Fridge'));
  b1s.handle(measurement(0, 278150));
  b1s.handle(measurement(0, 278160));
  const first = deltas[0].updates[0];
  assert.strictEqual(first.values[0].path, 'environment.inside.refrigerator.temperature');
  assert.deepStrictEqual(first.meta[0].value, { description: 'B1S device 1082470, channel 0', displayName: 'Fridge', units: 'K' });
  assert.strictEqual(deltas[1].updates[0].meta, undefined);
});

test('special values become null', () => {
  const { b1s, deltas } = state();
  b1s.handle(claim);
  for (const raw of [0x7fffffff, 0x7ffffffe, 0x7ffffffd, null]) b1s.handle(measurement(0, raw));
  assert.deepStrictEqual(deltas.map(delta => delta.updates[0].values[0].value), [null, null, null, null]);
});

test('a device that moves to another address keeps its channels', () => {
  const { b1s, deltas } = state();
  b1s.handle(claim);
  b1s.handle(description(0, 'inside', 'Cabin'));
  b1s.handle({ ...claim, src: 40 });
  b1s.handle({ ...measurement(0, 293150), src: 40 });
  assert.strictEqual(deltas[0].updates[0].values[0].path, 'environment.inside.temperature');
  assert.strictEqual(b1s.snapshot()[0].address, 40);
});

test('messages of other manufacturers are ignored', () => {
  const { b1s, deltas, requests } = state();
  b1s.handle({ pgn: 65280, src: 50, fields: { manufacturerCode: 1855, data: 'ff' } });
  assert.strictEqual(deltas.length + requests.length, 0);
});

const { B1sClient } = require('../src/b1s/client');

function client() {
  const sent = [];
  const consoles = [];
  const b1sClient = new B1sClient(registry, {
    send: (destination, pgn, bytes, priority) => sent.push({ destination, pgn, bytes, priority }),
    onConsole: (address, text) => consoles.push([address, text]),
    timeoutMs: 20
  });
  return { b1sClient, sent, consoles };
}

const hex = bytes => bytes.map(byte => byte.toString(16).padStart(2, '0')).join(' ');

test('a parameter read is sent with the B1S header and resolved by its reply', async () => {
  const { b1sClient, sent } = client();
  const reading = b1sClient.read(38, 16, 0);
  await new Promise(resolve => setImmediate(resolve));
  assert.deepStrictEqual(sent[0].bytes, [0xfe, 0x9f, 2, sent[0].bytes[3], 16, 0, 0]);
  const sequence = sent[0].bytes[3];
  // Reply: service | 0x80, sequence, status, id, channel, type 1, STRING_LAU "lab", frame padding.
  const payload = hex([0x82, sequence, 0, 16, 0, 0, 1, 5, 1, 0x6c, 0x61, 0x62, 0xff, 0xff]);
  assert.ok(b1sClient.handle({ pgn: 126720, src: 38, fields: { manufacturerCode: 2046, payload } }));
  assert.deepStrictEqual(await reading, { status: 'done', value: 'lab' });
});

test('a request without reply is retried, then rejected', async () => {
  const { b1sClient, sent } = client();
  await assert.rejects(b1sClient.read(38, 64), /no reply from address 38/);
  assert.strictEqual(sent.length, 3);
});

test('command replies carry their result as a number; console text loses padding', async () => {
  const { b1sClient, consoles } = client();
  const command = b1sClient.command(38, 'calibrate', 2, 300650);
  await new Promise(resolve => setImmediate(resolve));
  b1sClient.handle({ pgn: 61184, src: 38, fields: { manufacturerCode: 2046, code: 0x84, channel: 2, argument: 0xffffff04 } });
  assert.strictEqual(await command, 'refused');
  const text = hex([0x84, 7, 0, ...Buffer.from('> help\r\n'), 0xff, 0xff, 0x00]);
  assert.ok(b1sClient.handle({ pgn: 126720, src: 38, fields: { manufacturerCode: 2046, payload: text } }));
  assert.deepStrictEqual(consoles, [[38, '> help\r\n']]);
});

test('a log stream request carries the minutes and returns the remaining seconds', async () => {
  const { b1sClient, sent } = client();
  const request = b1sClient.logStream(38, 5);
  await new Promise(resolve => setImmediate(resolve));
  const [, , service, sequence, minutes] = sent[0].bytes;
  assert.deepStrictEqual([service, minutes], [8, 5]);
  b1sClient.handle({ pgn: 126720, src: 38, fields: { manufacturerCode: 2046, payload: hex([0x88, sequence, 0, 0x2c, 0x01, 0xff]) } });
  assert.strictEqual(await request, 300);
});

test('prefixes form a namespace for B1S paths, not for standard PGN channels', () => {
  const deltas = [];
  const b1s = new B1sState(registry, { onDelta: delta => deltas.push(delta), request: () => {}, now: () => 1000,
    pathPrefix: 'sb', devicePrefix: id => (id === 1082470 ? 'lab' : '') });
  b1s.handle(claim);
  b1s.handle(description(0, 'inside', 'Cabin'));
  b1s.handle(measurement(0, 293150));
  assert.strictEqual(deltas[0].updates[0].values[0].path, 'sb.lab.environment.inside.temperature');
  b1s.handle(measurement(1, 293150));
  assert.strictEqual(deltas[1].updates[0].values[0].path, 'sb.lab.b1s.unassigned.1082470.1.temperature');
  // A channel sent as standard PGN: the path Signal K mapped, no prefixes.
  b1s.handle({ pgn: 130819, src: 38, fields: { manufacturerCode: 2046, channel: 2, kind: 1, encoding: 1,
    standardInstance: 2, period: 2, label: 'Air' } });
  let channel = b1s.snapshot()[0].channels.find(entry => entry.channel === 2);
  assert.strictEqual(channel.path, null); // nothing mapped yet
  b1s.standardValue({ path: 'environment.inside.2.temperature', value: 300.1, timestamp: new Date(400).toISOString(),
    source: { label: 'can1', src: '38', pgn: 130316, instance: '2' } });
  channel = b1s.snapshot()[0].channels.find(entry => entry.channel === 2);
  assert.deepStrictEqual([channel.path, channel.value, channel.ageMs, channel.standardPgn],
    ['environment.inside.2.temperature', 300.1, 600, 130316]);
});

test('a channel switched off shows no value', () => {
  const { b1s } = state();
  b1s.handle(claim);
  b1s.handle(description(0, '', 'Board'));
  b1s.handle(measurement(0, 300000));
  b1s.handle({ ...description(0, '', 'Board'), fields: { ...description(0, '', 'Board').fields, encoding: 2 } });
  const channel = b1s.snapshot()[0].channels[0];
  assert.deepStrictEqual([channel.encoding, channel.value, channel.ageMs], ['off', null, null]);
});

test('a custom path is used as written, or below the prefixes with *', () => {
  const paths = { 0: 'electrical.lab.voltage', 1: '*environment.cabin.temperature' };
  const deltas = [];
  const b1s = new B1sState(registry, { onDelta: delta => deltas.push(delta), request: () => {}, pathPrefix: 'sb',
    devicePrefix: () => 'lab', channelPath: (id, channel) => paths[channel] ?? '' });
  b1s.handle(claim);
  b1s.handle(description(0, 'inside', 'A'));
  b1s.handle(description(1, 'inside', 'B'));
  b1s.handle(measurement(0, 1));
  b1s.handle(measurement(1, 1));
  assert.deepStrictEqual(deltas.map(delta => delta.updates[0].values[0].path),
    ['electrical.lab.voltage', 'sb.lab.environment.cabin.temperature']);
});
