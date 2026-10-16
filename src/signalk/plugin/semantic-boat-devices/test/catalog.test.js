'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { Catalog } = require('../src/catalog');
const { validateOptions } = require('../src/runtime');
const document = { schema: 'semantic-display/v1', name: 'Test', sections: [{
  id: 'test', title: 'TEST', pages: [{ id: 'test', title: 'TEST', grid: { columns: 1, rows: 1 },
    widgets: [{ type: 'text', text: 'Test', cell: { column: 0, row: 0 } }] }]
}] };

test('catalog validates, persists, lists, deletes and prevents path traversal', async context => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'boat-catalog-'));
  context.after(() => fs.rm(directory, { recursive: true, force: true }));
  const catalog = new Catalog(directory); await catalog.init();
  await catalog.save('test.json', document);
  assert.deepEqual(await catalog.list(), ['test.json']);
  assert.deepEqual(await catalog.read('test.json'), document);
  await assert.rejects(catalog.save('../test.json', document));
  await assert.rejects(catalog.save('invalid.json', {}));
  await assert.rejects(catalog.save('large.json', { ...document, name: 'x'.repeat(9000) }));
  const withAge = { ...document, permanent_sources: [{ id: 'position', path: 'navigation.position', outdated_ms: 10000, stale_ms: 60000 }] };
  assert.ok(catalog.validate(withAge));
  assert.throws(() => catalog.validate({ ...withAge, permanent_sources: [{ ...withAge.permanent_sources[0], stale_ms: 10000 }] }), /outdated_ms/);
  assert.throws(() => catalog.validate({ ...document, sections: [{ ...document.sections[0], sources: [{ ...withAge.permanent_sources[0], stale_ms: 5000 }] }] }), /outdated_ms/);
  assert.ok(catalog.validate({ ...withAge, permanent_sources: [{ ...withAge.permanent_sources[0], stale_ms: 0 }] }));
  await catalog.remove('test.json'); assert.deepEqual(await catalog.list(), []);
});

test('history plots require storage, valid ranges and the device RAM budget', () => {
  const catalog = new Catalog('/tmp');
  const plotted = structuredClone(document);
  plotted.permanent_sources = [{ id: 'temperature', path: 'environment.weather.air.temperature', unit: 'K',
    history: { interval_ms: 60000, points: 120, reducer: 'mean' } }];
  const page = plotted.sections[0].pages[0];
  page.history_plot = { source: 'temperature', display_unit: 'C', y_min: -10.5, y_max: 40 };
  assert.ok(catalog.validate(plotted));
  page.history_plot.source = 'missing';
  assert.throws(() => catalog.validate(plotted), /history storage/);
  page.history_plot.source = 'temperature'; page.history_plot.y_max = -20;
  assert.throws(() => catalog.validate(plotted), /range/);
  page.history_plot.y_min = -3e38; page.history_plot.y_max = 3e38;
  assert.throws(() => catalog.validate(plotted), /range/);
  page.history_plot.y_max = 40; delete page.history_plot.y_min;
  assert.throws(() => catalog.validate(plotted));
  delete page.history_plot.y_max;
  assert.ok(catalog.validate(plotted));
  plotted.sections[0].sources = [{ id: 'temperature', path: 'local.temperature' }];
  assert.throws(() => catalog.validate(plotted), /history storage/);
  delete plotted.sections[0].sources;
  plotted.permanent_sources[0].history.points = 249;
  assert.throws(() => catalog.validate(plotted), /1024/);
  plotted.permanent_sources[0].history.points = 120;
  plotted.permanent_sources[0].provider = 'signalk-text';
  assert.throws(() => catalog.validate(plotted), /numeric/);
});

test('navigation and weather templates provide permanent plot examples within RAM limits', async () => {
  const catalog = new Catalog('/tmp');
  for (const [name, plotCount, expectedBytes] of [['navigation.json', 3, 960], ['weather-station.json', 4, 1024]]) {
    const configuration = JSON.parse(await fs.readFile(path.join(__dirname, '../configs', name), 'utf8'));
    assert.ok(catalog.validate(configuration));
    const histories = configuration.permanent_sources.filter(source => source.history);
    assert.equal(histories.reduce((sum, source) => sum + 32 + 4 * source.history.points, 0), expectedBytes);
    const plots = configuration.sections.flatMap(section => section.pages).filter(page => page.history_plot);
    assert.equal(plots.length, plotCount);
    for (const page of plots) assert.ok(histories.some(source => source.id === page.history_plot.source));
  }
});

test('configuration transfer uses contiguous bounded chunks and complete CRC', async context => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'boat-transfer-'));
  context.after(() => fs.rm(directory, { recursive: true, force: true }));
  const catalog = new Catalog(directory); await catalog.init(); await catalog.save('test.json', document);
  const protocol = await import('../../../../tools/disp-sim/src/protocol.js');
  const sent = []; const send = async (type, payload) => sent.push({ type, payload });
  await catalog.transfer('test.json', send, protocol, 7);
  const chunks = sent.slice(1, -1); let offset = 0;
  for (const chunk of chunks) {
    assert.equal(chunk.type, protocol.MessageType.CONFIG_CHUNK);
    assert.equal(chunk.payload.readUInt32LE(2), offset);
    assert.ok(chunk.payload.length <= 38); offset += chunk.payload.length - 6;
  }
  const bytes = Buffer.concat(chunks.map(chunk => chunk.payload.subarray(6)));
  assert.equal(offset, sent[0].payload.readUInt32LE(2));
  assert.equal(protocol.crc32(bytes), sent[0].payload.readUInt32LE(6));
  assert.deepEqual(JSON.parse(bytes), document);
  await catalog.sendList(send, protocol, 8);
  assert.equal(sent.at(-1).type, protocol.MessageType.CONFIG_LIST_END);
});

test('options reject unsupported CAN and unsafe shared legacy serial buses', () => {
  const options = { buses: [{ id: 'rs485', type: 'serial-point-to-point', path: '/dev/example' }],
    devices: [{ id: 'display-2', bus: 'rs485', address: 2 }] };
  assert.equal(validateOptions(options).devices[0].address, 2);
  assert.throws(() => validateOptions({ buses: [{ id: 'can', type: 'semantic-can' }] }));
  assert.throws(() => validateOptions({ ...options, devices: [...options.devices,
    { id: 'display-1', bus: 'rs485', address: 1 }] }));
});