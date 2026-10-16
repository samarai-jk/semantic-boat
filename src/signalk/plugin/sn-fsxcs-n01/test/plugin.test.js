'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { decode, metadata, unavailable, prefix } = require('../src/measurements');

const liveSample = [0, 0, 0, 0, 521, 250, 523, 8, 18, 1019, 0, 96, 0, 0, 6190, 0];

test('every value, metadata entry and unavailable update is isolated from navigation/N2K paths', () => {
  for (const entry of [...decode(liveSample), ...metadata(), ...unavailable()]) {
    assert.ok(entry.path.startsWith('environment.weather.'));
    assert.ok(!entry.path.startsWith('environment.wind.'));
    assert.ok(!entry.path.startsWith('navigation.'));
    assert.ok(!entry.path.startsWith('environment.outside.'));
  }
  assert.equal(decode(liveSample).length, 15);
  assert.equal(prefix(), 'environment.weather');
});

test('live measurement sample converts to explicit SI units', () => {
  const values = Object.fromEntries(decode(liveSample).map(entry => [entry.path.replace(`${prefix()}.`, ''), entry.value]));
  assert.equal(values['air.temperature'], 298.15);
  assert.equal(values['air.relativeHumidity'], 0.521);
  assert.equal(values['air.pressure'], 101900);
  assert.equal(values['air.noiseLevel'], 52.3);
  assert.equal(values['air.pm25'], 8e-9);
  assert.ok(Math.abs(values['air.pm10'] - 18e-9) < 1e-20);
  assert.equal(values['light.illuminance'], 96);
  assert.equal(values['compass.angle'], 6190 * Math.PI / 18000);
  assert.equal(values['wind.speed'], 0);
  assert.equal(values['rain.accumulation'], 0);
});

test('only particulate metadata converts SI concentration into readable micrograms per cubic metre', () => {
  for (const rootPath of ['environment.weather', 'environment.weatherStations.deck']) {
    const entries = metadata(rootPath);
    const particulate = entries.filter(entry => entry.value.displayUnits);
    assert.deepEqual(particulate.map(entry => entry.path), [`${rootPath}.air.pm25`, `${rootPath}.air.pm10`]);
    for (const entry of particulate) {
      assert.equal(entry.value.units, 'kg/m3');
      assert.deepEqual(entry.value.displayUnits, {
        category: 'custom', targetUnit: 'ug/m3', formula: 'value * 1000000000',
        inverseFormula: 'value / 1000000000', symbol: '\u00b5g/m\u00b3', displayFormat: '0'
      });
    }
  }
});

test('signed temperature, high-word light, rain conversion and invalid data are handled', () => {
  const words = [...liveSample];
  words[5] = 0xFF9B; words[10] = 2; words[11] = 7; words[13] = 123;
  const values = decode(words);
  assert.ok(Math.abs(values[5].value - 263.05) < 1e-10);
  assert.equal(values[10].value, 131079);
  assert.equal(values[12].value, 0.0123);
  words[10] = 0xFFFF; words[11] = 0xFFFF; words[3] = 360; words[4] = 1001;
  assert.equal(decode(words)[10].value, null);
  assert.equal(decode(words)[3].value, null);
  assert.equal(decode(words)[4].value, null);
  assert.throws(() => decode(words.slice(1)), /16 unsigned/);
  words[0] = -1;
  assert.throws(() => decode(words), /16 unsigned/);
});

const { StationRuntime, validateOptions } = require('../src/runtime');
const createPlugin = require('../index');

function harness() {
  const deltas = [];
  const errors = [];
  const scheduled = new Set();
  const clients = [];
  let read = async () => ({ data: [...liveSample] });
  let openingError = null;
  const app = { handleMessage: (_id, delta) => deltas.push(delta), setPluginStatus() {}, setPluginError: message => errors.push(message) };
  const dependencies = {
    createClient: () => {
      const client = { isOpen: false, requests: [], closed: false,
        async connectRTUBuffered(path, settings) { this.path = path; this.settings = settings; if (openingError) throw openingError; this.isOpen = true; },
        setID(address) { this.address = address; }, setTimeout(timeout) { this.timeout = timeout; },
        async readHoldingRegisters(start, count) { this.requests.push([start, count]); return read(); },
        close(callback) { this.isOpen = false; this.closed = true; callback(); }
      };
      clients.push(client);
      return client;
    },
    schedule: (callback, delay) => { const entry = { callback, delay }; scheduled.add(entry); return entry; },
    cancel: entry => scheduled.delete(entry),
    now: () => Date.parse('2026-10-13T12:00:00Z')
  };
  return { app, dependencies, deltas, errors, clients, scheduled,
    setRead: handler => { read = handler; }, setOpeningError: error => { openingError = error; },
    async next() { const entry = scheduled.values().next().value; assert.ok(entry); scheduled.delete(entry); await entry.callback(); }
  };
}

test('runtime uses documented read-only transport and clears data on failure, then recovers', async () => {
  const fixture = harness();
  const runtime = new StationRuntime(fixture.app, { path: '/dev/weather' }, fixture.dependencies);
  runtime.start(); await runtime.inflight;
  assert.equal(runtime.snapshot().available, true);
  assert.deepEqual(fixture.clients[0].requests, [[500, 16]]);
  assert.deepEqual(fixture.clients[0].settings, { baudRate: 4800, dataBits: 8, parity: 'none', stopBits: 1 });
  assert.equal(fixture.clients[0].address, 1);
  assert.equal(fixture.clients[0].timeout, 1000);
  assert.equal(fixture.scheduled.size, 1);
  assert.equal(fixture.scheduled.values().next().value.delay, 1000);
  fixture.setRead(async () => { throw new Error('Timed out'); });
  await fixture.next();
  assert.equal(runtime.snapshot().available, false);
  assert.equal(runtime.snapshot().failedReads, 1);
  assert.equal(fixture.clients[0].closed, true);
  assert.ok(runtime.snapshot().measurements.every(entry => entry.value === null));
  fixture.setRead(async () => ({ data: [...liveSample] }));
  await fixture.next();
  assert.equal(runtime.snapshot().available, true);
  assert.equal(runtime.snapshot().successfulReads, 2);
  assert.equal(fixture.clients.length, 2);
  await runtime.stop();
  assert.equal(fixture.scheduled.size, 0);
  assert.equal(fixture.clients[1].closed, true);
  for (const delta of fixture.deltas) {
    assert.equal(delta.context, 'vessels.self');
    assert.equal(delta.updates[0].source.label, 'sn-fsxcs-n01.station1');
    assert.equal(delta.updates[0].source.src, 'station1');
    for (const entry of [...(delta.updates[0].values ?? []), ...(delta.updates[0].meta ?? [])]) {
      assert.ok(entry.path.startsWith(`${prefix()}.`));
    }
  }
});

test('missing adapter retries without blocking startup or inventing measurements', async () => {
  const fixture = harness();
  fixture.setOpeningError(new Error('No such device'));
  const runtime = new StationRuntime(fixture.app, { path: '/dev/weather' }, fixture.dependencies);
  runtime.start(); await runtime.inflight;
  assert.equal(runtime.snapshot().available, false);
  assert.equal(runtime.snapshot().rawRegisters, null);
  fixture.setOpeningError(null);
  await fixture.next();
  assert.equal(runtime.snapshot().available, true);
  await runtime.stop();
});

test('shutdown waits for an in-flight read, publishes no late sample, and closes the port', async () => {
  const fixture = harness();
  let resolveRead;
  fixture.setRead(() => new Promise(resolve => { resolveRead = resolve; }));
  const runtime = new StationRuntime(fixture.app, { path: '/dev/weather' }, fixture.dependencies);
  runtime.start();
  await Promise.resolve();
  const stopping = runtime.stop();
  resolveRead({ data: [...liveSample] });
  await stopping;
  assert.equal(runtime.snapshot().successfulReads, 0);
  assert.equal(fixture.clients[0].closed, true);
  assert.equal(fixture.scheduled.size, 0);
});

test('invalid settings fail before any port is opened', () => {
  for (const options of [{ address: 0 }, { address: 248 }, { baudRate: 115200 }, { timeoutMs: 200 }, { pollIntervalMs: 200 }, { stationId: 'navigation.wind' }]) {
    assert.throws(() => validateOptions({ path: '/dev/weather', ...options }));
  }
  assert.throws(() => validateOptions({}), /serial device/);
});

test('custom roots apply to all measurements, metadata, status and unavailable updates, not source identity', async () => {
  const rootPath = 'environment.weatherStations.deck';
  for (const entry of [...decode(liveSample, rootPath), ...metadata(rootPath), ...unavailable(rootPath)]) {
    assert.ok(entry.path.startsWith(`${rootPath}.`));
  }
  const fixture = harness();
  const runtime = new StationRuntime(fixture.app, { path: '/dev/weather', rootPath, stationId: 'replacement' }, fixture.dependencies);
  runtime.start(); await runtime.inflight;
  assert.equal(runtime.snapshot().pathPrefix, rootPath);
  assert.ok(runtime.snapshot().measurements.every(entry => entry.path.startsWith(`${rootPath}.`)));
  await runtime.stop();
  for (const delta of fixture.deltas) {
    assert.equal(delta.updates[0].source.src, 'replacement');
    for (const entry of [...(delta.updates[0].values ?? []), ...(delta.updates[0].meta ?? [])]) {
      assert.ok(entry.path.startsWith(`${rootPath}.`));
    }
  }
});

test('invalid or shared instrument roots cannot be configured', () => {
  for (const rootPath of ['', 'environment', 'navigation.weather', 'environment.wind', 'environment.wind.station',
    'environment.outside', 'environment.water.weather', 'environment.depth', 'environment.current',
    'environment.inside', 'environment.tide', 'environment.heave', 'environment.time', 'environment.mode',
    'environment.weather.', 'environment..weather', 'environment.weather.*', 'environment.weather station', 'environment.__proto__']) {
    assert.throws(() => validateOptions({ path: '/dev/weather', rootPath }), /Root path/);
  }
  assert.equal(validateOptions({ path: '/dev/weather' }).rootPath, 'environment.weather');
  assert.equal(validateOptions({ path: '/dev/weather', rootPath: 'environment.weatherStations.fsxcsN01.station1' }).rootPath,
    'environment.weatherStations.fsxcsN01.station1');
});

test('incomplete register responses are rejected and previous samples cannot remain available', async () => {
  const fixture = harness();
  const runtime = new StationRuntime(fixture.app, { path: '/dev/weather' }, fixture.dependencies);
  runtime.start(); await runtime.inflight;
  fixture.setRead(async () => ({ data: liveSample.slice(1) }));
  await fixture.next();
  assert.equal(runtime.snapshot().available, false);
  assert.equal(runtime.snapshot().successfulReads, 1);
  assert.match(runtime.snapshot().error, /16 unsigned/);
  assert.ok(runtime.snapshot().measurements.every(entry => entry.value === null));
  await runtime.stop();
});

test('Signal K plugin lifecycle exposes read-only diagnostics and releases ownership on restart', async () => {
  const fixture = harness();
  const plugin = createPlugin(fixture.app, fixture.dependencies);
  const routes = new Map();
  plugin.registerWithRouter({ get: (route, handler) => routes.set(route, handler) });
  assert.deepEqual([...routes.keys()], ['/state']);
  let snapshot;
  const response = { json(value) { snapshot = value; }, status(code) { this.code = code; return this; } };
  routes.get('/state')({}, response);
  assert.equal(response.code, 503);
  await plugin.start({ path: '/dev/weather' });
  await Promise.resolve();
  routes.get('/state')({}, response);
  assert.equal(snapshot.model, 'SN-*-FSXCS-N01');
  await plugin.start({ path: '/dev/weather' });
  assert.equal(fixture.clients[0].closed, true);
  await plugin.stop();
  assert.equal(fixture.clients.at(-1).closed, true);
  assert.equal(fixture.scheduled.size, 0);
});

test('changing the configured root invalidates old measurements before publishing at the new root', async () => {
  const fixture = harness();
  const plugin = createPlugin(fixture.app, fixture.dependencies);
  await plugin.start({ path: '/dev/weather' });
  await Promise.resolve();
  const previousCount = fixture.deltas.length;
  await plugin.start({ path: '/dev/weather', rootPath: 'environment.weatherStations.deck' });
  await Promise.resolve();
  const updates = fixture.deltas.slice(previousCount).map(delta => delta.updates[0]);
  const oldUpdates = updates.filter(update => update.values?.some(entry => entry.path.startsWith('environment.weather.')));
  assert.equal(oldUpdates.length, 1);
  assert.ok(oldUpdates[0].values.every(entry => entry.value === null || entry.value === false));
  assert.ok(updates.some(update => update.values?.some(entry => entry.path === 'environment.weatherStations.deck.air.temperature' && entry.value === 298.15)));
  assert.equal(fixture.clients[0].closed, true);
  await plugin.stop();
});