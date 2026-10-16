'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { Simulation } = require('../src/simulation');
const { Geodesic } = require('geographiclib-geodesic');

test('marina heading and course stay fixed despite wind changes', () => {
  const simulation = new Simulation({ marinaHeading: 70, windDirection: 180 });
  simulation.configure({ windDirection: 300, windSpeed: 30 });
  for (let tick = 0; tick < 100; tick += 1) simulation.step(1);
  const state = simulation.snapshot();
  assert.ok(Math.abs(state.heading - 70 * Math.PI / 180) < 1e-12);
  assert.equal(state.courseOverGround, state.heading);
  assert.equal(state.speedOverGround, 0);
  simulation.configure({ marinaHeading: 360 });
  assert.equal(simulation.snapshot().heading, 0);
  assert.throws(() => simulation.configure({ marinaHeading: 361 }));
});

test('anchor swing follows wind, stronger wind extends the rode, and velocity matches position', () => {
  const weak = new Simulation({ mode: 'anchor', windSpeed: 2, windDirection: 0, anchorRadius: 40 });
  const strong = new Simulation({ mode: 'anchor', windSpeed: 12, windDirection: 0, anchorRadius: 40 });
  const offset = simulation => Geodesic.WGS84.Inverse(simulation.controls.departureLatitude,
    simulation.controls.departureLongitude, simulation.latitude, simulation.longitude);
  for (let tick = 0; tick < 300; tick += 1) {
    weak.step(1);
    const before = strong.snapshot();
    const after = strong.step(1);
    const movement = Geodesic.WGS84.Inverse(before.latitude, before.longitude, after.latitude, after.longitude);
    assert.ok(offset(strong).s12 <= 40.000001);
    assert.ok(Math.abs(movement.s12 - after.speedOverGround) < 1e-8);
    if (movement.s12 > 0.000001) assert.ok(Math.abs(Math.sin(after.courseOverGround - movement.azi1 * Math.PI / 180)) < 1e-8);
  }
  assert.ok(offset(strong).s12 > offset(weak).s12 * 3);
  assert.ok(Math.abs(Math.abs(offset(strong).azi1) - 180) < 15);
  strong.configure({ windDirection: 90 });
  const previousHeading = strong.heading;
  strong.step(0.1);
  assert.ok(Math.abs(strong.heading - previousHeading) < 0.03);
  for (let tick = 0; tick < 300; tick += 1) strong.step(1);
  assert.ok(Math.abs(offset(strong).azi1 + 90) < 15);
  assert.ok(Math.abs(strong.heading - Math.PI / 2) < 0.22);
  strong.configure({ windSpeed: 0 });
  for (let tick = 0; tick < 300; tick += 1) strong.step(1);
  assert.ok(offset(strong).s12 < 0.001);
});

test('chain length and depth determine the bounded anchor swing radius', () => {
  const simulation = new Simulation({ mode: 'anchor', chainLength: 50, depth: 30, windSpeed: 12 });
  assert.equal(simulation.snapshot().anchorSwingRadius, 40);
  for (let tick = 0; tick < 300; tick += 1) {
    const state = simulation.step(1);
    assert.ok(state.anchorDistance <= state.anchorSwingRadius + 0.000001);
    assert.equal(state.anchorChainTooShort, false);
  }
  const longChainDistance = simulation.snapshot().anchorDistance;
  simulation.configure({ chainLength: 34 });
  assert.equal(simulation.snapshot().anchorSwingRadius, 16);
  for (let tick = 0; tick < 300; tick += 1) simulation.step(1);
  assert.ok(simulation.snapshot().anchorDistance < longChainDistance / 2);
  simulation.configure({ chainLength: 20 });
  const short = simulation.step(1);
  assert.equal(short.anchorSwingRadius, 0);
  assert.equal(short.anchorChainTooShort, true);
  assert.ok(short.anchorDistance < 0.000001);
  simulation.configure({ chainLength: 0, depth: 0 });
  assert.equal(simulation.step(1).anchorSwingRadius, 0);
  simulation.configure({ chainLength: 50, depth: 30, anchorRadius: 10 });
  assert.equal(simulation.snapshot().anchorSwingRadius, 10);
  assert.throws(() => simulation.configure({ chainLength: -1 }));
  assert.throws(() => simulation.configure({ chainLength: 201 }));
  simulation.configure({ anchorRadius: 200, fluctuations: { depth: { min: 10, max: 45 } } });
  for (let tick = 0; tick < 300; tick += 1) {
    const state = simulation.step(1);
    assert.equal(state.anchorSwingRadius, Math.sqrt(2500 - state.depth ** 2));
    assert.ok(state.anchorDistance <= state.anchorSwingRadius + 0.000001);
  }
});

test('underway heading follows destination while course includes current', () => {
  const simulation = new Simulation({ mode: 'underway', destinationLatitude: 51,
    destinationLongitude: -1.3, currentSpeed: 1, currentDirection: 90 });
  const state = simulation.step(1);
  assert.ok(Math.abs(state.heading) < 1e-12);
  assert.ok(state.courseOverGround > state.heading);
  simulation.configure({ destinationLatitude: state.latitude, destinationLongitude: 0 });
  const route = Geodesic.WGS84.Inverse(simulation.latitude, simulation.longitude,
    simulation.controls.destinationLatitude, simulation.controls.destinationLongitude);
  assert.ok(Math.abs(simulation.step(1).heading - route.azi1 * Math.PI / 180) < 1e-12);
});

test('marina stays stationary even with a running engine', () => {
  const simulation = new Simulation({ engineRunning: true });
  for (let tick = 0; tick < 100; tick += 1) simulation.step(0.1);
  const state = simulation.snapshot();
  assert.equal(state.latitude, state.controls.departureLatitude);
  assert.equal(state.longitude, state.controls.departureLongitude);
  assert.equal(state.speedOverGround, 0);
  assert.equal(state.engineRpm, 1800);
  assert.ok(state.engineHours > 0);
});

test('underway advances and responds to live engine controls', () => {
  const simulation = new Simulation({ mode: 'underway', currentSpeed: 0 });
  for (let tick = 0; tick < 100; tick += 1) simulation.step(0.1);
  assert.notEqual(simulation.latitude, simulation.controls.departureLatitude);
  const speed = simulation.speed;
  simulation.configure({ engineRunning: true, engineRpm: 2400 });
  for (let tick = 0; tick < 100; tick += 1) simulation.step(0.1);
  assert.ok(simulation.speed > speed);
});

test('anchor swing is bounded and repeated runs are deterministic', () => {
  const first = new Simulation({ mode: 'anchor' });
  const second = new Simulation({ mode: 'anchor' });
  for (let tick = 0; tick < 100; tick += 1) {
    assert.deepEqual(first.step(0.1), second.step(0.1));
    assert.ok(Math.abs(first.latitude - first.controls.departureLatitude) < 0.001);
  }
});

test('invalid controls are rejected atomically', () => {
  const simulation = new Simulation();
  assert.throws(() => simulation.configure({ engineRpm: 2000, wavePeriod: 0 }));
  assert.equal(simulation.controls.engineRpm, 1800);
  assert.throws(() => simulation.configure({ typo: 1 }));
  assert.throws(() => simulation.step(0));
});

test('bounded fluctuations are smooth, irregular, repeatable and independent of polling', () => {
  const controls = { fluctuations: { windSpeed: { min: 4, max: 10 }, waterTemperature: { min: 10, max: 20 } } };
  const first = new Simulation(controls);
  const second = new Simulation(controls);
  let previous = first.snapshot().trueWindSpeed;
  const values = [];
  for (let tick = 0; tick < 2000; tick += 1) {
    first.snapshot();
    const state = first.step(0.5);
    assert.deepEqual(state, second.step(0.5));
    assert.ok(state.trueWindSpeed >= 4 && state.trueWindSpeed <= 10);
    assert.ok(Math.abs(state.trueWindSpeed - previous) < 0.6);
    assert.ok(state.waterTemperature >= 283.15 && state.waterTemperature <= 293.15);
    previous = state.trueWindSpeed;
    values.push(previous);
  }
  assert.ok(Math.max(...values) - Math.min(...values) > 3);
  assert.throws(() => first.configure({ engineRpm: 3000, fluctuations: { windSpeed: { min: 10, max: 4 } } }));
  assert.equal(first.controls.engineRpm, 1800);
  first.reset();
  second.reset();
  assert.deepEqual(first.step(0.5), second.step(0.5));
});

test('movement distance matches emitted speed and motoring follows RPM', () => {
  const { Geodesic } = require('geographiclib-geodesic');
  const simulation = new Simulation({ mode: 'underway', currentSpeed: 0, sailingSpeed: 3 });
  for (let tick = 0; tick < 100; tick += 1) simulation.step(1);
  const before = simulation.snapshot();
  const after = simulation.step(1);
  const distance = Geodesic.WGS84.Inverse(before.latitude, before.longitude, after.latitude, after.longitude).s12;
  assert.ok(Math.abs(distance - after.speedThroughWater) < 0.001);
  assert.ok(Math.abs(after.speedThroughWater - 3) < 0.001);
  simulation.configure({ engineRunning: true, engineRpm: 2400 });
  for (let tick = 0; tick < 100; tick += 1) simulation.step(1);
  assert.ok(Math.abs(simulation.snapshot().speedThroughWater - 4) < 0.001);
  const elapsed = simulation.elapsed;
  simulation.configure({ position: { latitude: 51, longitude: -1 } });
  assert.equal(simulation.latitude, 51);
  assert.equal(simulation.elapsed, elapsed);
});