'use strict';

const http = require('node:http');
const fs = require('node:fs/promises');
const { SimulatorRuntime } = require('./runtime');
const { assets } = require('./ui');
const { readings } = require('./instruments');
const { timescales } = require('./fluctuation');

function parseArguments(argv) {
  const options = { port: 10112, apiPort: 10113, mode: 'marina' };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '--help') return { help: true };
    const key = { '--port': 'port', '--api-port': 'apiPort', '--mode': 'mode' }[argument];
    if (!key || argv[index + 1] === undefined) throw new Error(`Invalid argument: ${argument}`);
    options[key] = key === 'mode' ? argv[++index] : Number(argv[++index]);
  }
  for (const key of ['port', 'apiPort']) {
    if (!Number.isInteger(options[key]) || options[key] < 1024 || options[key] > 65535) {
      throw new RangeError(`${key} must be between 1024 and 65535`);
    }
  }
  return options;
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  if (options.help) {
    console.log('npm start -- [--mode marina|anchor|underway] [--port 10112] [--api-port 10113]');
    return;
  }
  const runtime = new SimulatorRuntime({ controls: { mode: options.mode }, onError: error => console.error(error.message) });
  const api = http.createServer(async (request, response) => {
    const reply = (status, body) => {
      response.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
      response.end(JSON.stringify(body));
    };
    if (request.method === 'GET' && (request.url === '/' || request.url === '/ui')) {
      response.writeHead(302, { Location: '/ui/' });
      response.end();
      return;
    }
    const asset = request.url.startsWith('/ui/')
      ? assets[request.url.slice(4) || 'index.html'] : null;
    if (request.method === 'GET' && asset) {
      try {
        const content = await fs.readFile(asset[0]);
        response.writeHead(200, { 'Content-Type': asset[1], 'Cache-Control': 'no-cache' });
        response.end(content);
      } catch { reply(500, { error: 'Unable to load UI asset' }); }
      return;
    }
    if (request.method === 'GET' && request.url === '/state') return reply(200, runtime.snapshot());
    if (request.method === 'GET' && request.url === '/parameters') return reply(200, { readings, timescales });
    if (request.method !== 'POST') return reply(404, { error: 'Not found' });
    if (request.headers.origin && request.headers.origin !== `http://${request.headers.host}`) {
      return reply(403, { error: 'Cross-origin controls are disabled' });
    }
    if ((request.headers['content-type'] || '').split(';')[0] !== 'application/json') {
      return reply(415, { error: 'Use application/json' });
    }
    try {
      let body = '';
      for await (const chunk of request) {
        body += chunk.toString('utf8');
        if (body.length > 8192) return reply(413, { error: 'Request too large' });
      }
      const patch = JSON.parse(body || '{}');
      if (request.url === '/controls') return reply(200, runtime.control(patch));
      if (request.url === '/pause') runtime.paused = true;
      else if (request.url === '/resume') runtime.paused = false;
      else if (request.url === '/reset') runtime.reset();
      else return reply(404, { error: 'Not found' });
      reply(200, runtime.snapshot());
    } catch (error) { reply(400, { error: error.message }); }
  });
  const stop = async () => {
    api.close();
    await runtime.stop();
  };
  try {
    await runtime.start(options.port);
    await new Promise((resolve, reject) => {
      api.once('error', reject);
      api.listen(options.apiPort, '127.0.0.1', resolve);
    });
    console.log(`SIMULATION ONLY: N2K Actisense text at 127.0.0.1:${options.port}`);
    console.log(`Helm UI: http://127.0.0.1:${options.apiPort}/ui/`);
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  } catch (error) {
    await stop();
    throw error;
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });