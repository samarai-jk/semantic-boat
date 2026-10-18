'use strict';

// Turns decoded B1S messages into Signal K deltas (src/lib/b1s/README.md,
// "Signal K"). Devices are identified by the NAME unique number of their
// address claim (the device ID), never by address. A measurement's path comes
// from its quantity's Signal K template and the channel's location; channels
// without a location go to b1s.unassigned.<device ID>.<channel>.<quantity>.
// Unknown devices and channels are asked for their address claim and Channel
// Descriptions, at most once per requestIntervalMs.

const isoAddressClaim = 60928;
const productInformation = 126996;
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
  // pathPrefix: Signal K namespace of all B1S paths (plugin setting);
  // devicePrefix(id): the namespace of one device (Pi-side device setting).
  // channelPath(id, channel): a custom path set for a channel ('' = none).
  constructor(registry, { onDelta, request, now = Date.now, requestIntervalMs = 10000, pathPrefix = '',
    devicePrefix = () => '', channelPath = () => '' }) {
    this.pathPrefix = pathPrefix;
    this.devicePrefix = devicePrefix;
    this.channelPath = channelPath;
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
    if (n2k.pgn === productInformation) return this.product(n2k.src, fields);
    if (n2k.pgn !== this.registry.pgns.measurement && n2k.pgn !== this.registry.pgns.channelDescription) return;
    if (field(fields, 'manufacturerCode', 'Manufacturer Code') !== this.registry.manufacturerCode) return;
    this.lastMessageAt = this.now();
    const device = this.deviceAt(n2k.src);
    if (device) device.lastSeen = this.lastMessageAt;
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
      device = { id, address, channels: new Map(), values: new Map(), product: null };
      this.devices.set(id, device);
      this.ask(address, productInformation);
    }
    if (device.address !== address) this.addresses.delete(device.address);
    device.address = address;
    this.addresses.set(address, id);
  }

  product(address, fields) {
    const device = this.deviceAt(address);
    if (!device) return;
    device.product = {
      code: field(fields, 'productCode', 'Product Code') ?? null,
      model: field(fields, 'modelId', 'Model ID') ?? '',
      software: field(fields, 'softwareVersionCode', 'Software Version Code') ?? '',
      hardware: field(fields, 'modelVersion', 'Model Version') ?? ''
    };
  }

  deviceById(id) {
    return this.devices.get(Number(id));
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
      noStandard: field(fields, 'noStandard', 'No Standard PGN') === 1,
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

  // Path of a channel sent as a B1S message. A custom path is used as it is;
  // one starting with * goes below the prefixes. Otherwise the quantity's
  // template with the channel's location (or the unassigned path), below the
  // plugin's and the device's prefixes if set.
  pathOf(device, description, quantityCode) {
    const prefixes = [this.pathPrefix, this.devicePrefix(device.id)];
    const custom = this.channelPath(device.id, description?.channel);
    if (custom) {
      return custom.startsWith('*') ? [...prefixes, custom.replace(/^\*\.?/, '')].filter(Boolean).join('.') : custom;
    }
    const quantity = this.registry.quantities.get(quantityCode);
    const name = quantity ? quantity.id : `quantity${quantityCode}`;
    const base = description?.location && quantity?.signalk
      ? quantity.signalk.replace('{location}', description.location)
      : `b1s.unassigned.${device.id}.${description?.channel ?? '?'}.${name}`;
    return [...prefixes, base].filter(Boolean).join('.');
  }

  // A value Signal K mapped from a standard PGN of a B1S device (its own
  // NMEA 2000 mapping decides the path; prefixes do not apply).
  standardValue(entry) {
    const device = this.deviceAt(Number(entry.source?.src));
    if (!device) return;
    device.standard ??= new Map();
    device.standard.set(`${entry.source.pgn}:${entry.source.instance ?? ''}`,
      { path: entry.path, value: entry.value, at: Date.parse(entry.timestamp) || this.now() });
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
    device.values.set(channel, { value, at: this.now() });
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
      product: device.product,
      online: device.lastSeen !== undefined && this.now() - device.lastSeen < 10000,
      pathPrefix: this.devicePrefix(device.id),
      channels: [...device.channels.values()].sort((a, b) => a.channel - b.channel).map(channel => {
        const quantity = this.registry.quantities.get(channel.kind);
        const standardPgn = quantity?.standard_pgn ?? null;
        let latest = device.values.get(channel.channel);
        let path = this.pathOf(device, channel, channel.kind);
        if (channel.encoding === 'standard') {
          // Learned from Signal K's mapping once the first value arrived.
          const mapped = device.standard?.get(`${standardPgn}:${channel.standardInstance ?? ''}`);
          latest = mapped;
          path = mapped?.path ?? null;
        } else if (channel.encoding === 'off') {
          latest = null; // not sent: an older value would mislead
        }
        return {
          ...channel,
          quantity: quantity?.id ?? null,
          unit: quantity?.unit ?? null,
          standardPgn,
          customPath: this.channelPath(device.id, channel.channel),
          value: latest?.value ?? null,
          ageMs: latest?.at ? Math.max(0, this.now() - latest.at) : null,
          path
        };
      })
    }));
  }
}

module.exports = { B1sState, scaled };
