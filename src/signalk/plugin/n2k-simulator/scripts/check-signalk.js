'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { once } = require('node:events');
const { setTimeout: delay } = require('node:timers/promises');

async function unusedPort() {
  const listener = net.createServer();
  listener.listen(0, '127.0.0.1');
  await once(listener, 'listening');
  const port = listener.address().port;
  await new Promise(resolve => listener.close(resolve));
  return port;
}

async function main() {
  if (!process.env.SIGNALK_SERVER_MODULE) {
    throw new Error('Set SIGNALK_SERVER_MODULE to the installed signalk-server directory');
  }
  process.env.SIGNALK_DISABLE_SERVER_UPDATES = 'true';
  const serverDirectory = path.resolve(process.env.SIGNALK_SERVER_MODULE);
  const Server = require(serverDirectory);
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'n2k-signalk-'));
  process.env.SIGNALK_NODE_CONFIG_DIR = directory;
  const port = await unusedPort();
  const tcpPort = await unusedPort();
  const packageName = '@semantic-boat/n2k-simulator';
  const pluginId = 'semantic-boat-n2k-simulator';
  const pluginDirectory = path.resolve(__dirname, '..');
  await fs.mkdir(path.join(directory, 'node_modules/@semantic-boat'), { recursive: true });
  await fs.symlink(pluginDirectory, path.join(directory, 'node_modules', packageName));
  await fs.writeFile(path.join(directory, 'package.json'), JSON.stringify({
    name: 'isolated-n2k-check', version: '1.0.0', private: true,
    dependencies: { [packageName]: `file:${pluginDirectory}` }
  }));
  await fs.mkdir(path.join(directory, 'plugin-config-data'));
  await fs.writeFile(path.join(directory, 'plugin-config-data', `${pluginId}.json`), JSON.stringify({
    enabled: true, configuration: { output: 'n2k', tcpPort, sourceAddress: 200, mode: 'marina' }
  }));
  const interfaces = Object.fromEntries(Object.keys(require(path.join(serverDirectory, 'dist/interfaces')))
    .map(name => [name, ['plugins', 'rest', 'webapps'].includes(name)]));
  const server = new Server({ config: { configPath: directory, settings: {
    port, interfaces, mdns: false, pipedProviders: [{
      id: 'n2k-simulator', enabled: true, pipeElements: [
        { type: 'providers/tcp', options: { host: '127.0.0.1', port: tcpPort } },
        { type: 'providers/liner', options: {} },
        { type: 'providers/canboatjs', options: {} },
        { type: 'providers/n2k-signalk', options: { useCanName: false } }
      ]
    }]
  } } });
  const base = `http://127.0.0.1:${port}`;
  const pluginBase = `${base}/plugins/${pluginId}`;
  const get = async url => {
    const response = await fetch(url);
    assert.equal(response.status, 200, `${url}: ${await response.clone().text()}`);
    return response.json();
  };
  const eventually = async check => {
    const deadline = Date.now() + 12000;
    let lastError;
    while (Date.now() < deadline) {
      try { return await check(); } catch (error) { lastError = error; }
      await delay(100);
    }
    throw lastError;
  };
  try {
    await server.start();
    await eventually(async () => assert.equal((await get(`${pluginBase}/state`)).output, 'n2k'));
    const { SERVERROUTESPREFIX } = require(path.join(serverDirectory, 'dist/constants'));
    const webappsUrl = `${base}${SERVERROUTESPREFIX}/webapps`;
    assert.ok((await get(webappsUrl)).some(webapp => webapp.name === packageName));
    const launcher = await fetch(`${base}/${packageName}/`);
    assert.equal(launcher.status, 200);
    assert.match(await launcher.text(), /window.location.replace\('\/plugins\/semantic-boat-n2k-simulator\/ui\/'\)/);
    const configurationPath = path.join(directory, 'plugin-config-data', `${pluginId}.json`);
    const configuration = JSON.parse(await fs.readFile(configurationPath, 'utf8'));
    await fs.writeFile(configurationPath, JSON.stringify({ ...configuration, enabled: false }));
    assert.ok(!(await get(webappsUrl)).some(webapp => webapp.name === packageName));
    await fs.writeFile(configurationPath, JSON.stringify(configuration));
    console.log('PASS: webapp discovery, dashboard launcher and enabled-plugin visibility');
    const ui = await fetch(`${pluginBase}/ui/`);
    assert.equal(ui.status, 200);
    assert.match(await ui.text(), /N2K Simulator/);
    for (const asset of ['app.js', 'style.css', 'leaflet.js', 'leaflet.css', 'lucide.js', 'nouislider.js', 'nouislider.css']) {
      assert.equal((await fetch(`${pluginBase}/ui/${asset}`)).status, 200, asset);
    }
    const rejected = await fetch(`${pluginBase}/controls`, { method: 'POST',
      headers: { 'Content-Type': 'application/json', Origin: 'http://untrusted.invalid' },
      body: JSON.stringify({ engineRunning: true }) });
    assert.equal(rejected.status, 403);
    const changed = await fetch(`${pluginBase}/controls`, { method: 'POST',
      headers: { 'Content-Type': 'application/json', Origin: base },
      body: JSON.stringify({ engineRunning: true, engineRpm: 2400, waterTemperature: 24 }) });
    assert.equal(changed.status, 200);
    const vessel = `${base}/signalk/v1/api/vessels/self`;
    await eventually(async () => {
      const rpm = await get(`${vessel}/propulsion/port/revolutions`);
      assert.equal(rpm.value, 40);
      assert.equal(rpm.$source, 'n2k-simulator.200');
      const water = await get(`${vessel}/environment/water/temperature`);
      assert.ok(Math.abs(water.value - 297.15) < 0.02);
      assert.equal((await get(`${vessel}/navigation/position`)).value.latitude, 50.8);
    });
    console.log('PASS: real plugin loader, UI assets, origin protection and native TCP -> PGN -> Signal K REST');
    const waterBefore = await get(`${vessel}/environment/water/temperature`);
    const airBefore = await get(`${vessel}/environment/outside/temperature`);
    const disabled = await fetch(`${pluginBase}/controls`, { method: 'POST',
      headers: { 'Content-Type': 'application/json', Origin: base },
      body: JSON.stringify({ outputs: { 'environment.water.temperature': false }, airTemperature: 25 }) });
    assert.equal(disabled.status, 200);
    await eventually(async () => {
      const air = await get(`${vessel}/environment/outside/temperature`);
      assert.ok(air.timestamp > airBefore.timestamp);
      assert.ok(Math.abs(air.value - 298.15) < 0.02);
    });
    const waterAfter = await get(`${vessel}/environment/water/temperature`);
    assert.equal(waterAfter.timestamp, waterBefore.timestamp);
    console.log('PASS: disabled water reading stays stale while the shared temperature PGN updates air');
    const plugin = server.app.plugins.find(candidate => candidate.id === pluginId);
    assert.ok(plugin, 'plugin discovered by server');
    server.app.pipedProviders.stopProvider('n2k-simulator');
    await plugin.stop();
    await plugin.start({ output: 'signalk', tcpPort, mode: 'marina' });
    await eventually(async () => {
      const rpm = await get(`${vessel}/propulsion/main/revolutions`);
      assert.equal(rpm.value, 0);
      assert.equal(rpm.$source, 'semantic-boat.n2k-simulator');
    });
    console.log('PASS: direct plugin injection -> Signal K REST (without an N2K provider)');
    await plugin.stop();
  } finally {
    for (const plugin of server.app.plugins ?? []) await plugin.stop();
    await server.stop();
    await fs.rm(directory, { recursive: true, force: true });
  }
}

main().then(() => process.exit(0), error => { console.error(error); process.exit(1); });