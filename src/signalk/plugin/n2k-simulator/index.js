'use strict';

const { SimulatorRuntime } = require('./src/runtime');
const { defaults } = require('./src/simulation');
const { readings } = require('./src/instruments');
const { timescales } = require('./src/fluctuation');
const { registerUi } = require('./src/ui');

module.exports = app => {
  let runtime = null;
  let operation = Promise.resolve();
  const transition = action => {
    operation = operation.then(action).catch(error => app.setPluginError(error.message));
    return operation;
  };
  const stopRuntime = async () => {
    const instance = runtime;
    runtime = null;
    if (instance) await instance.stop();
  };
  const plugin = {
    id: 'semantic-boat-n2k-simulator',
    name: 'Semantic Boat N2K Simulator',
    description: 'Development-only sailboat data. Do not enable against live vessel data.',
    schema: {
      type: 'object',
      properties: {
        output: { type: 'string', enum: ['n2k', 'signalk'], default: 'n2k', title: 'Output mode' },
        tcpPort: { type: 'integer', minimum: 1024, maximum: 65535, default: 10112, title: 'Local N2K TCP port' },
        sourceAddress: { type: 'integer', minimum: 0, maximum: 251, default: 200 },
        mode: { type: 'string', enum: ['marina', 'anchor', 'underway'], default: 'marina' }
      }
    },
    start: (options = {}) => transition(async () => {
      await stopRuntime();
      const output = options.output ?? 'n2k';
      if (!['n2k', 'signalk'].includes(output)) throw new RangeError('Invalid output mode');
      const instance = new SimulatorRuntime({
        controls: { mode: options.mode ?? 'marina' },
        sourceAddress: options.sourceAddress ?? 200,
        onDelta: message => { if (output === 'signalk') app.handleMessage(plugin.id, message); },
        onError: error => app.setPluginError(error.message)
      });
      instance.output = output;
      runtime = instance;
      try {
        const port = await instance.start(options.tcpPort ?? 10112);
        app.setPluginStatus(`SIMULATION ONLY: ${output}; N2K feed 127.0.0.1:${port}`);
      } catch (error) {
        await stopRuntime();
        throw error;
      }
    }),
    stop: () => transition(stopRuntime),
    registerWithRouter: router => {
      registerUi(router);
      const action = handler => (request, response) => {
        const origin = request.get?.('origin');
        if (request.method === 'POST' && origin && origin !== `${request.protocol}://${request.get('host')}`) {
          return response.status(403).json({ error: 'Cross-origin controls are disabled' });
        }
        if (!runtime) return response.status(503).json({ error: 'Simulator is not running' });
        try { response.json(handler(request)); }
        catch (error) { response.status(400).json({ error: error.message }); }
      };
      router.get('/state', action(() => runtime.snapshot()));
      router.get('/defaults', (_request, response) => response.json(defaults));
      router.get('/parameters', (_request, response) => response.json({ readings, timescales }));
      router.post('/controls', action(request => runtime.control(request.body)));
      router.post('/pause', action(() => { runtime.paused = true; return runtime.snapshot(); }));
      router.post('/resume', action(() => { runtime.paused = false; return runtime.snapshot(); }));
      router.post('/reset', action(() => runtime.reset()));
    }
  };
  return plugin;
};