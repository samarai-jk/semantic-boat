'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const { randomUUID } = require('node:crypto');
const Ajv = require('ajv/dist/2020');
const schema = require('../../../../lib/semantic-display/schema/display-v1.schema.json');
const validate = new Ajv({ strict: false, allErrors: true }).compile(schema);

class Catalog {
  constructor(directory) { this.directory = directory; }

  filename(name) {
    if (typeof name !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._-]*\.json$/.test(name) ||
        Buffer.byteLength(name) > 47) throw new Error('Use a JSON filename of at most 47 bytes, without directories');
    return path.join(this.directory, name);
  }

  async init() { await fs.mkdir(this.directory, { recursive: true }); }

  async list() {
    const entries = await fs.readdir(this.directory, { withFileTypes: true });
    return entries.filter(entry => {
      try { this.filename(entry.name); return entry.isFile(); } catch { return false; }
    }).map(entry => entry.name).sort();
  }

  validate(document) {
    if (!validate(document)) throw new Error(validate.errors.map(error => `${error.instancePath || '/'} ${error.message}`).join('; '));
    const permanent = document.permanent_sources ?? [];
    const sources = [...permanent, ...document.sections.flatMap(section => section.sources ?? [])];
    const historyBytes = sources.reduce((sum, source) => sum + (source.history ? 32 + 4 * source.history.points : 0), 0);
    if (historyBytes > 1024) throw new Error('History exceeds the remote-a 1024-byte limit');
    for (const source of sources) {
      if (source.history && source.provider === 'signalk-text') throw new Error(`Source ${source.id}: history requires numeric values`);
      if (source.outdated_ms && source.stale_ms && source.outdated_ms >= source.stale_ms) {
        throw new Error(`Source ${source.id}: outdated_ms must be less than stale_ms`);
      }
    }
    for (const section of document.sections) {
      const available = new Map([...permanent, ...(section.sources ?? [])].map(source => [source.id, source]));
      for (const page of section.pages) {
        const plot = page.history_plot;
        if (!plot) continue;
        if (!available.get(plot.source)?.history) throw new Error(`Page ${page.id}: plot source requires history storage`);
        if (plot.y_min !== undefined && (!(plot.y_min < plot.y_max) ||
            !Number.isFinite(Math.fround(plot.y_min)) || !Number.isFinite(Math.fround(plot.y_max)) ||
            !Number.isFinite(Math.fround(Math.fround(plot.y_max) - Math.fround(plot.y_min))) ||
            !(Math.fround(plot.y_min) < Math.fround(plot.y_max)))) throw new Error(`Page ${page.id}: plot range must increase and fit finite float values`);
      }
    }
    const bytes = Buffer.from(JSON.stringify(document));
    if (bytes.length > 8192) throw new Error('Configuration exceeds the remote-a 8192-byte limit');
    return bytes;
  }

  async read(name) {
    const document = JSON.parse(await fs.readFile(this.filename(name), 'utf8'));
    this.validate(document);
    return document;
  }

  async save(name, document) {
    const filename = this.filename(name);
    this.validate(document);
    const names = await this.list();
    if (!names.includes(name) && names.length >= 16) throw new Error('The remote-a catalog supports at most 16 configurations');
    const temporary = `${filename}.${randomUUID()}.tmp`;
    await fs.writeFile(temporary, `${JSON.stringify(document, null, 2)}\n`, { mode: 0o600 });
    await fs.rename(temporary, filename);
  }

  async remove(name) { await fs.unlink(this.filename(name)); }

  async sendList(send, protocol, listId) {
    const names = (await this.list()).slice(0, 16);
    const begin = Buffer.alloc(4);
    begin.writeUInt16LE(listId); begin.writeUInt16LE(names.length, 2);
    await send(protocol.MessageType.CONFIG_LIST_BEGIN, begin);
    for (const [index, name] of names.entries()) {
      const nameBytes = Buffer.from(name);
      const header = Buffer.alloc(5);
      header.writeUInt16LE(listId); header.writeUInt16LE(index, 2); header[4] = nameBytes.length;
      await send(protocol.MessageType.CONFIG_LIST_ITEM, Buffer.concat([header, nameBytes]));
    }
    const end = Buffer.alloc(2); end.writeUInt16LE(listId);
    await send(protocol.MessageType.CONFIG_LIST_END, end);
  }

  async transfer(name, send, protocol, transferId) {
    const data = this.validate(await this.read(name));
    const nameBytes = Buffer.from(name);
    const begin = Buffer.alloc(11);
    begin.writeUInt16LE(transferId); begin.writeUInt32LE(data.length, 2);
    begin.writeUInt32LE(protocol.crc32(data), 6); begin[10] = nameBytes.length;
    await send(protocol.MessageType.CONFIG_BEGIN, Buffer.concat([begin, nameBytes]));
    for (let offset = 0; offset < data.length; offset += 32) {
      const header = Buffer.alloc(6);
      header.writeUInt16LE(transferId); header.writeUInt32LE(offset, 2);
      await send(protocol.MessageType.CONFIG_CHUNK, Buffer.concat([header, data.subarray(offset, offset + 32)]));
    }
    const end = Buffer.alloc(2); end.writeUInt16LE(transferId);
    await send(protocol.MessageType.CONFIG_END, end);
  }
}

module.exports = { Catalog };