'use strict';

// B1S, the Semantic Boat CAN network, in Signal K (src/lib/b1s/README.md):
// measurements to Signal K paths, and configuration, calibration, console,
// and backups of B1S devices (src/lib/b1s/CONFIGURATION.md).
//
// The B1S connection (canbus-canboatjs on the B1S interface) uses its own
// event names, so B1S traffic stays apart from NMEA 2000 inside Signal K.

const fs = require('node:fs/promises');
const path = require('node:path');
const { loadRegistry } = require('./registry');
const { canboatDefinitions } = require('./canboat');
const { B1sState, scaled } = require('./state');
const { B1sClient } = require('./client');
const { decodeRecord, encodeRecord } = require('./records');

const isoRequest = 59904;
const isoAddressClaim = 60928;
const consoleLimit = 65536;
const quantityUnits = new Set(['quantity', 'quantityDelta', 'alarmValue', 'alarmDelta']);
// Alarm values are differences for these functions (registry, unit alarmValue).
const deltaFunctions = new Set(['change', 'rate', 'range']);

const defaults = {
  analyzerEvent: 'b1s-N2KAnalyzerOut',
  outEvent: 'b1s-nmea2000out',
  jsonOutEvent: 'b1s-nmea2000JsonOut',
  providerId: 'can1', // the B1S connection; its standard PGNs are mapped by Signal K
  pathPrefix: ''
};
const prefixPattern = /^([A-Za-z0-9]+(\.[A-Za-z0-9]+)*)?$/;
// A custom channel path: Signal K path segments, optionally after * (below the prefixes).
const customPathPattern = /^(\*\.?)?[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)*$/;

function hex(byte) {
  return byte.toString(16).padStart(2, '0');
}

class B1sAdapter {
  constructor({ app, options = {}, directory, onDelta, onError = () => {}, pluginId = 'semantic-boat-devices' }) {
    this.pluginId = pluginId;
    this.app = app;
    this.options = { ...defaults, ...options };
    this.directory = path.join(directory, 'b1s-backups');
    this.onDelta = onDelta;
    this.onError = onError;
    this.consoles = new Map(); // device ID -> { text, start }: the device log as streamed
    this.settingsFile = path.join(directory, 'b1s-devices.json');
    this.deviceSettings = {}; // device ID -> { pathPrefix }: Pi-side settings of a device
    this.streams = new Map();  // device ID -> time the device stops its log stream
    this.timers = [];
  }

  async start() {
    const registry = loadRegistry(this.options.registryPath || undefined);
    this.registry = registry;
    if (!prefixPattern.test(this.options.pathPrefix ?? '')) throw new Error('B1S path prefix: letters and digits, separated by dots');
    await fs.mkdir(this.directory, { recursive: true });
    this.deviceSettings = JSON.parse(await fs.readFile(this.settingsFile, 'utf8').catch(() => '{}'));
    this.app.emitPropertyValue('canboat-custom-pgns', canboatDefinitions(registry));
    const request = (destination, pgn) => this.app.emit(this.options.jsonOutEvent,
      { pgn: isoRequest, dst: destination, prio: 6, fields: { pgn } });
    this.state = new B1sState(registry, { onDelta: this.onDelta, request, pathPrefix: this.options.pathPrefix ?? '',
      devicePrefix: id => this.deviceSettings[id]?.pathPrefix ?? '',
      channelPath: (id, channel) => this.deviceSettings[id]?.channels?.[channel]?.path ?? '' });
    // Standard PGNs of B1S devices: Signal K maps them itself; their paths,
    // values, and times are learned from its stream.
    const standardPgns = new Set([...registry.quantities.values()].map(entry => entry.standard_pgn).filter(Boolean));
    this.unsubscribeStream = this.app.streambundle?.getSelfBus().onValue(entry => {
      if (!entry.isMeta && entry.source?.label === this.options.providerId && standardPgns.has(entry.source.pgn)) {
        this.state.standardValue(entry);
      }
    });
    this.client = new B1sClient(registry, {
      send: (destination, pgn, bytes, priority) => this.app.emit(this.options.outEvent,
        `${new Date().toISOString()},${priority},${pgn},0,${destination},${bytes.length},${bytes.map(hex).join(',')}`),
      onConsole: (address, text) => this.appendConsole(address, text)
    });
    this.listener = n2k => {
      try {
        if (!this.client.handle(n2k)) this.state.handle(n2k);
      } catch (error) {
        this.onError(error);
      }
    };
    this.app.on(this.options.analyzerEvent, this.listener);
    // Discover the devices: every node answers a global request for its
    // address claim, B1S devices one for their Channel Descriptions.
    this.timers.push(setTimeout(() => request(255, isoAddressClaim), 3000),
      setTimeout(() => request(255, registry.pgns.channelDescription), 4000));
  }

  stop() {
    // Devices stop their streams by themselves; stopping them now saves the bus the rest.
    for (const [id, until] of this.streams) {
      const device = this.state?.deviceById(id);
      if (device && until > Date.now()) this.client.logStream(device.address, 0).catch(() => {});
    }
    this.streams.clear();
    if (this.listener) this.app.removeListener(this.options.analyzerEvent, this.listener);
    this.unsubscribeStream?.();
    this.timers.forEach(clearTimeout);
    this.listener = null;
  }

  status() {
    return this.state ? this.state.status() : 'B1S not started';
  }

  devices() {
    return this.state.snapshot();
  }

  // Pi-side settings of a device (they concern Signal K, not the device).
  async setDeviceSettings(id, { pathPrefix } = {}) {
    const device = this.device(id);
    const prefix = String(pathPrefix ?? '').trim();
    if (!prefixPattern.test(prefix)) throw new Error('Path prefix: letters and digits, separated by dots');
    this.deviceSettings[device.id] = { ...this.deviceSettings[device.id], pathPrefix: prefix };
    await this.saveDeviceSettings();
    return { pathPrefix: prefix, pluginPrefix: this.options.pathPrefix ?? '' };
  }

  // Custom Signal K path of a channel ('' = automatic); Pi-side.
  async setChannelSettings(id, channel, { path: wanted } = {}) {
    const device = this.device(id);
    const number = Number(channel);
    if (!device.channels.has(number)) throw new Error(`Unknown channel ${channel}`);
    const path = String(wanted ?? '').trim();
    if (path && !customPathPattern.test(path)) {
      throw new Error('Path: Signal K path segments separated by dots, optionally after * (below the prefixes)');
    }
    const settings = this.deviceSettings[device.id] ??= {};
    settings.channels ??= {};
    if (path) settings.channels[number] = { path };
    else delete settings.channels[number];
    await this.saveDeviceSettings();
    return { channel: number, path };
  }

  async saveDeviceSettings() {
    await fs.writeFile(`${this.settingsFile}.tmp`, JSON.stringify(this.deviceSettings, null, 2));
    await fs.rename(`${this.settingsFile}.tmp`, this.settingsFile);
    this.state.metaSent.clear(); // new paths get their metadata
  }

  // Other sources writing a channel's Signal K path (e.g. the boat's NMEA 2000
  // network): the channel would overwrite or mix with their values.
  conflicts(device, channel) {
    if (!channel.path || !this.app.getSelfPath) return [];
    const node = this.app.getSelfPath(channel.path);
    if (!node) return [];
    const sources = node.values ? Object.keys(node.values) : node.$source ? [node.$source] : [];
    const own = new Set([`${this.pluginId}.${device.address}`, `${this.options.providerId}.${device.address}`]);
    return sources.filter(source => !own.has(source));
  }

  // B1S devices in the form of the hub's transport contract.
  transportDevices() {
    return this.state.snapshot().map(device => ({
      id: String(device.id),
      label: device.product?.model || `B1S device ${device.id}`,
      kind: 'b1s',
      online: device.online,
      status: device.online ? 'online' : 'silent',
      address: `address ${device.address}`,
      detail: { ...device, pluginPrefix: this.options.pathPrefix ?? '',
        channels: device.channels.map(channel => ({ ...channel, conflicts: this.conflicts(device, channel),
          standardSources: this.registry.standardSources[channel.standardPgn] ?? {} })) }
    }));
  }

  device(id) {
    const device = this.state.deviceById(id);
    if (!device) throw new Error(`Unknown B1S device ${id}`);
    return device;
  }

  quantityOf(device, channel) {
    return this.registry.quantities.get(device.channels.get(channel)?.kind);
  }

  // A parameter as the API shows it: records decoded into fields; values
  // with a quantity unit also in SI (si, siUnit), with `absolute` telling
  // which are absolute (a temperature, not a difference). Records: si and
  // absolute per field.
  describeParameter(entry, device, channel, status, raw) {
    const parameter = { name: entry.id, code: entry.code, scope: entry.scope, type: entry.type, access: entry.access,
      unit: entry.unit ?? null, values: entry.values ?? null, record: entry.record ?? null,
      description: entry.description ?? '', channel, status, value: raw, si: null, siUnit: null, absolute: null };
    if (status !== 'done') return parameter;
    const quantity = this.quantityOf(device, channel);
    if (entry.type === 'record') {
      const layout = this.registry.records[entry.record] ?? [];
      parameter.value = decodeRecord(layout, raw);
      if (quantity) {
        parameter.si = {};
        parameter.absolute = {};
        const delta = deltaFunctions.has(parameter.value.function);
        for (const field of layout.filter(field => quantityUnits.has(field.unit))) {
          parameter.si[field.name] = scaled(parameter.value[field.name], quantity.scale);
          parameter.absolute[field.name] = field.unit === 'quantity' || (field.unit === 'alarmValue' && !delta);
        }
        parameter.siUnit = quantity.unit;
      }
    } else if (quantity && quantityUnits.has(entry.unit) && typeof raw === 'number') {
      parameter.si = scaled(raw, quantity.scale);
      parameter.siUnit = quantity.unit;
      parameter.absolute = entry.unit === 'quantity';
    }
    return parameter;
  }

  // All parameters of a device, read live, then stored as its backup.
  async parameters(id) {
    const device = this.device(id);
    const codes = await this.client.parameterCodes(device.address);
    const channels = [...device.channels.keys()].sort((a, b) => a - b);
    const result = [];
    for (const code of codes) {
      const entry = this.registry.parameters.get(code) ?? { id: `parameter${code}`, code, scope: 'device', access: 'ro' };
      for (const channel of entry.scope === 'channel' ? channels : [null]) {
        const { status, value } = await this.client.read(device.address, code, channel ?? 0xff);
        if (status === 'unknown parameter' && channel !== null) continue; // e.g. no measurement behind the channel
        result.push(this.describeParameter(entry, device, channel, status, value));
      }
    }
    await this.saveBackup(device, result);
    return { id: device.id, address: device.address, product: device.product, parameters: result };
  }

  // value: raw number, text, enum name, or for a record its fields (raw
  // values, enum names, flag lists); si: a number in the SI unit for
  // parameters with a quantity unit, or for a record an object of fields in
  // SI that replace the raw ones (converted with the channel's scale).
  async setParameter(id, name, { channel = null, value, si } = {}) {
    const device = this.device(id);
    const entry = this.registry.parameterNames.get(name);
    if (!entry) throw new Error(`Unknown parameter ${name}`);
    if (entry.access !== 'rw') throw new Error(`${name} is read-only`);
    if (entry.scope === 'channel' && !device.channels.has(channel)) throw new Error(`Unknown channel ${channel}`);
    const quantity = this.quantityOf(device, channel);
    const toRaw = number => {
      if (!quantity) throw new Error(`${name} has no SI form`);
      return Math.round(Number(number) / quantity.scale);
    };
    let raw = value;
    if (entry.type === 'record') {
      const layout = this.registry.records[entry.record];
      if (!value || typeof value !== 'object') throw new Error(`${name} needs its fields`);
      const fields = { ...value };
      for (const [field, number] of Object.entries(si ?? {})) {
        if (number !== null && number !== undefined) fields[field] = toRaw(number);
      }
      raw = encodeRecord(layout, fields);
    } else if (si !== undefined && si !== null) {
      raw = toRaw(si);
    } else if (entry.type === 'enum' && typeof value === 'string') {
      raw = entry.values.indexOf(value);
      if (raw < 0) throw new Error(`${name}: one of ${entry.values.join(', ')}`);
    } else if (entry.type !== 'string') {
      raw = Number(value);
      if (!Number.isInteger(raw)) throw new Error(`${name} needs an integer`);
    }
    if (entry.type === 'string' && typeof raw !== 'string') throw new Error(`${name} needs a text`);
    const result = await this.client.write(device.address, entry.code, entry.scope === 'channel' ? channel : 0xff, raw);
    if (result.status === 'done') {
      result.value = this.describeParameter(entry, device, channel, 'done', result.value).value;
      await this.updateBackup(device, name, channel, result.value);
    }
    return result;
  }

  async command(id, name, { channel = null, argument, si } = {}) {
    const device = this.device(id);
    let raw = argument ?? -1;
    if (si !== undefined && si !== null) {
      const quantity = this.quantityOf(device, channel);
      if (!quantity) throw new Error(`${name} needs a measurement channel`);
      raw = Math.round(si / quantity.scale);
    }
    return { result: await this.client.command(device.address, name, channel ?? 0xff, raw) };
  }

  appendConsole(address, text) {
    const device = this.state.deviceAt(address);
    if (!device) return;
    const log = this.consoles.get(device.id) ?? { text: '', start: 0 };
    log.text += text.replace(/\r/g, '');
    if (log.text.length > consoleLimit) {
      const cut = log.text.length - consoleLimit;
      log.text = log.text.slice(cut);
      log.start += cut;
    }
    this.consoles.set(device.id, log);
  }

  // Runs a console line on the device; its output arrives with the log,
  // which the device streams for at least 30 s after a console line.
  sendConsole(id, line = '') {
    const device = this.device(id);
    this.client.console(device.address, String(line));
    const until = Date.now() + 30000;
    if ((this.streams.get(device.id) ?? 0) < until) this.streams.set(device.id, until);
    return { sent: line };
  }

  // Starts (1..60 minutes) or stops (0) the device's log stream.
  async setLogStream(id, minutes) {
    const device = this.device(id);
    const wanted = Math.max(0, Math.min(60, Math.round(Number(minutes) || 0)));
    const remaining = await this.client.logStream(device.address, wanted);
    this.streams.set(device.id, Date.now() + remaining * 1000);
    return { remaining };
  }

  // Log text since position `since`; next is the position to ask for next;
  // remaining: seconds until the device stops its stream (as last known).
  consoleOutput(id, since = 0) {
    const device = this.device(id);
    const log = this.consoles.get(device.id) ?? { text: '', start: 0 };
    const from = Math.max(Number(since) || 0, log.start);
    const remaining = Math.max(0, Math.round(((this.streams.get(device.id) ?? 0) - Date.now()) / 1000));
    return { text: log.text.slice(from - log.start), next: log.start + log.text.length, remaining };
  }

  backupPath(id) {
    return path.join(this.directory, `${Number(id)}.json`);
  }

  async saveBackup(device, parameters) {
    const backup = {
      deviceId: device.id, product: device.product, savedAt: new Date().toISOString(),
      piSettings: this.deviceSettings[device.id] ?? {},
      parameters: parameters.filter(parameter => parameter.access === 'rw' && parameter.status === 'done')
        .map(({ name, channel, value }) => ({ name, channel, value }))
    };
    await this.writeBackup(device.id, backup);
  }

  async updateBackup(device, name, channel, value) {
    const backup = await this.backup(device.id).catch(() => null);
    if (!backup) return;
    const entry = backup.parameters.find(parameter => parameter.name === name && parameter.channel === channel);
    if (entry) entry.value = value;
    else backup.parameters.push({ name, channel, value });
    backup.savedAt = new Date().toISOString();
    await this.writeBackup(device.id, backup);
  }

  async writeBackup(id, backup) {
    const file = this.backupPath(id);
    await fs.writeFile(`${file}.tmp`, JSON.stringify(backup, null, 2));
    await fs.rename(`${file}.tmp`, file);
  }

  async backup(id) {
    return JSON.parse(await fs.readFile(this.backupPath(id), 'utf8'));
  }

  async backups() {
    const names = (await fs.readdir(this.directory)).filter(name => /^\d+\.json$/.test(name));
    const result = [];
    for (const name of names) {
      const backup = JSON.parse(await fs.readFile(path.join(this.directory, name), 'utf8'));
      result.push({ deviceId: backup.deviceId, product: backup.product, savedAt: backup.savedAt,
        parameters: backup.parameters.length });
    }
    return result;
  }

  // Writes a backup onto a device: its own (from = its ID) or another
  // device's (replacement). Calibration belongs to the physical sensors and
  // is copied only from the device's own backup unless `calibration` is set.
  async restore(id, { from = id, calibration } = {}) {
    const device = this.device(id);
    const backup = await this.backup(from);
    const ownBackup = Number(from) === device.id;
    const copyCalibration = calibration ?? ownBackup;
    const results = [];
    if (backup.piSettings?.pathPrefix !== undefined) {
      await this.setDeviceSettings(device.id, { pathPrefix: backup.piSettings.pathPrefix });
      results.push({ name: 'pathPrefix (Pi)', channel: null, value: backup.piSettings.pathPrefix, status: 'done' });
    }
    for (const [channel, settings] of Object.entries(backup.piSettings?.channels ?? {})) {
      if (!device.channels.has(Number(channel))) continue;
      await this.setChannelSettings(device.id, channel, settings);
      results.push({ name: 'path (Pi)', channel: Number(channel), value: settings.path, status: 'done' });
    }
    for (const parameter of backup.parameters) {
      if (!copyCalibration && parameter.name.startsWith('calibration')) continue;
      if (parameter.channel !== null && !device.channels.has(parameter.channel)) {
        results.push({ ...parameter, status: 'no such channel' });
        continue;
      }
      try {
        const { status } = await this.setParameter(device.id, parameter.name,
          { channel: parameter.channel, value: parameter.value });
        results.push({ ...parameter, status });
      } catch (error) {
        results.push({ ...parameter, status: error.message });
      }
    }
    return { restored: results.filter(result => result.status === 'done').length, results };
  }
}

module.exports = { B1sAdapter };
