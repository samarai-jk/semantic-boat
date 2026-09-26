export const PROTOCOL_VERSION = 1;
export const MAX_PAYLOAD_SIZE = 240;
export const MAX_ENCODED_FRAME_SIZE = 250;

export const MessageType = Object.freeze({
  HELLO: 0x01,
  SUBSCRIPTIONS_BEGIN: 0x02,
  SUBSCRIBE: 0x03,
  SUBSCRIPTIONS_END: 0x04,
  VALUE_NUMBER: 0x10,
  VALUE_TEXT: 0x11,
  VALUE_UNAVAILABLE: 0x12,
  PING: 0x20,
  PONG: 0x21,
});

export function crc16Ccitt(data) {
  let crc = 0xffff;
  for (const byte of data) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) !== 0 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}

function cobsEncode(data) {
  const output = Buffer.alloc(data.length + Math.floor(data.length / 254) + 1);
  let read = 0;
  let write = 1;
  let codeIndex = 0;
  let code = 1;
  while (read < data.length) {
    if (data[read] === 0) {
      output[codeIndex] = code;
      code = 1;
      codeIndex = write;
      write += 1;
      read += 1;
    } else {
      output[write] = data[read];
      write += 1;
      read += 1;
      code += 1;
      if (code === 0xff) {
        output[codeIndex] = code;
        code = 1;
        codeIndex = write;
        write += 1;
      }
    }
  }
  output[codeIndex] = code;
  return output.subarray(0, write);
}

function cobsDecode(data) {
  if (data.length === 0) throw new Error('empty COBS frame');
  const output = Buffer.alloc(data.length);
  let read = 0;
  let write = 0;
  while (read < data.length) {
    const code = data[read];
    read += 1;
    if (code === 0) throw new Error('zero inside COBS frame');
    const count = code - 1;
    if (read + count > data.length) throw new Error('truncated COBS frame');
    data.copy(output, write, read, read + count);
    read += count;
    write += count;
    if (code !== 0xff && read < data.length) {
      output[write] = 0;
      write += 1;
    }
  }
  return output.subarray(0, write);
}

export function encodeFrame(type, sequence, payload = Buffer.alloc(0), flags = 0) {
  if (!Buffer.isBuffer(payload)) payload = Buffer.from(payload);
  if (payload.length > MAX_PAYLOAD_SIZE) throw new RangeError('payload exceeds 240 bytes');
  const decoded = Buffer.alloc(6 + payload.length + 2);
  decoded[0] = PROTOCOL_VERSION;
  decoded[1] = type;
  decoded[2] = sequence;
  decoded[3] = flags;
  decoded.writeUInt16LE(payload.length, 4);
  payload.copy(decoded, 6);
  decoded.writeUInt16LE(crc16Ccitt(decoded.subarray(0, decoded.length - 2)), decoded.length - 2);
  return Buffer.concat([cobsEncode(decoded), Buffer.from([0])]);
}

export class FrameDecoder {
  #encoded = [];
  #discarding = false;
  errors = 0;

  push(chunk) {
    const messages = [];
    for (const byte of chunk) {
      if (byte === 0) {
        if (this.#discarding) {
          this.#discarding = false;
          this.#encoded = [];
        } else if (this.#encoded.length !== 0) {
          try {
            messages.push(this.#finish());
          } catch {
            this.errors += 1;
          }
          this.#encoded = [];
        }
      } else if (!this.#discarding) {
        if (this.#encoded.length >= MAX_ENCODED_FRAME_SIZE) {
          this.errors += 1;
          this.#discarding = true;
          this.#encoded = [];
        } else {
          this.#encoded.push(byte);
        }
      }
    }
    return messages;
  }

  #finish() {
    const decoded = cobsDecode(Buffer.from(this.#encoded));
    if (decoded.length < 8) throw new Error('frame too short');
    if (decoded[0] !== PROTOCOL_VERSION) throw new Error('unsupported protocol version');
    const payloadLength = decoded.readUInt16LE(4);
    if (payloadLength > MAX_PAYLOAD_SIZE || decoded.length !== 8 + payloadLength) {
      throw new Error('invalid payload length');
    }
    const expected = decoded.readUInt16LE(decoded.length - 2);
    const actual = crc16Ccitt(decoded.subarray(0, decoded.length - 2));
    if (expected !== actual) throw new Error('CRC mismatch');
    return {
      version: decoded[0],
      type: decoded[1],
      sequence: decoded[2],
      flags: decoded[3],
      payload: decoded.subarray(6, decoded.length - 2),
    };
  }
}

export function helloPayload(role, name) {
  const encodedName = Buffer.from(name, 'utf8');
  if (encodedName.length > 255) throw new RangeError('hello name is too long');
  return Buffer.concat([Buffer.from([role, encodedName.length]), encodedName]);
}

export function numberValuePayload(sourceIndex, value) {
  const payload = Buffer.alloc(6);
  payload.writeUInt16LE(sourceIndex, 0);
  payload.writeFloatLE(value, 2);
  return payload;
}

export function textValuePayload(sourceIndex, value) {
  const text = Buffer.from(value, 'utf8');
  if (text.length > 237) throw new RangeError('text value is too long');
  const payload = Buffer.alloc(3 + text.length);
  payload.writeUInt16LE(sourceIndex, 0);
  payload[2] = text.length;
  text.copy(payload, 3);
  return payload;
}

export function parseSubscription(payload) {
  if (payload.length < 9) throw new Error('subscription is too short');
  const providerLength = payload[7];
  const pathLength = payload[8];
  if (payload.length !== 9 + providerLength + pathLength) throw new Error('invalid subscription lengths');
  return {
    sourceIndex: payload.readUInt16LE(0),
    periodMs: payload.readUInt32LE(2),
    permanent: (payload[6] & 1) !== 0,
    provider: payload.subarray(9, 9 + providerLength).toString('utf8'),
    path: payload.subarray(9 + providerLength).toString('utf8'),
  };
}
