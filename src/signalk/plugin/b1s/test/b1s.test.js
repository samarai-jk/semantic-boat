'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { FromPgn } = require('@canboat/canboatjs');
const { loadRegistry } = require('../src/registry');
const { canboatDefinitions } = require('../src/canboat');
const { B1sState } = require('../src/state');

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
  assert.deepStrictEqual(requests, [[38, 60928], [38, 130819]]);
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
