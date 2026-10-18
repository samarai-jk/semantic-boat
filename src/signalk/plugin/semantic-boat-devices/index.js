'use strict';

const path = require('node:path');
const fs = require('node:fs/promises');
const { DevicesRuntime } = require('./src/runtime');

module.exports = app => {
  let runtime = null;
  let statusTimer = null;
  const showStatus = instance => app.setPluginStatus(`Live Signal K gateway: ${instance.links.length} device(s)` +
    (instance.b1s ? `; B1S: ${instance.b1s.status()}` : ''));
  let transition = Promise.resolve();
  const serialize = action => {
    transition = transition.then(action).catch(error => app.setPluginError(error.message));
    return transition;
  };
  const stop = async () => {
    clearInterval(statusTimer); statusTimer = null;
    const previous = runtime; runtime = null;
    if (previous) await previous.stop();
  };
  const plugin = {
    id: 'semantic-boat-devices',
    name: 'Semantic Boat Devices',
    description: 'Live Signal K bindings, a shared display configuration catalog, and B1S CAN devices.',
    schema: {
      type: 'object',
      properties: {
        buses: { type: 'array', title: 'Device buses', default: [], items: {
          type: 'object', required: ['id', 'type', 'path'], properties: {
            id: { type: 'string', title: 'Bus ID' },
            type: { type: 'string', enum: ['serial-point-to-point', 'rs485-addressed'], default: 'rs485-addressed' },
            path: { type: 'string', title: 'Stable serial device path' },
            baudRate: { type: 'integer', minimum: 1200, maximum: 1000000, default: 115200 }
          }
        } },
        devices: { type: 'array', title: 'Devices', default: [], items: {
          type: 'object', required: ['id', 'bus', 'address'], properties: {
            id: { type: 'string', title: 'Stable device ID' },
            label: { type: 'string', title: 'Device label' },
            bus: { type: 'string', title: 'Bus ID' },
            address: { type: 'integer', minimum: 1, maximum: 254, title: 'Stored device address' }
          }
        } },
        b1s: { type: 'object', title: 'B1S (Semantic Boat CAN network)', properties: {
          enabled: { type: 'boolean', title: 'B1S devices: measurements, configuration, console', default: false },
          analyzerEvent: { type: 'string', title: 'Decoded messages event of the B1S connection', default: 'b1s-N2KAnalyzerOut' },
          outEvent: { type: 'string', title: 'Raw output event of the B1S connection', default: 'b1s-nmea2000out' },
          jsonOutEvent: { type: 'string', title: 'JSON output event of the B1S connection', default: 'b1s-nmea2000JsonOut' },
          registryPath: { type: 'string', title: 'B1S registry (empty: the repository copy)', default: '' }
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
      const instance = new DevicesRuntime(app, app.getDataDirPath(), error => app.setPluginError(error.message));
      runtime = instance;
      try {
        await instance.start(options);
        showStatus(instance);
        statusTimer = setInterval(() => showStatus(instance), 5000);
      } catch (error) { await stop(); throw error; }
    }),
    stop: () => serialize(stop),
    registerWithRouter: router => {
      const publicDirectory = path.join(__dirname, 'public');
      router.get('/ui', (request, response) => request.path.endsWith('/')
        ? response.sendFile(path.join(publicDirectory, 'index.html')) : response.redirect('ui/'));
      for (const name of ['app.js', 'b1s.js', 'style.css']) router.get(`/ui/${name}`,
        (_request, response) => response.sendFile(path.join(publicDirectory, name)));
      router.get('/ui/lucide.js', (_request, response) => response.sendFile(require.resolve('lucide/dist/umd/lucide.js')));
      const action = handler => async (request, response) => {
        const origin = request.get?.('origin');
        if (request.method !== 'GET' && origin && origin !== `${request.protocol}://${request.get('host')}`) {
          return response.status(403).json({ error: 'Cross-origin writes are disabled' });
        }
        if (!runtime) return response.status(503).json({ error: 'Gateway is not running' });
        try { response.json(await handler(request, runtime)); }
        catch (error) { response.status(400).json({ error: error.message }); }
      };
      router.get('/state', action((_request, instance) => instance.snapshot()));
      router.get('/configurations', action(async (_request, instance) => ({ names: await instance.catalog.list() })));
      router.get('/configurations/:name', action((request, instance) => instance.catalog.read(request.params.name)));
      router.put('/configurations/:name', action(async (request, instance) => {
        await instance.catalog.save(request.params.name, request.body); return { saved: request.params.name };
      }));
      router.delete('/configurations/:name', action(async (request, instance) => {
        await instance.catalog.remove(request.params.name); return { deleted: request.params.name };
      }));
      router.get('/templates', action(async () => {
        const directory = path.join(__dirname, 'configs');
        const entries = await fs.readdir(directory);
        const templates = {};
        for (const name of entries.filter(name => name.endsWith('.json'))) {
          templates[name] = JSON.parse(await fs.readFile(path.join(directory, name), 'utf8'));
        }
        return templates;
      }));
      // B1S devices (src/lib/b1s/CONFIGURATION.md).
      const b1s = handler => action((request, instance) => {
        if (!instance.b1s) throw new Error('B1S is not enabled in the plugin configuration');
        return handler(request, instance.b1s);
      });
      const channelOf = value => (value === undefined || value === null || value === '' ? null : Number(value));
      router.get('/b1s/devices', b1s((_request, adapter) => adapter.devices()));
      router.get('/b1s/devices/:id/parameters', b1s((request, adapter) => adapter.parameters(request.params.id)));
      router.put('/b1s/devices/:id/parameters/:name', b1s((request, adapter) => adapter.setParameter(
        request.params.id, request.params.name, { ...request.body, channel: channelOf(request.body?.channel) })));
      router.post('/b1s/devices/:id/commands/:name', b1s((request, adapter) => adapter.command(
        request.params.id, request.params.name, { ...request.body, channel: channelOf(request.body?.channel) })));
      router.post('/b1s/devices/:id/console', b1s((request, adapter) => adapter.sendConsole(request.params.id, request.body?.line ?? '')));
      router.get('/b1s/devices/:id/console', b1s((request, adapter) => adapter.consoleOutput(request.params.id, request.query.since)));
      router.get('/b1s/backups', b1s((_request, adapter) => adapter.backups()));
      router.get('/b1s/backups/:id', b1s((request, adapter) => adapter.backup(request.params.id)));
      router.post('/b1s/devices/:id/restore', b1s((request, adapter) => adapter.restore(request.params.id, request.body ?? {})));
      router.post('/devices/:id/install', action(async (request, instance) => {
        await instance.install(request.params.id, request.body.name);
        return { sent: request.body.name, status: 'Awaiting device validation and restart; not an install acknowledgement' };
      }));
    }
  };
  return plugin;
};