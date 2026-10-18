'use strict';

// B1S view: devices on the Semantic Boat CAN network, their channels and
// parameters, calibration, console, and backups (src/lib/b1s/CONFIGURATION.md).
(() => {
  const base = new URL('/plugins/semantic-boat-devices/b1s/', window.location.origin);
  const view = document.getElementById('b1s-view');
  const list = document.getElementById('b1s-devices');
  const status = document.getElementById('b1s-status');
  const encodings = ['automatic', 'standard', 'b1s', 'off'];
  const cards = new Map(); // device ID -> { root, parameters, consoleSince, consoleOpen }

  async function call(route, method = 'GET', body) {
    const response = await fetch(new URL(route, base), { method,
      headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body) });
    if (!response.headers.get('content-type')?.includes('application/json')) {
      throw new Error(response.status === 401 || response.status === 403
        ? 'Sign in to Signal K, then reload this page.' : `HTTP ${response.status}`);
    }
    const result = await response.json();
    if (!response.ok) throw new Error(result.error ?? `HTTP ${response.status}`);
    return result;
  }

  function node(tag, content = '', className = '') {
    const element = document.createElement(tag);
    element.textContent = content;
    if (className) element.className = className;
    return element;
  }

  function button(label, action) {
    const element = node('button', label, 'b1s-button');
    element.onclick = async () => {
      element.disabled = true;
      try { await action(); } catch (error) { say(error.message, true); } finally { element.disabled = false; }
    };
    return element;
  }

  function say(message, error = false) {
    status.textContent = message;
    status.className = error ? 'b1s-status error-text' : 'b1s-status';
  }

  // SI value <-> what people read: °C for absolute temperatures, % for ratios.
  function display(si, unit, absolute) {
    if (si === null || si === undefined) return { text: '–', value: '' };
    if (unit === 'K' && absolute) return { text: `${(si - 273.15).toFixed(2)} °C`, value: (si - 273.15).toFixed(3), unit: '°C' };
    if (unit === 'ratio') return { text: `${(si * 100).toFixed(2)} %`, value: (si * 100).toFixed(4), unit: '%' };
    const value = Number(si.toPrecision(7));
    return { text: `${value} ${unit ?? ''}`.trim(), value: String(value), unit: unit ?? '' };
  }
  function fromDisplay(text, unit, absolute) {
    const number = Number(text);
    if (!Number.isFinite(number)) throw new Error(`Not a number: ${text}`);
    if (unit === 'K' && absolute) return number + 273.15;
    if (unit === 'ratio') return number / 100;
    return number;
  }

  function channelRow(device, channel, card) {
    const row = document.createElement('tr');
    const parameter = name => card.parameters?.find(entry => entry.name === name && entry.channel === channel.channel);
    const label = Object.assign(document.createElement('input'), { value: parameter('channelLabel')?.value ?? channel.label });
    const location = Object.assign(document.createElement('input'), {
      value: parameter('channelLocation')?.value ?? channel.location, placeholder: 'unassigned' });
    const encoding = document.createElement('select');
    for (const name of encodings) encoding.append(Object.assign(node('option', name), { value: name }));
    encoding.value = encodings[parameter('channelEncoding')?.value] ?? 'automatic';
    encoding.disabled = !card.parameters;
    const value = channel.encoding === 'standard' ? 'standard PGN' : display(channel.value, channel.unit, true).text;
    row.append(node('td', String(channel.channel)), node('td', channel.quantity ?? String(channel.kind)), node('td', value, 'value'));
    for (const input of [label, location, encoding]) { const cell = node('td'); cell.append(input); row.append(cell); }
    row.append(node('td', channel.path ?? `standard PGN, instance ${channel.standardInstance}`, 'path'));
    const actions = node('td', '', 'b1s-actions');
    actions.append(button('Save', async () => {
      const id = device.id;
      const writes = [['channelLabel', label.value], ['channelLocation', location.value]];
      if (card.parameters) writes.push(['channelEncoding', encoding.value]);
      for (const [name, text] of writes) {
        const result = await call(`devices/${id}/parameters/${name}`, 'PUT', { channel: channel.channel, value: text });
        if (result.status !== 'done') throw new Error(`${name}: ${result.status}`);
      }
      say(`Channel ${channel.channel} saved on device ${id}`);
      await loadParameters(device.id);
    }));
    actions.append(button('Calibrate…', async () => {
      const unit = channel.unit === 'K' ? '°C' : channel.unit === 'ratio' ? '%' : channel.unit;
      const text = prompt(`Reference value for ${channel.label || `channel ${channel.channel}`} in ${unit}:`);
      if (text === null) return;
      const result = await call(`devices/${device.id}/commands/calibrate`, 'POST',
        { channel: channel.channel, si: fromDisplay(text, channel.unit, true) });
      say(`Calibrate channel ${channel.channel}: ${result.result}`, result.result !== 'done');
      await loadParameters(device.id);
    }));
    actions.append(button('Reset', async () => {
      if (!confirm(`Reset calibration and alarm thresholds of channel ${channel.channel}?`)) return;
      const result = await call(`devices/${device.id}/commands/resetChannel`, 'POST', { channel: channel.channel });
      say(`Reset channel ${channel.channel}: ${result.result}`, result.result !== 'done');
      await loadParameters(device.id);
    }));
    row.append(actions);
    return row;
  }

  function parameterTable(device, card) {
    const table = document.createElement('table');
    const head = document.createElement('tr');
    for (const title of ['Parameter', 'Channel', 'Value', '']) head.append(node('th', title));
    table.append(head);
    for (const parameter of card.parameters ?? []) {
      if (['channelLabel', 'channelLocation', 'channelEncoding'].includes(parameter.name)) continue; // in the channel table
      const row = document.createElement('tr');
      row.title = parameter.description;
      row.append(node('td', parameter.name), node('td', parameter.channel === null ? '' : String(parameter.channel)));
      const shown = parameter.si !== null ? display(parameter.si, parameter.siUnit, parameter.absolute)
        : parameter.values ? { text: parameter.values[parameter.value] ?? String(parameter.value), value: parameter.values[parameter.value] }
          : { text: parameter.status === 'done' ? String(parameter.value) : parameter.status, value: String(parameter.value ?? '') };
      const cell = node('td', '', 'value');
      const actions = node('td');
      if (parameter.access === 'rw' && parameter.status === 'done') {
        const input = Object.assign(document.createElement('input'), { value: shown.value });
        cell.append(input, node('span', ` ${shown.unit ?? (parameter.si === null ? parameter.unit ?? '' : '')}`));
        actions.append(button('Set', async () => {
          const body = { channel: parameter.channel };
          if (parameter.si !== null) body.si = fromDisplay(input.value, parameter.siUnit, parameter.absolute);
          else body.value = parameter.type === 'string' || parameter.values ? input.value : Number(input.value);
          const result = await call(`devices/${device.id}/parameters/${parameter.name}`, 'PUT', body);
          say(`${parameter.name}: ${result.status}`, result.status !== 'done');
          await loadParameters(device.id);
        }));
      } else {
        cell.textContent = shown.text;
      }
      row.append(cell, actions);
      table.append(row);
    }
    return table;
  }

  async function loadParameters(id) {
    const card = cards.get(id);
    if (!card || card.loading) return;
    card.loading = true;
    try {
      card.parameters = (await call(`devices/${id}/parameters`)).parameters;
      card.version = (card.version ?? 0) + 1; // redraw with the new values
      say(`Parameters of device ${id} read (and backed up)`);
    } finally {
      card.loading = false;
    }
  }

  function consolePanel(device, card) {
    const panel = document.createElement('details');
    panel.className = 'b1s-console';
    panel.open = card.consoleOpen;
    panel.append(node('summary', 'Console'));
    const output = node('pre', card.consoleText ?? '');
    const input = Object.assign(document.createElement('input'), { placeholder: 'help, sensors, health, supply …' });
    const run = async () => {
      await call(`devices/${device.id}/console`, 'POST', { line: input.value });
      input.value = '';
    };
    input.onkeydown = event => { if (event.key === 'Enter') void run().catch(error => say(error.message, true)); };
    panel.ontoggle = () => {
      card.consoleOpen = panel.open;
      if (panel.open) void call(`devices/${device.id}/console`, 'POST', { line: '' }).catch(() => {});
    };
    card.consoleOutput = output;
    panel.append(output, input);
    return panel;
  }

  function render(devices) {
    if (!devices.length) {
      list.replaceChildren(node('p', 'No B1S devices seen yet', 'empty'));
      return;
    }
    for (const device of devices) {
      let card = cards.get(device.id);
      if (!card) {
        card = { root: node('article', '', 'device'), consoleOpen: false, consoleSince: 0, consoleText: '' };
        cards.set(device.id, card);
        list.append(card.root);
        void loadParameters(device.id).catch(error => say(error.message, true));
      }
      // Rebuild only when something other than live values changed, so inputs keep their edits.
      const key = JSON.stringify([device.address, device.product, device.channels.map(channel =>
        [channel.channel, channel.label, channel.location, channel.encoding]), card.version ?? 0]);
      if (card.key === key) {
        card.root.querySelectorAll('tbody.b1s-channels tr').forEach((row, index) => {
          const channel = device.channels[index];
          if (channel && channel.encoding !== 'standard') row.children[2].textContent = display(channel.value, channel.unit, true).text;
        });
        continue;
      }
      card.key = key;
      const header = node('div', '', 'device-header');
      const title = node('div');
      title.append(node('h2', device.product?.model || 'B1S device'), node('span', `address ${device.address}`, 'badge'));
      const controls = node('div', '', 'install-controls');
      controls.append(
        button('Identify', async () => say(`Identify: ${(await call(`devices/${device.id}/commands/identify`, 'POST', { argument: 5 })).result}`)),
        button('Parameters', () => loadParameters(device.id)),
        button('Restore…', async () => {
          const backups = await call('backups');
          const own = backups.find(backup => backup.deviceId === device.id);
          const choices = backups.map(backup => `${backup.deviceId} (${backup.product?.model ?? '?'}, ${backup.savedAt})`).join('\n');
          const from = prompt(`Restore the configuration of which device ID?\n${choices}`, own ? String(device.id) : '');
          if (!from) return;
          const result = await call(`devices/${device.id}/restore`, 'POST', { from: Number(from) });
          say(`Restored ${result.restored} of ${result.results.length} parameter(s)`);
          await loadParameters(device.id);
        }),
        button('Restart', async () => {
          if (!confirm(`Restart device ${device.id}?`)) return;
          say(`Restart: ${(await call(`devices/${device.id}/commands/restart`, 'POST', {})).result}`);
        }));
      header.append(title, controls);
      const meta = node('p', [`device ID ${device.id}`, device.product?.software, device.product?.hardware,
        device.product?.code !== undefined && device.product?.code !== null ? `product ${device.product.code}` : null]
        .filter(Boolean).join(' · '), 'device-meta');
      const channels = document.createElement('table');
      const head = document.createElement('tr');
      for (const title of ['Ch', 'Quantity', 'Value', 'Label', 'Location', 'Encoding', 'Signal K path', '']) head.append(node('th', title));
      const body = Object.assign(document.createElement('tbody'), { className: 'b1s-channels' });
      for (const channel of device.channels) body.append(channelRow(device, channel, card));
      channels.append(head, body);
      const scroll = node('div', '', 'table-scroll');
      scroll.append(channels);
      const parameters = document.createElement('details');
      parameters.append(node('summary', card.parameters ? 'Parameters' : 'Parameters (reading …)'));
      if (card.parameters) parameters.append(parameterTable(device, card));
      card.root.replaceChildren(header, meta, scroll, parameters, consolePanel(device, card));
    }
  }

  async function pollConsoles() {
    for (const [id, card] of cards) {
      if (!card.consoleOpen) continue;
      const output = await call(`devices/${id}/console?since=${card.consoleSince}`);
      card.consoleSince = output.next;
      if (output.text) {
        card.consoleText = (card.consoleText + output.text).slice(-20000);
        if (card.consoleOutput) {
          card.consoleOutput.textContent = card.consoleText;
          card.consoleOutput.scrollTop = card.consoleOutput.scrollHeight;
        }
      }
      // Keep the device's log mirror alive while the console is open.
      const now = Date.now();
      if (!card.keepAliveAt || now - card.keepAliveAt > 20000) {
        card.keepAliveAt = now;
        await call(`devices/${id}/console`, 'POST', { line: '' });
      }
    }
  }

  async function poll() {
    if (view.hidden) return;
    try {
      render(await call('devices'));
      await pollConsoles();
    } catch (error) {
      say(error.message, true);
    }
  }

  setInterval(poll, 1000);
})();
