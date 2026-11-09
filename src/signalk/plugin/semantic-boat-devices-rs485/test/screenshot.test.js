'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const path = require('node:path');
const zlib = require('node:zlib');
const { pathToFileURL } = require('node:url');
const { AddressedBus } = require('../src/addressed-bus');
const { ScreenCapture } = require('../src/screenshot');

const loadProtocol = () => import(pathToFileURL(path.join(__dirname, '../../../../tools/disp-sim/src/protocol.js')));

// remote-a: a 280 x 480 panel, turned by 90 degrees to 480 x 280.
const WIDTH = 280; const HEIGHT = 480; const ROTATION = 1;

// The framebuffer as MonochromeCanvas draws it: white, one black pixel at
// the logical (shown) position x, y.
function frameWithPixel(x, y) {
  const frame = Buffer.alloc(WIDTH * HEIGHT / 8, 0xff);
  const rawX = y; const rawY = HEIGHT - x - 1;
  frame[rawY * (WIDTH / 8) + (rawX >> 3)] &= ~(0x80 >> (rawX & 7));
  return frame;
}

function screenData(protocol, frame, offset, { width = WIDTH, height = HEIGHT } = {}) {
  const header = Buffer.alloc(9);
  header.writeUInt16LE(width, 0); header.writeUInt16LE(height, 2); header[4] = ROTATION; header.writeUInt32LE(offset, 5);
  const data = width ? frame.subarray(offset, offset + 224) : Buffer.alloc(0);
  return { type: protocol.MessageType.SCREEN_DATA, payload: Buffer.concat([header, data]) };
}

// The display as the master sees it: answers are queued behind older messages.
class SimulatedDisplay {
  constructor(protocol, frame, { busy = false } = {}) {
    this.protocol = protocol; this.frame = frame; this.busy = busy; this.paused = [];
    this.queued = [{ type: protocol.MessageType.HISTORY_REQUEST, payload: Buffer.alloc(11) }];
  }
  pause(address, paused) { this.paused.push(paused); }
  async exchange(address, type, payload, keep) {
    assert.equal(keep, this.protocol.MessageType.SCREEN_DATA);
    if (type === this.protocol.MessageType.SCREEN_REQUEST) {
      this.queued.push(screenData(this.protocol, this.frame, payload.readUInt32LE(0), this.busy ? { width: 0, height: 0 } : {}));
    }
    return this.queued.shift() ?? { type: this.protocol.MessageType.BUS_IDLE, payload: Buffer.alloc(0) };
  }
}

function decodePng(png) {
  assert.deepEqual([...png.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  let offset = 8; let width; let height; const data = [];
  while (offset < png.length) {
    const length = png.readUInt32BE(offset); const type = png.subarray(offset + 4, offset + 8).toString();
    const body = png.subarray(offset + 8, offset + 8 + length);
    if (type === 'IHDR') { width = body.readUInt32BE(0); height = body.readUInt32BE(4); assert.deepEqual([body[8], body[9]], [1, 0]); }
    if (type === 'IDAT') data.push(body);
    offset += 12 + length;
  }
  const rows = zlib.inflateSync(Buffer.concat(data)); const stride = Math.ceil(width / 8) + 1;
  return { width, height, pixel: (x, y) => (rows[y * stride + 1 + (x >> 3)] >> (7 - (x & 7))) & 1 };
}

test('a screenshot reads the framebuffer piece by piece and shows it as the display does', async () => {
  const protocol = await loadProtocol();
  const display = new SimulatedDisplay(protocol, frameWithPixel(10, 3));
  const png = await new ScreenCapture({ master: display, address: 1, protocol }).capture();
  assert.deepEqual(display.paused, [true, false]);
  const image = decodePng(png);
  assert.equal(image.width, 480); assert.equal(image.height, 280);
  let black = 0;
  for (let y = 0; y < image.height; y += 1) for (let x = 0; x < image.width; x += 1) if (!image.pixel(x, y)) black += 1;
  assert.equal(black, 1);
  assert.equal(image.pixel(10, 3), 0);
});

test('a display that is installing a configuration refuses screenshots', async () => {
  const protocol = await loadProtocol();
  const display = new SimulatedDisplay(protocol, frameWithPixel(0, 0), { busy: true });
  await assert.rejects(new ScreenCapture({ master: display, address: 1, protocol }).capture(), /busy/);
  assert.deepEqual(display.paused, [true, false]);
});

test('an exchange that keeps the session hands other replies to the display session', async context => {
  const protocol = await loadProtocol();
  const history = { type: protocol.MessageType.HISTORY_REQUEST, payload: Buffer.alloc(11, 1) };
  class FakePort extends EventEmitter {
    isOpen = false;
    open(callback) { this.isOpen = true; this.emit('open'); callback(null); }
    drain(callback) { callback(null); }
    close(callback) { this.isOpen = false; this.emit('close'); callback?.(null); }
    write(frame, callback) {
      callback(null);
      const message = new protocol.FrameDecoder(2).push(frame)[0];
      const response = message.type === protocol.MessageType.SCREEN_REQUEST ? history
        : { type: protocol.MessageType.BUS_IDLE, payload: Buffer.alloc(0) };
      setImmediate(() => this.emit('data', protocol.encodeFrame(response.type, message.sequence, response.payload, message.flags, 2, true)));
    }
  }
  const bus = { id: 'shared', type: 'rs485-addressed', path: '/dev/fake', baudRate: 115200 };
  const master = new AddressedBus({ bus, protocol, onError: () => {}, Port: FakePort, gapMs: 1, responseTimeoutMs: 15 });
  const port = master.devicePort(1);
  context.after(() => master.stop());
  const received = []; let heartbeats = 0;
  port.on('data', frame => received.push(...new protocol.FrameDecoder().push(frame)));
  port.on('heartbeat', () => { heartbeats += 1; });
  master.start();
  master.pause(1, true);
  const reply = await master.exchange(1, protocol.MessageType.SCREEN_REQUEST, Buffer.alloc(4), protocol.MessageType.SCREEN_DATA);
  assert.equal(reply.type, protocol.MessageType.HISTORY_REQUEST);
  assert.equal(received.length, 1);
  assert.equal(received[0].type, protocol.MessageType.HISTORY_REQUEST);
  assert.ok(received[0].payload.equals(history.payload));
  assert.equal(heartbeats, 1);
  // Without `keep` (firmware updates) the session gets nothing.
  await master.exchange(1, protocol.MessageType.SCREEN_REQUEST, Buffer.alloc(4));
  assert.equal(received.length, 1);
});
