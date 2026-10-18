'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { DevicesHub, hubProperty } = require('../src/hub');

async function hub(context) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'boat-hub-'));
  context.after(() => fs.rm(directory, { recursive: true, force: true }));
  const published = [];
  const errors = [];
  const app = { emitPropertyValue: (name, value) => published.push({ name, value }) };
  const instance = new DevicesHub(app, directory, error => errors.push(error));
  await instance.start({});
  return { instance, published, errors };
}

test('the hub publishes its interface on start and null on stop', async context => {
  const { instance, published } = await hub(context);
  assert.equal(published[0].name, hubProperty);
  assert.equal(published[0].value.apiVersion, 1);
  assert.equal(published[0].value.catalog, instance.catalog);
  await instance.stop();
  assert.deepEqual(published.at(-1), { name: hubProperty, value: null });
});

test('transports register, list their devices in one form, and unregister', async context => {
  const { instance, published, errors } = await hub(context);
  const installs = [];
  const transport = {
    id: 'rs485', label: 'RS485',
    devices: () => [{ id: 'remote-a-1', label: 'Remote', kind: 'display', online: true, status: 'online' }],
    install: (id, name) => { installs.push([id, name]); }
  };
  const unregister = published[0].value.registerTransport(transport);
  published[0].value.registerTransport({ id: 'zigbee', label: 'Zigbee', devices: () => { throw new Error('radio down'); } });
  assert.deepEqual(instance.snapshot().transports, [{ id: 'rs485', label: 'RS485' }, { id: 'zigbee', label: 'Zigbee' }]);
  assert.deepEqual(instance.devices(), [{ id: 'remote-a-1', label: 'Remote', kind: 'display', online: true, status: 'online',
    transport: 'rs485', transportLabel: 'RS485', canInstall: true }]);
  assert.equal(errors[0].message, 'radio down');
  await assert.rejects(instance.install('rs485', 'remote-a-1', 'missing.json'));
  await instance.catalog.save('test.json', { schema: 'semantic-display/v1', name: 'Test', sections: [{ id: 'test',
    title: 'TEST', pages: [{ id: 'test', title: 'TEST', grid: { columns: 1, rows: 1 },
      widgets: [{ type: 'text', text: 'Test', cell: { column: 0, row: 0 } }] }] }] });
  await instance.install('rs485', 'remote-a-1', 'test.json');
  assert.deepEqual(installs, [['remote-a-1', 'test.json']]);
  await assert.rejects(instance.install('zigbee', 'x', 'test.json'), /cannot install/);
  unregister();
  assert.deepEqual(instance.snapshot().transports.map(entry => entry.id), ['zigbee']);
  assert.throws(() => instance.registerTransport({ id: 'Bad Id', devices: () => [] }), /lowercase/);
  assert.throws(() => instance.registerTransport({ id: 'wifi' }), /devices\(\) is missing/);
});
