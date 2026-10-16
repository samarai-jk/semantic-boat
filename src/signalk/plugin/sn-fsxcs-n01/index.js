'use strict';

const { StationRuntime } = require('./src/runtime');

module.exports = (app, dependencies) => {
  let runtime = null;
  let operation = Promise.resolve();
  const transition = action => {
    operation = operation.then(action).catch(error => app.setPluginError(error.message));
    return operation;
  };
  const stop = async () => {
    const previous = runtime;
    runtime = null;
    if (previous) await previous.stop();
  };
  return {
    id: 'sn-fsxcs-n01',
    name: 'SN-*-FSXCS-N01 Weather Station',
    description: 'Read-only Modbus acquisition for the particulate variant; isolated from navigation and N2K paths.',
    schema: {
      type: 'object', required: ['path'], additionalProperties: false,
      properties: {
        path: { type: 'string', title: 'Stable serial device path (/dev/serial/by-id/...)' },
        stationId: { type: 'string', pattern: '^[a-zA-Z][a-zA-Z0-9]*$', default: 'station1', title: 'Stable source ID (independent of data paths)' },
        rootPath: { type: 'string', pattern: '^environment(?:\\.[a-zA-Z][a-zA-Z0-9]*)+$', default: 'environment.weather', title: 'Custom environment root path (standard instrument namespaces are protected)' },
        address: { type: 'integer', minimum: 1, maximum: 247, default: 1, title: 'Existing Modbus address (does not change the station)' },
        baudRate: { type: 'integer', enum: [2400, 4800, 9600], default: 4800, title: 'Existing station baud rate (does not change the station)' },
        pollIntervalMs: { type: 'integer', minimum: 250, maximum: 60000, default: 1000, title: 'Polling interval (ms)' },
        timeoutMs: { type: 'integer', minimum: 250, maximum: 60000, default: 1000, title: 'Response timeout (ms)' }
      }
    },
    start: (options = {}) => transition(async () => {
      await stop();
      runtime = new StationRuntime(app, options, dependencies);
      app.setPluginStatus('SN-*-FSXCS-N01: connecting; read-only acquisition');
      runtime.start();
    }),
    stop: () => transition(stop),
    registerWithRouter: router => {
      router.get('/state', (_request, response) => runtime
        ? response.json(runtime.snapshot()) : response.status(503).json({ error: 'Station plugin is not running' }));
    }
  };
};