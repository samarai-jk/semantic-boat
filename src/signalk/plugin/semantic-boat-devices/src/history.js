'use strict';

// Graph history for displays (README, "History"): the values of one Signal K
// path from the InfluxDB that signalk-to-influxdb writes (InfluxDB 1.x HTTP
// API), aggregated into the display's intervals with the display's reducer.
// Only the own vessel; the source Signal K prefers for the path when it has
// data there, else every source of the path.

const REDUCERS = { 1: 'last', 2: 'mean', 3: 'min', 4: 'max' };

function validateOptions(options = {}) {
  const result = { enabled: options.enabled ?? true, url: options.url ?? 'http://localhost:8086',
    database: options.database ?? 'signalk' };
  let url;
  try { url = new URL(result.url); } catch { throw new Error('History: invalid InfluxDB URL'); }
  if (!['http:', 'https:'].includes(url.protocol)) throw new Error('History: InfluxDB URL must be http or https');
  if (!/^[A-Za-z0-9_-]+$/.test(result.database)) throw new Error('History: invalid database name');
  return result;
}

const quoteString = value => `'${String(value).replace(/\\/g, '\\\\').replace(/'/g, "\\'")}'`;

class HistoryProvider {
  constructor({ app, options, fetchImpl = globalThis.fetch, now = Date.now }) {
    this.app = app; this.options = validateOptions(options); this.fetch = fetchImpl; this.now = now;
    this.queries = 0; this.errors = 0; this.lastError = null;
  }

  async query(statement) {
    const url = new URL('/query', this.options.url);
    url.searchParams.set('db', this.options.database);
    url.searchParams.set('epoch', 'ms');
    url.searchParams.set('q', statement);
    const response = await this.fetch(url, { signal: AbortSignal.timeout(15000) });
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

  status() { return { ...this.options, queries: this.queries, errors: this.errors, lastError: this.lastError }; }
}

module.exports = { HistoryProvider, validateOptions };
