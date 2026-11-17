'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { loadRegistry } = require('../src/b1s/registry');
const { decodeSession, engineStartSummary, DiagnosticSessionStore } = require('../src/b1s/diagnostics');

const registry = loadRegistry();
// Written by the firmware library's host test (src/lib/b1s/tests/diagnostics_tests.cpp).
const fixture = fs.readFileSync(path.join(__dirname, 'fixtures/engine-start-session.bin'));

test('a diagnostic session decodes into series, events and phases', () => {
  const session = decodeSession(fixture, registry, channel => (channel === 21 ? { label: 'Engine hours' } : {}));
  assert.strictEqual(session.kind, 'engineStart');
  assert.strictEqual(session.endReason, 'finished');
  assert.strictEqual(session.start, new Date(1794870000 * 1000).toISOString());
  assert.strictEqual(session.durationMs, 1500);
  const [battery, speed, missing] = session.series;
  assert.deepStrictEqual([battery.channel, battery.role, battery.unit], [13, 'batteryVoltage', 'V']);
  assert.deepStrictEqual([speed.role, speed.unit], ['starterVoltage', 'Hz']);
  // Lead-in 5, waiting 11 (100 ms), cranking 51 (10 ms) samples.
  assert.strictEqual(battery.t.length, 5 + 11 + 51);
  assert.deepStrictEqual(battery.t.slice(0, 2), [-500, -400]);
  assert.strictEqual(battery.v[0], 13.28);
  assert.strictEqual(battery.v.at(-1), 10.5);
  assert.deepStrictEqual(battery.t.slice(-2), [1490, 1500]);
  assert.strictEqual(speed.v.at(-1), 4.16667); // 250 rpm in Hz
  assert.ok(missing.v.every(value => value === null));
  assert.deepStrictEqual(session.phases, [{ t: 0, phase: 'waiting' }, { t: 1000, phase: 'cranking' }]);
  const hours = session.events.find(event => event.snapshot);
  assert.deepStrictEqual([hours.channel, hours.label, hours.quantity, hours.value], [21, 'Engine hours', 'runTime', 2185560]);
  assert.deepStrictEqual(session.events.filter(event => !event.snapshot).map(event => [event.input, event.on]),
    [['ignition', true], ['starter', false]]);
  const summary = engineStartSummary(session);
  assert.strictEqual(summary.outcome, 'started');
  assert.strictEqual(summary.restingV, 13.28);
  assert.strictEqual(summary.crankAttempts, 1);
  assert.strictEqual(summary.cranks[0].durationMs, 500);
  assert.strictEqual(summary.cranks[0].minBatteryV, 10.5);
});

test('the store fetches a complete session in pieces, saves it and releases it', async context => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'sessions-'));
  context.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const requests = [];
  const client = {
    async service(address, name, fields = []) {
      requests.push(name);
      if (name === 'diagnosticSessionInfo') {
        const bytes = Buffer.alloc(16); bytes.writeUInt16LE(1, 0); bytes[2] = 1; bytes[3] = 2;
        bytes.writeUInt32LE(fixture.length, 4); bytes.writeUInt32LE(1794870000, 8);
        return { status: 0, bytes: [...bytes] };
      }
      if (name === 'diagnosticSessionRead') {
        const offset = Buffer.from(fields.slice(2, 6)).readUInt32LE(0);
        const piece = fixture.subarray(offset, offset + fields[6]);
        const head = Buffer.alloc(7); head.writeUInt16LE(1, 0); head.writeUInt32LE(offset, 2); head[6] = piece.length;
        return { status: 0, bytes: [...head, ...piece] };
      }
      return { status: 0, bytes: [1, 0] };
    }
  };
  const device = { id: 719643, address: 40, lastSeen: Date.now(), product: { model: 'Semantic Boat Engine Monitor' },
    channels: new Map([[13, { label: '12 V system voltage', location: 'engine.system' }]]) };
  const store = new DiagnosticSessionStore({ client, state: { devices: new Map([[device.id, device]]) }, registry, directory });
  await store.poll();
  assert.deepStrictEqual([requests[0], requests.at(-1)], ['diagnosticSessionInfo', 'diagnosticSessionRelease']);
  assert.strictEqual(requests.filter(name => name === 'diagnosticSessionRead').length, Math.ceil(fixture.length / 200));
  const [listed] = await store.list();
  assert.strictEqual(listed.kind, 'engineStart');
  assert.strictEqual(listed.summary.outcome, 'started');
  const session = await store.read(listed.name);
  assert.strictEqual(session.series[0].label, '12 V system voltage');
  assert.strictEqual(Buffer.from(session.raw, 'base64').length, fixture.length);
  await assert.rejects(store.read('../secret.json'), /invalid/);
});
