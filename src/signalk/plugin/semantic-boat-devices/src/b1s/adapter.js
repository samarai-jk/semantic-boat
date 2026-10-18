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

const isoRequest = 59904;
const isoAddressClaim = 60928;
const consoleLimit = 65536;
const absoluteKinds = new Set([0, 1]); // alarm rules: high, low

const defaults = {
  analyzerEvent: 'b1s-N2KAnalyzerOut',
  outEvent: 'b1s-nmea2000out',
  jsonOutEvent: 'b1s-nmea2000JsonOut'
};

function hex(byte) {
  return byte.toString(16).padStart(2, '0');
}

class B1sAdapter {
  constructor({ app, options = {}, directory, onDelta, onError = () => {} }) {
    this.app = app;
    this.options = { ...defaults, ...options };
    this.directory = path.join(directory, 'b1s-backups');
    this.onDelta = onDelta;
    this.onError = onError;
    this.consoles = new Map(); // device ID -> { text, start }
    this.timers = [];
  }

  async start() {
    const registry = loadRegistry(this.options.registryPath || undefined);
    this.registry = registry;
    await fs.mkdir(this.directory, { recursive: true });
    this.app.emitPropertyValue('canboat-custom-pgns', canboatDefinitions(registry));
    const request = (destination, pgn) => this.app.emit(this.options.jsonOutEvent,
      { pgn: isoRequest, dst: destination, prio: 6, fields: { pgn } });
    this.state = new B1sState(registry, { onDelta: this.onDelta, request });
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
    if (this.listener) this.app.removeListener(this.options.analyzerEvent, this.listener);
    this.timers.forEach(clearTimeout);
    this.listener = null;
  }

  status() {
    return this.state ? this.state.status() : 'B1S not started';
  }

  devices() {
    return this.state.snapshot();
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
      detail: device
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

  // SI form of a raw parameter value: { si, unit, absolute } or null.
  siOf(entry, device, channel, raw, ruleKind) {
    if (typeof raw !== 'number') return null;
    const quantity = this.quantityOf(device, channel);
    if (['quantity', 'quantityDelta', 'alarmRule'].includes(entry.unit) && quantity) {
      const absolute = entry.unit === 'quantity' || (entry.unit === 'alarmRule' && absoluteKinds.has(ruleKind));
      return { si: scaled(raw, quantity.scale), unit: quantity.unit, absolute };
    }
    return null;
  }

  // All parameters of a device, read live, then stored as its backup.
  async parameters(id) {
    const device = this.device(id);
    const codes = await this.client.parameterCodes(device.address);
    const channels = [...device.channels.keys()].sort((a, b) => a - b);
    const result = [];
    for (const code of codes) {
      const entry = this.registry.parameters.get(code) ?? { id: `parameter${code}`, scope: 'device', access: 'ro' };
      for (const channel of entry.scope === 'channel' ? channels : [null]) {
        const { status, value } = await this.client.read(device.address, code, channel ?? 0xff);
        if (status === 'unknown parameter' && channel !== null) continue; // e.g. a rule the channel lacks
        result.push({ name: entry.id, code, scope: entry.scope, type: entry.type, access: entry.access,
          unit: entry.unit ?? null, values: entry.values ?? null, description: entry.description ?? '',
          channel, status, value });
      }
    }
    // Alarm thresholds are absolute or differences depending on their rule.
    for (const parameter of result) {
      const rule = parameter.name.startsWith('alarmThreshold') ? parameter.name.at(-1) : null;
      const ruleValue = rule === null ? null
        : result.find(other => other.name === `alarmRule${rule}` && other.channel === parameter.channel)?.value;
      const converted = this.siOf(this.registry.parameters.get(parameter.code) ?? {}, device, parameter.channel,
        parameter.value, ruleValue === null || ruleValue === undefined ? null : ruleValue & 0xff);
      // A sensor-fault rule has no threshold to set.
      if (rule !== null && ruleValue !== null && ruleValue !== undefined && (ruleValue & 0xff) === 3) parameter.access = 'ro';
      parameter.si = converted?.si ?? null;
      parameter.siUnit = converted?.unit ?? null;
      parameter.absolute = converted?.absolute ?? null;
    }
    await this.saveBackup(device, result);
    return { id: device.id, address: device.address, product: device.product, parameters: result };
  }

  // value: raw number, text, enum name; si: a number in the SI unit, for
  // parameters with a quantity unit (converted with the channel's scale).
  async setParameter(id, name, { channel = null, value, si } = {}) {
    const device = this.device(id);
    const entry = this.registry.parameterNames.get(name);
    if (!entry) throw new Error(`Unknown parameter ${name}`);
    if (entry.access !== 'rw') throw new Error(`${name} is read-only`);
    if (entry.scope === 'channel' && !device.channels.has(channel)) throw new Error(`Unknown channel ${channel}`);
    let raw = value;
    if (si !== undefined && si !== null) {
      const quantity = this.quantityOf(device, channel);
      if (!quantity) throw new Error(`${name} has no SI form`);
      raw = Math.round(si / quantity.scale);
    } else if (entry.type === 'enum' && typeof value === 'string') {
      raw = entry.values.indexOf(value);
      if (raw < 0) throw new Error(`${name}: one of ${entry.values.join(', ')}`);
    } else if (entry.type !== 'string') {
      raw = Number(value);
      if (!Number.isInteger(raw)) throw new Error(`${name} needs an integer`);
    }
    if (entry.type === 'string' && typeof raw !== 'string') throw new Error(`${name} needs a text`);
    const result = await this.client.write(device.address, entry.code, entry.scope === 'channel' ? channel : 0xff, raw);
    if (result.status === 'done') await this.updateBackup(device, name, channel, result.value);
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

  // Runs a console line on the device (empty: keep its log coming).
  sendConsole(id, line = '') {
    const device = this.device(id);
    this.client.console(device.address, String(line));
    return { sent: line };
  }

  // Console output since position `since`; next is the position to ask for next.
  consoleOutput(id, since = 0) {
    const log = this.consoles.get(this.device(id).id) ?? { text: '', start: 0 };
    const from = Math.max(Number(since) || 0, log.start);
    return { text: log.text.slice(from - log.start), next: log.start + log.text.length };
  }

  backupPath(id) {
    return path.join(this.directory, `${Number(id)}.json`);
  }

  async saveBackup(device, parameters) {
    const backup = {
      deviceId: device.id, product: device.product, savedAt: new Date().toISOString(),
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
