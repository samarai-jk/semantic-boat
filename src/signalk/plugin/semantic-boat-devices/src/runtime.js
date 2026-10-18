'use strict';

const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { Catalog } = require('./catalog');
const { SerialLink } = require('./serial-link');
const { AddressedBus } = require('./addressed-bus');
const { B1sAdapter } = require('./b1s/adapter');

function validateOptions(options) {
  const buses = options.buses ?? [];
  const devices = options.devices ?? [];
  const mappings = Object.fromEntries((options.mappings ?? []).map(entry => {
    if (!entry.from || !entry.to || /[*\s]/.test(entry.from + entry.to)) throw new Error('Mappings require exact Signal K paths');
    return [entry.from, entry.to];
  }));
  const busIds = new Set();
  const paths = new Set();
  for (const bus of buses) {
    if (!bus.id || busIds.has(bus.id)) throw new Error('Bus IDs must be unique');
    if (!['serial-point-to-point', 'rs485-addressed'].includes(bus.type)) throw new Error(`Bus ${bus.id}: unknown type (B1S is configured under b1s, not as a bus)`);
    if (!bus.path || paths.has(bus.path)) throw new Error('Each serial port may belong to only one bus');
    bus.baudRate ??= 115200;
    if (!Number.isInteger(bus.baudRate) || bus.baudRate < 1200 || bus.baudRate > 1000000) throw new Error('Invalid baud rate');
    if (bus.type === 'rs485-addressed' && bus.baudRate !== 115200) throw new Error('Addressed RS485 currently requires 115200 baud');
    paths.add(bus.path); busIds.add(bus.id);
  }
  const ids = new Set();
  const occupied = new Set();
  const addresses = new Set();
  for (const device of devices) {
    if (!/^[a-zA-Z0-9][a-zA-Z0-9_-]*$/.test(device.id) || ids.has(device.id)) throw new Error('Device IDs must be unique strings');
    if (!busIds.has(device.bus)) throw new Error(`Unknown bus for ${device.id}`);
    if (!Number.isInteger(device.address) || device.address < 1 || device.address > 254) throw new Error('Device address must be 1-254');
    if (occupied.has(device.bus) && buses.find(bus => bus.id === device.bus).type === 'serial-point-to-point') throw new Error('Legacy serial buses support exactly one device; multi-drop requires the addressed protocol');
    const address = `${device.bus}:${device.address}`;
    if (addresses.has(address)) throw new Error('Device addresses must be unique on each bus');
    addresses.add(address);
    occupied.add(device.bus); ids.add(device.id);
  }
  const b1s = options.b1s ?? {};
  for (const name of ['analyzerEvent', 'outEvent', 'jsonOutEvent']) {
    if (b1s[name] !== undefined && (typeof b1s[name] !== 'string' || !/^[\w:-]+$/.test(b1s[name]))) {
      throw new Error(`B1S ${name} must be an event name`);
    }
  }
  return { buses, devices, mappings, b1s };
}

class DevicesRuntime {
  constructor(app, directory, onError, pluginId = 'semantic-boat-devices') {
    this.app = app;
    this.directory = directory;
    this.catalog = new Catalog(path.join(directory, 'configurations'));
    this.onError = onError;
    this.pluginId = pluginId;
    this.links = [];
    this.buses = [];
    this.b1s = null;
  }

  async start(options) {
    const { buses, devices, mappings, b1s } = validateOptions(structuredClone(options));
    await this.catalog.init();
    if (b1s.enabled) {
      this.b1s = new B1sAdapter({ app: this.app, options: b1s, directory: this.directory, onError: this.onError,
        onDelta: delta => this.app.handleMessage(this.pluginId, delta) });
      await this.b1s.start();
    }
    const protocol = await import(pathToFileURL(path.join(__dirname, '../../../../tools/disp-sim/src/protocol.js')));
    const masters = new Map();
    for (const bus of buses.filter(entry => entry.type === 'rs485-addressed')) {
      const master = new AddressedBus({ bus, protocol, onError: this.onError });
      masters.set(bus.id, master); this.buses.push(master);
    }
    for (const device of devices) {
      const bus = buses.find(entry => entry.id === device.bus);
      const master = masters.get(bus.id);
      const port = master?.devicePort(device.address);
      const link = new SerialLink({ app: this.app, bus, device, mappings, protocol,
        catalog: this.catalog, onError: this.onError, ...(port ? { Port: function () { return port; } } : {}) });
      this.links.push(link); link.start();
    }
    for (const master of this.buses) master.start();
  }

  async stop() {
    this.b1s?.stop();
    this.b1s = null;
    await Promise.all(this.links.map(link => link.stop()));
    await Promise.all(this.buses.map(bus => bus.stop()));
    this.links = []; this.buses = [];
  }
  snapshot() { return { devices: this.links.map(link => link.snapshot()), buses: this.buses.map(bus => bus.snapshot()),
    adapters: [{ id: 'serial-point-to-point', available: true },
      { id: 'rs485-addressed', available: true }, { id: 'b1s', available: Boolean(this.b1s) }],
    b1s: this.b1s ? { status: this.b1s.status() } : null }; }
  install(id, name) {
    const link = this.links.find(entry => entry.session.device.id === id);
    if (!link) throw new Error('Unknown device');
    return link.install(name);
  }
}

module.exports = { DevicesRuntime, validateOptions };