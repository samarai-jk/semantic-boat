'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { FromPgn } = require('@canboat/canboatjs');
const { Simulation } = require('../src/simulation');
const { InstrumentScheduler, delta, readings } = require('../src/instruments');

test('every instrument round-trips through the NMEA 2000 codec', () => {
  const state = new Simulation({ engineRunning: true }).step(0.1);
  const messages = new InstrumentScheduler().sample(state, '2026-10-11T12:00:00.000Z');
  const parser = new FromPgn();
  for (const message of messages) {
    const decoded = parser.parseString(message.line);
    assert.ok(decoded, `PGN ${message.pgn}`);
    assert.equal(decoded.pgn, message.pgn);
    assert.equal(decoded.src, 200);
    assert.ok(Object.keys(decoded.fields).length > 0);
    if (message.pgn === 127488) {
      assert.equal(decoded.fields.speed, 1800);
      assert.equal(decoded.fields.instance, 'Single Engine or Dual Engine Port');
    }
    if (message.pgn === 130306) assert.ok(decoded.fields.reference);
  }
  const update = delta(messages, '2026-10-11T12:00:00.000Z');
  assert.equal(update.context, 'vessels.self');
  assert.equal(update.updates[0].values.find(value => value.path === 'propulsion.main.revolutions').value, 30);
});

test('scheduler emits at instrument rates without catch-up bursts', () => {
  const simulation = new Simulation();
  const scheduler = new InstrumentScheduler();
  const counts = new Map();
  for (let tick = 0; tick < 10; tick += 1) {
    for (const message of scheduler.sample(simulation.snapshot())) {
      counts.set(message.pgn, (counts.get(message.pgn) || 0) + 1);
    }
    simulation.step(0.1);
  }
  assert.equal(counts.get(127250), 10);
  assert.equal(counts.get(129025), 5);
  assert.equal(counts.get(128267), 1);
  assert.equal(counts.get(127489), 2);
});

test('late ticks preserve rate deadlines without replaying missed frames', () => {
  const simulation = new Simulation();
  const scheduler = new InstrumentScheduler();
  scheduler.sample(simulation.step(0.11));
  assert.equal(scheduler.sample(simulation.step(0.09)).filter(message => message.pgn === 127250).length, 1);
  assert.equal(scheduler.sample(simulation.step(2)).filter(message => message.pgn === 127250).length, 1);
});

test('each reading encodes alone and disabled shared-PGN fields become unavailable', () => {
  const parser = new FromPgn();
  const state = new Simulation({ engineRunning: true }).snapshot();
  for (const [selected] of readings) {
    const outputs = Object.fromEntries(readings.map(([path]) => [path, path === selected]));
    const messages = new InstrumentScheduler().sample({ ...state, outputs });
    assert.equal(messages.length, 1, selected);
    assert.ok(parser.parseString(messages[0].line), selected);
    const values = delta(messages).updates[0].values;
    if (selected.startsWith('navigation.attitude.')) {
      assert.deepEqual(Object.keys(values[0].value), [selected.split('.').at(-1)]);
    } else assert.deepEqual(values.map(value => value.path), [selected]);
  }
  const outputs = { 'environment.water.temperature': false };
  const temperature = new InstrumentScheduler().sample({ ...state, outputs }).find(message => message.pgn === 130310);
  const fields = parser.parseString(temperature.line).fields;
  assert.equal(fields.waterTemperature, undefined);
  assert.ok(fields.outsideAmbientAirTemperature > 0);
});