'use strict';

const { prefix, metadata, decode, unavailable } = require('./measurements');

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
  }

  publish(update) {
    this.app.handleMessage('sn-fsxcs-n01', {
      context: 'vessels.self',
      updates: [{ timestamp: new Date(this.now()).toISOString(),
        source: { label: `sn-fsxcs-n01.${this.options.stationId}`, type: 'SignalK', src: this.options.stationId }, ...update }]
    });
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
    this.invalidate();
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
    if (client?.isOpen) await new Promise(resolve => client.close(resolve));
  }

  async cycle() {
    const startedAt = this.now();
    this.attempts += 1;
    try {
      if (!this.client?.isOpen) {
        this.client = this.createClient();
        await this.client.connectRTUBuffered(this.options.path, {
          baudRate: this.options.baudRate, dataBits: 8, parity: 'none', stopBits: 1
        });
        this.client.setID(this.options.address);
        this.client.setTimeout(this.options.timeoutMs);
      }
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
      if (!this.running) await this.disconnect();
      else this.timer = this.schedule(() => this.tick(), Math.max(250, this.options.pollIntervalMs - (this.now() - startedAt)));
    }
  }

  async stop() {
    this.running = false;
    if (this.timer !== null) this.cancel(this.timer);
    this.timer = null;
    await this.inflight;
    await this.disconnect();
    this.invalidate();
  }

  snapshot() {
    return { model: 'SN-*-FSXCS-N01', running: this.running, available: this.available,
      options: { ...this.options }, pathPrefix: this.options.rootPath,
      lastSuccessfulRead: this.lastReadAt === null ? null : new Date(this.lastReadAt).toISOString(),
      ageMs: this.lastReadAt === null ? null : Math.max(0, this.now() - this.lastReadAt),
      successfulReads: this.successfulReads, failedReads: this.failedReads, attempts: this.attempts,
      error: this.error, rawRegisters: this.raw ? [...this.raw] : null,
      measurements: this.raw ? decode(this.raw, this.options.rootPath).map(entry => ({ ...entry, value: this.available ? entry.value : null })) : unavailable(this.options.rootPath) };
  }
}

module.exports = { StationRuntime, validateOptions };