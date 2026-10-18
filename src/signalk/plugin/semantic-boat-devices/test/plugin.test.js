'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const createPlugin = require('../index');

test('web app and direct UI use the same plugin API and icon routes', async () => {
  const source = await fs.readFile(path.join(__dirname, '../public/app.js'), 'utf8');
  const html = await fs.readFile(path.join(__dirname, '../public/index.html'), 'utf8');
  assert.ok(html.includes('src="/plugins/semantic-boat-devices/ui/lucide.js"'));
  for (const pathname of ['/@semantic-boat/devices/', '/plugins/semantic-boat-devices/ui/']) {
    const requests = [];
    let response = { ok: true, status: 200, headers: { get: () => 'application/json' }, json: async () => ({}) };
    const api = vm.runInNewContext(`${source.replace(/^void init\(\);$/m, '')}\napi;`, {
      URL,
      window: { location: new URL(pathname, 'http://navigator'), addEventListener() {} },
      document: { getElementById: () => ({}), querySelectorAll: () => [] },
      fetch: async (url, options) => { requests.push({ url: url.href, options }); return response; }
    });
    for (const route of ['devices', 'configurations', 'templates']) {
      await api(route);
      assert.equal(requests.at(-1).url, `http://navigator/plugins/semantic-boat-devices/${route}`);
    }
    await api('configurations/new.json', 'PUT', { name: 'New' });
    assert.equal(requests.at(-1).url, 'http://navigator/plugins/semantic-boat-devices/configurations/new.json');
    assert.equal(requests.at(-1).options.body, '{"name":"New"}');
    response = { ok: false, status: 404, headers: { get: () => 'text/html' } };
    await assert.rejects(api('devices'), /API devices: expected JSON, received HTTP 404/);
    response.status = 401;
    await assert.rejects(api('devices'), /Sign in to Signal K/);
  }
});

test('property editor discovers scalar paths, preserves extensions and rejects overlapping cells', async () => {
  const source = await fs.readFile(path.join(__dirname, '../public/app.js'), 'utf8');
  const nodes = new Map();
  const getElementById = id => {
    if (!nodes.has(id)) nodes.set(id, { value: '', textContent: '', hidden: false });
    return nodes.get(id);
  };
  const editor = vm.runInNewContext(`${source.replace(/^void init\(\);$/m, '')}\n({ collectPaths, widgetFits, editDocument });`, {
    URL, TextEncoder,
    window: { location: new URL('http://navigator/@semantic-boat/devices/'), addEventListener() {} },
    document: { getElementById, querySelectorAll: () => [] }
  });
  const paths = editor.collectPaths({ navigation: {
    speedOverGround: { value: 0, meta: { units: 'm/s', displayName: 'Speed' }, values: { other: { value: 9 } } },
    state: { value: 'moored' }, position: { value: { latitude: 1, longitude: 2 } },
    unknown: { value: null }, enabled: { value: true }
  }, $source: { value: 'ignore' } });
  assert.deepEqual(JSON.parse(JSON.stringify(paths)), [
    { path: 'navigation.speedOverGround', unit: 'm/s', value: 0, label: 'Speed' },
    { path: 'navigation.state', unit: '', value: 'moored', label: '' },
    { path: 'navigation.position', unit: '', value: '1, 2', label: 'Position' }
  ]);
  const page = { grid: { columns: 2, rows: 2 }, widgets: [{ cell: { column: 0, row: 0, row_span: 2 } }] };
  assert.equal(editor.widgetFits(page, { cell: { column: 1, row: 1 } }), true);
  assert.equal(editor.widgetFits(page, { cell: { column: 0, row: 1 } }), false);
  assert.equal(editor.widgetFits(page, { cell: { column: 1, row: 0, column_span: 2 } }), false);
  assert.equal(editor.widgetFits(page, page.widgets[0], 0), true);
  const configuration = { name: 'Original', extension: { custom: [1, 2] }, sections: [{ id: 'section', pages: [] }] };
  getElementById('json').value = JSON.stringify(configuration);
  assert.equal(editor.editDocument(document => { document.name = 'Edited'; }), true);
  const edited = JSON.parse(getElementById('json').value);
  assert.deepEqual(edited.extension, configuration.extension);
  assert.deepEqual(edited.sections, configuration.sections);
  assert.equal(edited.name, 'Edited');
  const before = getElementById('json').value;
  assert.equal(editor.editDocument(document => { document.name = 'Rejected'; throw new Error('Invalid change'); }), false);
  assert.equal(getElementById('json').value, before);
  assert.equal(getElementById('notice').textContent, 'Invalid change');
});

test('editor exposes sections and pages with level-specific creation and deletion', async () => {
  const source = await fs.readFile(path.join(__dirname, '../public/app.js'), 'utf8');
  const createElement = tag => ({ tag, children: [], attributes: {}, dataset: {}, style: {},
    value: '', textContent: '', isConnected: true,
    append(...children) { this.children.push(...children); },
    replaceChildren(...children) { this.children = children; },
    setAttribute(key, value) { this.attributes[key] = value; },
    focus() {}, close() {}, reportValidity() { return true; } });
  const nodes = new Map();
  const getElementById = id => {
    if (!nodes.has(id)) nodes.set(id, createElement('div'));
    return nodes.get(id);
  };
  const editor = vm.runInNewContext(`${source.replace(/^void init\(\);$/m, '')}\n({ renderProperties });`, {
    URL, TextEncoder, confirm: () => true,
    window: { location: new URL('http://navigator/@semantic-boat/devices/'), addEventListener() {} },
    document: { getElementById, createElement, querySelectorAll: () => [] }
  });
  const page = title => ({ id: title.toLowerCase(), title, grid: { columns: 2, rows: 2, gap: 4 }, widgets: [], extension: { title } });
  getElementById('json').value = JSON.stringify({ schema: 'semantic-display/v1', name: 'Navigation', sections: [
    { id: 'navigation', title: 'NAVIGATION', sources: [{ id: 'position', path: 'navigation.position', outdated_ms: 10000, stale_ms: 60000 }], extension: { custom: true }, pages: [page('OVERVIEW'), page('MOTION')] },
    { id: 'depth', title: 'DEPTH', sources: [], pages: [page('DEPTH REFERENCES')] }
  ] });
  const descendants = node => [node, ...node.children.flatMap(descendants)];
  const controls = () => descendants(getElementById('properties'));
  const button = name => controls().find(node => node.tag === 'button' && node.attributes['aria-label'] === name);
  editor.renderProperties();
  const tabs = controls().filter(node => node.attributes.role === 'tab');
  assert.deepEqual(tabs.map(node => node.textContent), ['NAVIGATION', 'DEPTH']);
  assert.equal(tabs[0].attributes['aria-selected'], 'true');
  assert.ok(button('Page: MOTION'));
  button('Add widget').onclick();
  const positionFormat = () => controls().find(node => node.tag === 'select' && node.attributes['aria-label'] === 'Position format');
  assert.equal(positionFormat().value, 'DDM');
  assert.deepEqual(positionFormat().children.map(option => option.value), ['DD', 'DDM', 'DMS']);
  for (const format of ['DD', 'DMS', 'DDM']) {
    positionFormat().value = format; positionFormat().onchange();
    assert.equal(JSON.parse(getElementById('json').value).sections[0].pages[0].widgets[0].position_format, format);
  }
  assert.equal(JSON.parse(getElementById('json').value).sections[0].pages[0].widgets[0].source, 'position');
  const original = JSON.parse(getElementById('json').value);
  const input = label => controls().find(node => node.tag === 'input' && node.attributes['aria-label'] === label);
  input('Outdated after (ms)').value = '60000'; input('Outdated after (ms)').onchange();
  assert.deepEqual(JSON.parse(getElementById('json').value), original);
  assert.equal(input('Outdated after (ms)').value, 10000);
  input('Outdated after (ms)').value = '15000'; input('Outdated after (ms)').onchange();
  assert.equal(JSON.parse(getElementById('json').value).sections[0].sources[0].outdated_ms, 15000);
  input('Stale after (ms)').value = '10000'; input('Stale after (ms)').onchange();
  assert.equal(JSON.parse(getElementById('json').value).sections[0].sources[0].stale_ms, 60000);
  input('Outdated after (ms)').value = '10000'; input('Outdated after (ms)').onchange();
  const sectionSources = () => controls().find(node => node.tag === 'details' && node.children[0].textContent.startsWith('Section sources'));
  sectionSources().open = true; sectionSources().ontoggle();
  assert.equal(button('Move section earlier').disabled, true);
  assert.equal(button('Move page earlier').disabled, true);
  button('Move page earlier').onclick();
  assert.deepEqual(JSON.parse(getElementById('json').value), original);
  button('Move page later').onclick();
  assert.deepEqual(JSON.parse(getElementById('json').value).sections[0].pages.map(entry => entry.id), ['motion', 'overview']);
  assert.equal(button('Page: OVERVIEW').attributes['aria-current'], 'page');
  assert.equal(button('Move page later').disabled, true);
  button('Move section later').onclick();
  assert.deepEqual(JSON.parse(getElementById('json').value).sections.map(entry => entry.id), ['depth', 'navigation']);
  assert.equal(controls().find(node => node.textContent === 'NAVIGATION').attributes['aria-selected'], 'true');
  assert.equal(button('Page: OVERVIEW').attributes['aria-current'], 'page');
  assert.equal(sectionSources().open, true);
  assert.equal(button('Move section later').disabled, true);
  button('Move section earlier').onclick();
  button('Move page earlier').onclick();
  assert.deepEqual(JSON.parse(getElementById('json').value), original);
  assert.equal(getElementById('editor-status').textContent, 'Unsaved');
  tabs[1].onclick();
  assert.equal(sectionSources().open, false);
  assert.ok(button('Page: DEPTH REFERENCES'));
  assert.equal(button('Move page earlier').disabled, true);
  assert.equal(button('Move page later').disabled, true);
  assert.equal(button('Page: MOTION'), undefined);
  button('Add page').onclick();
  assert.equal(JSON.parse(getElementById('json').value).sections[1].pages.length, 2);
  assert.ok(button('Page: NEW PAGE'));
  button('Add widget').onclick();
  assert.equal(JSON.parse(getElementById('json').value).sections[1].pages[1].widgets[0].type, 'text');
  button('Delete page').onclick();
  assert.equal(JSON.parse(getElementById('json').value).sections[1].pages.length, 1);
  assert.equal(button('Delete page').disabled, true);
  button('Add section').onclick();
  assert.equal(JSON.parse(getElementById('json').value).sections.length, 3);
  assert.equal(controls().find(node => node.textContent === 'NEW SECTION').attributes['aria-selected'], 'true');
  button('Delete section').onclick();
  assert.equal(JSON.parse(getElementById('json').value).sections.length, 2);
  getElementById('json').value = JSON.stringify({ schema: 'semantic-display/v1', sections: [{ id: 'history',
    sources: [{ id: 'temperature', path: 'environment.outside.temperature', unit: 'K' }], pages: [page('TEMPERATURE')] }] });
  editor.renderProperties();
  input('Record history temperature').checked = true; input('Record history temperature').onchange();
  const draft = () => JSON.parse(getElementById('json').value);
  assert.equal(draft().sections[0].sources[0].history.points, 120);
  const select = label => controls().find(node => node.tag === 'select' && node.attributes['aria-label'] === label);
  select('History source').value = 'temperature'; select('History source').onchange();
  assert.equal(draft().sections[0].pages[0].history_plot.source, 'temperature');
  assert.equal(button('Delete source temperature').disabled, true);
  input('Record history temperature').checked = false; input('Record history temperature').onchange();
  assert.equal(input('Record history temperature').checked, true);
  input('Fixed Y range').checked = true; input('Fixed Y range').onchange();
  input('Y minimum').value = '-10.5'; input('Y minimum').onchange();
  assert.equal(draft().sections[0].pages[0].history_plot.y_min, -10.5);
  input('Y maximum').value = '-20'; input('Y maximum').onchange();
  assert.equal(draft().sections[0].pages[0].history_plot.y_max, 100);
  input('Source ID').value = 'air'; input('Source ID').onchange();
  assert.equal(draft().sections[0].pages[0].history_plot.source, 'air');
  input('History points').value = '249'; input('History points').onchange();
  assert.equal(draft().sections[0].sources[0].history.points, 120);
  select('History source').value = ''; select('History source').onchange();
  assert.equal(draft().sections[0].pages[0].history_plot, undefined);
  assert.equal(button('Delete source air').disabled, false);
  input('Record history air').checked = false; input('Record history air').onchange();
  assert.equal(draft().sections[0].sources[0].history, undefined);
});

test('plugin lifecycle and catalog routes work with the server API', async context => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'boat-plugin-'));
  context.after(() => fs.rm(directory, { recursive: true, force: true }));
  const errors = [];
  const plugin = createPlugin({ getDataDirPath: () => directory,
    setPluginStatus() {}, setPluginError: message => errors.push(message) });
  const routes = new Map();
  const router = {};
  for (const method of ['get', 'post', 'put', 'delete']) router[method] = (route, handler) => routes.set(`${method} ${route}`, handler);
  plugin.registerWithRouter(router);
  await plugin.start({});
  async function call(method, route, body, params = {}, origin) {
    let result; let status = 200;
    const response = { status(code) { status = code; return this; }, json(data) { result = data; } };
    await routes.get(`${method} ${route}`)({ method: method.toUpperCase(), body, params,
      protocol: 'http', get: name => name === 'host' ? 'localhost' : origin }, response);
    return { result, status };
  }
  const templates = await call('get', '/templates');
  assert.equal(templates.status, 200); assert.ok(templates.result['engine.json']);
  const names = ['electrics.json', 'engine.json', 'environment.json', 'navigation.json', 'server.json', 'weather-station.json'];
  assert.deepEqual(Object.keys(templates.result).sort(), names);
  const weather = templates.result['weather-station.json'];
  const air = weather.sections.find(section => section.id === 'weather-air');
  for (const id of ['pm25', 'pm10']) {
    const source = [...weather.permanent_sources, ...air.sources].find(entry => entry.id === id);
    const widget = air.pages.flatMap(page => page.widgets).find(entry => entry.source === id);
    assert.equal(source.provider, 'signalk-ugm3');
    assert.equal(source.unit, 'ug/m3');
    assert.equal(widget.display_unit, 'ug/m3');
    assert.equal(widget.decimals, 0);
  }
  for (const name of names) {
    const document = templates.result[name];
    for (const section of document.sections) {
      for (const source of section.sources) {
        assert.ok(['signalk', 'signalk-text', 'signalk-ugm3'].includes(source.provider));
        assert.ok(!source.path.startsWith('propulsion.main.'));
        assert.ok(!source.path.startsWith('system.server.'));
        if (name === 'server.json') assert.ok(source.path.startsWith('environment.rpi.'));
        if (name === 'electrics.json') assert.ok(!source.path.startsWith('environment.'));
      }
    }
    const saved = await call('put', '/configurations/:name', document, { name });
    assert.equal(saved.status, 200);
  }
  assert.deepEqual((await call('get', '/configurations')).result.names, names);
  const crossOrigin = await call('delete', '/configurations/:name', undefined, { name: 'engine.json' }, 'https://other.example');
  assert.equal(crossOrigin.status, 403);
  await plugin.stop();
  assert.equal((await call('get', '/devices')).status, 503);
  assert.deepEqual(errors, []);
});