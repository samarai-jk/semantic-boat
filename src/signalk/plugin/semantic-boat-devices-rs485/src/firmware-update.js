'use strict';

// Firmware updates of remote-a displays over the addressed RS485 bus
// (src/lib/semantic-link/PROTOCOL.md, "Firmware update"; image format:
// src/lib/b1s/FIRMWARE.md). The display session is paused meanwhile; the
// application restarts into its bootloader, the image goes over in 232-byte
// chunks, and the new application has to start and confirm itself.

const HEADER_OFFSET = 0x200;
const MAGIC = 0x21533142; // "B1S!"
const BOARD_REMOTE_A = 2; // B1S registry, boards
const MAX_IMAGE = 224 * 1024;
const RESULTS = ['done', 'unknown request', null, 'invalid value', null, 'busy', null, 'wrong board',
  'image too large', 'flash error', 'unexpected offset', 'CRC mismatch'];

// Checks a stamped image (stamp_image.py) and returns its header fields.
function inspectImage(image, protocol) {
  if (!Buffer.isBuffer(image) || image.length < HEADER_OFFSET + 64 || image.length % 8 !== 0) {
    throw new Error('Not a firmware image (too short or not padded to 8 bytes)');
  }
  const header = image.subarray(HEADER_OFFSET, HEADER_OFFSET + 64);
  if (header.readUInt32LE(0) !== MAGIC || header.readUInt16LE(4) !== 1) throw new Error('No firmware image header at 0x200');
  if (header.readUInt16LE(6) !== BOARD_REMOTE_A) throw new Error(`Image is for board ${header.readUInt16LE(6)}, not remote-a`);
  const size = header.readUInt32LE(12);
  if (size !== image.length || size > MAX_IMAGE) throw new Error(`Image size ${image.length} does not match its header (${size})`);
  const crc = header.readUInt32LE(16);
  const copy = Buffer.from(image);
  copy.fill(0xff, HEADER_OFFSET + 16, HEADER_OFFSET + 20);
  if (protocol.crc32(copy) !== crc) throw new Error('Image CRC does not match its header (not stamped?)');
  const text = header.subarray(24, 64).toString('utf8').replace(/\0.*$/s, '');
  return { header: Buffer.from(header), size, crc, version: header.readUInt32LE(20), text };
}

const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

class FirmwareUpdate {
  constructor({ master, address, protocol, sleep = delay, now = Date.now }) {
    this.master = master; this.address = address; this.protocol = protocol; this.sleep = sleep; this.now = now;
  }

  // One request answered with a firmware status. An application may answer
  // with older queued messages first; poll until the status arrives.
  async request(type, payload = Buffer.alloc(0)) {
    const { MessageType } = this.protocol;
    let reply = await this.master.exchange(this.address, type, payload);
    for (let tries = 0; reply.type !== MessageType.FIRMWARE_STATUS && tries < 8; tries += 1) {
      reply = await this.master.exchange(this.address, MessageType.BUS_POLL);
    }
    if (reply.type !== MessageType.FIRMWARE_STATUS) throw new Error('The display does not answer firmware requests (firmware too old?)');
    return this.protocol.parseFirmwareStatus(reply.payload);
  }

  async checked(type, payload) {
    const status = await this.request(type, payload);
    if (status.result !== 0) {
      const error = new Error(`Display refused: ${RESULTS[status.result] ?? `result ${status.result}`}`);
      error.status = status; throw error;
    }
    return status;
  }

  async status() { return this.request(this.protocol.MessageType.FIRMWARE_QUERY); }

  // Polls the status until accept(status) holds; requests may time out while
  // the display restarts.
  async waitFor(accept, timeoutMs, what) {
    const until = this.now() + timeoutMs;
    while (this.now() < until) {
      try {
        const status = await this.status();
        if (accept(status)) return status;
      } catch { /* the display is restarting */ }
      await this.sleep(500);
    }
    throw new Error(`Timed out waiting for ${what}`);
  }

  async run(image, progress = () => {}) {
    const { MessageType, FirmwareMode, FIRMWARE_CHUNK_BYTES } = this.protocol;
    const target = inspectImage(image, this.protocol);
    const started = this.now();
    this.master.pause(this.address, true);
    try {
      let status = await this.status();
      progress({ phase: 'found', mode: status.mode === FirmwareMode.BOOTLOADER ? 'bootloader' : 'application', running: status.text, image: target.text });
      if (status.mode !== FirmwareMode.BOOTLOADER) {
        // The reply may be lost in the restart; the bootloader answering is what counts.
        try { await this.checked(MessageType.FIRMWARE_ENTER_BOOTLOADER); } catch { /* restarting */ }
        progress({ phase: 'restarting into the bootloader' });
        status = await this.waitFor(entry => entry.mode === FirmwareMode.BOOTLOADER, 30000, 'the bootloader');
        progress({ phase: 'bootloader', running: status.text });
      }
      await this.checked(MessageType.FIRMWARE_BEGIN, target.header);
      let reported = -1;
      for (let offset = 0; offset < image.length;) {
        const data = image.subarray(offset, offset + FIRMWARE_CHUNK_BYTES);
        const payload = Buffer.alloc(4 + data.length);
        payload.writeUInt32LE(offset, 0); data.copy(payload, 4);
        let result;
        try { result = await this.checked(MessageType.FIRMWARE_DATA, payload); }
        catch (error) {
          // Resume where the display is (a reply lost after it was applied).
          if (error.status?.result !== 10) throw error;
          result = error.status;
        }
        offset = result.nextOffset;
        const percent = Math.floor(offset * 100 / image.length);
        if (percent >= reported + 5 || offset === image.length) {
          reported = percent; progress({ phase: 'sending', percent, bytes: offset, of: image.length });
        }
      }
      try { await this.checked(MessageType.FIRMWARE_END); } catch (error) {
        // A refusal (CRC mismatch) is final; a lost reply is not: the new
        // firmware running with the image's CRC is what counts.
        if (error.status) throw error;
        progress({ phase: 'no reply to the end of the transfer, checking the display' });
      }
      progress({ phase: 'installed, starting the new firmware' });
      await this.sleep(1000);
      status = await this.waitFor(entry => entry.mode === FirmwareMode.APPLICATION, 30000, 'the new firmware to start');
      if (status.imageCrc !== target.crc) throw new Error('A different firmware is running after the update');
      progress({ phase: 'running, waiting for confirmation', running: status.text });
      status = await this.waitFor(entry => entry.mode === FirmwareMode.APPLICATION && !entry.unconfirmed, 30000,
        'the new firmware to confirm itself');
      return { version: status.text, seconds: Math.round((this.now() - started) / 100) / 10, bytes: image.length };
    } finally {
      this.master.pause(this.address, false);
    }
  }
}

module.exports = { FirmwareUpdate, inspectImage };
