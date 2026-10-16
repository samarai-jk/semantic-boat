'use strict';

const { EventEmitter } = require('node:events');
const { SerialPort } = require('serialport');

class DevicePort extends EventEmitter {
  constructor(master, address) {
    super(); this.master = master; this.address = address;
    this.isOpen = false; this.opening = false; this.receivedFrames = 0; this.timeouts = 0;
  }
  get busDecodeErrors() { return this.master.decoder.errors; }
  open(callback) {
    if (this.isOpen) { callback?.(null); return; }
    this.opening = true; this.openCallback = callback;
    if (this.master.port.isOpen) this.opened();
  }
  opened() {
    if (!this.opening) return;
    this.isOpen = true; this.opening = false; this.emit('open');
    const callback = this.openCallback; this.openCallback = null; callback?.(null);
  }
  write(frame, callback) {
    if (!this.isOpen || this.pending) { callback(new Error('Addressed device port is unavailable')); return; }
    const messages = new this.master.protocol.FrameDecoder().push(frame);
    if (messages.length !== 1) { callback(new Error('Expected one application frame')); return; }
    this.pending = { message: messages[0], callback };
    this.master.schedule(0);
  }
  drain(callback) { callback(null); }
  close(callback) {
    this.isOpen = false; this.opening = false;
    this.master.cancelDevice(this); this.emit('close'); callback?.(null);
  }
}

class AddressedBus {
  constructor({ bus, protocol, onError, Port = SerialPort, responseTimeoutMs = 200, gapMs = 30 }) {
    this.bus = bus; this.protocol = protocol; this.onError = onError;
    this.responseTimeoutMs = responseTimeoutMs; this.gapMs = gapMs;
    this.port = new Port({ path: bus.path, baudRate: bus.baudRate, autoOpen: false });
    this.decoder = new protocol.FrameDecoder(protocol.ADDRESSED_PROTOCOL_VERSION);
    this.devices = []; this.cursor = 0; this.sequence = 0; this.running = false; this.unexpectedReplies = 0;
    this.port.on('open', () => {
      this.decoder = new protocol.FrameDecoder(protocol.ADDRESSED_PROTOCOL_VERSION);
      for (const device of this.devices) device.opened();
      this.schedule(this.gapMs);
    });
    this.port.on('data', chunk => {
      for (const message of this.decoder.push(chunk)) {
        const request = this.current;
        if (!message.response) continue;
        if (!request || message.flags !== request.device.address || message.sequence !== request.sequence || request.response) {
          this.unexpectedReplies += 1; continue;
        }
        request.response = message;
        if (request.drained) this.finish();
      }
    });
    this.port.on('error', error => {
      for (const device of this.devices) device.lastError = error.message;
      this.onError(error);
    });
    this.port.on('close', () => {
      clearTimeout(this.responseTimer); this.current = null;
      for (const device of this.devices) {
        this.cancelDevice(device); device.isOpen = false; device.opening = false; device.emit('close');
      }
      this.reconnect();
    });
  }
  devicePort(address) {
    if (this.devices.some(device => device.address === address)) throw new Error('Duplicate address on RS485 bus');
    const device = new DevicePort(this, address); this.devices.push(device); return device;
  }
  start() { this.running = true; this.open(); }
  open() {
    if (!this.running || this.port.isOpen || this.port.opening) return;
    this.port.open(error => {
      if (!error) return;
      this.onError(error);
      for (const device of this.devices) {
        if (!device.opening) continue;
        device.opening = false; const callback = device.openCallback; device.openCallback = null;
        callback?.(error); device.emit('error', error);
      }
      this.reconnect();
    });
  }
  reconnect() {
    if (!this.running || this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => { this.reconnectTimer = null; this.open(); }, 1000);
  }
  schedule(delay = this.gapMs) {
    if (!this.running || this.current || this.timer) return;
    this.timer = setTimeout(() => { this.timer = null; this.poll(); }, delay);
  }
  poll() {
    if (!this.running || !this.port.isOpen || this.current) return;
    let device;
    for (let count = 0; count < this.devices.length; count += 1) {
      const candidate = this.devices[this.cursor++ % this.devices.length];
      if (candidate.isOpen && Date.now() >= (candidate.probeAt ?? 0)) { device = candidate; break; }
    }
    if (!device) { this.schedule(100); return; }
    if (!device.request) {
      const message = device.pending?.message ?? { type: this.protocol.MessageType.BUS_POLL, payload: Buffer.alloc(0) };
      const sequence = this.sequence++ & 255;
      device.request = { device, sequence, attempts: 0, pending: device.pending,
        frame: this.protocol.encodeFrame(message.type, sequence, message.payload, device.address, this.protocol.ADDRESSED_PROTOCOL_VERSION) };
    }
    const request = device.request; request.attempts += 1; request.drained = false; request.response = null;
    this.current = request;
    this.port.write(request.frame, error => {
      if (this.current !== request) return;
      if (error) { this.finish(error); return; }
      this.port.drain(error => {
        if (this.current !== request) return;
        if (error) { this.finish(error); return; }
        request.drained = true;
        if (request.pending?.message.type === this.protocol.MessageType.CONFIG_END) {
          const pending = request.pending; request.pending = null; request.endSent = true;
          if (device.pending === pending) device.pending = null;
          pending.callback(null);
        }
        if (request.response) this.finish();
        else this.responseTimer = setTimeout(() => this.timeout(), this.responseTimeoutMs);
      });
    });
  }
  timeout() {
    if (!this.current) return;
    const request = this.current; request.device.timeouts += 1;
    if (request.endSent) { this.finish(new Error('Waiting for display restart')); return; }
    if (request.attempts < 3) {
      this.current = null; this.schedule();
    } else this.finish(new Error(`No reply from RS485 address ${request.device.address}`));
  }
  finish(error) {
    const request = this.current; if (!request) return;
    clearTimeout(this.responseTimer); this.current = null;
    const device = request.device; device.request = null;
    if (error) { device.lastError = error.message; device.probeAt = Date.now() + 1000; }
    else {
      device.lastError = null; device.probeAt = 0; device.receivedFrames += 1;
      device.emit('heartbeat');
      const response = request.response;
      if (response.type !== this.protocol.MessageType.BUS_IDLE) device.emit('data',
        this.protocol.encodeFrame(response.type, response.sequence, response.payload));
    }
    if (request.pending) {
      if (device.pending === request.pending) device.pending = null;
      request.pending.callback(error ?? null);
    }
    this.schedule();
  }
  cancelDevice(device) {
    if (this.current?.device === device) { clearTimeout(this.responseTimer); this.current = null; }
    device.request = null;
    if (device.pending) { const pending = device.pending; device.pending = null; pending.callback(new Error('RS485 bus session ended')); }
  }
  async stop() {
    this.running = false;
    clearTimeout(this.timer); clearTimeout(this.responseTimer); clearTimeout(this.reconnectTimer);
    this.timer = null;
    for (const device of this.devices) this.cancelDevice(device);
    if (this.port.opening) await new Promise(resolve => {
      const done = () => { this.port.off('open', done); this.port.off('error', done); resolve(); };
      this.port.once('open', done); this.port.once('error', done);
    });
    if (this.port.isOpen) await new Promise(resolve => this.port.close(resolve));
  }
  snapshot() {
    return { ...this.bus, open: this.port.isOpen, decodeErrors: this.decoder.errors,
      unexpectedReplies: this.unexpectedReplies, devices: this.devices.map(device => ({
        address: device.address, replies: device.receivedFrames, timeouts: device.timeouts, error: device.lastError ?? null })) };
  }
}

module.exports = { AddressedBus };