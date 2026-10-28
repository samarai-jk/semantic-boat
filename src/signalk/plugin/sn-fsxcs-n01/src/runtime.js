'use strict';

const { prefix, metadata, decode, unavailable } = require('./measurements');

// RS485 sound meter (docs: ../semantic-boat-devices-rs485/docs/sound-meter.md)
// on the station's bus: function 03, register 0, dB(A) * 10.
const meterRetryMs = 10000;

function validateSoundMeter(options = {}, station) {
  const meter = {
    enabled: options.enabled ?? false,
    address: options.address ?? 1,
    baudRate: options.baudRate ?? 9600,
    path: options.path ?? 'environment.inside.noiseLevel'
  };
  if (typeof meter.path !== 'string' || !/^environment(?:\.[a-zA-Z][a-zA-Z0-9]*)+$/.test(meter.path)) {
    throw new Error('Sound meter path must be a Signal K path under environment with letter/digit segments');
  }
  if (meter.path.split('.')[1] === 'wind' || `${meter.path}.`.startsWith(`${station.rootPath}.`)) {
    throw new Error('Sound meter path overlaps the station root or environment.wind');
  }
  if (!Number.isInteger(meter.address) || meter.address < 1 || meter.address > 247) throw new Error('Sound meter Modbus address must be 1-247');
  if (![2400, 4800, 9600].includes(meter.baudRate)) throw new Error('Unsupported sound meter baud rate');
  // At the same baud rate both devices would answer the same address.
  if (meter.enabled && meter.baudRate === station.baudRate && meter.address === station.address) {
    throw new Error('Sound meter and station share baud rate and address');
  }
  return meter;
}

function decodeSoundLevel(words) {
  if (!Array.isArray(words) || words.length !== 1 || !Number.isInteger(words[0]) || words[0] < 0 || words[0] > 0xFFFF) {
    throw new Error('Expected exactly 1 unsigned sound level word');
  }
  // Specified range 30..130 dB(A); a little margin either side.
  return words[0] >= 200 && words[0] <= 1400 ? words[0] / 10 : null;
}

function validateOptions(options = {}) {
  const result = {
    path: options.path,
    stationId: options.stationId ?? 'station1',
    rootPath: options.rootPath ?? 'environment.weather',
    address: options.address ?? 1,
    baudRate: options.baudRate ?? 4800,
    pollIntervalMs: options.pollIntervalMs ?? 1000,
    timeoutMs: options.timeoutMs ?? 1000
  };
  prefix(result.rootPath);
  if (typeof result.stationId !== 'string' || !/^[a-zA-Z][a-zA-Z0-9]*$/.test(result.stationId)) {
    throw new Error('Station ID must start with a letter and contain only letters and digits');
  }
  if (typeof result.path !== 'string' || !result.path.startsWith('/dev/')) throw new Error('A serial device path is required');
  if (!Number.isInteger(result.address) || result.address < 1 || result.address > 247) throw new Error('Modbus address must be 1-247');
  if (![2400, 4800, 9600].includes(result.baudRate)) throw new Error('Unsupported station baud rate');
  for (const name of ['pollIntervalMs', 'timeoutMs']) {
    if (!Number.isInteger(result[name]) || result[name] < 250 || result[name] > 60000) throw new Error(`${name} must be 250-60000 ms`);
  }
  result.soundMeter = validateSoundMeter(options.soundMeter, result);
  return result;
}

class StationRuntime {
  constructor(app, options, { createClient = () => new (require('modbus-serial'))(),
    schedule = setTimeout, cancel = clearTimeout, now = Date.now } = {}) {
    this.app = app;
    this.options = validateOptions(options);
    this.createClient = createClient;
    this.schedule = schedule;
    this.cancel = cancel;
    this.now = now;
    this.client = null;
    this.clientBaudRate = null;
    this.timer = null;
    this.inflight = null;
    this.running = false;
    this.available = false;
    this.lastReadAt = null;
    this.raw = null;
    this.error = null;
    this.successfulReads = 0;
    this.failedReads = 0;
    this.attempts = 0;
    this.meter = { available: false, value: null, raw: null, lastReadAt: null, nextAt: 0,
      successfulReads: 0, failedReads: 0, error: null };
  }

  publish(update, src = this.options.stationId) {
    this.app.handleMessage('sn-fsxcs-n01', {
      context: 'vessels.self',
      updates: [{ timestamp: new Date(this.now()).toISOString(),
        source: { label: `sn-fsxcs-n01.${src}`, type: 'SignalK', src }, ...update }]
    });
  }

  publishMeter(value) {
    if (this.options.soundMeter.enabled) this.publish({ values: [{ path: this.options.soundMeter.path, value }] }, 'soundMeter');
  }

  statusPath(suffix) { return `${this.options.rootPath}.status.${suffix}`; }

  invalidate() {
    this.available = false;
    this.publish({ values: [...unavailable(this.options.rootPath), { path: this.statusPath('available'), value: false }] });
  }

  start() {
    this.running = true;
    this.publish({ meta: [...metadata(this.options.rootPath),
      { path: this.statusPath('available'), value: { displayName: 'Station data available', description: 'True after a complete validated read; false on failure or shutdown.' } },
      { path: this.statusPath('lastSuccessfulRead'), value: { displayName: 'Station last successful read', description: 'Host receipt time, not the individual sensor measurement time.' } }] });
    if (this.options.soundMeter.enabled) {
      this.publish({ meta: [{ path: this.options.soundMeter.path, value: { units: 'dB', displayName: 'Sound level',
        description: 'A-weighted sound pressure level, dB(A), from the RS485 sound meter; logarithmic, not a linear quantity.' } }] },
      'soundMeter');
    }
    this.invalidate();
    this.publishMeter(null);
    this.tick();
  }

  tick() {
    this.timer = null;
    this.inflight = this.cycle();
    return this.inflight;
  }

  async disconnect() {
    const client = this.client;
    this.client = null;
    this.clientBaudRate = null;
    if (client?.isOpen) await new Promise(resolve => client.close(resolve));
  }

  // One port, reopened when the next device uses another baud rate.
  async open(baudRate, address) {
    if (!this.client?.isOpen || this.clientBaudRate !== baudRate) {
      await this.disconnect();
      this.client = this.createClient();
      await this.client.connectRTUBuffered(this.options.path, { baudRate, dataBits: 8, parity: 'none', stopBits: 1 });
      this.clientBaudRate = baudRate;
      this.client.setTimeout(this.options.timeoutMs);
    }
    this.client.setID(address);
  }

  async cycle() {
    const startedAt = this.now();
    this.attempts += 1;
    try {
      await this.open(this.options.baudRate, this.options.address);
      if (!this.running) return;
      const result = await this.client.readHoldingRegisters(500, 16);
      const values = decode(result.data, this.options.rootPath);
      if (!this.running) return;
      this.lastReadAt = this.now();
      this.raw = [...result.data];
      this.available = true;
      this.error = null;
      this.successfulReads += 1;
      this.publish({ values: [...values,
        { path: this.statusPath('available'), value: true },
        { path: this.statusPath('lastSuccessfulRead'), value: new Date(this.lastReadAt).toISOString() }] });
      this.app.setPluginStatus(`SN-*-FSXCS-N01 ${this.options.stationId}: receiving at ${this.options.baudRate} baud, address ${this.options.address}`);
    } catch (error) {
      if (this.running) {
        this.error = error.message;
        this.failedReads += 1;
        this.invalidate();
        this.app.setPluginError(`SN-*-FSXCS-N01: ${error.message}; retrying`);
      }
      await this.disconnect();
    } finally {
      if (this.running) await this.readMeter();
      if (!this.running) await this.disconnect();
      else this.timer = this.schedule(() => this.tick(), Math.max(250, this.options.pollIntervalMs - (this.now() - startedAt)));
    }
  }

  // A failure here never affects the station's data; a missing meter is
  // retried every 10 s so it does not slow down the station's polling.
  async readMeter() {
    const meter = this.meter;
    const { enabled, baudRate, address } = this.options.soundMeter;
    if (!enabled || this.now() < meter.nextAt) return;
    try {
      await this.open(baudRate, address);
      if (!this.running) return;
      const result = await this.client.readHoldingRegisters(0, 1);
      const value = decodeSoundLevel(result.data);
      if (!this.running) return;
      Object.assign(meter, { available: true, value, raw: [...result.data], lastReadAt: this.now(), error: null });
      meter.successfulReads += 1;
      this.publishMeter(value);
    } catch (error) {
      if (!this.running) return;
      Object.assign(meter, { available: false, value: null, error: error.message, nextAt: this.now() + meterRetryMs });
      meter.failedReads += 1;
      this.publishMeter(null);
      await this.disconnect();
    }
  }

  async stop() {
    this.running = false;
    if (this.timer !== null) this.cancel(this.timer);
    this.timer = null;
    await this.inflight;
    await this.disconnect();
    this.invalidate();
    this.meter.available = false;
    this.publishMeter(null);
  }

  snapshot() {
    return { model: 'SN-*-FSXCS-N01', running: this.running, available: this.available,
      options: { ...this.options }, pathPrefix: this.options.rootPath,
      lastSuccessfulRead: this.lastReadAt === null ? null : new Date(this.lastReadAt).toISOString(),
      ageMs: this.lastReadAt === null ? null : Math.max(0, this.now() - this.lastReadAt),
      successfulReads: this.successfulReads, failedReads: this.failedReads, attempts: this.attempts,
      error: this.error, rawRegisters: this.raw ? [...this.raw] : null,
      soundMeter: { ...this.options.soundMeter, available: this.meter.available,
        value: this.meter.available ? this.meter.value : null,
        lastSuccessfulRead: this.meter.lastReadAt === null ? null : new Date(this.meter.lastReadAt).toISOString(),
        successfulReads: this.meter.successfulReads, failedReads: this.meter.failedReads,
        error: this.meter.error, rawRegisters: this.meter.raw ? [...this.meter.raw] : null },
      measurements: this.raw ? decode(this.raw, this.options.rootPath).map(entry => ({ ...entry, value: this.available ? entry.value : null })) : unavailable(this.options.rootPath) };
  }
}

module.exports = { StationRuntime, validateOptions, decodeSoundLevel };