'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { HistoryProvider, validateOptions } = require('../src/history');

test('history queries the own vessel and the preferred source, aligned to now, and falls back to every source', async () => {
  const statements = [];
  const now = 1_000_000_000;
  const fetchImpl = async url => {
    const statement = url.searchParams.get('q');
    statements.push(statement);
    // The preferred source has no data; all sources do.
    const values = statement.includes('"source"') ? [[now - 3000, null]]
      : [[now - 3000, 1.5], [now - 2000, null], [now - 1000, 2.5]];
    return { ok: true, json: async () => ({ results: [{ series: [{ values }] }] }) };
  };
  const app = { selfContext: 'vessels.urn:mrn:imo:mmsi:1', getSelfPath: () => ({ $source: "can0.1'x" }) };
  const provider = new HistoryProvider({ app, options: {}, fetchImpl, now: () => now });
  const values = await provider.values({ path: 'environment.wind.speedTrue', intervalMs: 1000, points: 3, reducer: 4 });
  assert.deepEqual(values, [1.5, null, 2.5]);
  assert.equal(statements.length, 2);
  assert.match(statements[0], /^SELECT max\("value"\) FROM "environment\.wind\.speedTrue" WHERE "context" = 'vessels\.urn:mrn:imo:mmsi:1' AND "source" = 'can0\.1\\'x'/);
  assert.match(statements[0], new RegExp(`time > ${now - 3000}ms AND time <= ${now}ms GROUP BY time\\(1000ms, 0ms\\) fill\\(null\\)`));
  assert.doesNotMatch(statements[1], /"source"/);
  await assert.rejects(provider.values({ path: 'bad"path', intervalMs: 1000, points: 3, reducer: 1 }), /unsupported path/);
  assert.throws(() => validateOptions({ url: 'file:///x' }), /http/);
  assert.throws(() => validateOptions({ database: 'a;b' }), /database/);
});

test('an empty connection setting takes the one of signalk-to-influxdb, credentials in a header', async () => {
  const fs = require('node:fs'); const os = require('node:os'); const path = require('node:path');
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'history-'));
  const writer = path.join(directory, 'signalk-to-influxdb.json');
  fs.writeFileSync(writer, JSON.stringify({ enabled: true, configuration: { protocol: 'http', host: 'influx.local',
    port: 8087, database: 'boat', username: 'reader', password: 'secret' } }));
  const requests = [];
  const fetchImpl = async (url, init) => {
    requests.push({ url, init });
    return { ok: true, json: async () => ({ results: [{ series: [{ values: [] }] }] }) };
  };
  const app = { selfContext: 'vessels.self', getSelfPath: () => undefined };
  const provider = new HistoryProvider({ app, options: {}, writerSettings: writer, fetchImpl, now: () => 5000 });
  await provider.values({ path: 'navigation.speedOverGround', intervalMs: 1000, points: 2, reducer: 2 });
  assert.equal(requests[0].url.origin, 'http://influx.local:8087');
  assert.equal(requests[0].url.searchParams.get('db'), 'boat');
  assert.equal(requests[0].url.searchParams.get('p'), null);
  assert.equal(requests[0].init.headers.Authorization, `Basic ${Buffer.from('reader:secret').toString('base64')}`);
  const status = provider.status();
  assert.equal(status.from, 'signalk-to-influxdb');
  assert.ok(!JSON.stringify(status).includes('secret'));
  // An own URL wins; without any settings file the defaults apply.
  const own = new HistoryProvider({ app, options: { url: 'http://db:9999', database: 'x' }, writerSettings: writer, fetchImpl });
  assert.equal(own.connection().url, 'http://db:9999');
  const none = new HistoryProvider({ app, options: {}, writerSettings: path.join(directory, 'missing.json'), fetchImpl });
  assert.equal(none.connection().url, 'http://localhost:8086');
  fs.rmSync(directory, { recursive: true });
});
