'use strict';

const { SerialPort } = require('serialport');
const { DeviceSession } = require('./session');

class SerialLink {
  constructor({ app, bus, device, protocol, catalog, mappings, onError, Port = SerialPort }) {
    this.bus = bus;
    this.protocol = protocol;
    // The display configuration catalog of the Semantic Boat Devices hub, or
    // a function returning it (null while the hub is not running).
    this.catalog = typeof catalog === 'function' ? catalog : () => catalog;
    this.onError = onError;
    this.port = new Port({ path: bus.path, baudRate: bus.baudRate, autoOpen: false });
    this.decoder = new protocol.FrameDecoder();
    this.queue = [];
    this.sequence = 0;
    this.running = false;
    this.sending = false;
    this.receivedFrames = 0;
    this.sentFrames = 0;
    this.error = null;
    this.transferId = 0;
    this.listId = 0;
    this.transferActive = false;
    this.lastReceiveAt = 0;
    this.session = new DeviceSession({ app, device, protocol, mappings,
      onError, send: (type, payload) => this.send(type, payload) });
    this.port.on('open', () => {
      if (!this.running) return;
      this.decoder = new protocol.FrameDecoder();
      this.error = null;
      this.session.reset();
      this.session.lastSeen = Date.now();
      this.hello();
    });
    this.port.on('error', error => { this.error = error.message; onError(error); });
    this.port.on('heartbeat', () => { this.session.lastSeen = Date.now(); });
    this.port.on('close', () => {
      this.session.reset();
      this.cancelQueue();
      this.reconnect();
    });
    this.port.on('data', chunk => {
      this.lastReceiveAt = Date.now();
      for (const message of this.decoder.push(chunk)) {
        this.receivedFrames += 1;
        try {
          this.session.handle(message);
          this.handleCatalog(message);
        } catch (error) { onError(error); }
      }
    });
  }

  start() {
    this.running = true;
    this.open();
    this.valueTimer = setInterval(() => {
      if (this.session.lastSeen && Date.now() - this.session.lastSeen > 12000) {
        this.session.reset();
        this.session.lastSeen = 0;
      }
      if (!this.transferActive && this.port.isOpen) this.session.flush();
    }, 100);
    this.helloTimer = setInterval(() => { if (!this.session.ready) this.hello(); }, 1000);
    this.pingTimer = setInterval(() => {
      if (this.session.pending || this.transferActive) return;
      const payload = Buffer.alloc(4); payload.writeUInt32LE(Date.now() >>> 0);
      void this.send(this.protocol.MessageType.PING, payload).catch(this.onError);
    }, 5000);
  }

  open() {
    if (!this.running || this.port.isOpen || this.port.opening) return;
    this.port.open(error => { if (error) { this.error = error.message; this.reconnect(); } });
  }

  reconnect() {
    if (!this.running || this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => { this.reconnectTimer = null; this.open(); }, 1000);
  }

  hello() {
    if (this.session.pending && Date.now() - this.lastReceiveAt < 3000) return;
    void this.send(this.protocol.MessageType.HELLO,
      this.protocol.helloPayload(2, 'semantic-boat-devices')).catch(this.onError);
  }

  send(type, payload = Buffer.alloc(0)) {
    if (!this.running || !this.port.isOpen) return Promise.reject(new Error('Serial link is disconnected'));
    const valueFrame = [this.protocol.MessageType.VALUE_NUMBER, this.protocol.MessageType.VALUE_TEXT,
      this.protocol.MessageType.VALUE_UNAVAILABLE].includes(type);
    const index = valueFrame ? payload.readUInt16LE(0) : null;
    const replaceableControl = [this.protocol.MessageType.HELLO, this.protocol.MessageType.PING].includes(type);
    const pending = this.queue.find(entry => valueFrame ? entry.valueFrame && entry.index === index
      : replaceableControl && entry.type === type);
    if (!pending && this.queue.length >= 256) return Promise.reject(new Error('Serial transmit queue is full'));
    return new Promise((resolve, reject) => {
      if (pending) {
        pending.resolve(false);
        Object.assign(pending, { type, payload, resolve, reject, generation: this.session.generation });
      } else this.queue.push({ type, payload, resolve, reject, valueFrame, index,
        generation: this.session.generation });
      this.pump();
    });
  }

  pump() {
    if (this.sending || !this.running || !this.port.isOpen) return;
    const silence = Date.now() - this.lastReceiveAt;
    if (silence < 50 || (this.session.pending && silence < 3000)) {
      clearTimeout(this.paceTimer);
      this.paceTimer = setTimeout(() => this.pump(), 50);
      return;
    }
    const entry = this.queue.shift();
    if (!entry) return;
    if ([this.protocol.MessageType.VALUE_NUMBER, this.protocol.MessageType.VALUE_TEXT,
      this.protocol.MessageType.VALUE_UNAVAILABLE].includes(entry.type) &&
        entry.generation !== this.session.generation) {
      entry.resolve(false);
      this.pump();
      return;
    }
    this.sending = true;
    const frame = this.protocol.encodeFrame(entry.type, this.sequence++ & 255, entry.payload);
    this.port.write(frame, writeError => {
      const complete = error => {
        if (error) entry.reject(error);
        else { this.sentFrames += 1; entry.resolve(true); }
        if (this.running) this.paceTimer = setTimeout(() => { this.sending = false; this.pump(); },
          entry.valueFrame ? 250 : 30);
      };
      if (writeError) complete(writeError);
      else this.port.drain(complete);
    });
  }

  cancelQueue() {
    for (const entry of this.queue.splice(0)) entry.reject(new Error('Serial session ended'));
  }

  handleCatalog(message) {
    const { MessageType } = this.protocol;
    const catalog = this.catalog();
    if (!catalog) return; // no hub: the display keeps its installed configuration
    if (message.type === MessageType.CONFIG_LIST_REQUEST && message.payload.length === 0 && !this.listActive) {
      this.listActive = true;
      void catalog.sendList((...args) => this.send(...args), this.protocol, ++this.listId & 65535)
        .catch(this.onError).finally(() => { this.listActive = false; });
    } else if (message.type === MessageType.CONFIG_REQUEST && message.payload.length > 1 &&
               message.payload[0] === message.payload.length - 1) {
      void this.install(message.payload.subarray(1).toString('utf8')).catch(this.onError);
    }
  }

  async install(name) {
    const catalog = this.catalog();
    if (!catalog) throw new Error('Semantic Boat Devices (configuration catalog) is not running');
    if (this.transferActive) throw new Error('A configuration transfer is already active');
    if (!this.session.ready) throw new Error('Device has not completed its subscription snapshot');
    this.transferActive = true;
    this.lastTransfer = { name, state: 'sending', startedAt: Date.now() };
    try {
      await catalog.transfer(name, (...args) => this.send(...args), this.protocol, ++this.transferId & 65535);
      this.lastTransfer.state = 'sent-awaiting-device-restart';
    } catch (error) {
      this.lastTransfer.state = 'failed';
      this.lastTransfer.error = error.message;
      throw error;
    } finally { this.transferActive = false; }
  }

  async stop() {
    this.running = false;
    clearInterval(this.valueTimer); clearInterval(this.helloTimer); clearInterval(this.pingTimer);
    clearTimeout(this.reconnectTimer); clearTimeout(this.paceTimer);
    this.session.reset(); this.cancelQueue();
    if (this.port.opening) await new Promise(resolve => {
      const done = () => { this.port.off('open', done); this.port.off('error', done); this.port.off('close', done); resolve(); };
      this.port.once('open', done); this.port.once('error', done); this.port.once('close', done);
    });
    if (this.port.isOpen) await new Promise(resolve => this.port.close(resolve));
  }

  snapshot() {
    const subscriptions = this.session.snapshot().subscriptions.map(source => {
      const reading = this.session.app.getSelfPath(source.resolvedPath);
      return { ...source, value: reading?.value ?? null, timestamp: reading?.timestamp,
        source: reading?.$source };
    });
    return { ...this.session.snapshot(), subscriptions, bus: this.bus.id, open: this.port.isOpen,
      transport: this.bus.type ?? 'serial-point-to-point', error: this.port.lastError ?? this.error, receivedFrames: this.receivedFrames,
      sentFrames: this.sentFrames, decodeErrors: this.decoder.errors + (this.port.busDecodeErrors ?? 0), queuedFrames: this.queue.length,
      lastTransfer: this.lastTransfer };
  }
}

module.exports = { SerialLink };