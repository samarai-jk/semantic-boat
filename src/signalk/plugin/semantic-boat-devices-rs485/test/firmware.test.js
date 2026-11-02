'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { FirmwareUpdate, inspectImage } = require('../src/firmware-update');

const loadProtocol = () => import(pathToFileURL(path.join(__dirname, '../../../../tools/disp-sim/src/protocol.js')));

// A stamped image as stamp_image.py writes it.
function makeImage(protocol, size = 3000 * 8, board = 2) {
  const image = Buffer.alloc(size);
  for (let index = 0; index < size; index += 1) image[index] = (index * 7 + 3) & 255;
  const header = 0x200;
  image.writeUInt32LE(0x21533142, header); image.writeUInt16LE(1, header + 4); image.writeUInt16LE(board, header + 6);
  image.writeUInt32LE(size, header + 12); image.writeUInt32LE(0xffffffff, header + 16);
  image.writeUInt32LE(0x00020000, header + 20);
  image.fill(0, header + 24, header + 64); image.write('0.2.0 test', header + 24);
  image.writeUInt32LE(protocol.crc32(image), header + 16);
  return image;
}

function statusPayload({ result = 0, mode, board = 2, next = 0, crc = 0, version = 0, unconfirmed = false, text }) {
  const payload = Buffer.alloc(18 + Buffer.byteLength(text));
  payload[0] = result; payload[1] = mode; payload.writeUInt16LE(board, 2); payload.writeUInt32LE(next, 4);
  payload.writeUInt32LE(crc, 8); payload.writeUInt32LE(version, 12); payload[16] = unconfirmed ? 1 : 0;
  payload[17] = Buffer.byteLength(text); payload.write(text, 18);
  return payload;
}

// A display as the master sees it: the application, then its bootloader.
class SimulatedDisplay {
  constructor(protocol, { loseReplies = [] } = {}) {
    this.protocol = protocol; this.mode = 1; this.flash = Buffer.alloc(0); this.crc = 0x11111111;
    this.restartFor = 0; this.unconfirmedQueries = 0; this.queued = ['hello'];
    this.loseReplies = new Set(loseReplies); this.dataRequests = 0; this.paused = [];
  }
  pause(address, paused) { this.paused.push(paused); }
  status(result = 0) {
    return { type: this.protocol.MessageType.FIRMWARE_STATUS, payload: statusPayload({
      result, mode: this.mode, next: this.next ?? 0, crc: this.crc, unconfirmed: this.unconfirmedQueries > 0,
      text: this.mode === 1 ? 'remote-a 0.1.0' : 'bootloader 1.0.0: update requested' }) };
  }
  async exchange(address, type, payload = Buffer.alloc(0)) {
    const { MessageType } = this.protocol;
    if (this.restartFor > 0) { this.restartFor -= 1; throw new Error('No reply'); }
    if (this.mode === 1) {
      // The application queues its status behind older messages; each turn
      // returns the oldest queued message.
      if (type === MessageType.FIRMWARE_QUERY || type === MessageType.FIRMWARE_ENTER_BOOTLOADER) {
        this.queued.push(this.status());
        if (type === MessageType.FIRMWARE_QUERY && this.unconfirmedQueries > 0) this.unconfirmedQueries -= 1;
      }
      if (type === MessageType.FIRMWARE_ENTER_BOOTLOADER) {
        const reply = this.queued.shift(); this.mode = 2; this.restartFor = 3; this.queued = [];
        return reply === 'hello' ? { type: MessageType.HELLO, payload: Buffer.alloc(0) } : reply;
      }
      const next = this.queued.shift();
      if (next === 'hello') return { type: MessageType.HELLO, payload: Buffer.alloc(0) };
      return next ?? { type: MessageType.BUS_IDLE, payload: Buffer.alloc(0) };
    }
    switch (type) {
    case MessageType.FIRMWARE_QUERY: return this.status();
    case MessageType.FIRMWARE_BEGIN:
      if (payload.readUInt16LE(6) !== 2) return this.status(7);
      this.size = payload.readUInt32LE(12); this.expected = payload.readUInt32LE(16);
      this.flash = Buffer.alloc(this.size); this.next = 0; return this.status();
    case MessageType.FIRMWARE_DATA: {
      const offset = payload.readUInt32LE(0);
      if (offset !== this.next) return this.status(10);
      payload.subarray(4).copy(this.flash, offset); this.next += payload.length - 4;
      this.dataRequests += 1;
      // The device applied the chunk but its reply was lost: the master's
      // own retries are exhausted, so the update resumes at the reported offset.
      if (this.loseReplies.has(this.dataRequests)) throw new Error('No reply');
      return this.status();
    }
    case MessageType.FIRMWARE_END: {
      const copy = Buffer.from(this.flash); copy.fill(0xff, 0x210, 0x214);
      if (this.next !== this.size || this.protocol.crc32(copy) !== this.expected) return this.status(11);
      const reply = this.status();
      this.mode = 1; this.crc = this.expected; this.restartFor = 2; this.unconfirmedQueries = 2; this.queued = ['hello'];
      return reply;
    }
    default: return { type: MessageType.BUS_IDLE, payload: Buffer.alloc(0) };
    }
  }
}

const fastUpdate = (protocol, display) => new FirmwareUpdate({ master: display, address: 2, protocol, sleep: async () => {} });

test('an update restarts into the bootloader, sends the image, and waits for the new firmware to confirm itself', async () => {
  const protocol = await loadProtocol();
  const image = makeImage(protocol);
  const display = new SimulatedDisplay(protocol);
  const phases = [];
  const result = await fastUpdate(protocol, display).run(image, progress => phases.push(progress.phase));
  assert.deepEqual(display.flash, image);
  assert.equal(display.mode, 1);
  assert.equal(display.crc, inspectImage(image, protocol).crc);
  assert.equal(result.bytes, image.length);
  assert.deepEqual(display.paused, [true, false]);
  assert.ok(phases.includes('restarting into the bootloader'));
  assert.ok(phases.includes('running, waiting for confirmation'));
  assert.equal(display.dataRequests, Math.ceil(image.length / protocol.FIRMWARE_CHUNK_BYTES));
});

test('a lost reply after an applied chunk resumes at the offset the bootloader reports', async () => {
  const protocol = await loadProtocol();
  const image = makeImage(protocol);
  const display = new SimulatedDisplay(protocol, { loseReplies: [5] });
  // The lost reply surfaces as an error of that request; the next one is
  // answered with "unexpected offset" and the reported offset.
  const update = fastUpdate(protocol, display);
  const exchange = display.exchange.bind(display);
  let failed = false;
  display.exchange = async (...args) => {
    try { return await exchange(...args); } catch (error) {
      if (args[1] === protocol.MessageType.FIRMWARE_DATA && !failed) {
        failed = true;
        return exchange(args[0], args[1], args[2]); // resend: answered with unexpected offset
      }
      throw error;
    }
  };
  await update.run(image);
  assert.deepEqual(display.flash, image);
});

test('images for another board or without a valid stamp are refused before anything is sent', async () => {
  const protocol = await loadProtocol();
  const display = new SimulatedDisplay(protocol);
  await assert.rejects(fastUpdate(protocol, display).run(makeImage(protocol, 3000 * 8, 1)), /board 1/);
  const broken = makeImage(protocol); broken[0x1000] ^= 1;
  await assert.rejects(fastUpdate(protocol, display).run(broken), /CRC/);
  await assert.rejects(fastUpdate(protocol, display).run(Buffer.alloc(100)), /Not a firmware image/);
  assert.equal(display.mode, 1);
  assert.deepEqual(display.paused, []);
});

test('the bus master sends exchanges to a paused device and keeps polling the others', async () => {
  const protocol = await loadProtocol();
  const { EventEmitter } = require('node:events');
  const { AddressedBus } = require('../src/addressed-bus');
  const requests = [];
  class FakePort extends EventEmitter {
    isOpen = false;
    open(callback) { this.isOpen = true; this.emit('open'); callback(null); }
    drain(callback) { callback(null); }
    close(callback) { this.isOpen = false; this.emit('close'); callback?.(null); }
    write(frame, callback) {
      callback(null);
      const message = new protocol.FrameDecoder(2).push(frame)[0];
      requests.push([message.flags, message.type]);
      const status = message.type === protocol.MessageType.FIRMWARE_QUERY;
      const reply = protocol.encodeFrame(status ? protocol.MessageType.FIRMWARE_STATUS : protocol.MessageType.BUS_IDLE,
        message.sequence, status ? statusPayload({ mode: 2, text: 'bootloader' }) : Buffer.alloc(0),
        message.flags, 2, true);
      setImmediate(() => this.emit('data', reply));
    }
  }
  const bus = new AddressedBus({ bus: { path: '/dev/fake', baudRate: 115200 }, protocol, onError: () => {}, Port: FakePort, gapMs: 1 });
  for (const address of [1, 2]) bus.devicePort(address).open();
  bus.start();
  bus.pause(2, true);
  await new Promise(resolve => setTimeout(resolve, 30));
  const reply = await bus.exchange(2, protocol.MessageType.FIRMWARE_QUERY);
  assert.equal(protocol.parseFirmwareStatus(reply.payload).mode, 2);
  await new Promise(resolve => setTimeout(resolve, 30));
  await bus.stop();
  const toPaused = requests.filter(([address]) => address === 2);
  assert.deepEqual(toPaused.map(([, type]) => type), [protocol.MessageType.FIRMWARE_QUERY]);
  assert.ok(requests.filter(([address]) => address === 1).length > 2);
});
