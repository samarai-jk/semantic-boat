'use strict';

// Semantic Boat B1S: decodes the B1S messages on the B1S CAN provider and
// publishes the measurements under their Signal K paths. Standard PGNs from
// B1S devices are decoded by Signal K itself. Protocol: src/lib/b1s/README.md.
//
// The B1S provider (canbus-canboatjs on the B1S interface) must use its own
// event names, so that B1S traffic stays apart from NMEA 2000 inside Signal K:
//   "analyzerOutEvent": "b1s-N2KAnalyzerOut",
//   "outEvent": "b1s-nmea2000out", "jsonOutEvent": "b1s-nmea2000JsonOut"

const { loadRegistry } = require('./src/registry');
const { canboatDefinitions } = require('./src/canboat');
const { B1sState } = require('./src/state');

const isoRequest = 59904;
const isoAddressClaim = 60928;

module.exports = app => {
  let running = null;

  const plugin = {
    id: 'semantic-boat-b1s',
    name: 'Semantic Boat B1S',
    description: 'B1S devices on the Semantic Boat CAN network',
    schema: {
      type: 'object',
      properties: {
        analyzerEvent: { type: 'string', title: 'Decoded messages event of the B1S provider', default: 'b1s-N2KAnalyzerOut' },
        jsonOutEvent: { type: 'string', title: 'Output event of the B1S provider', default: 'b1s-nmea2000JsonOut' },
        registryPath: { type: 'string', title: 'B1S registry (empty: the repository copy)', default: '' }
      }
    },

    start(options = {}) {
      const analyzerEvent = options.analyzerEvent || 'b1s-N2KAnalyzerOut';
      const jsonOutEvent = options.jsonOutEvent || 'b1s-nmea2000JsonOut';
      const registry = loadRegistry(options.registryPath || undefined);
      app.emitPropertyValue('canboat-custom-pgns', canboatDefinitions(registry));
      const request = (destination, pgn) =>
        app.emit(jsonOutEvent, { pgn: isoRequest, dst: destination, prio: 6, fields: { pgn } });
      const state = new B1sState(registry, {
        onDelta: delta => app.handleMessage(plugin.id, delta),
        request
      });
      const listener = n2k => {
        try {
          state.handle(n2k);
        } catch (error) {
          app.setPluginError(error.message);
        }
      };
      app.on(analyzerEvent, listener);
      // Discover the devices: every node answers a global request for its
      // address claim, B1S devices one for their Channel Descriptions.
      const discovery = [
        setTimeout(() => request(255, isoAddressClaim), 3000),
        setTimeout(() => request(255, registry.pgns.channelDescription), 4000)
      ];
      const timer = setInterval(() => app.setPluginStatus(state.status()), 5000);
      app.setPluginStatus('waiting for B1S messages');
      running = { state, stop: () => {
        app.removeListener(analyzerEvent, listener);
        discovery.forEach(clearTimeout);
        clearInterval(timer);
      } };
    },

    stop() {
      running?.stop();
      running = null;
    },

    registerWithRouter(router) {
      router.get('/devices', (request, response) => response.json(running ? running.state.snapshot() : []));
    }
  };
  return plugin;
};
