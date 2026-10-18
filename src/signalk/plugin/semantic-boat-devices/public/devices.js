'use strict';

// Devices view: every device of every transport in one list (left), the
// selected device in detail (right). Displays (any transport) show their
// live bindings and install configurations from the designer's catalog; B1S
// devices show channels, parameters, calibration, their log, and backups
// (src/lib/b1s/CONFIGURATION.md). Uses api(), notice() and text() of app.js.
(() => {
  const list = document.getElementById('device-list');
  const detail = document.getElementById('device-detail');
  const view = document.getElementById('devices-view');
  const encodings = ['automatic', 'standard', 'b1s', 'off'];
  const kinds = { display: 'Display', b1s: 'B1S device' };
  let devices = [];
  let catalogNames = [];
  let selected = decodeURIComponent(window.location.hash.slice(1)) || null; // "transport/id"
  let rendered = { key: null, version: 0 };
  const b1sCards = new Map(); // device ID -> { parameters, version, log state }

  const keyOf = device => `${device.transport}/${device.id}`;

  function button(label, action, title = '') {
    const element = text('button', label, 'small-button');
    if (title) element.title = title;
    element.onclick = async () => {
      element.disabled = true;
      try { await action(); } catch (error) { notice(error.message, true); } finally { element.disabled = false; }
    };
    return element;
  }

  function table(headings, rows, className = '') {
    const scroll = text('div', '', 'table-scroll');
    const element = document.createElement('table');
    const head = document.createElement('tr');
    for (const heading of headings) head.append(text('th', heading));
    const body = document.createElement('tbody');
    if (className) body.className = className;
    for (const row of rows) body.append(row);
    element.append(head, body);
    scroll.append(element);
    return scroll;
  }

  function cells(...values) {
    const row = document.createElement('tr');
    for (const value of values) {
      if (value instanceof Node) { const cell = document.createElement('td'); cell.append(value); row.append(cell); }
      else row.append(text('td', value ?? ''));
    }
    return row;
  }

  // SI value <-> what people read: °C for absolute temperatures, % for ratios.
  function display(si, unit, absolute) {
    if (si === null || si === undefined) return { text: '–', value: '' };
    if (unit === 'K' && absolute) return { text: `${(si - 273.15).toFixed(2)} °C`, value: (si - 273.15).toFixed(3), unit: '°C' };
    if (unit === 'ratio') return { text: `${(si * 100).toFixed(2)} %`, value: (si * 100).toFixed(4), unit: '%' };
    const value = Number(si.toPrecision(7));
    return { text: `${value} ${unit ?? ''}`.trim(), value: String(value), unit: unit ?? '' };
  }
  function fromDisplay(input, unit, absolute) {
    const number = Number(input);
    if (!Number.isFinite(number)) throw new Error(`Not a number: ${input}`);
    if (unit === 'K' && absolute) return number + 273.15;
    if (unit === 'ratio') return number / 100;
    return number;
  }

  // --- list -----------------------------------------------------------------

  function renderList() {
    list.replaceChildren();
    if (!devices.length) list.append(text('p', 'No devices. Enable B1S or the RS485 plugin.', 'empty'));
    const transports = [...new Set(devices.map(device => device.transportLabel))];
    for (const transport of transports) {
      list.append(text('h3', transport, 'transport-heading'));
      for (const device of devices.filter(entry => entry.transportLabel === transport)) {
        const item = text('button', '', `device-item${keyOf(device) === selected ? ' selected' : ''}`);
        item.append(text('span', '', `dot ${device.online ? 'online' : 'offline'}`), text('span', device.label, 'device-name'),
          text('small', `${kinds[device.kind] ?? device.kind} · ${device.status}`));
        item.onclick = () => {
          selected = keyOf(device);
          window.location.hash = encodeURIComponent(selected);
          rendered.key = null;
          render();
        };
        list.append(item);
      }
    }
    const online = devices.filter(device => device.online).length;
    document.getElementById('connection').textContent = `${online}/${devices.length} device(s) online`;
  }

  // --- displays -------------------------------------------------------------

  function renderDisplay(device) {
    const info = device.detail ?? {};
    const header = text('div', '', 'device-header');
    const title = text('div');
    title.append(text('h2', device.label), text('span', device.status, `badge ${device.online ? '' : 'offline'}`));
    header.append(title);
    if (device.canInstall) {
      const controls = text('div', '', 'install-controls');
      const select = document.createElement('select');
      select.setAttribute('aria-label', 'Configuration to install');
      for (const name of catalogNames) select.append(Object.assign(text('option', name), { value: name }));
      if (!catalogNames.length) select.append(text('option', 'No configurations'));
      controls.append(select, button('Install', async () => {
        if (!confirm(`Install ${select.value} on ${device.label}?`)) return;
        const result = await api(`devices/${encodeURIComponent(device.transport)}/${encodeURIComponent(device.id)}/install`,
          'POST', { name: select.value });
        notice(result.status);
      }, 'Send the configuration from the designer catalog to this display'));
      header.append(controls);
    }
    const meta = text('p', `${device.transportLabel} · ${device.address ?? ''} · ${device.id}`, 'device-meta');
    const rows = (info.subscriptions ?? []).map(source => {
      const row = cells(String(source.sourceIndex), source.path + (source.path !== source.resolvedPath ? ` → ${source.resolvedPath}` : ''),
        source.value === null || source.value === undefined ? 'Unavailable' : String(source.value), source.source ?? '');
      row.children[1].className = 'path';
      row.children[2].className = 'value';
      return row;
    });
    const stats = text('div', [`${info.receivedFrames ?? 0} RX`, `${info.sentFrames ?? 0} TX`, `${info.valuesSent ?? 0} values`,
      `${info.decodeErrors ?? 0} decode errors`, `${info.queuedFrames ?? 0} queued`].join(' · '), 'stats');
    const transfer = text('div', info.lastTransfer ? `Last install: ${info.lastTransfer.name}: ${info.lastTransfer.state}` : '', 'transfer');
    detail.replaceChildren(header, meta, text('h3', 'Live values requested by the display'),
      rows.length ? table(['#', 'Signal K path', 'Value (SI)', 'Source'], rows, 'subscriptions') : text('p', 'No subscriptions yet', 'empty'),
      stats, text('div', info.error ?? '', 'error-text'), transfer);
  }

  function updateDisplay(device) {
    const body = detail.querySelector('tbody.subscriptions');
    (device.detail?.subscriptions ?? []).forEach((source, index) => {
      const row = body?.children[index];
      if (row) {
        row.children[2].textContent = source.value === null || source.value === undefined ? 'Unavailable' : String(source.value);
        row.children[3].textContent = source.source ?? '';
      }
    });
  }

  // --- B1S devices ------------------------------------------------------------

  function b1sCard(id) {
    if (!b1sCards.has(id)) {
      b1sCards.set(id, { parameters: null, version: 0, logOpen: false, logSince: 0, logText: '', logRemaining: 0,
        levels: new Set(Object.keys(levels)) });
    }
    return b1sCards.get(id);
  }

  async function loadParameters(id) {
    const card = b1sCard(id);
    if (card.loading) return;
    card.loading = true;
    try {
      card.parameters = (await api(`b1s/devices/${id}/parameters`)).parameters;
      card.version += 1;
    } finally { card.loading = false; }
  }

  function channelRow(device, channel, card) {
    const parameter = name => card.parameters?.find(entry => entry.name === name && entry.channel === channel.channel);
    const label = Object.assign(document.createElement('input'), { value: parameter('channelLabel')?.value ?? channel.label });
    const location = Object.assign(document.createElement('input'), {
      value: parameter('channelLocation')?.value ?? channel.location, placeholder: 'unassigned' });
    const encoding = document.createElement('select');
    for (const name of encodings) encoding.append(Object.assign(text('option', name), { value: name }));
    encoding.value = encodings[parameter('channelEncoding')?.value] ?? 'automatic';
    encoding.disabled = !card.parameters;
    const value = channel.encoding === 'standard' ? 'standard PGN' : display(channel.value, channel.unit, true).text;
    const actions = text('div', '', 'row-actions');
    actions.append(
      button('Save', async () => {
        const writes = [['channelLabel', label.value], ['channelLocation', location.value]];
        if (card.parameters) writes.push(['channelEncoding', encoding.value]);
        for (const [name, entry] of writes) {
          const result = await api(`b1s/devices/${device.id}/parameters/${name}`, 'PUT', { channel: channel.channel, value: entry });
          if (result.status !== 'done') throw new Error(`${name}: ${result.status}`);
        }
        notice(`Channel ${channel.channel} saved`);
        await loadParameters(device.id);
      }, 'Store label, location, and encoding on the device'),
      button('Calibrate…', async () => {
        const unit = channel.unit === 'K' ? '°C' : channel.unit === 'ratio' ? '%' : channel.unit;
        const input = prompt(`Reference value for ${channel.label || `channel ${channel.channel}`} in ${unit}:`);
        if (input === null) return;
        const result = await api(`b1s/devices/${device.id}/commands/calibrate`, 'POST',
          { channel: channel.channel, si: fromDisplay(input, channel.unit, true) });
        notice(`Calibrate channel ${channel.channel}: ${result.result}`, result.result !== 'done');
        await loadParameters(device.id);
      }, 'Offset calibration against a reference at the current reading'),
      button('Reset', async () => {
        if (!confirm(`Reset calibration and alarm thresholds of channel ${channel.channel}?`)) return;
        const result = await api(`b1s/devices/${device.id}/commands/resetChannel`, 'POST', { channel: channel.channel });
        notice(`Reset channel ${channel.channel}: ${result.result}`, result.result !== 'done');
        await loadParameters(device.id);
      }, 'Calibration and alarm thresholds back to the defaults'));
    const row = cells(String(channel.channel), channel.quantity ?? String(channel.kind), value, label, location, encoding,
      channel.path ?? `standard PGN, instance ${channel.standardInstance}`, actions);
    row.children[2].className = 'value';
    row.children[6].className = 'path';
    return row;
  }

  function parameterRows(device, card) {
    return (card.parameters ?? []).filter(parameter =>
      !['channelLabel', 'channelLocation', 'channelEncoding'].includes(parameter.name)).map(parameter => {
      const shown = parameter.si !== null ? display(parameter.si, parameter.siUnit, parameter.absolute)
        : parameter.values ? { text: parameter.values[parameter.value] ?? String(parameter.value), value: parameter.values[parameter.value] }
          : { text: parameter.status === 'done' ? String(parameter.value) : parameter.status, value: String(parameter.value ?? '') };
      let valueCell = shown.text;
      let action = '';
      if (parameter.access === 'rw' && parameter.status === 'done') {
        const input = Object.assign(document.createElement('input'), { value: shown.value });
        valueCell = text('span', '', 'parameter-input');
        valueCell.append(input, text('span', ` ${shown.unit ?? (parameter.si === null ? parameter.unit ?? '' : '')}`));
        action = button('Set', async () => {
          const body = { channel: parameter.channel };
          if (parameter.si !== null) body.si = fromDisplay(input.value, parameter.siUnit, parameter.absolute);
          else body.value = parameter.type === 'string' || parameter.values ? input.value : Number(input.value);
          const result = await api(`b1s/devices/${device.id}/parameters/${parameter.name}`, 'PUT', body);
          notice(`${parameter.name}: ${result.status}`, result.status !== 'done');
          await loadParameters(device.id);
        });
      }
      const row = cells(parameter.name, parameter.channel === null ? '' : String(parameter.channel), valueCell, action);
      row.title = parameter.description;
      return row;
    });
  }

  // Log panel: the device streams its log only when asked, for a fixed time,
  // rate limited, and stops by itself (src/lib/b1s/CONFIGURATION.md).
  const levels = { D: 'debug', I: 'info', W: 'warning', E: 'error' };

  function filteredLog(card) {
    return card.logText.split('\n').filter(line => {
      const level = /^\[\s*[\d.]+\] ([DIWE]) /.exec(line)?.[1];
      return !level || card.levels.has(level);
    }).join('\n');
  }

  function showLog(card) {
    if (!card.logOutput) return;
    const atBottom = card.logOutput.scrollTop + card.logOutput.clientHeight >= card.logOutput.scrollHeight - 4;
    card.logOutput.textContent = filteredLog(card);
    if (atBottom) card.logOutput.scrollTop = card.logOutput.scrollHeight;
    const remaining = card.logRemaining ?? 0;
    card.logState.textContent = remaining > 0
      ? `streaming, ${Math.floor(remaining / 60)}:${String(remaining % 60).padStart(2, '0')} left (at most 512 B/s; the device stops by itself)`
      : 'off';
    card.logState.className = remaining > 0 ? 'log-state streaming' : 'log-state';
  }

  function logPanel(device, card) {
    const panel = document.createElement('details');
    panel.className = 'log';
    panel.open = card.logOpen;
    panel.append(text('summary', 'Log'));
    const controls = text('div', '', 'log-controls');
    card.logState = text('span', '', 'log-state');
    const stream = minutes => button(minutes ? `${minutes} min` : 'Stop', async () => {
      const result = await api(`b1s/devices/${device.id}/log`, 'POST', { minutes });
      card.logRemaining = result.remaining;
      showLog(card);
    }, minutes ? `Stream the device log for ${minutes} minutes` : 'Stop the log stream now');
    controls.append(text('span', 'Stream:', 'log-label'), stream(5), stream(15), stream(60), stream(0), card.logState);
    const filter = text('div', '', 'log-filter');
    for (const [letter, name] of Object.entries(levels)) {
      const label = text('label', '');
      const box = Object.assign(document.createElement('input'), { type: 'checkbox', checked: card.levels.has(letter) });
      box.onchange = () => { if (box.checked) card.levels.add(letter); else card.levels.delete(letter); showLog(card); };
      label.append(box, ` ${name}`);
      filter.append(label);
    }
    filter.append(button('Clear', async () => { card.logText = ''; showLog(card); }, 'Clear the shown log'));
    card.logOutput = text('pre', '');
    const input = Object.assign(document.createElement('input'), { placeholder: 'Console: help, sensors, health, supply … (Enter; streams its output for 30 s)' });
    input.setAttribute('aria-label', 'Console command');
    input.onkeydown = event => {
      if (event.key !== 'Enter' || !input.value.trim()) return;
      void api(`b1s/devices/${device.id}/console`, 'POST', { line: input.value }).catch(error => notice(error.message, true));
      input.value = '';
    };
    panel.ontoggle = () => { card.logOpen = panel.open; };
    panel.append(controls, filter, card.logOutput, input);
    showLog(card);
    return panel;
  }

  function renderB1s(device) {
    const info = device.detail;
    const card = b1sCard(device.id);
    if (!card.parameters && !card.loading) void loadParameters(device.id).then(render).catch(error => notice(error.message, true));
    const header = text('div', '', 'device-header');
    const title = text('div');
    title.append(text('h2', device.label), text('span', device.status, `badge ${device.online ? '' : 'offline'}`));
    const controls = text('div', '', 'install-controls');
    controls.append(
      button('Identify', async () => notice(`Identify: ${(await api(`b1s/devices/${device.id}/commands/identify`, 'POST', { argument: 5 })).result}`),
        'Blink the device for 5 s'),
      button('Read parameters', async () => { await loadParameters(device.id); render(); notice('Parameters read and backed up'); }),
      button('Restore…', async () => {
        const backups = await api('b1s/backups');
        const own = backups.find(backup => backup.deviceId === Number(device.id));
        const choices = backups.map(backup => `${backup.deviceId} (${backup.product?.model ?? '?'}, ${backup.savedAt})`).join('\n');
        const from = prompt(`Restore the configuration of which device ID?\n${choices}`, own ? device.id : '');
        if (!from) return;
        const result = await api(`b1s/devices/${device.id}/restore`, 'POST', { from: Number(from) });
        notice(`Restored ${result.restored} of ${result.results.length} parameter(s)`);
        await loadParameters(device.id);
      }, 'Write a backup onto this device: its own, or a replaced device\'s'),
      button('Restart', async () => {
        if (!confirm(`Restart ${device.label}?`)) return;
        notice(`Restart: ${(await api(`b1s/devices/${device.id}/commands/restart`, 'POST', {})).result}`);
      }));
    header.append(title, controls);
    const product = info.product ?? {};
    const meta = text('p', [`B1S · ${device.address}`, `device ID ${device.id}`, product.software, product.hardware,
      product.code !== undefined && product.code !== null ? `product ${product.code}` : null].filter(Boolean).join(' · '), 'device-meta');
    const channels = table(['Ch', 'Quantity', 'Value', 'Label', 'Location', 'Encoding', 'Signal K path', ''],
      (info.channels ?? []).map(channel => channelRow(device, channel, card)), 'channels');
    const parameters = document.createElement('details');
    parameters.append(text('summary', card.parameters ? 'Parameters' : 'Parameters (reading …)'));
    if (card.parameters) parameters.append(table(['Parameter', 'Channel', 'Value', ''], parameterRows(device, card)));
    detail.replaceChildren(header, meta, text('h3', 'Channels'), channels, parameters, logPanel(device, card));
  }

  function updateB1s(device) {
    const body = detail.querySelector('tbody.channels');
    (device.detail?.channels ?? []).forEach((channel, index) => {
      const row = body?.children[index];
      if (row && channel.encoding !== 'standard') row.children[2].textContent = display(channel.value, channel.unit, true).text;
    });
  }

  // The log keeps coming in while the panel is closed; it is polled for the
  // selected device.
  async function pollLog(device) {
    const card = b1sCard(device.id);
    const output = await api(`b1s/devices/${device.id}/console?since=${card.logSince}`);
    card.logSince = output.next;
    card.logRemaining = output.remaining;
    if (output.text) card.logText = (card.logText + output.text).slice(-60000);
    showLog(card);
  }

  // --- detail -----------------------------------------------------------------

  function render() {
    renderList();
    const device = devices.find(entry => keyOf(entry) === selected);
    if (!device) {
      rendered.key = null;
      detail.replaceChildren(text('p', devices.length ? 'Select a device' : '', 'empty'));
      return;
    }
    // Rebuild when the structure changed; otherwise only live values, so
    // inputs keep what is being typed.
    const structure = device.kind === 'b1s'
      ? [device.detail.address, device.detail.product, device.detail.channels.map(channel =>
        [channel.channel, channel.label, channel.location, channel.encoding]), b1sCard(device.id).version]
      : [device.status, (device.detail?.subscriptions ?? []).map(source => source.sourceIndex), catalogNames,
        device.detail?.lastTransfer, device.detail?.error];
    const key = JSON.stringify([keyOf(device), structure]);
    if (key !== rendered.key) {
      rendered.key = key;
      if (device.kind === 'b1s') renderB1s(device);
      else renderDisplay(device);
    } else if (device.kind === 'b1s') updateB1s(device);
    else updateDisplay(device);
  }

  async function poll() {
    if (view.hidden) return;
    try {
      devices = (await api('devices')).devices;
      if (!selected && devices.length) selected = keyOf(devices[0]);
      render();
      const device = devices.find(entry => keyOf(entry) === selected);
      if (device?.kind === 'b1s') await pollLog(device);
    } catch (error) {
      document.getElementById('connection').textContent = 'Unavailable';
      notice(error.message, true);
    }
  }

  async function refreshNames() {
    try { catalogNames = (await api('configurations')).names; } catch { /* the designer reports it */ }
  }

  void refreshNames();
  setInterval(refreshNames, 5000);
  setInterval(poll, 1000);
  void poll();
})();
