'use strict';

// Semantic Boat Devices RS485: the legacy display gateway for Semantic Boat
// displays on RS485 or another serial line (semantic-link protocol). It is a
// transport of the Semantic Boat Devices hub: the hub lists its displays,
// offers the display designer and configuration catalog, and installs
// configurations through it. Without the hub the displays keep their live
// values and installed configurations.

const { Rs485Runtime } = require('./src/runtime');

// Contract: src/signalk/plugin/semantic-boat-devices/README.md, "Transports".
const hubProperty = 'semanticBoatDevicesHub';

module.exports = app => {
  let runtime = null;
  let hub = null;
  let registration = null;
  let unsubscribeHub = null;
  let statusTimer = null;
  let transition = Promise.resolve();
  const serialize = action => {
    transition = transition.then(action).catch(error => app.setPluginError(error.message));
    return transition;
  };

  const transport = {
    id: 'rs485',
    label: 'RS485',
    devices: () => runtime ? runtime.devices() : [],
    install: (id, name) => {
      if (!runtime) throw new Error('RS485 gateway is not running');
      return runtime.install(id, name);
    }
  };

  const connect = () => {
    registration?.();
    registration = hub && runtime ? hub.registerTransport(transport) : null;
  };
  const showStatus = () => {
    const devices = transport.devices();
    app.setPluginStatus(`${devices.filter(device => device.online).length}/${devices.length} display(s) online; ` +
      (hub ? 'catalog from Semantic Boat Devices' : 'Semantic Boat Devices not running: no configuration installs'));
  };
  const stop = async () => {
    clearInterval(statusTimer); statusTimer = null;
    registration?.(); registration = null;
    unsubscribeHub?.(); unsubscribeHub = null;
    const previous = runtime; runtime = null;
    if (previous) await previous.stop();
  };

  const plugin = {
    id: 'semantic-boat-devices-rs485',
    name: 'Semantic Boat Devices RS485',
    description: 'Legacy Semantic Boat displays on RS485 or a serial line; managed by Semantic Boat Devices.',
    schema: {
      type: 'object',
      properties: {
        buses: { type: 'array', title: 'Serial buses', default: [], items: {
          type: 'object', required: ['id', 'type', 'path'], properties: {
            id: { type: 'string', title: 'Bus ID' },
            type: { type: 'string', enum: ['serial-point-to-point', 'rs485-addressed'], default: 'rs485-addressed' },
            path: { type: 'string', title: 'Stable serial device path' },
            baudRate: { type: 'integer', minimum: 1200, maximum: 1000000, default: 115200 }
          }
        } },
        devices: { type: 'array', title: 'Displays', default: [], items: {
          type: 'object', required: ['id', 'bus', 'address'], properties: {
            id: { type: 'string', title: 'Stable display ID' },
            label: { type: 'string', title: 'Display label' },
            bus: { type: 'string', title: 'Bus ID' },
            address: { type: 'integer', minimum: 1, maximum: 254, title: 'Stored display address' }
          }
        } },
        mappings: { type: 'array', title: 'Exact Signal K path mappings', default: [], items: {
          type: 'object', required: ['from', 'to'], properties: {
            from: { type: 'string', title: 'Requested display path' },
            to: { type: 'string', title: 'Actual Signal K path' }
          }
        } }
      }
    },
    start: (options = {}) => serialize(async () => {
      await stop();
      const instance = new Rs485Runtime(app, () => hub?.catalog ?? null, error => app.setPluginError(error.message));
      runtime = instance;
      try {
        await instance.start(options);
      } catch (error) { await stop(); throw error; }
      // The hub publishes itself (or null when it stops) as a property value;
      // late subscribers receive all values so far, the last one counts.
      unsubscribeHub = app.onPropertyValues?.(hubProperty, values => {
        const latest = values.filter(Boolean).at(-1)?.value ?? null;
        hub = latest && latest.apiVersion === 1 ? latest : null;
        connect();
      }) ?? null;
      connect();
      showStatus();
      statusTimer = setInterval(showStatus, 5000);
    }),
    stop: () => serialize(stop),
    registerWithRouter: router => {
      router.get('/state', (_request, response) => response.json(runtime
        ? { displays: transport.devices(), buses: runtime.busStates(), hub: Boolean(hub) }
        : { displays: [], buses: [], hub: Boolean(hub) }));
    }
  };
  return plugin;
};
