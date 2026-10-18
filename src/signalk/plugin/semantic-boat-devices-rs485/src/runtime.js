'use strict';

// The legacy RS485 / serial display gateway: one SerialLink per display, on a
// point-to-point serial port or a shared, master-polled RS485 bus.

const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { SerialLink } = require('./serial-link');
const { AddressedBus } = require('./addressed-bus');

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
    if (!['serial-point-to-point', 'rs485-addressed'].includes(bus.type)) throw new Error(`Bus ${bus.id}: unknown type`);
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
  return { buses, devices, mappings };
}

class Rs485Runtime {
  // catalog: function returning the hub's display configuration catalog (or null).
  constructor(app, catalog, onError) {
    this.app = app;
    this.catalog = catalog;
    this.onError = onError;
    this.links = [];
    this.buses = [];
  }

  async start(options) {
    const { buses, devices, mappings } = validateOptions(structuredClone(options));
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
    await Promise.all(this.links.map(link => link.stop()));
    await Promise.all(this.buses.map(bus => bus.stop()));
    this.links = []; this.buses = [];
  }

  // Displays in the form of the hub's transport contract.
  devices() {
    return this.links.map(link => {
      const detail = link.snapshot();
      return {
        id: detail.id,
        label: detail.label || detail.id,
        kind: 'display',
        online: detail.ready,
        status: detail.ready ? 'online' : detail.open ? 'waiting for display' : 'disconnected',
        address: `${detail.bus} · ${String(detail.address).padStart(3, '0')}`,
        detail
      };
    });
  }

  busStates() { return this.buses.map(bus => bus.snapshot()); }

  install(id, name) {
    const link = this.links.find(entry => entry.session.device.id === id);
    if (!link) throw new Error(`Unknown display ${id}`);
    return link.install(name);
  }
}

module.exports = { Rs485Runtime, validateOptions };
