'use strict';

// Graph history for displays (README, "History"): the values of one Signal K
// path from the InfluxDB that signalk-to-influxdb writes (InfluxDB 1.x HTTP
// API), aggregated into the display's intervals with the display's reducer.
// Only the own vessel; the source Signal K prefers for the path when it has
// data there, else every source of the path.
//
// Connection: the hub's own setting when its URL is set, else the settings of
// the writer plugin (its configuration file next to the hub's data
// directory, read again after a minute), else localhost:8086, signalk.

const fs = require('node:fs');

const REDUCERS = { 1: 'last', 2: 'mean', 3: 'min', 4: 'max' };
const WRITER_SETTINGS_MS = 60000;

function checkConnection({ url, database }) {
  let parsed;
  try { parsed = new URL(url); } catch { throw new Error('History: invalid InfluxDB URL'); }
  if (!['http:', 'https:'].includes(parsed.protocol)) throw new Error('History: InfluxDB URL must be http or https');
  if (!/^[A-Za-z0-9_-]+$/.test(database)) throw new Error('History: invalid database name');
}

function validateOptions(options = {}) {
  const result = { enabled: options.enabled ?? true, url: options.url || '', database: options.database || '',
    username: options.username || '', password: options.password || '' };
  if (result.url) checkConnection({ url: result.url, database: result.database || 'signalk' });
  else if (result.database && !/^[A-Za-z0-9_-]+$/.test(result.database)) throw new Error('History: invalid database name');
  return result;
}

// The connection of signalk-to-influxdb (protocol, host, port, database,
// username, password), or null without a readable configuration.
function writerConnection(file) {
  let configuration;
  try { configuration = JSON.parse(fs.readFileSync(file, 'utf8')).configuration; } catch { return null; }
  if (!configuration) return null;
  const protocol = configuration.protocol === 'https' ? 'https' : 'http';
  return { url: `${protocol}://${configuration.host || 'localhost'}:${configuration.port || 8086}`,
    database: configuration.database || 'signalk', username: configuration.username || '',
    password: configuration.password || '', from: 'signalk-to-influxdb' };
}

const quoteString = value => `'${String(value).replace(/\\/g, '\\\\').replace(/'/g, "\\'")}'`;

class HistoryProvider {
  // writerSettings: the configuration file of signalk-to-influxdb.
  constructor({ app, options, writerSettings = null, fetchImpl = globalThis.fetch, now = Date.now }) {
    this.app = app; this.options = validateOptions(options); this.writerSettings = writerSettings;
    this.fetch = fetchImpl; this.now = now;
    this.queries = 0; this.errors = 0; this.lastError = null; this.writer = null; this.writerReadAt = -Infinity;
  }

  connection() {
    if (this.options.url) {
      return { url: this.options.url, database: this.options.database || 'signalk', username: this.options.username,
        password: this.options.password, from: 'history setting' };
    }
    if (this.writerSettings && this.now() - this.writerReadAt >= WRITER_SETTINGS_MS) {
      this.writer = writerConnection(this.writerSettings);
      this.writerReadAt = this.now();
    }
    const connection = this.writer ?? { url: 'http://localhost:8086', database: 'signalk', username: '', password: '',
      from: 'defaults' };
    return this.options.database ? { ...connection, database: this.options.database } : connection;
  }

  async query(statement) {
    const connection = this.connection();
    checkConnection(connection);
    const url = new URL('/query', connection.url);
    url.searchParams.set('db', connection.database);
    url.searchParams.set('epoch', 'ms');
    url.searchParams.set('q', statement);
    // Credentials in a header, not in the URL (which may end up in logs).
    const headers = connection.username
      ? { Authorization: `Basic ${Buffer.from(`${connection.username}:${connection.password}`).toString('base64')}` } : {};
    const response = await this.fetch(url, { headers, signal: AbortSignal.timeout(15000) });
    if (!response.ok) throw new Error(`InfluxDB answered ${response.status}`);
    const body = await response.json();
    const result = body.results?.[0];
    if (result?.error) throw new Error(`InfluxDB: ${result.error}`);
    return result?.series?.[0]?.values ?? [];
  }

  // points values, oldest first, ending now (null: no data in that interval).
  async values({ path, intervalMs, points, reducer }) {
    if (!this.options.enabled) throw new Error('History is disabled');
    if (typeof path !== 'string' || !/^[A-Za-z0-9_.]+$/.test(path)) throw new Error(`History: unsupported path ${path}`);
    if (!Number.isInteger(intervalMs) || intervalMs < 1000 || !Number.isInteger(points) || points < 1 || points > 1000) {
      throw new Error('History: invalid interval or points');
    }
    const fn = REDUCERS[reducer] ?? 'last';
    const context = this.app.selfContext ?? 'vessels.self';
    const source = this.app.getSelfPath?.(path)?.$source;
    const end = this.now();
    // Buckets aligned to the end, so the newest interval ends now.
    const start = end - intervalMs * points;
    const statement = filter => `SELECT ${fn}("value") FROM "${path}" WHERE "context" = ${quoteString(context)}${filter}` +
      ` AND time > ${start}ms AND time <= ${end}ms GROUP BY time(${intervalMs}ms, ${end % intervalMs}ms) fill(null)`;
    this.queries += 1;
    try {
      let rows = source ? await this.query(statement(` AND "source" = ${quoteString(source)}`)) : [];
      if (!rows.some(row => typeof row[1] === 'number')) rows = await this.query(statement(''));
      const values = new Array(points).fill(null);
      for (const [time, value] of rows) {
        const index = Math.floor((time - start) / intervalMs);
        if (index >= 0 && index < points && typeof value === 'number' && Number.isFinite(value)) values[index] = value;
      }
      this.lastError = null;
      return values;
    } catch (error) {
      this.errors += 1; this.lastError = error.message;
      throw error;
    }
  }

  status() {
    const { url, database, username, from } = this.connection();
    return { enabled: this.options.enabled, url, database, user: username || null, from,
      queries: this.queries, errors: this.errors, lastError: this.lastError };
  }
}

module.exports = { HistoryProvider, validateOptions };
