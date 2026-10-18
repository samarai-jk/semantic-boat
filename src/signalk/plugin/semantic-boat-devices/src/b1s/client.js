'use strict';

// B1S configuration client (src/lib/b1s/CONFIGURATION.md): Service requests
// (parameter list, read, write, console) and Commands to devices, with their
// replies. One request at a time per device; each waits timeoutMs for its
// reply and is sent up to `tries` times.

const reply = 0x80;
const parameterStatus = ['done', 'unknown parameter', 'unknown channel', 'invalid value', 'read-only', 'busy',
  'not permitted'];
const commandResults = ['done', 'unknown command', 'unknown channel', 'invalid argument', 'refused', 'busy'];

function hexBytes(text) {
  if (typeof text !== 'string' || !text.trim()) return [];
  return text.trim().split(/\s+/).map(byte => parseInt(byte, 16));
}

function int32(bytes, offset) {
  return (bytes[offset] | bytes[offset + 1] << 8 | bytes[offset + 2] << 16 | bytes[offset + 3] << 24) | 0;
}

function int32Bytes(value) {
  return [value & 0xff, value >> 8 & 0xff, value >> 16 & 0xff, value >> 24 & 0xff];
}

// Value of a read or write reply: bytes after the status.
function decodeValue(bytes) {
  const [codeLow, codeHigh, channel, type] = bytes;
  const code = codeLow | codeHigh << 8;
  if (type === 1) {
    const length = bytes[4];
    return { code, channel, value: String.fromCharCode(...bytes.slice(6, 4 + length)) };
  }
  return { code, channel, value: int32(bytes, 4) };
}

class B1sClient {
  constructor(registry, { send, onConsole = () => {}, timeoutMs = 1000, tries = 3 }) {
    this.registry = registry;
    this.send = send; // (destination, pgn, bytes, priority)
    this.onConsole = onConsole; // (address, text)
    this.timeoutMs = timeoutMs;
    this.tries = tries;
    this.sequence = 0;
    this.waiting = new Map(); // key -> { resolve, timer }
    this.queues = new Map(); // address -> promise of the last request
    this.header = [registry.manufacturerCode & 0xff,
      (registry.manufacturerCode >> 8 & 0x07) | 0x18 | registry.industryCode << 5];
  }

  // Decoded B1S Service or Command messages from the B1S provider. Returns
  // true if it was a reply this client handles.
  handle(n2k) {
    const fields = n2k.fields ?? {};
    if (fields.manufacturerCode !== this.registry.manufacturerCode) return false;
    if (n2k.pgn === this.registry.pgns.service) {
      const bytes = hexBytes(fields.payload);
      if (bytes.length < 3 || !(bytes[0] & reply)) return false;
      const service = bytes[0] & ~reply;
      if (service === this.registry.services.console) {
        let end = bytes.length;
        while (end > 3 && (bytes[end - 1] === 0xff || bytes[end - 1] === 0x00)) end -= 1;
        this.onConsole(n2k.src, String.fromCharCode(...bytes.slice(3, end)));
        return true;
      }
      return this.resolve(`${n2k.src}:s${service}:${bytes[1]}`, { status: bytes[2], bytes: bytes.slice(3) });
    }
    if (n2k.pgn === this.registry.pgns.command) {
      if (!(fields.code & reply)) return false;
      // canboat returns a binary field of up to 64 bits as a number.
      const result = typeof fields.argument === 'number' ? fields.argument & 0xff : hexBytes(fields.argument)[0];
      return this.resolve(`${n2k.src}:c${fields.code & ~reply}:${fields.channel}`, result);
    }
    return false;
  }

  resolve(key, value) {
    const entry = this.waiting.get(key);
    if (!entry) return false;
    clearTimeout(entry.timer);
    this.waiting.delete(key);
    entry.resolve(value);
    return true;
  }

  // Runs requests to one device one after another.
  serialize(address, task) {
    const previous = this.queues.get(address) ?? Promise.resolve();
    const next = previous.catch(() => {}).then(task);
    this.queues.set(address, next);
    return next;
  }

  exchange(address, key, transmit) {
    return new Promise((resolve, reject) => {
      let tries = 0;
      const attempt = () => {
        if (tries++ >= this.tries) {
          this.waiting.delete(key);
          reject(new Error(`no reply from address ${address}`));
          return;
        }
        transmit();
        this.waiting.set(key, { resolve, timer: setTimeout(attempt, this.timeoutMs) });
      };
      attempt();
    });
  }

  service(address, name, fields = []) {
    const code = this.registry.services[name];
    return this.serialize(address, () => {
      this.sequence = (this.sequence + 1) & 0xff;
      const sequence = this.sequence;
      return this.exchange(address, `${address}:s${code}:${sequence}`,
        () => this.send(address, this.registry.pgns.service, [...this.header, code, sequence, ...fields], 6));
    });
  }

  async command(address, name, channel = 0xff, argument = -1) {
    const code = this.registry.commands[name];
    if (code === undefined) throw new Error(`unknown command ${name}`);
    const result = await this.serialize(address, () => this.exchange(address, `${address}:c${code}:${channel}`,
      () => this.send(address, this.registry.pgns.command, [...this.header, code, channel, ...int32Bytes(argument)], 3)));
    return commandResults[result] ?? `result ${result}`;
  }

  async parameterCodes(address) {
    const codes = [];
    for (let first = 0; ;) {
      const { status, bytes } = await this.service(address, 'parameterList', [first]);
      if (status !== 0) throw new Error(`parameter list: ${parameterStatus[status] ?? status}`);
      const [, count, more] = bytes;
      for (let index = 0; index < count; index += 1) codes.push(bytes[3 + 2 * index] | bytes[4 + 2 * index] << 8);
      if (!more || count === 0) return codes;
      first += count;
    }
  }

  async read(address, code, channel = 0xff) {
    const { status, bytes } = await this.service(address, 'parameterRead', [code & 0xff, code >> 8, channel]);
    if (status !== 0) return { status: parameterStatus[status] ?? `status ${status}`, value: null };
    return { status: 'done', value: decodeValue(bytes).value };
  }

  async write(address, code, channel, value) {
    let encoded;
    if (typeof value === 'string') {
      const text = [...value].map(character => character.charCodeAt(0) & 0x7f).slice(0, 32);
      encoded = [1, text.length + 2, 1, ...text];
    } else {
      encoded = [0, ...int32Bytes(value)];
    }
    const { status, bytes } = await this.service(address, 'parameterWrite', [code & 0xff, code >> 8, channel, ...encoded]);
    if (status !== 0) return { status: parameterStatus[status] ?? `status ${status}`, value: null };
    return { status: 'done', value: decodeValue(bytes).value };
  }

  // Runs a console line (empty: keep the session alive). The device log
  // follows through onConsole; there is no reply to wait for.
  console(address, line) {
    this.sequence = (this.sequence + 1) & 0xff;
    const text = [...line].map(character => character.charCodeAt(0) & 0x7f).slice(0, 80);
    this.send(address, this.registry.pgns.service, [...this.header, this.registry.services.console, this.sequence, ...text], 6);
  }
}

module.exports = { B1sClient, hexBytes, decodeValue };
