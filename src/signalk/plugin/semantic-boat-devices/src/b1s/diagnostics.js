'use strict';

// Diagnostic sessions of B1S devices (src/lib/b1s/DIAGNOSTICS.md): high-rate
// recordings of a situation (an engine start) that a device keeps until the
// Pi has fetched them. The hub asks every device for its session now and
// then, fetches a complete one in pieces, stores it as a JSON file (decoded,
// with the raw bytes), and lets the device free it. The Diagnostic Sessions
// app shows them.

const fs = require('node:fs/promises');
const path = require('node:path');

const endReasons = ['none', 'finished', 'timeout', 'full', 'aborted'];
const eventKinds = ['state', 'snapshot', 'phase'];
const pieceBytes = 200;
const missing = -32768;

function u16(bytes, offset) { return bytes[offset] | bytes[offset + 1] << 8; }
function i16(bytes, offset) { const value = u16(bytes, offset); return value >= 0x8000 ? value - 0x10000 : value; }
function u32(bytes, offset) { return (bytes[offset] | bytes[offset + 1] << 8 | bytes[offset + 2] << 16) + bytes[offset + 3] * 0x1000000; }
function i32(bytes, offset) { return u32(bytes, offset) | 0; }
function f32(bytes, offset) { return Buffer.from(bytes.slice(offset, offset + 4)).readFloatLE(0); }
const round = (value, digits = 6) => Number(value.toPrecision(digits));

// The session's byte stream as data: series with their samples in SI
// units (t in ms from the trigger), events, phases, and the kind's names.
function decodeSession(input, registry, describe = () => ({})) {
  const bytes = [...input];
  if (bytes.length < 24 || String.fromCharCode(...bytes.slice(0, 3)) !== 'B1D' || bytes[3] !== 1) {
    throw new Error('not a version 1 diagnostic session');
  }
  const kind = registry.diagnosticSessions.get(bytes[4]) ?? { id: `kind${bytes[4]}`, phases: [], series: [], inputs: [] };
  const seriesCount = bytes[7];
  const eventCount = u16(bytes, 10);
  const sampleBytes = u32(bytes, 20);
  const session = {
    kind: kind.id, kindCode: bytes[4], number: u16(bytes, 8), truncated: (bytes[5] & 1) === 1,
    endReason: endReasons[bytes[6]] ?? `reason${bytes[6]}`,
    start: u32(bytes, 12) ? new Date(u32(bytes, 12) * 1000).toISOString() : null,
    durationMs: u32(bytes, 16), series: [], events: [], phases: []
  };
  let offset = 24;
  for (let index = 0; index < seriesCount; index += 1, offset += 12) {
    const quantity = registry.quantities.get(bytes[offset + 1]);
    const channel = bytes[offset];
    session.series.push({ channel, role: kind.series?.[index] ?? null, quantity: quantity?.id ?? null,
      unit: quantity?.unit ?? null, ...describe(channel), scale: f32(bytes, offset + 4), offset: f32(bytes, offset + 8),
      t: [], v: [] });
  }
  for (let index = 0; index < eventCount; index += 1, offset += 12) {
    const event = { t: i32(bytes, offset), id: bytes[offset + 4], kind: eventKinds[bytes[offset + 5]] ?? 'other',
      aux: bytes[offset + 6], value: i32(bytes, offset + 8) };
    if (event.kind === 'phase') {
      session.phases.push({ t: event.t, phase: kind.phases?.[event.aux - 1] ?? `phase${event.aux}` });
      continue;
    }
    if (event.kind === 'state') {
      session.events.push({ t: event.t, channel: event.id, input: kind.inputs?.[event.aux - 1] ?? null,
        on: event.value !== 0, ...describe(event.id) });
    } else if (event.kind === 'snapshot') {
      const quantity = registry.quantities.get(event.aux);
      const special = event.value >= 0x7ffffffd;
      session.events.push({ t: event.t, channel: event.id, snapshot: true, quantity: quantity?.id ?? null,
        unit: quantity?.unit ?? null, value: special || !quantity ? null : round(event.value * quantity.scale),
        ...describe(event.id) });
    }
  }
  const end = offset + sampleBytes;
  if (end > bytes.length) throw new Error('diagnostic session shorter than its header says');
  while (offset + 8 + seriesCount <= end) {
    const start = i32(bytes, offset);
    const tick = u16(bytes, offset + 4);
    const ticks = u16(bytes, offset + 6);
    const multiples = bytes.slice(offset + 8, offset + 8 + seriesCount);
    offset += 8 + seriesCount;
    for (let tickIndex = 0; tickIndex < ticks; tickIndex += 1) {
      for (let index = 0; index < seriesCount; index += 1) {
        if (!multiples[index] || tickIndex % multiples[index] !== 0) continue;
        const raw = i16(bytes, offset); offset += 2;
        const series = session.series[index];
        series.t.push(start + tickIndex * tick);
        series.v.push(raw === missing ? null : round(raw * series.scale + series.offset));
      }
    }
  }
  return session;
}

// Key numbers of an engine start, from the series and input roles.
function engineStartSummary(session) {
  const role = name => session.series.find(series => series.role === name);
  const battery = role('batteryVoltage');
  const starter = role('starterVoltage');
  const excitation = role('alternatorExcitation');
  const speed = role('engineSpeed');
  const valuesBetween = (series, from, to) => (series ? series.v.filter((value, index) =>
    value !== null && series.t[index] >= from && series.t[index] < to) : []);
  const mean = values => (values.length ? values.reduce((sum, value) => sum + value, 0) / values.length : null);
  const min = values => (values.length ? Math.min(...values) : null);
  // Phase intervals: each phase lasts until the next or the end.
  const intervals = session.phases.map((phase, index) => ({ ...phase,
    end: session.phases[index + 1]?.t ?? session.durationMs }));
  const firstPhase = session.phases[0]?.t ?? 0;
  const resting = mean(valuesBetween(battery, -Infinity, Math.min(firstPhase, 0) + 1));
  // Preheat: from its input events.
  let preheatMs = 0; let preheatOn = null; const preheatVoltages = [];
  for (const event of session.events.filter(entry => entry.input === 'preheat')) {
    if (event.on && preheatOn === null) preheatOn = event.t;
    if (!event.on && preheatOn !== null) {
      preheatMs += event.t - preheatOn;
      preheatVoltages.push(...valuesBetween(battery, preheatOn + 500, event.t));
      preheatOn = null;
    }
  }
  if (preheatOn !== null) preheatMs += session.durationMs - preheatOn;
  const cranks = intervals.filter(entry => entry.phase === 'cranking').map(crank => {
    const batteryValues = valuesBetween(battery, crank.t, crank.end);
    const starterValues = valuesBetween(starter, crank.t, crank.end);
    // Voltage lost in the starter cables: battery minus starter at the same moment.
    let cableLoss = null;
    if (battery && starter) {
      const atBattery = new Map(battery.t.map((t, index) => [t, battery.v[index]]));
      starter.t.forEach((t, index) => {
        if (t < crank.t || t >= crank.end || starter.v[index] === null || !atBattery.has(t)) return;
        const loss = atBattery.get(t) - starter.v[index];
        if (cableLoss === null || loss > cableLoss) cableLoss = loss;
      });
    }
    // Compression pulses: dips of the starter voltage (one per compression stroke).
    let pulses = 0; const values = starterValues; const spanMs = crank.end - crank.t;
    if (values.length > 20) {
      const average = mean(values);
      let below = false;
      for (const value of values) {
        if (!below && value < average - 0.05) { below = true; pulses += 1; } else if (below && value > average) below = false;
      }
    }
    return { startMs: crank.t, durationMs: crank.end - crank.t, minBatteryV: min(batteryValues),
      minStarterV: min(starterValues), maxCableLossV: cableLoss === null ? null : round(cableLoss, 3),
      compressionPulsesPerS: spanMs > 500 && pulses > 2 ? round(pulses / (spanMs / 1000), 3) : null };
  });
  const running = intervals.find(entry => entry.phase === 'runningUp');
  const chargingAt = running && excitation
    ? excitation.t.find((t, index) => t >= running.t && excitation.v[index] !== null && excitation.v[index] > 13) : undefined;
  const speedValues = running ? valuesBetween(speed, running.t, running.end) : [];
  return {
    outcome: session.endReason === 'finished' ? 'started' : session.endReason === 'aborted' ? 'no start' : session.endReason,
    restingV: resting === null ? null : round(resting, 4),
    preheatS: preheatMs ? round(preheatMs / 1000, 3) : 0,
    preheatDropV: resting !== null && preheatVoltages.length ? round(resting - mean(preheatVoltages), 3) : null,
    crankAttempts: cranks.length,
    cranks,
    timeToRunS: running && cranks.length ? round((running.t - cranks[0].startMs) / 1000, 3) : null,
    chargingAfterS: running && chargingAt !== undefined ? round((chargingAt - running.t) / 1000, 3) : null,
    idleRpm: speedValues.length ? Math.round(mean(speedValues.slice(-10)) * 60) : null
  };
}

const summaries = { engineStart: engineStartSummary };

class DiagnosticSessionStore {
  // client: B1sClient; state: B1sState (devices); directory: where sessions go.
  constructor({ client, state, registry, directory, pollMs = 15000, log = () => {} }) {
    Object.assign(this, { client, state, registry, directory, pollMs, log });
    this.devices = new Map(); // device ID -> { supported, checkedAt, busy }
    this.lastError = null;
  }

  async start() {
    await fs.mkdir(this.directory, { recursive: true });
    this.timer = setInterval(() => this.poll().catch(error => { this.lastError = error.message; }), this.pollMs);
    this.timer.unref?.();
  }

  stop() { clearInterval(this.timer); }

  async poll() {
    for (const device of this.state.devices.values()) {
      if (device.lastSeen === undefined || Date.now() - device.lastSeen > 10000 || device.address === undefined) continue;
      const entry = this.devices.get(device.id) ?? { supported: undefined, checkedAt: 0 };
      this.devices.set(device.id, entry);
      // A device without sessions (older firmware) does not answer; ask again after 10 minutes.
      if (entry.busy || (entry.supported === false && Date.now() - entry.checkedAt < 600000)) continue;
      entry.checkedAt = Date.now();
      entry.busy = true;
      try {
        await this.check(device, entry);
      } catch (error) {
        if (entry.supported === undefined) entry.supported = false;
        else this.lastError = `${device.id}: ${error.message}`;
      } finally { entry.busy = false; }
    }
  }

  async check(device, entry) {
    const info = await this.client.service(device.address, 'diagnosticSessionInfo');
    entry.supported = true;
    if (info.status !== 0) return;
    const bytes = info.bytes;
    const number = u16(bytes, 0); const state = bytes[3]; const size = u32(bytes, 4);
    if (state !== 2 || !size) return; // none, or still recording
    const raw = [];
    while (raw.length < size) {
      const offset = raw.length;
      const reply = await this.client.service(device.address, 'diagnosticSessionRead',
        [number & 0xff, number >> 8, offset & 0xff, offset >> 8 & 0xff, offset >> 16 & 0xff, offset >>> 24,
          Math.min(pieceBytes, size - offset)]);
      if (reply.status !== 0) throw new Error(`session read: status ${reply.status}`);
      const count = reply.bytes[6];
      if (u32(reply.bytes, 2) !== offset || !count) throw new Error('session read: unexpected piece');
      raw.push(...reply.bytes.slice(7, 7 + count));
    }
    await this.save(device, raw);
    await this.client.service(device.address, 'diagnosticSessionRelease', [number & 0xff, number >> 8]);
  }

  async save(device, raw) {
    const describe = channel => {
      const description = device.channels.get(channel);
      return description ? { label: description.label || null, location: description.location || null } : {};
    };
    const session = decodeSession(raw, this.registry, describe);
    const summary = summaries[session.kind]?.(session) ?? null;
    const startedAt = session.start ?? new Date(Date.now() - session.durationMs).toISOString();
    const stamp = startedAt.replace(/[-:]/g, '').replace(/\..*$/, '').replace('T', '-');
    const name = `${stamp}-${device.id}-${session.kind}-${session.number}.json`;
    const document = { name, deviceId: device.id, device: device.product?.model || `B1S device ${device.id}`,
      address: device.address, receivedAt: new Date().toISOString(), startedAt, ...session, summary,
      raw: Buffer.from(raw).toString('base64') };
    const file = path.join(this.directory, name);
    const temporary = `${file}.tmp`;
    await fs.writeFile(temporary, JSON.stringify(document));
    await fs.rename(temporary, file);
    this.log(`diagnostic session ${name} stored (${raw.length} bytes)`);
    return name;
  }

  async list() {
    const names = (await fs.readdir(this.directory).catch(() => [])).filter(name => name.endsWith('.json')).sort().reverse();
    const sessions = [];
    for (const name of names) {
      try {
        const document = JSON.parse(await fs.readFile(path.join(this.directory, name), 'utf8'));
        sessions.push({ name, kind: document.kind, deviceId: document.deviceId, device: document.device,
          startedAt: document.startedAt, durationMs: document.durationMs, endReason: document.endReason,
          truncated: document.truncated, summary: document.summary });
      } catch { /* a damaged file is skipped */ }
    }
    return sessions;
  }

  async read(name) {
    if (!/^[\w.-]+\.json$/.test(name)) throw new Error('invalid session name');
    return JSON.parse(await fs.readFile(path.join(this.directory, name), 'utf8'));
  }

  status() {
    const supported = [...this.devices.values()].filter(entry => entry.supported).length;
    return `${supported} device(s) with diagnostic sessions${this.lastError ? `; last error: ${this.lastError}` : ''}`;
  }
}

module.exports = { DiagnosticSessionStore, decodeSession, engineStartSummary };
