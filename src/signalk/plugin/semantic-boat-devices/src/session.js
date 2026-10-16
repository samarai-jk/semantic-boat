'use strict';

class DeviceSession {
  constructor({ app, device, protocol, send, mappings = {}, onError = () => {} }) {
    this.app = app;
    this.device = device;
    this.protocol = protocol;
    this.send = send;
    this.mappings = mappings;
    this.onError = onError;
    this.unsubscribes = [];
    this.active = new Map();
    this.pending = null;
    this.ready = false;
    this.lastSeen = 0;
    this.valuesSent = 0;
    this.generation = 0;
    this.mailbox = new Map();
  }

  reset() {
    this.generation += 1;
    for (const unsubscribe of this.unsubscribes.splice(0)) unsubscribe();
    this.active.clear();
    this.mailbox.clear();
    this.pending = null;
    this.ready = false;
  }

  handle(message) {
    this.lastSeen = Date.now();
    const { MessageType, parseSubscription } = this.protocol;
    if (message.type === MessageType.HELLO) {
      if (message.payload.length < 2 || message.payload[0] !== 1 ||
          message.payload[1] !== message.payload.length - 2) return;
      this.reset();
      this.name = message.payload.subarray(2).toString('utf8');
    } else if (message.type === MessageType.SUBSCRIPTIONS_BEGIN && message.payload.length === 4) {
      this.pending = new Map();
      this.expected = message.payload.readUInt16LE(2);
      this.section = message.payload.readUInt16LE(0);
      if (this.expected > 256) this.pending = null;
    } else if (message.type === MessageType.SUBSCRIBE && this.pending) {
      const source = parseSubscription(message.payload);
      if (!source.path || /[*\s]/.test(source.path)) throw new Error('Subscription requires an exact path');
      if (this.pending.has(source.sourceIndex) || this.pending.size >= this.expected) {
        this.pending = null;
        return;
      }
      this.pending.set(source.sourceIndex, source);
    } else if (message.type === MessageType.SUBSCRIPTIONS_END && message.payload.length === 0 && this.pending) {
      if (this.pending.size !== this.expected) {
        this.pending = null;
        return;
      }
      const replacement = this.pending;
      this.reset();
      this.active = replacement;
      this.ready = true;
      this.subscribe();
    } else if (message.type === MessageType.PING && message.payload.length === 4) {
      void this.send(MessageType.PONG, message.payload);
    }
  }

  subscribe() {
    const paths = new Map();
    for (const source of this.active.values()) {
      source.resolvedPath = this.mappings[source.path] ?? source.path;
      source.nextAt = 0;
      if (!['signalk', 'signalk-text', 'signalk-ugm3'].includes(source.provider)) {
        this.queue(source, null);
        continue;
      }
      const sources = paths.get(source.resolvedPath) ?? [];
      sources.push(source);
      paths.set(source.resolvedPath, sources);
    }
    const generation = this.generation;
    const publish = path => {
      if (generation !== this.generation) return;
      const sample = this.app.getSelfPath(path);
      for (const source of paths.get(path) ?? []) this.queue(source, sample?.value, sample?.timestamp);
    };
    for (const path of paths.keys()) publish(path);
    if (paths.size === 0) return;
    this.app.subscriptionmanager.subscribe({
      context: 'vessels.self',
      subscribe: [...paths.keys()].map(path => ({ path, period: 100, policy: 'instant', format: 'delta' }))
    }, this.unsubscribes, error => this.onError(new Error(String(error))), delta => {
      for (const update of delta.updates ?? []) {
        for (const entry of update.values ?? []) if (paths.has(entry.path)) publish(entry.path);
      }
    });
  }

  queue(source, value, timestamp) {
    if (source.provider === 'signalk-ugm3') {
      value = typeof value === 'number' && Number.isFinite(value) ? value * 1e9 : null;
    }
    if (source.provider === 'signalk-text') {
      if (typeof value === 'boolean') value = value ? 'YES' : 'NO';
      else if (typeof value === 'number') value = Number.isFinite(value)
        ? String(Number(value.toPrecision(6))) : null;
    }
    if (source.resolvedPath === 'navigation.position' && value && typeof value === 'object') {
      const { latitude, longitude } = value;
      value = Number.isFinite(latitude) && Math.abs(latitude) <= 90 &&
        Number.isFinite(longitude) && Math.abs(longitude) <= 180
        ? `${Math.abs(latitude).toFixed(6)}${latitude < 0 ? 'S' : 'N'} ${Math.abs(longitude).toFixed(6)}${longitude < 0 ? 'W' : 'E'}`
        : null;
    }
    this.mailbox.set(source.sourceIndex, { source, value, timestamp });
  }

  flush(now = Date.now()) {
    const { MessageType, numberValuePayload, textValuePayload } = this.protocol;
    for (const [index, entry] of this.mailbox) {
      if (now < entry.source.nextAt) continue;
      let type = MessageType.VALUE_UNAVAILABLE;
      let payload = Buffer.alloc(2);
      payload.writeUInt16LE(index);
      if (typeof entry.value === 'number' && Number.isFinite(entry.value) &&
          Math.abs(entry.value) <= 3.4028234663852886e38) {
        type = MessageType.VALUE_NUMBER;
        payload = numberValuePayload(index, entry.value);
      } else if (typeof entry.value === 'string') {
        type = MessageType.VALUE_TEXT;
        let text = '';
        for (const character of entry.value) {
          if (Buffer.byteLength(text + character) > (entry.source.ageSupported ? 233 : 237)) break;
          text += character;
        }
        payload = textValuePayload(index, text);
      }
      if (entry.source.ageSupported && type !== MessageType.VALUE_UNAVAILABLE) {
        const timestamp = Date.parse(entry.timestamp);
        const age = Buffer.alloc(4);
        age.writeUInt32LE(Number.isFinite(timestamp) ? Math.min(0x7fffffff, Math.max(0, now - timestamp)) : 0);
        payload = Buffer.concat([payload, age]);
      }
      this.mailbox.delete(index);
      entry.source.nextAt = now + Math.max(100, entry.source.periodMs || 500);
      void Promise.resolve(this.send(type, payload)).then(sent => {
        if (sent !== false) this.valuesSent += 1;
      }).catch(this.onError);
    }
  }

  snapshot() {
    return { ...this.device, name: this.name, ready: this.ready, lastSeen: this.lastSeen,
      pendingSubscriptions: this.pending ? this.pending.size : null,
      expectedSubscriptions: this.pending ? this.expected : null,
      valuesSent: this.valuesSent, subscriptions: [...this.active.values()].map(source => ({
        sourceIndex: source.sourceIndex, provider: source.provider, path: source.path,
        resolvedPath: source.resolvedPath, periodMs: source.periodMs, permanent: source.permanent
      })) };
  }
}

module.exports = { DeviceSession };