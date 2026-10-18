'use strict';

// Turns decoded B1S messages into Signal K deltas (src/lib/b1s/README.md,
// "Signal K"). Devices are identified by the NAME unique number of their
// address claim (the device ID), never by address. A measurement's path comes
// from its quantity's Signal K template and the channel's location; channels
// without a location go to b1s.unassigned.<device ID>.<channel>.<quantity>.
// Unknown devices and channels are asked for their address claim and Channel
// Descriptions, at most once per requestIntervalMs.

const isoAddressClaim = 60928;
const specialValues = new Set([0x7fffffff, 0x7ffffffe, 0x7ffffffd]); // not available, error, out of range
const encodings = ['b1s', 'standard', 'off'];

// raw x scale without binary noise for decimal scales (300400 x 0.001 is
// 300.40000000000003, 300400 / 1000 is 300.4).
function scaled(raw, scale) {
  const divisor = Math.round(1 / scale);
  return scale < 1 && Math.abs(1 / scale - divisor) < 1e-6 ? raw / divisor : raw * scale;
}

function field(fields, id, name) {
  return fields[id] ?? fields[name];
}

class B1sState {
  constructor(registry, { onDelta, request, now = Date.now, requestIntervalMs = 10000 }) {
    this.registry = registry;
    this.onDelta = onDelta;
    this.request = request;
    this.now = now;
    this.requestIntervalMs = requestIntervalMs;
    this.devices = new Map(); // device ID -> { address, channels: Map, product }
    this.addresses = new Map(); // address -> device ID
    this.requests = new Map(); // `${address}:${pgn}` -> time
    this.metaSent = new Set();
    this.measurements = 0;
    this.lastMessageAt = null;
  }

  handle(n2k) {
    const fields = n2k.fields ?? {};
    if (n2k.pgn === isoAddressClaim) return this.addressClaim(n2k.src, fields);
    if (n2k.pgn !== this.registry.pgns.measurement && n2k.pgn !== this.registry.pgns.channelDescription) return;
    if (field(fields, 'manufacturerCode', 'Manufacturer Code') !== this.registry.manufacturerCode) return;
    this.lastMessageAt = this.now();
    const device = this.deviceAt(n2k.src);
    if (!device) {
      this.ask(n2k.src, isoAddressClaim);
      return;
    }
    if (n2k.pgn === this.registry.pgns.channelDescription) this.describe(device, fields);
    else this.measure(device, n2k, fields);
  }

  addressClaim(address, fields) {
    const id = field(fields, 'uniqueNumber', 'Unique Number');
    const manufacturer = field(fields, 'manufacturerCode', 'Manufacturer Code');
    if (id === undefined || id === null) return;
    // Another device that held this address has moved or gone.
    const previous = this.addresses.get(address);
    if (previous !== undefined && previous !== id) this.addresses.delete(address);
    if (manufacturer !== this.registry.manufacturerCode) return;
    let device = this.devices.get(id);
    if (!device) {
      device = { id, address, channels: new Map() };
      this.devices.set(id, device);
    }
    if (device.address !== address) this.addresses.delete(device.address);
    device.address = address;
    this.addresses.set(address, id);
  }

  deviceAt(address) {
    const id = this.addresses.get(address);
    return id === undefined ? undefined : this.devices.get(id);
  }

  describe(device, fields) {
    const channel = field(fields, 'channel', 'Channel');
    if (channel === undefined || channel === null) return;
    const encoding = field(fields, 'encoding', 'Encoding');
    const description = {
      channel,
      kind: field(fields, 'kind', 'Quantity or Output Kind'),
      encoding: encodings[encoding] ?? 'n/a',
      output: field(fields, 'output', 'Output Channel') === 1,
      standardInstance: field(fields, 'standardInstance', 'Standard PGN Instance') ?? null,
      period: field(fields, 'period', 'Nominal Period') ?? null,
      location: field(fields, 'location', 'Location') ?? '',
      label: field(fields, 'label', 'Label') ?? ''
    };
    const previous = device.channels.get(channel);
    if (previous && (previous.location !== description.location || previous.label !== description.label)) {
      this.metaSent.delete(this.pathOf(device, previous, previous.kind));
    }
    device.channels.set(channel, description);
  }

  pathOf(device, description, quantityCode) {
    const quantity = this.registry.quantities.get(quantityCode);
    const name = quantity ? quantity.id : `quantity${quantityCode}`;
    if (description?.location && quantity?.signalk) {
      return quantity.signalk.replace('{location}', description.location);
    }
    return `b1s.unassigned.${device.id}.${description?.channel ?? '?'}.${name}`;
  }

  measure(device, n2k, fields) {
    const channel = field(fields, 'channel', 'Channel');
    const quantityCode = field(fields, 'quantity', 'Quantity');
    if (channel === undefined || channel === null || quantityCode === undefined || quantityCode === null) return;
    let description = device.channels.get(channel);
    if (!description) {
      this.ask(n2k.src, this.registry.pgns.channelDescription);
      description = { channel, location: '', label: '' };
    }
    const quantity = this.registry.quantities.get(quantityCode);
    const raw = field(fields, 'value', 'Value');
    const value = raw === null || raw === undefined || specialValues.has(raw) || !quantity
      ? null
      : scaled(raw, quantity.scale);
    const path = this.pathOf(device, description, quantityCode);
    const update = {
      source: { label: 'b1s', type: 'NMEA2000', pgn: n2k.pgn, src: String(n2k.src) },
      values: [{ path, value }]
    };
    if (n2k.timestamp) update.timestamp = n2k.timestamp;
    if (!this.metaSent.has(path) && device.channels.has(channel)) {
      const meta = { description: `B1S device ${device.id}, channel ${channel}` };
      if (description.label) meta.displayName = description.label;
      if (quantity && quantity.unit !== '1') meta.units = quantity.unit;
      update.meta = [{ path, value: meta }];
      this.metaSent.add(path);
    }
    this.measurements += 1;
    this.onDelta({ updates: [update] });
  }

  ask(address, pgn) {
    const key = `${address}:${pgn}`;
    const now = this.now();
    const last = this.requests.get(key);
    if (last !== undefined && now - last < this.requestIntervalMs) return;
    this.requests.set(key, now);
    this.request(address, pgn);
  }

  status() {
    const channels = [...this.devices.values()].reduce((sum, device) => sum + device.channels.size, 0);
    const age = this.lastMessageAt === null ? 'none yet' : `${Math.round((this.now() - this.lastMessageAt) / 1000)} s ago`;
    return `${this.devices.size} device(s), ${channels} channel(s), ${this.measurements} measurement(s); last B1S message ${age}`;
  }

  // Devices and channels for the HTTP API and the status page.
  snapshot() {
    return [...this.devices.values()].map(device => ({
      id: device.id,
      address: device.address,
      channels: [...device.channels.values()].map(channel => ({
        ...channel,
        quantity: this.registry.quantities.get(channel.kind)?.id ?? null,
        path: channel.encoding === 'standard' ? null : this.pathOf(device, channel, channel.kind)
      }))
    }));
  }
}

module.exports = { B1sState };
