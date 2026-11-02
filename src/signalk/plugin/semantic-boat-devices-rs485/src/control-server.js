'use strict';

// Local control socket of the RS485 plugin, for tools on this computer
// (src/tools/remote-a-update.py): JSON lines over a Unix socket in the
// plugin's data directory, readable by its owner only. The plugin owns the
// serial port, so firmware updates have to run here.
//
// Requests (one JSON object per line):
//   {"command": "devices"}
//   {"command": "bus"}  (counters of the RS485 buses: replies, timeouts, errors)
//   {"command": "firmware-status", "device": "<id>"}
//   {"command": "install-config", "device": "<id>", "name": "<catalog file, e.g. navigation.json>"}
//   {"command": "firmware-update", "device": "<id>", "image": "<absolute path of remote-a.bin>"}
// Replies: JSON lines; an update streams {"event": "progress", ...} and ends
// with {"event": "done", ...} or {"event": "error", "message": ...}.

const fs = require('node:fs');
const net = require('node:net');

class ControlServer {
  constructor({ path, runtime, log = () => {} }) {
    this.path = path; this.runtime = runtime; this.log = log; this.updating = new Set();
  }

  start() {
    try { fs.unlinkSync(this.path); } catch { /* not there */ }
    this.server = net.createServer(socket => this.connection(socket));
    return new Promise((resolve, reject) => {
      this.server.once('error', reject);
      this.server.listen(this.path, () => { fs.chmodSync(this.path, 0o600); resolve(); });
    });
  }

  async stop() {
    if (!this.server) return;
    await new Promise(resolve => this.server.close(resolve));
    this.server = null;
    try { fs.unlinkSync(this.path); } catch { /* gone */ }
  }

  connection(socket) {
    let buffer = '';
    socket.setEncoding('utf8');
    socket.on('error', () => {});
    socket.on('data', chunk => {
      buffer += chunk;
      let newline;
      while ((newline = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, newline); buffer = buffer.slice(newline + 1);
        if (line.trim()) void this.handle(line, reply => { if (!socket.destroyed) socket.write(`${JSON.stringify(reply)}\n`); });
      }
    });
  }

  async handle(line, send) {
    let request;
    try { request = JSON.parse(line); } catch { send({ event: 'error', message: 'Invalid JSON' }); return; }
    try {
      if (request.command === 'devices') {
        send({ event: 'done', devices: this.runtime.devices().map(device => ({
          id: device.id, label: device.label, address: device.address, online: device.online })) });
      } else if (request.command === 'bus') {
        send({ event: 'done', buses: this.runtime.busStates() });
      } else if (request.command === 'install-config') {
        // Sends a catalog configuration; the display compiles, stores, and restarts.
        await this.runtime.install(request.device, request.name);
        send({ event: 'done' });
      } else if (request.command === 'firmware-status') {
        const status = await this.runtime.firmware(request.device).status();
        send({ event: 'done', status });
      } else if (request.command === 'firmware-update') {
        if (typeof request.image !== 'string' || !request.image.startsWith('/')) throw new Error('image must be an absolute path');
        if (this.updating.has(request.device)) throw new Error(`${request.device} is already being updated`);
        const update = this.runtime.firmware(request.device);
        const image = fs.readFileSync(request.image);
        this.updating.add(request.device);
        this.log(`firmware update of ${request.device} from ${request.image}`);
        try {
          const result = await update.run(image, progress => send({ event: 'progress', ...progress }));
          this.log(`firmware update of ${request.device} done: ${result.version}`);
          send({ event: 'done', ...result });
        } finally { this.updating.delete(request.device); }
      } else throw new Error(`Unknown command ${request.command}`);
    } catch (error) {
      send({ event: 'error', message: error.message });
    }
  }
}

module.exports = { ControlServer };
