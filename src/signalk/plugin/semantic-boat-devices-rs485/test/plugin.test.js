'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const createPlugin = require('../index');
const { validateOptions } = require('../src/runtime');

test('options reject unsupported CAN and unsafe shared legacy serial buses', () => {
  const options = { buses: [{ id: 'rs485', type: 'serial-point-to-point', path: '/dev/example' }],
    devices: [{ id: 'display-2', bus: 'rs485', address: 2 }] };
  assert.equal(validateOptions(options).devices[0].address, 2);
  assert.throws(() => validateOptions({ buses: [{ id: 'can', type: 'semantic-can' }] }));
  assert.throws(() => validateOptions({ ...options, devices: [...options.devices,
    { id: 'display-1', bus: 'rs485', address: 1 }] }));
});
// A Signal K app with property values as the server keeps them: every
// subscriber gets all values emitted so far, then each new list.
function fakeApp() {
  const values = [];
  const subscribers = new Set();
  return {
    status: '', errors: [],
    setPluginStatus(status) { this.status = status; },
    setPluginError(message) { this.errors.push(message); },
    emitPropertyValue(name, value) {
      values.push({ name, value });
      for (const callback of subscribers) callback(values.filter(entry => entry.name === name));
    },
    onPropertyValues(name, callback) {
      subscribers.add(callback);
      callback(values.filter(entry => entry.name === name));
      return () => subscribers.delete(callback);
    }
  };
}

function fakeHub() {
  const registered = [];
  return { registered, api: { apiVersion: 1, catalog: { list: async () => [] },
    registerTransport(transport) { registered.push(transport); return () => registered.splice(registered.indexOf(transport), 1); } } };
}

test('the RS485 plugin registers as a transport with a hub that starts before or after it', async () => {
  for (const hubFirst of [true, false]) {
    const app = fakeApp();
    const hub = fakeHub();
    if (hubFirst) app.emitPropertyValue('semanticBoatDevicesHub', hub.api);
    const plugin = createPlugin(app);
    await plugin.start({});
    if (!hubFirst) {
      assert.equal(hub.registered.length, 0);
      assert.match(app.status, /not running/);
      app.emitPropertyValue('semanticBoatDevicesHub', hub.api);
    }
    assert.equal(hub.registered.length, 1);
    assert.equal(hub.registered[0].id, 'rs485');
    assert.deepEqual(hub.registered[0].devices(), []);
    app.emitPropertyValue('semanticBoatDevicesHub', null); // the hub stops
    assert.equal(hub.registered.length, 0);
    await plugin.stop();
    assert.deepEqual(app.errors, []);
  }
});
