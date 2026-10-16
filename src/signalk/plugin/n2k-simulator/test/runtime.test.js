'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const { once } = require('node:events');
const { SimulatorRuntime } = require('../src/runtime');
const createPlugin = require('..');

test('runtime streams encoded N2K and releases its listener on stop', async context => {
  const updates = [];
  const runtime = new SimulatorRuntime({ onDelta: message => updates.push(message) });
  context.after(() => runtime.stop());
  const port = await runtime.start(0);
  runtime.paused = true;
  const client = net.connect(port, '127.0.0.1');
  context.after(() => client.destroy());
  await once(client, 'connect');
  const data = once(client, 'data');
  runtime.tick(0.1);
  assert.match((await data)[0].toString(), /,129025,200,255,8,/);
  assert.equal(updates.length, 1);
  runtime.control({ mode: 'underway', engineRunning: true });
  assert.equal(runtime.snapshot().controls.engineRunning, true);
  runtime.control({ departureLatitude: 51 });
  runtime.tick(0.1);
  assert.equal(updates.length, 2);
  runtime.reset();
  assert.equal(runtime.snapshot().elapsed, 0);
  await runtime.stop();
  assert.equal(runtime.timer, null);
  assert.equal(runtime.server, null);
});

test('plugin keeps N2K mode separate from direct Signal K injection', async context => {
  const updates = [];
  const errors = [];
  const plugin = createPlugin({ handleMessage: (...args) => updates.push(args),
    setPluginStatus: () => {}, setPluginError: error => errors.push(error) });
  context.after(() => plugin.stop());
  const routes = new Map();
  plugin.registerWithRouter({ get: (path, handler) => routes.set(`GET ${path}`, handler),
    post: (path, handler) => routes.set(`POST ${path}`, handler) });
  await plugin.start({ tcpPort: 0, output: 'n2k' });
  let state;
  const response = { json: body => { state = body; }, status: () => response };
  routes.get('POST /controls')({ body: { engineRunning: true, engineRpm: 2200 } }, response);
  assert.equal(state.engineRpm, 2200);
  assert.equal(updates.length, 0);
  await plugin.stop();
  await plugin.start({ tcpPort: 0, output: 'signalk' });
  const update = await new Promise(resolve => {
    const original = updates.push.bind(updates);
    updates.push = (...items) => { original(...items); resolve(items[0]); };
  });
  assert.equal(update[0], plugin.id);
  assert.equal(update[1].context, 'vessels.self');
  assert.deepEqual(errors, []);
  await plugin.stop();
  routes.get('GET /state')({}, response);
  assert.equal(state.error, 'Simulator is not running');
});

test('plugin serializes pending lifecycle changes and reports startup errors', async () => {
  const errors = [];
  const plugin = createPlugin({ handleMessage: () => {}, setPluginStatus: () => {},
    setPluginError: error => errors.push(error) });
  const routes = new Map();
  plugin.registerWithRouter({ get: (path, handler) => routes.set(path, handler), post: () => {} });
  const starting = plugin.start({ tcpPort: 0 });
  await plugin.stop();
  await starting;
  let state;
  const response = { json: body => { state = body; }, status: () => response };
  routes.get('/state')({}, response);
  assert.equal(state.error, 'Simulator is not running');
  await plugin.start({ output: 'invalid' });
  assert.deepEqual(errors, ['Invalid output mode']);
  await plugin.start({ tcpPort: 0, sourceAddress: 201 });
  routes.get('/state')({}, response);
  assert.equal(state.output, 'n2k');
  assert.equal(state.sourceAddress, 201);
  routes.get('/ui/app.js')({}, { type: () => ({ sendFile: file => assert.match(file, /public\/app.js$/) }) });
  await plugin.stop();
});

test('individual outputs can be silenced atomically without stopping the model', () => {
  const { readings } = require('../src/instruments');
  const updates = [];
  const runtime = new SimulatorRuntime({ controls: { mode: 'underway' }, onDelta: message => updates.push(message) });
  runtime.control({ outputs: Object.fromEntries(readings.map(([path]) => [path, false])) });
  const latitude = runtime.snapshot().latitude;
  runtime.tick(1);
  assert.equal(updates.length, 0);
  assert.notEqual(runtime.snapshot().latitude, latitude);
  runtime.control({ outputs: { 'environment.water.temperature': true } });
  runtime.tick(1);
  assert.deepEqual(updates[0].updates[0].values.map(value => value.path), ['environment.water.temperature']);
  assert.throws(() => runtime.control({ outputs: { 'environment.water.temperature': false }, windSpeed: -1 }));
  assert.equal(runtime.outputs['environment.water.temperature'], true);
});