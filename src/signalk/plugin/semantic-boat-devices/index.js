'use strict';

// Semantic Boat Devices: every Semantic Boat device in one place. The hub
// keeps the display designer and configuration catalog, lists the devices of
// all transports (README, "Transports"), and brings B1S devices (the
// Semantic Boat CAN network) into Signal K with configuration, calibration,
// console, and backups. Legacy RS485 displays come from the separate plugin
// Semantic Boat Devices RS485, which registers itself as a transport.

const path = require('node:path');
const fs = require('node:fs/promises');
const { DevicesHub } = require('./src/hub');

module.exports = app => {
  let hub = null;
  let statusTimer = null;
  let transition = Promise.resolve();
  const serialize = action => {
    transition = transition.then(action).catch(error => app.setPluginError(error.message));
    return transition;
  };
  const showStatus = instance => {
    const devices = instance.devices();
    const transports = [...instance.transports.values()].map(transport => transport.label ?? transport.id);
    app.setPluginStatus(`${devices.filter(device => device.online).length}/${devices.length} device(s) online` +
      (transports.length ? ` (${transports.join(', ')})` : '') + (instance.b1s ? `; B1S: ${instance.b1s.status()}` : ''));
  };
  const stop = async () => {
    clearInterval(statusTimer); statusTimer = null;
    const previous = hub; hub = null;
    if (previous) await previous.stop();
  };
  const plugin = {
    id: 'semantic-boat-devices',
    name: 'Semantic Boat Devices',
    description: 'All Semantic Boat devices: display designer and catalog, device transports, B1S CAN devices.',
    schema: {
      type: 'object',
      properties: {
        b1s: { type: 'object', title: 'B1S (Semantic Boat CAN network)', properties: {
          enabled: { type: 'boolean', title: 'B1S devices: measurements, configuration, console', default: false },
          analyzerEvent: { type: 'string', title: 'Decoded messages event of the B1S connection', default: 'b1s-N2KAnalyzerOut' },
          outEvent: { type: 'string', title: 'Raw output event of the B1S connection', default: 'b1s-nmea2000out' },
          jsonOutEvent: { type: 'string', title: 'JSON output event of the B1S connection', default: 'b1s-nmea2000JsonOut' },
          registryPath: { type: 'string', title: 'B1S registry (empty: the repository copy)', default: '' }
        } }
      }
    },
    start: (options = {}) => serialize(async () => {
      await stop();
      const instance = new DevicesHub(app, app.getDataDirPath(), error => app.setPluginError(error.message));
      hub = instance;
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
      for (const name of ['app.js', 'devices.js', 'style.css']) router.get(`/ui/${name}`,
        (_request, response) => response.sendFile(path.join(publicDirectory, name)));
      router.get('/ui/lucide.js', (_request, response) => response.sendFile(require.resolve('lucide/dist/umd/lucide.js')));
      const action = handler => async (request, response) => {
        const origin = request.get?.('origin');
        if (request.method !== 'GET' && origin && origin !== `${request.protocol}://${request.get('host')}`) {
          return response.status(403).json({ error: 'Cross-origin writes are disabled' });
        }
        if (!hub) return response.status(503).json({ error: 'Semantic Boat Devices is not running' });
        try { response.json(await handler(request, hub)); }
        catch (error) { response.status(400).json({ error: error.message }); }
      };
      // Devices of all transports.
      router.get('/devices', action((_request, instance) => instance.snapshot()));
      router.post('/devices/:transport/:id/install', action(async (request, instance) => {
        await instance.install(request.params.transport, request.params.id, request.body?.name);
        return { sent: request.body?.name, status: 'Sent; the display validates and restarts (no install acknowledgement)' };
      }));
      // Display configuration catalog (the designer).
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
      router.post('/b1s/devices/:id/log', b1s((request, adapter) => adapter.setLogStream(request.params.id, request.body?.minutes)));
      router.get('/b1s/backups', b1s((_request, adapter) => adapter.backups()));
      router.get('/b1s/backups/:id', b1s((request, adapter) => adapter.backup(request.params.id)));
      router.post('/b1s/devices/:id/restore', b1s((request, adapter) => adapter.restore(request.params.id, request.body ?? {})));
    }
  };
  return plugin;
};
