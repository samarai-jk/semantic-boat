'use strict';

// The Semantic Boat Devices hub (README, "Transports"): the display
// configuration catalog behind the designer, a registry of device
// transports, and B1S as its built-in transport. Transport plugins (RS485,
// later others) find the hub through the Signal K property value
// `semanticBoatDevicesHub` and register themselves.

const path = require('node:path');
const { Catalog } = require('./catalog');
const { B1sAdapter } = require('./b1s/adapter');

const hubProperty = 'semanticBoatDevicesHub';
const apiVersion = 1;

function validateOptions(options) {
  const b1s = options.b1s ?? {};
  for (const name of ['analyzerEvent', 'outEvent', 'jsonOutEvent']) {
    if (b1s[name] !== undefined && (typeof b1s[name] !== 'string' || !/^[\w:-]+$/.test(b1s[name]))) {
      throw new Error(`B1S ${name} must be an event name`);
    }
  }
  return { b1s };
}

function checkTransport(transport) {
  if (!transport || typeof transport.id !== 'string' || !/^[a-z0-9][a-z0-9-]*$/.test(transport.id)) {
    throw new Error('A transport needs an id of lowercase letters, digits, and dashes');
  }
  if (typeof transport.devices !== 'function') throw new Error(`Transport ${transport.id}: devices() is missing`);
}

class DevicesHub {
  constructor(app, directory, onError, pluginId = 'semantic-boat-devices') {
    this.app = app;
    this.directory = directory;
    this.catalog = new Catalog(path.join(directory, 'configurations'));
    this.onError = onError;
    this.pluginId = pluginId;
    this.transports = new Map();
    this.b1s = null;
    // What transport plugins see of the hub.
    this.api = Object.freeze({
      apiVersion,
      catalog: this.catalog,
      registerTransport: transport => this.registerTransport(transport)
    });
  }

  async start(options) {
    const { b1s } = validateOptions(structuredClone(options));
    await this.catalog.init();
    if (b1s.enabled) {
      this.b1s = new B1sAdapter({ app: this.app, options: b1s, directory: this.directory, onError: this.onError,
        onDelta: delta => this.app.handleMessage(this.pluginId, delta) });
      await this.b1s.start();
      this.registerTransport({ id: 'b1s', label: 'B1S', devices: () => this.b1s.transportDevices() });
    }
    this.app.emitPropertyValue?.(hubProperty, this.api);
  }

  async stop() {
    this.app.emitPropertyValue?.(hubProperty, null);
    this.b1s?.stop();
    this.b1s = null;
    this.transports.clear();
  }

  // Returns the function that unregisters the transport again.
  registerTransport(transport) {
    checkTransport(transport);
    this.transports.set(transport.id, transport);
    return () => {
      if (this.transports.get(transport.id) === transport) this.transports.delete(transport.id);
    };
  }

  // Every device of every transport, in one form.
  devices() {
    const devices = [];
    for (const transport of this.transports.values()) {
      let list = [];
      try { list = transport.devices() ?? []; } catch (error) { this.onError(error); }
      for (const device of list) {
        devices.push({ ...device, id: String(device.id), transport: transport.id,
          transportLabel: transport.label ?? transport.id, canInstall: typeof transport.install === 'function' &&
          device.kind === 'display' });
      }
    }
    return devices;
  }

  async install(transportId, deviceId, name) {
    const transport = this.transports.get(transportId);
    if (!transport) throw new Error(`Unknown transport ${transportId}`);
    if (typeof transport.install !== 'function') throw new Error(`${transport.label ?? transportId} cannot install configurations`);
    await this.catalog.read(name); // exists and is valid
    await transport.install(deviceId, name);
  }

  snapshot() {
    return {
      transports: [...this.transports.values()].map(transport => ({ id: transport.id, label: transport.label ?? transport.id })),
      devices: this.devices()
    };
  }
}

module.exports = { DevicesHub, validateOptions, hubProperty, apiVersion };
