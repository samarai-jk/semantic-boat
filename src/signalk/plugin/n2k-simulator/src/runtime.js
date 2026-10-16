'use strict';

const net = require('node:net');
const { performance } = require('node:perf_hooks');
const { Simulation } = require('./simulation');
const { InstrumentScheduler, delta, readings, validateOutputs } = require('./instruments');

class SimulatorRuntime {
  constructor({ controls = {}, sourceAddress = 200, onDelta = () => {}, onError = () => {} } = {}) {
    this.simulation = new Simulation(controls);
    this.scheduler = new InstrumentScheduler(sourceAddress);
    this.onDelta = onDelta;
    this.onError = onError;
    this.clients = new Set();
    this.frames = 0;
    this.outputs = Object.fromEntries(readings.map(([path]) => [path, true]));
    this.paused = false;
    this.timer = null;
    this.server = null;
  }

  async start(port = 0) {
    if (this.timer || this.server) throw new Error('Simulator already started');
    if (!Number.isInteger(port) || port < 0 || port > 65535) throw new RangeError('Invalid TCP port');
    const server = net.createServer(client => {
      this.clients.add(client);
      client.on('error', () => client.destroy());
      client.on('close', () => this.clients.delete(client));
    });
    this.server = server;
    try {
      await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(port, '127.0.0.1', () => {
          server.removeListener('error', reject);
          resolve();
        });
      });
    } catch (error) {
      this.server = null;
      throw error;
    }
    server.on('error', this.onError);
    this.lastTick = performance.now();
    this.timer = setInterval(() => {
      const now = performance.now();
      const seconds = Math.min((now - this.lastTick) / 1000, 1);
      this.lastTick = now;
      if (this.paused || seconds <= 0) return;
      try {
        this.tick(seconds);
      } catch (error) {
        this.paused = true;
        this.onError(error);
      }
    }, 50);
    return server.address().port;
  }

  tick(seconds, timestamp = new Date().toISOString()) {
    const messages = this.scheduler.sample({ ...this.simulation.step(seconds), outputs: this.outputs }, timestamp);
    if (!messages.length) return;
    this.frames += messages.length;
    this.onDelta(delta(messages, timestamp));
    const data = messages.map(message => message.line).join('\n') + '\n';
    for (const client of this.clients) {
      if (client.writableLength > 65536) client.destroy();
      else client.write(data);
    }
  }

  snapshot() {
    return { ...this.simulation.snapshot(), paused: this.paused, frames: this.frames,
      clients: this.clients.size, n2kPort: this.server?.address()?.port ?? null,
      outputs: { ...this.outputs },
      sourceAddress: this.scheduler.sourceAddress, output: this.output ?? 'n2k' };
  }

  control(patch) {
    if (!patch || typeof patch !== 'object' || Array.isArray(patch)) throw new TypeError('Controls must be an object');
    const { outputs, ...controls } = patch;
    if (outputs !== undefined) validateOutputs(outputs);
    this.simulation.configure(controls);
    if (outputs) Object.assign(this.outputs, outputs);
    if ('departureLatitude' in patch || 'departureLongitude' in patch) {
      this.scheduler = new InstrumentScheduler(this.scheduler.sourceAddress);
    }
    return this.snapshot();
  }

  reset() {
    this.simulation.reset();
    this.scheduler = new InstrumentScheduler(this.scheduler.sourceAddress);
    this.frames = 0;
    return this.snapshot();
  }

  async stop() {
    clearInterval(this.timer);
    this.timer = null;
    for (const client of this.clients) client.destroy();
    this.clients.clear();
    const server = this.server;
    this.server = null;
    if (server) await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  }
}

module.exports = { SimulatorRuntime };