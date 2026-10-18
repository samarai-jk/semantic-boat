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
        levels: new Set(Object.keys(levels)), openChannels: new Set(), channelTab: new Map() });
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

  // --- B1S device: settings grouped by device and channel --------------------

  const severities = ['Warning', 'Alarm', 'Emergency'];
  const encodingLabels = { automatic: 'automatic', standard: 'standard PGN', b1s: 'B1S message', off: 'off (not sent)' };

  function age(ms) {
    if (ms === null || ms === undefined) return '';
    const seconds = Math.round(ms / 1000);
    if (seconds < 60) return `${seconds} s ago`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`;
    return `${Math.floor(seconds / 3600)} h ago`;
  }

  // What the channel header shows: value (or off), how it is sent, age, path.
  function channelState(channel) {
    const via = channel.encoding === 'standard' ? `standard PGN ${channel.standardPgn ?? ''}, instance ${channel.standardInstance}`
      : channel.encoding === 'off' ? 'off: not sent' : 'B1S message';
    const value = channel.encoding === 'off' ? 'off' : display(channel.value, channel.unit, true).text;
    const stale = channel.ageMs !== null && channel.period && channel.ageMs > Math.max(3 * channel.period * 1000, 10000);
    const path = channel.path ?? (channel.encoding === 'standard' ? 'path appears with the first value Signal K maps' : '');
    return { via, value, age: channel.encoding === 'off' ? '' : age(channel.ageMs), stale, path };
  }
  const ruleTexts = ['when above', 'when below', 'when rising faster than', 'when the sensor fails',
    'when falling by more than', 'when rising by more than'];

  function parameter(card, name, channel = null) {
    return card.parameters?.find(entry => entry.name === name && entry.channel === channel);
  }

  // An input bound to one parameter. change() returns the request body if the
  // value was edited, otherwise null; groups save all their changes at once.
  function editor(entry, options = {}) {
    if (!entry || entry.status !== 'done') return null;
    let element;
    let original;
    let read;
    let unit = '';
    if (entry.values) {
      element = document.createElement('select');
      for (const name of entry.values) element.append(Object.assign(text('option', options.labels?.[name] ?? name), { value: name }));
      original = entry.values[entry.value] ?? '';
      read = () => element.value === original ? null : { value: element.value };
    } else if (entry.type === 'string') {
      original = entry.value ?? '';
      read = () => element.value === original ? null : { value: element.value };
    } else if (options.factor) {
      original = (entry.value / options.factor).toFixed(6);
      read = () => element.value === original ? null : { value: Math.round(Number(element.value) * options.factor) };
    } else if (entry.si !== null && entry.si !== undefined) {
      const shown = display(entry.si, entry.siUnit, entry.absolute);
      original = shown.value;
      unit = shown.unit;
      read = () => element.value === original ? null : { si: fromDisplay(element.value, entry.siUnit, entry.absolute) };
    } else {
      original = String(entry.value);
      read = () => element.value === original ? null : { value: Number(element.value) };
    }
    if (!element) element = document.createElement('input');
    element.value = original;
    if (options.placeholder) element.placeholder = options.placeholder;
    element.disabled = entry.access !== 'rw';
    element.title = entry.description ?? '';
    element.setAttribute('aria-label', options.label ?? entry.name);
    return { element, unit: options.unit ?? unit, change: () => (entry.access === 'rw' ? read() : null), entry };
  }

  function fieldRow(label, edit, hint = '') {
    const row = text('div', '', 'field');
    row.append(text('span', label, 'field-label'));
    const value = text('span', '', 'field-value');
    if (edit) value.append(edit.element, text('span', edit.unit ?? '', 'field-unit'));
    else value.append(text('span', '–', 'muted'));
    row.append(value);
    if (hint) row.append(text('small', hint, 'field-hint'));
    return row;
  }

  // Stores the edited fields of one block; the button says what it saves.
  function saveButton(device, label, edits) {
    const writable = edits.filter(edit => edit && edit.entry.access === 'rw');
    if (!writable.length) return null;
    const element = button(label, async () => {
      const changes = writable.map(edit => [edit, edit.change()]).filter(([, body]) => body);
      if (!changes.length) { notice('Nothing changed'); return; }
      for (const [edit, body] of changes) {
        const result = edit.save ? await edit.save(body)
          : await api(`b1s/devices/${device.id}/parameters/${edit.entry.name}`, 'PUT', { channel: edit.entry.channel, ...body });
        if (result.status !== 'done') throw new Error(`${edit.entry.name}: ${result.status}`);
      }
      notice(`${changes.length} setting(s) stored on the device`);
      await loadParameters(device.id);
      render();
    }, 'Store the edited fields above on the device');
    element.classList?.add('primary');
    return element;
  }

  // A block of fields with its own actions underneath.
  function block(title, explanation, rows, actions) {
    const section = text('section', '', 'block');
    if (title) section.append(text('h4', title));
    if (explanation) section.append(text('p', explanation, 'block-text'));
    section.append(...rows);
    const bar = text('div', '', 'block-actions');
    bar.append(...actions.filter(Boolean));
    if (bar.children.length) section.append(bar);
    return section;
  }

  function devicePanel(device, card) {
    const role = editor(parameter(card, 'deviceRole'), { placeholder: 'e.g. fridgeSensor (unassigned)' });
    const address = editor(parameter(card, 'preferredAddress'));
    // The Signal K path prefix is a Pi-side setting of the device (not stored on it).
    const pluginPrefix = device.detail.pluginPrefix ?? '';
    const prefixInput = Object.assign(document.createElement('input'), { value: device.detail.pathPrefix ?? '', placeholder: 'none' });
    prefixInput.setAttribute('aria-label', 'Signal K path prefix of the device');
    const prefix = { element: prefixInput, unit: '', entry: { access: 'rw', name: 'pathPrefix' },
      change: () => prefixInput.value.trim() === (device.detail.pathPrefix ?? '') ? null : { pathPrefix: prefixInput.value.trim() },
      save: body => api(`b1s/devices/${device.id}/settings`, 'PUT', body).then(() => ({ status: 'done' })) };
    const example = [pluginPrefix, prefixInput.value.trim() || device.detail.pathPrefix, 'environment.inside.temperature'].filter(Boolean).join('.');
    const uptime = parameter(card, 'uptime');
    const seconds = uptime?.value ?? 0;
    const uptimeRow = text('div', '', 'field');
    uptimeRow.append(text('span', 'Uptime', 'field-label'), text('span',
      uptime ? `${Math.floor(seconds / 3600)} h ${Math.floor(seconds / 60) % 60} min (when read)` : '–', 'field-value'));
    const panel = text('section', '', 'device-settings');
    panel.append(text('h3', 'Device'), block('', '', [
      fieldRow('Role', role, 'What this device is for on the boat'),
      fieldRow('Preferred address', address, 'Address claimed first; takes effect after a restart'),
      fieldRow('Signal K path prefix', prefix, `Namespace for this device's B1S paths, below the plugin's${pluginPrefix ? ` (${pluginPrefix})` : ' (none set)'}: ` +
        `e.g. ${example}. Not for standard PGN channels: Signal K maps those itself. Stored on the Pi, part of the backup.`),
      uptimeRow],
    [saveButton(device, 'Save device settings', [role, address, prefix])]));
    return panel;
  }

  function setupPane(device, channel, card) {
    const label = editor(parameter(card, 'channelLabel', channel.channel), { placeholder: 'name for people' });
    const location = editor(parameter(card, 'channelLocation', channel.channel), { placeholder: 'unassigned' });
    const rows = [fieldRow('Label', label, 'Name shown for this channel'),
      fieldRow('Location', location, 'Signal K path fragment, e.g. inside.refrigerator; empty = unassigned')];
    const edits = [label, location];
    if (!channel.noStandard) {
      const encoding = editor(parameter(card, 'channelEncoding', channel.channel), { labels: encodingLabels });
      const instance = editor(parameter(card, 'channelStandardInstance', channel.channel));
      rows.push(fieldRow('Sent as', encoding, `automatic: standard PGN ${channel.standardPgn ?? ''} where the registry lists it for display, otherwise B1S message`),
        fieldRow('Standard PGN instance', instance, 'Instance in the standard PGN; 255 = firmware default'));
      edits.push(encoding, instance);
    } else {
      rows.push(fieldRow('Sent as', null, 'B1S message (no standard PGN for this quantity)'));
    }
    return [block('', '', rows, [saveButton(device, 'Save setup', edits)])];
  }

  function calibrationPane(device, channel, card) {
    const offset = editor(parameter(card, 'calibrationOffset', channel.channel));
    const gain = editor(parameter(card, 'calibrationGain', channel.channel), { factor: 1e6, unit: '×' });
    if (!offset && !gain) return [text('p', 'This channel has no calibration.', 'empty')];
    const unit = channel.unit === 'K' ? '°C' : channel.unit === 'ratio' ? '%' : channel.unit;
    const reference = Object.assign(document.createElement('input'), { placeholder: `e.g. ${unit === '°C' ? '21.5' : '50'}` });
    reference.setAttribute('aria-label', 'Reference value');
    const referenceRow = text('div', '', 'field');
    const referenceValue = text('span', '', 'field-value');
    referenceValue.append(reference, text('span', unit, 'field-unit'));
    referenceRow.append(text('span', 'Reference now', 'field-label'), referenceValue);
    const apply = (command, label, title) => button(label, async () => {
      if (!reference.value.trim()) throw new Error('Enter the reference value first');
      const result = await api(`b1s/devices/${device.id}/commands/${command}`, 'POST',
        { channel: channel.channel, si: fromDisplay(reference.value, channel.unit, true) });
      notice(`${label}: ${result.result}`, result.result !== 'done');
      await loadParameters(device.id);
      render();
    }, title);
    const reset = button('Reset channel to defaults', async () => {
      if (!confirm(`Reset calibration AND alarm thresholds of ${channel.label || `channel ${channel.channel}`} to the defaults?`)) return;
      const result = await api(`b1s/devices/${device.id}/commands/resetChannel`, 'POST', { channel: channel.channel });
      notice(`Reset: ${result.result}`, result.result !== 'done');
      await loadParameters(device.id);
      render();
    }, 'Calibration and alarm thresholds back to the firmware defaults');
    reset.classList?.add('danger');
    return [
      block('Calibrate against a reference',
        'Measure the real value with a reference instrument and enter it. One point corrects the offset; ' +
        'a second point, at least one unit away, also corrects the gain.',
        [referenceRow],
        [apply('calibrate', 'Calibrate (offset)', 'The current reading becomes the reference value'),
          apply('calibrateSecondPoint', 'Add second point (gain)', 'Gain and offset from the first and this point')]),
      block('Calibration values', 'value = raw × gain + offset', [fieldRow('Offset', offset), fieldRow('Gain', gain)],
        [saveButton(device, 'Save calibration values', [offset, gain])]),
      block('', '', [], [reset])
    ];
  }

  function alarmsPane(device, channel, card) {
    const rows = [];
    const edits = [];
    for (let rule = 0; rule < 4; rule += 1) {
      const definition = parameter(card, `alarmRule${rule}`, channel.channel);
      if (!definition || definition.status !== 'done') continue;
      const value = definition.value;
      const kind = value & 0xff;
      const severity = severities[(value >> 8) & 0xff] ?? 'Alarm';
      const state = value & (1 << 17) ? (value & (1 << 18) ? 'active, acknowledged' : 'ACTIVE') : 'ok';
      const row = text('div', '', 'alarm');
      row.append(text('span', severity, `severity ${severity.toLowerCase()}`), text('span', ruleTexts[kind] ?? `rule ${kind}`, 'alarm-rule'));
      const threshold = text('span', '', 'field-value');
      if (kind !== 3) {
        const edit = editor(parameter(card, `alarmThreshold${rule}`, channel.channel), { label: `${severity} threshold` });
        if (edit) {
          const perMinute = kind === 2 ? '/min' : kind >= 4 ? ' over the trend window' : '';
          threshold.append(edit.element, text('span', `${edit.unit}${perMinute}`, 'field-unit'));
          edits.push(edit);
        }
      }
      row.append(threshold, text('span', `${state}${value & (1 << 16) ? ' · latching' : ''}`,
        `alarm-state${state === 'ACTIVE' ? ' active' : ''}`));
      rows.push(row);
    }
    if (!rows.length) return [text('p', 'This channel has no alarm rules.', 'empty')];
    return [block('', 'Thresholds are stored per sensor; Reset on the Calibration tab restores the defaults.', rows,
      [saveButton(device, 'Save thresholds', edits)])];
  }

  const channelTabs = [['setup', 'Setup', setupPane], ['calibration', 'Calibration', calibrationPane], ['alarms', 'Alarms', alarmsPane]];

  function channelPanel(device, channel, card) {
    const panel = document.createElement('details');
    panel.className = 'channel';
    panel.open = card.openChannels.has(channel.channel);
    panel.ontoggle = () => { if (panel.open) card.openChannels.add(channel.channel); else card.openChannels.delete(channel.channel); };
    const summary = text('summary', '');
    const state = channelState(channel);
    const live = text('span', state.value, `channel-value${channel.encoding === 'off' ? ' off' : ''}`);
    live.dataset.channel = String(channel.channel);
    const when = text('span', state.age, `channel-age${state.stale ? ' stale' : ''}`);
    when.dataset.age = String(channel.channel);
    summary.append(text('span', `${channel.channel}`, 'channel-number'), text('span', channel.label || `Channel ${channel.channel}`, 'channel-name'),
      text('span', channel.quantity ?? '', 'muted'), text('span', state.via, `channel-via${channel.encoding === 'off' ? ' off' : ''}`),
      live, when, text('span', state.path, `channel-path${channel.encoding === 'off' ? ' off' : ''}`));
    panel.append(summary);
    if (!card.parameters) {
      panel.append(text('p', 'Reading the device’s settings …', 'empty'));
      return panel;
    }
    const current = card.channelTab.get(channel.channel) ?? 'setup';
    const tabs = text('div', '', 'channel-tabs');
    tabs.setAttribute('role', 'tablist');
    for (const [id, label] of channelTabs) {
      const tab = text('button', label, `channel-tab${id === current ? ' selected' : ''}`);
      tab.setAttribute('role', 'tab');
      tab.setAttribute('aria-selected', String(id === current));
      tab.onclick = () => { card.channelTab.set(channel.channel, id); card.version += 1; render(); };
      tabs.append(tab);
    }
    const pane = text('div', '', 'channel-pane');
    pane.append(...channelTabs.find(([id]) => id === current)[2](device, channel, card));
    panel.append(tabs, pane);
    return panel;
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
      button('Read settings', async () => { await loadParameters(device.id); render(); notice('Settings read from the device and backed up'); },
        'Read all settings from the device again (also stores the backup)'),
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
    const channels = text('div', '', 'channels');
    channels.append(...(info.channels ?? []).map(channel => channelPanel(device, channel, card)));
    detail.replaceChildren(header, meta, card.parameters ? devicePanel(device, card) : text('p', 'Reading the device’s settings …', 'empty'),
      text('h3', 'Channels'), channels, logPanel(device, card));
  }

  function updateB1s(device) {
    for (const channel of device.detail?.channels ?? []) {
      const state = channelState(channel);
      const live = detail.querySelector(`.channel-value[data-channel="${channel.channel}"]`);
      if (live) live.textContent = state.value;
      const when = detail.querySelector(`.channel-age[data-age="${channel.channel}"]`);
      if (when) {
        when.textContent = state.age;
        when.className = `channel-age${state.stale ? ' stale' : ''}`;
      }
    }
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
      ? [device.detail.address, device.detail.product, device.detail.pathPrefix, device.detail.channels.map(channel =>
        [channel.channel, channel.label, channel.location, channel.encoding, channel.path]), b1sCard(device.id).version]
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
