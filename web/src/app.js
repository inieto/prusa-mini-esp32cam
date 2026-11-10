// PrusaCam byClaude web UI. No frameworks; device data is only ever rendered via textContent.

const API = 'api/v1';

// ---------- tiny DOM helpers ----------
const $ = (sel) => document.querySelector(sel);

function h(tag, props = {}, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k in el) el[k] = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) if (c != null) el.append(c instanceof Node ? c : String(c));
  return el;
}

function toast(message, isError = false) {
  const t = h('div', { class: `toast${isError ? ' error' : ''}`, role: 'status', text: message });
  document.body.append(t);
  setTimeout(() => t.remove(), 3500);
}

// ---------- API ----------
class Unauthorized extends Error {}

async function api(path, { method = 'GET', body } = {}) {
  const res = await fetch(`${API}/${path}`, {
    method,
    credentials: 'same-origin',
    headers: body !== undefined ? { 'Content-Type': 'application/json' } : {},
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  if (res.status === 401 && path !== 'login' && path !== 'setup') {
    showLogin();
    throw new Unauthorized();
  }
  const type = res.headers.get('Content-Type') || '';
  const data = type.includes('json') ? await res.json() : await res.text();
  if (!res.ok) throw new Error((data && data.error) || `HTTP ${res.status}`);
  return data;
}

// ---------- formatting ----------
function ago(seconds) {
  if (seconds == null) return 'never';
  if (seconds < 60) return `${seconds} s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`;
  return `${Math.floor(seconds / 3600)} h ago`;
}

function duration(seconds) {
  const d = Math.floor(seconds / 86400);
  const hms = new Date(seconds * 1000).toISOString().substring(11, 19);
  return d ? `${d} d ${hms}` : hms;
}

const STATE_CLASS = {
  online: 'ok', starting: 'warn', 'not configured': 'warn',
  offline: 'bad', 'server error': 'warn', 'request rejected': 'bad', 'token rejected': 'bad',
};

// ---------- declarative settings forms ----------
const RESOLUTIONS = [
  ['qvga', '320×240'], ['cif', '352×288'], ['vga', '640×480'], ['svga', '800×600'],
  ['xga', '1024×768'], ['sxga', '1280×1024'], ['uxga', '1600×1200'], ['qxga', '2048×1536'],
];
// Only the resolutions the detected sensor can deliver (the device reports its largest one).
const resolutionsFor = (v) => {
  const last = RESOLUTIONS.findIndex(([key]) => key === v.max_resolution);
  return last < 0 ? RESOLUTIONS : RESOLUTIONS.slice(0, last + 1);
};
const LEVEL = { type: 'range', min: -2, max: 2, step: 1 };

const FORMS = {
  camera: {
    title: 'Camera',
    endpoint: 'config/camera',
    sections: [
      ['Image', [
        { key: 'sensor', label: 'Sensor', type: 'text', readonly: true },
        { key: 'resolution', label: 'Resolution', type: 'select', options: resolutionsFor },
        { key: 'jpeg_quality', label: 'JPEG quality', type: 'range', min: 10, max: 63, step: 1,
          invert: true, hint: 'Further right = better quality and a larger file' },
        { key: 'live_jpeg_quality', label: 'Live view quality', type: 'range', min: 10, max: 63, step: 1,
          invert: true, hint: 'Lower than the snapshot quality keeps the live view smooth on WiFi; snapshots are not affected' },
        { key: 'rotation', label: 'Rotation', type: 'select',
          options: [[0, '0°'], [90, '90°'], [180, '180°'], [270, '270°']] },
        { key: 'hmirror', label: 'Horizontal mirror', type: 'switch' },
        { key: 'vflip', label: 'Vertical flip', type: 'switch' },
      ]],
      ['Framing', [
        { key: 'zoom_pct', label: 'Zoom', type: 'range', min: 100, max: (v) => v.max_zoom_pct || 100, step: 5,
          live: true, format: (v) => `${(v / 100).toFixed(2)}×`,
          hint: 'Done by the sensor: zoomed frames keep full detail. The maximum depends on the resolution. Open the live view to frame while you adjust.' },
        { key: 'pan_x', label: 'Horizontal position', type: 'range', min: -100, max: 100, step: 5, live: true,
          showIf: (v) => v.zoom_pct > 100, format: (v) => (v === 0 ? 'center' : v < 0 ? `${-v} % left` : `${v} % right`) },
        { key: 'pan_y', label: 'Vertical position', type: 'range', min: -100, max: 100, step: 5, live: true,
          showIf: (v) => v.zoom_pct > 100, format: (v) => (v === 0 ? 'center' : v < 0 ? `${-v} % up` : `${v} % down`) },
      ]],
      ['Adjustments', [
        { key: 'brightness', label: 'Brightness', ...LEVEL },
        { key: 'contrast', label: 'Contrast', ...LEVEL },
        { key: 'saturation', label: 'Saturation', ...LEVEL },
        { key: 'sharpness', label: 'Sharpness', type: 'range', min: -3, max: 3, step: 1,
          showIf: (v) => v.supports_sharpness },
        { key: 'denoise', label: 'Noise reduction', type: 'range', min: 0, max: 8, step: 1,
          showIf: (v) => v.supports_sharpness, format: (v) => (v === 0 ? 'off' : String(v)),
          hint: 'Smooths grain in low light at the cost of fine detail' },
      ]],
      ['Exposure and gain', [
        { key: 'auto_exposure', label: 'Auto exposure', type: 'switch' },
        { key: 'aec_dsp', label: 'Night exposure (AEC2)', type: 'switch',
          hint: 'Improves low-light scenes' },
        { key: 'ae_level', label: 'Exposure level', ...LEVEL },
        { key: 'manual_exposure', label: 'Manual exposure', type: 'range', min: 0, max: 1200, step: 10,
          showIf: (v) => !v.auto_exposure },
        { key: 'auto_gain', label: 'Auto gain', type: 'switch' },
        { key: 'gain_ceiling', label: 'Gain ceiling', type: 'select',
          options: [[0, '2×'], [1, '4×'], [2, '8×'], [3, '16×'], [4, '32×'], [5, '64×'], [6, '128×']],
          hint: 'Higher = brighter image in low light, but noisier' },
        { key: 'manual_gain', label: 'Manual gain', type: 'range', min: 0, max: 30, step: 1,
          showIf: (v) => !v.auto_gain },
        { key: 'auto_white_balance', label: 'Auto white balance', type: 'switch' },
        { key: 'wb_mode', label: 'White balance preset', type: 'select',
          options: [[0, 'Auto'], [1, 'Sunny'], [2, 'Cloudy'], [3, 'Office'], [4, 'Home']],
          showIf: (v) => v.auto_white_balance,
          hint: 'A fixed preset keeps colours stable under the printer\'s LED lighting' },
        { key: 'lens_correction', label: 'Lens correction', type: 'switch' },
      ]],
      ['Light and flash', [
        { key: 'flash_duty_pct', label: 'Light intensity', type: 'range', min: 1, max: 100, step: 1,
          unit: '%', live: true,
          hint: 'For the Light button and the flash on capture. Applies at once (turn the light on to see it).' },
        { key: 'flash_on_capture', label: 'Flash on capture', type: 'switch' },
        { key: 'flash_lead_ms', label: 'Lead time', type: 'range', min: 0, max: 2000, step: 50,
          unit: 'ms', showIf: (v) => v.flash_on_capture },
      ]],
    ],
  },
  connect: {
    title: 'PrusaConnect',
    endpoint: 'config/connect',
    sections: [
      ['Connection', [
        { key: 'token', label: 'Camera token', type: 'secret', setKey: 'token_set',
          hint: 'In PrusaConnect: Cameras → Add new other camera → copy the token' },
        { key: 'fingerprint', label: 'Fingerprint', type: 'text', readonly: true },
        { key: 'hostname', label: 'Server', type: 'text',
          hint: 'The snapshot interval is chosen in PrusaConnect, in the camera settings' },
      ]],
    ],
  },
  network: {
    title: 'Network',
    endpoint: 'config/network',
    sections: [
      ['WiFi', [
        { key: 'ssid', label: 'Network (SSID)', type: 'text' },
        { key: 'password', label: 'Password', type: 'secret', setKey: 'password_set' },
        { key: 'hostname', label: 'Network name (mDNS)', type: 'text', hint: 'Reachable as http://<name>.local' },
        { key: 'tx_power_dbm', label: 'Transmit power', type: 'select',
          options: [[20, '20 dBm (maximum)'], [17, '17 dBm'], [15, '15 dBm'], [13, '13 dBm'], [11, '11 dBm'], [8, '8 dBm']],
          hint: 'Lower draws less current in bursts: try 13 dBm if the signal is good (above -65 dBm) and the camera drops off WiFi under load. Applies at once.' },
      ]],
    ],
    extra: () => wifiScanner(),
  },
  printer: {
    title: 'Printer',
    endpoint: 'config/printer',
    sections: [
      ['PrusaLink', [
        { key: 'host', label: 'Printer address', type: 'text',
          hint: 'IP or name of the printer on your network, e.g. 192.168.0.50. Empty disables PrusaLink.' },
        { key: 'user', label: 'User', type: 'text', hint: 'Usually "maker"' },
        { key: 'password', label: 'Password', type: 'secret', setKey: 'password_set',
          hint: 'On the printer: Settings → Network → PrusaLink' },
        { key: 'upload_only_while_printing', label: 'Upload to PrusaConnect only while printing', type: 'switch',
          hint: 'Keeps capturing for this page and the timelapse; uploads resume 5 min after a print ends. If the printer is unreachable, uploads continue.' },
      ]],
    ],
  },
  mqtt: {
    title: 'MQTT',
    endpoint: 'config/mqtt',
    sections: [
      ['MQTT / Home Assistant', [
        { key: 'connected', label: 'Status', type: 'text', readonly: true, format: (v) => (v ? 'connected' : 'not connected') },
        { key: 'uri', label: 'Broker', type: 'text',
          hint: 'mqtt://192.168.0.10:1883 (plain MQTT on your network). Empty disables it. Home Assistant finds the camera by itself (MQTT discovery).' },
        { key: 'user', label: 'User', type: 'text' },
        { key: 'password', label: 'Password', type: 'secret', setKey: 'password_set' },
        { key: 'publish_images', label: 'Publish snapshots', type: 'switch',
          hint: 'Each capture goes to prusacam/<name>/image, shown as a camera entity' },
      ]],
    ],
  },
  storage: {
    title: 'Storage',
    endpoint: 'config/storage',
    sections: [
      ['SD card', [
        { key: 'log_to_sd', label: 'Keep logs on the card', type: 'switch',
          hint: 'One file per day in /logs, the last 14 days are kept' },
        { key: 'timelapse', label: 'Timelapse', type: 'select',
          options: [['off', 'Off'], ['always', 'Always'], ['printing', 'Only while printing (needs PrusaLink)']] },
        { key: 'timelapse_interval_s', label: 'Timelapse interval', type: 'range', min: 10, max: 600, step: 10,
          unit: 's', showIf: (v) => v.timelapse !== 'off',
          hint: 'Frames come from the snapshot schedule, so the effective interval is never shorter than the PrusaConnect one' },
      ]],
    ],
    extra: () => sdBrowser(),
  },
};

function fieldControl(field, values, onChange) {
  const value = values[field.key];
  switch (field.type) {
    case 'switch': {
      const input = h('input', { type: 'checkbox', checked: !!value, id: `f-${field.key}` });
      input.addEventListener('change', () => onChange(input.checked));
      return h('label', { class: 'switch' }, input, h('span'));
    }
    case 'range': {
      const max = typeof field.max === 'function' ? field.max(values) : field.max;
      const toUi = (v) => (field.invert ? max + field.min - v : v);
      const input = h('input', { type: 'range', min: field.min, max, step: field.step,
                                 value: toUi(Math.min(value, max)), id: `f-${field.key}`, disabled: max <= field.min });
      const shown = h('span', { class: 'range-value' });
      const paint = () => {
        const pct = max > field.min ? ((input.value - field.min) / (max - field.min)) * 100 : 0;
        input.style.setProperty('--fill', `${pct}%`);
        // Inverted scales (JPEG quality) read better as a percentage than as raw driver values.
        shown.textContent = field.invert
          ? `${Math.round(pct)} %`
          : field.format ? field.format(Number(input.value))
            : `${input.value}${field.unit ? ` ${field.unit}` : ''}`;
      };
      input.addEventListener('input', paint);
      input.addEventListener('change', () => onChange(toUi(Number(input.value))));
      paint();
      return [input, shown];
    }
    case 'select': {
      const options = typeof field.options === 'function' ? field.options(values) : field.options;
      const select = h('select', { id: `f-${field.key}` },
        options.map(([v, text]) => h('option', { value: v, text, selected: String(v) === String(value) })));
      select.addEventListener('change', () => {
        const raw = select.value;
        onChange(typeof options[0][0] === 'number' ? Number(raw) : raw);
      });
      return select;
    }
    case 'secret': {
      const isSet = !!values[field.setKey];
      const input = h('input', { type: 'password', id: `f-${field.key}`, autocomplete: 'off',
                                 placeholder: isSet ? '•••••••• (saved)' : 'not set' });
      input.addEventListener('change', () => onChange(input.value));
      const eye = h('img', { src: 'icons/eye.svg', alt: 'show', width: 18, style: 'cursor:pointer' });
      eye.addEventListener('click', () => {
        input.type = input.type === 'password' ? 'text' : 'password';
        eye.src = input.type === 'password' ? 'icons/eye.svg' : 'icons/eye-slash.svg';
      });
      return [input, eye];
    }
    default: {
      const shown = field.format ? field.format(value) : value;
      const input = h('input', { type: 'text', value: shown ?? '', readOnly: !!field.readonly,
                                 id: `f-${field.key}` });
      input.addEventListener('change', () => onChange(input.value));
      return input;
    }
  }
}

async function renderForm(name) {
  const spec = FORMS[name];
  const values = await api(spec.endpoint);
  const changes = {};
  const view = $('#view');

  const draw = () => {
    view.replaceChildren();
    const merged = { ...values, ...changes };
    for (const [heading, fields] of spec.sections) {
      view.append(h('h2', { text: heading }));
      const form = h('div', { class: 'form' });
      for (const field of fields) {
        if (field.showIf && !field.showIf(merged)) continue;
        form.append(
          h('label', { class: 'key', for: `f-${field.key}`, text: field.label }),
          h('div', { class: 'value' }, fieldControl(field, merged, async (v) => {
            if (field.live) {
              // Framing applies at once (the camera picks it up before the next frame), so it can
              // be tuned while watching the live view; no Save needed.
              try {
                Object.assign(values, await api(spec.endpoint, { method: 'PUT', body: { [field.key]: v } }));
                if (!liveOn) setTimeout(() => $('#refresh').click(), 300);
              } catch (e) {
                if (!(e instanceof Unauthorized)) toast(`Could not apply: ${e.message}`, true);
              }
              if (fields.some((f) => f.showIf)) draw();
              return;
            }
            changes[field.key] = v;
            if (fields.some((f) => f.showIf)) draw();
            save.disabled = false;
          })),
        );
        if (field.hint) form.append(h('div', { class: 'hint', text: field.hint }));
      }
      view.append(form);
    }
    view.append(h('div', { class: 'form-actions' }, save));
    if (spec.extra) view.append(spec.extra());
  };

  const save = h('button', { class: 'btn medium', type: 'button', text: 'Save', disabled: true });
  save.addEventListener('click', async () => {
    save.disabled = true;
    try {
      const result = await api(spec.endpoint, { method: 'PUT', body: changes });
      Object.assign(values, result);
      for (const k of Object.keys(changes)) delete changes[k];
      toast(result.rebooting
        ? 'Saved. Rebooting to join the network: reconnect this device to your WiFi.'
        : result.reboot_required ? 'Saved. Applies after a reboot.' : 'Saved');
      draw();
      refreshStatus();
    } catch (e) {
      if (!(e instanceof Unauthorized)) toast(`Could not save: ${e.message}`, true);
      save.disabled = false;
    }
  });
  draw();
}

function wifiScanner() {
  const body = h('tbody');
  const button = h('button', { class: 'btn medium', type: 'button', text: 'Scan networks' });
  button.addEventListener('click', async () => {
    button.disabled = true;
    try {
      const networks = await api('wifi/scan');
      body.replaceChildren(...networks.map((n) => h('tr', {},
        h('td', { text: n.ssid || '(hidden)' }),
        h('td', { text: `${n.rssi} dBm` }),
        h('td', { text: n.channel }),
        h('td', { text: n.auth }),
        h('td', {}, h('button', { class: 'btn small', type: 'button', text: 'Use',
          onclick: () => { const i = $('#f-ssid'); i.value = n.ssid; i.dispatchEvent(new Event('change')); } })),
      )));
    } catch (e) {
      if (!(e instanceof Unauthorized)) toast(e.message, true);
    }
    button.disabled = false;
  });
  return h('div', {},
    h('h2', { text: 'Available networks' }),
    h('div', { class: 'form-actions' }, button),
    h('table', { class: 'data' },
      h('thead', {}, h('tr', {}, ['SSID', 'Signal', 'Channel', 'Security', ''].map((t) => h('th', { text: t })))),
      body));
}

function formatBytes(n) {
  if (n >= 1 << 30) return `${(n / (1 << 30)).toFixed(1)} GB`;
  if (n >= 1 << 20) return `${(n / (1 << 20)).toFixed(1)} MB`;
  if (n >= 1 << 10) return `${Math.round(n / 1024)} KB`;
  return `${n} B`;
}

function sdBrowser() {
  const box = h('div', {});
  let path = '';
  const fileUrl = (p) => `${API}/sd/file?path=${encodeURIComponent(p)}`;
  const load = async () => {
    try {
      const sd = await api('sd');
      if (!sd.mounted) {
        box.replaceChildren(h('h2', { text: 'Files' }), h('p', { class: 'lead', text: 'No SD card (FAT32, inserted before power-on or retried every minute).' }));
        return;
      }
      const listing = await api(`sd/list?path=${encodeURIComponent(path)}`);
      const crumbs = [h('a', { href: '#storage', text: 'card', onclick: (e) => { e.preventDefault(); path = ''; load(); } })];
      let acc = '';
      for (const part of path ? path.split('/') : []) {
        acc = acc ? `${acc}/${part}` : part;
        const target = acc;
        crumbs.push(' / ', h('a', { href: '#storage', text: part, onclick: (e) => { e.preventDefault(); path = target; load(); } }));
      }
      const entries = listing.entries.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
      box.replaceChildren(
        h('h2', { text: 'Files' }),
        h('p', { class: 'lead' }, `${formatBytes(sd.free_bytes)} free of ${formatBytes(sd.total_bytes)} · `, ...crumbs),
        h('table', { class: 'data' },
          h('thead', {}, h('tr', {}, ['Name', 'Size', ''].map((t) => h('th', { text: t })))),
          h('tbody', {}, entries.length ? entries.map((e) => {
            const full = path ? `${path}/${e.name}` : e.name;
            const name = e.dir
              ? h('a', { href: '#storage', text: `${e.name}/`, onclick: (ev) => { ev.preventDefault(); path = full; load(); } })
              : h('a', { href: fileUrl(full), target: '_blank', rel: 'noopener', text: e.name });
            return h('tr', {},
              h('td', {}, name),
              h('td', { text: e.dir ? '—' : formatBytes(e.size) }),
              h('td', {}, confirmButton('Delete', 'Confirm', async () => {
                await action('sd/delete', { path: full }, `Deleted ${e.name}`);
                load();
              })));
          }) : h('tr', {}, h('td', { text: 'empty' }), h('td'), h('td')))),
        h('p', { class: 'hint', text: 'Make a video from a timelapse folder: ffmpeg -framerate 24 -pattern_type glob -i "*.jpg" timelapse.mp4' }));
    } catch (e) {
      if (!(e instanceof Unauthorized)) box.replaceChildren(h('p', { text: `Error: ${e.message}` }));
    }
  };
  load();
  return box;
}

// Two-step button for destructive actions (no modal dialogs: they block the page).
function confirmButton(label, armedLabel, onConfirm) {
  const b = h('button', { class: 'btn medium', type: 'button', text: label });
  let timer;
  b.addEventListener('click', async () => {
    if (!b.dataset.armed) {
      b.dataset.armed = '1';
      b.textContent = armedLabel;
      timer = setTimeout(() => { delete b.dataset.armed; b.textContent = label; }, 4000);
      return;
    }
    clearTimeout(timer);
    delete b.dataset.armed;
    b.textContent = label;
    b.disabled = true;
    try { await onConfirm(); } finally { b.disabled = false; }
  });
  return b;
}

// Waits for the camera to come back after a reboot, then reloads the page.
function reloadWhenBack(timeoutMs = 120000) {
  const until = Date.now() + timeoutMs;
  const poll = async () => {
    try {
      await fetch(`${API}/session`, { cache: 'no-store' }).then((r) => { if (!r.ok) throw r; });
      location.reload();
    } catch {
      if (Date.now() < until) setTimeout(poll, 2000);
    }
  };
  setTimeout(poll, 5000);
}

function firmwareUpdate() {
  const file = h('input', { type: 'file', accept: '.bin,application/octet-stream' });
  const progress = h('progress', { max: 100, value: 0, hidden: true });
  const button = h('button', { class: 'btn medium', type: 'button', text: 'Upload and install', disabled: true });
  file.addEventListener('change', () => { button.disabled = !file.files.length; });
  button.addEventListener('click', () => {
    const bin = file.files[0];
    if (!bin) return;
    button.disabled = true;
    progress.hidden = false;
    // XMLHttpRequest instead of fetch: it reports upload progress.
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `${API}/ota`);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (e) => { if (e.lengthComputable) progress.value = (100 * e.loaded) / e.total; };
    xhr.onload = () => {
      let data = {};
      try { data = JSON.parse(xhr.responseText); } catch { /* keep empty */ }
      if (xhr.status === 200) {
        toast(`Installed ${data.version}. Rebooting… the page reloads by itself.`);
        reloadWhenBack();
      } else {
        if (xhr.status === 401) showLogin();
        toast(`Update failed: ${data.error || `HTTP ${xhr.status}`}`, true);
        button.disabled = false;
        progress.hidden = true;
      }
    };
    xhr.onerror = () => { toast('Update failed: connection lost', true); button.disabled = false; };
    xhr.send(bin);
  });
  return [file, progress, button];
}

function coredumpRows(dump) {
  if (!dump) {
    return [h('label', { class: 'key', text: 'Crash dump' }),
            h('div', { class: 'value', text: 'none in flash (with an SD card, dumps are moved to /coredumps)' })];
  }
  return [
    h('label', { class: 'key', text: 'Crash dump' }),
    h('div', { class: 'value strong', text: `${dump.reason || 'crash'} · task ${dump.task || '?'} · pc ${dump.pc} · ${Math.round(dump.size / 1024)} KB` }),
    h('div', { class: 'value', style: 'grid-column:2;gap:.5rem' },
      h('a', { class: 'btn medium', href: `${API}/coredump`, download: '', text: 'Download',
               style: 'display:inline-flex;align-items:center;justify-content:center;text-decoration:none' }),
      confirmButton('Erase dump', 'Click again to erase', async () => {
        await action('coredump/erase', {}, 'Crash dump erased');
        route();
      })),
    h('div', { class: 'hint', text: 'Decode it with: idf.py coredump-info -c coredump.elf (same build as the one that crashed).' }),
  ];
}

// "99 KB free of 290 KB (34 % free) · lowest 70 KB"
function memory(free, total, lowest) {
  const kb = (n) => `${Math.round(n / 1024)} KB`;
  const pct = total ? ` (${Math.round((100 * free) / total)} % free)` : '';
  return `${kb(free)} free of ${total ? kb(total) : '?'}${pct}${lowest !== undefined ? ` · lowest ${kb(lowest)}` : ''}`;
}

function statusLedSwitch(enabled) {
  const input = h('input', { type: 'checkbox', checked: enabled, id: 'f-status-led' });
  input.addEventListener('change', async () => {
    try {
      await api('config/device', { method: 'PUT', body: { status_led: input.checked } });
    } catch (e) {
      if (!(e instanceof Unauthorized)) toast(e.message, true);
      input.checked = !input.checked;
    }
  });
  return h('label', { class: 'switch' }, input, h('span'));
}

function apiTokenRows(isSet) {
  const shown = h('div', { class: 'value', style: 'grid-column:2;flex-direction:column;align-items:stretch;gap:.3rem' });
  const showToken = (token) => {
    const base = `${location.origin}/api/v1`;
    const line = (label, text) => h('div', {}, h('b', { text: `${label}: ` }),
      h('input', { type: 'text', readOnly: true, value: text, style: 'width:100%', onfocus: (e) => e.target.select() }));
    shown.replaceChildren(
      h('div', { text: 'Copy it now: it is not shown again.' }),
      line('Token', token),
      line('Snapshot URL', `${base}/snapshot.jpg?token=${token}`),
      line('Stream URL', `${base}/stream?token=${token}`));
  };
  return [
    h('label', { class: 'key', text: 'API token' }),
    h('div', { class: 'value', style: 'gap:.5rem;flex-wrap:wrap' },
      h('span', { text: isSet ? 'set (hidden)' : 'none' }),
      confirmButton(isSet ? 'Generate new' : 'Generate', isSet ? 'Replace it?' : 'Confirm', async () => {
        try {
          const r = await api('api-token', { method: 'POST', body: {} });
          showToken(r.token);
        } catch (e) {
          if (!(e instanceof Unauthorized)) toast(e.message, true);
        }
      }),
      isSet ? confirmButton('Revoke', 'Click again to revoke', async () => {
        await action('api-token/revoke', {}, 'API token revoked');
        route();
      }) : null),
    shown,
    h('div', { class: 'hint', text: 'Read-only access for integrations (snapshot, live stream, status) without logging in: Bearer header or ?token=. It cannot change settings. Anyone with the URL can watch the camera, so keep it private.' }),
  ];
}

async function renderSystem() {
  const view = $('#view');
  const [s, device, apiToken] = await Promise.all([api('status'), api('config/device'), api('api-token')]);
  const fw = s.firmware || {};
  const kv = (label, value) => [h('label', { class: 'key', text: label }), h('div', { class: 'value strong', text: value })];
  const logs = h('pre', { class: 'log', text: 'Loading…' });
  view.replaceChildren(
    h('h2', { text: 'Device' }),
    h('div', { class: 'form' },
      kv('Firmware', s.version),
      kv('Uptime', duration(s.uptime_s)),
      kv('Internal RAM', memory(s.heap.internal_free, s.heap.internal_total, s.heap.internal_min)),
      kv('PSRAM', memory(s.heap.psram_free, s.heap.psram_total)),
      kv('WiFi', `${s.wifi.ssid} · ${s.wifi.ip} · ${s.wifi.rssi} dBm`),
      kv('Uploads OK / failed', `${s.connect.uploads_ok} / ${s.connect.uploads_failed}`),
      kv('Last error', s.connect.last_error || '—')),
    h('h2', { text: 'Recent reboots' }),
    h('table', { class: 'data' },
      h('thead', {}, h('tr', {}, h('th', { text: 'Reason' }), h('th', { text: 'After' }))),
      h('tbody', {}, (s.resets || []).map((r) => h('tr', {}, h('td', { text: r.reason }), h('td', { text: duration(r.uptime_s) }))))),
    h('h2', { text: 'Firmware' }),
    h('div', { class: 'form' },
      kv('Running', `${s.version} (${fw.partition || '?'})${fw.pending_verify ? ' · on probation: confirms after 1 min online' : ''}`),
      fw.rolled_back ? kv('Rolled back', `Update ${fw.rolled_back} failed its health check; the previous version was restored.`) : null,
      h('label', { class: 'key', text: 'Update' }),
      h('div', { class: 'value', style: 'flex-wrap:wrap;gap:.5rem' }, firmwareUpdate()),
      h('div', { class: 'hint', text: 'firmware/build/prusacam_byclaude.bin. If the new version does not come online within a minute after rebooting, the previous one is restored on the next reset.' }),
      coredumpRows(s.coredump)),
    h('h2', { text: 'Maintenance' }),
    h('div', { class: 'form' },
      h('label', { class: 'key', for: 'f-status-led', text: 'Status LED' }),
      h('div', { class: 'value' }, statusLedSwitch(device.status_led)),
      h('div', { class: 'hint', text: 'Red LED on the back: one short blink per minute when all is well, slow blink without WiFi, double blink when uploads fail' }),
      apiTokenRows(apiToken.set),
      h('label', { class: 'key', text: 'Factory reset' }),
      h('div', { class: 'value' }, confirmButton('Erase settings', 'Click again to erase', async () => {
        await action('factory-reset', {}, 'Settings erased. Rebooting into setup mode…');
      })),
      h('div', { class: 'hint', text: 'Forgets the web user, WiFi, PrusaConnect token and camera settings. Same as holding GPIO12 to GND for 10 s at power-on.' })),
    h('h2', { text: 'Log' }),
    logs,
  );
  logs.textContent = await api('logs');
  logs.scrollTop = logs.scrollHeight;
}

// ---------- status header ----------
let lightOn = false;
let liveOn = false;

// Live view swaps the snapshot for the MJPEG stream (one viewer at a time on the camera).
function setLive(on) {
  liveOn = on;
  const photo = $('#photo');
  $('#live').textContent = on ? 'Stop live view' : 'Live view';
  $('#photo-caption').textContent = on ? 'Live · up to 5 fps, stops after 30 min' : '';
  delete photo.dataset.seq;
  photo.src = on ? `${API}/stream?t=${Date.now()}` : `${API}/snapshot.jpg?t=${Date.now()}`;
  if (!on) refreshStatus();
}

async function refreshStatus() {
  try {
    const s = await api('status');
    const state = $('#connect-state');
    state.textContent = s.connect.state;
    state.className = `state ${STATE_CLASS[s.connect.state] || ''}`;
    $('#interval').textContent = s.connect.interval_s ? `${s.connect.interval_s} s` : 'manual';
    $('#last-upload').textContent = ago(s.connect.last_upload_ago_s);
    $('#version').textContent = s.version;
    $('#printer-line').hidden = !s.printer;
    if (s.printer) {
      const p = s.printer;
      const job = p.phase === 'printing' || p.phase === 'paused'
        ? ` ${Math.round(p.progress)} %${p.file ? ` · ${p.file}` : ''}${p.time_left_s ? ` · ${duration(p.time_left_s)} left` : ''}`
        : '';
      $('#printer-state').textContent = p.reachable ? `${p.phase}${job}` : `unreachable${p.error ? ` (${p.error})` : ''}`;
    }
    if (s.connect.uploads_paused) $('#connect-state').textContent = 'paused (printer idle)';
    lightOn = !!s.light;
    $('#light-icon').src = lightOn ? 'icons/light-on-icon.svg' : 'icons/light-off-icon.svg';
    const photo = $('#photo');
    if (!liveOn && s.connect.frame_seq !== undefined && photo.dataset.seq !== String(s.connect.frame_seq)) {
      photo.dataset.seq = s.connect.frame_seq;
      photo.src = `${API}/snapshot.jpg?seq=${s.connect.frame_seq}`;
      $('#photo-caption').textContent = `Captured ${ago(s.connect.frame_age_s)}`;
    }
  } catch (e) {
    if (!(e instanceof Unauthorized)) $('#connect-state').textContent = 'camera unreachable';
  }
}

// ---------- routing ----------
const ROUTES = { camera: () => renderForm('camera'), connect: () => renderForm('connect'),
                 network: () => renderForm('network'), printer: () => renderForm('printer'),
                 storage: () => renderForm('storage'), mqtt: () => renderForm('mqtt'),
                 system: renderSystem };

async function route() {
  const name = (location.hash || '#camera').slice(1);
  for (const a of document.querySelectorAll('#tabs a')) a.classList.toggle('active', a.hash === `#${name}`);
  try {
    await (ROUTES[name] || ROUTES.camera)();
  } catch (e) {
    if (!(e instanceof Unauthorized)) $('#view').replaceChildren(h('p', { text: `Error: ${e.message}` }));
  }
}

// ---------- session ----------
let setupMode = false;

function showLogin(setup = false, windowOpen = true) {
  setupMode = setup;
  $('#app').hidden = true;
  $('#login').hidden = false;
  $('#setup-hint').hidden = !setup;
  $('#login-submit').textContent = setup ? 'Create user' : 'Sign in';
  $('#login-form [name=password]').autocomplete = setup ? 'new-password' : 'current-password';
  $('#confirm-wrap').hidden = !setup;
  $('#login-form [name=confirm]').required = setup;
  $('#login-submit').disabled = setup && !windowOpen;
  $('#login-error').textContent = setup && !windowOpen
    ? 'For security, the initial user can only be created during the first 15 minutes after powering on the camera. Reboot it and try again.'
    : '';
}

function showApp() {
  $('#login').hidden = true;
  $('#app').hidden = false;
  refreshStatus();
  route();
}

$('#login-form').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const body = Object.fromEntries(new FormData(ev.target));
  if (setupMode && body.password !== body.confirm) {
    $('#login-error').textContent = 'Passwords do not match';
    return;
  }
  delete body.confirm;  // client-side check only; the API takes username + password
  try {
    await api(setupMode ? 'setup' : 'login', { method: 'POST', body });
    $('#login-error').textContent = '';
    showApp();
  } catch (e) {
    $('#login-error').textContent = e instanceof Unauthorized ? 'Wrong username or password' : e.message;
  }
});

// Show/hide toggle for the login and setup password fields.
for (const btn of document.querySelectorAll('#login-form .pw-eye')) {
  btn.addEventListener('click', () => {
    const input = btn.previousElementSibling;
    const show = input.type === 'password';
    input.type = show ? 'text' : 'password';
    btn.firstElementChild.src = show ? 'icons/eye-slash.svg' : 'icons/eye.svg';
    btn.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
  });
}

async function action(path, body, message) {
  try {
    await api(path, { method: 'POST', body: body ?? {} });
    if (message) toast(message);
  } catch (e) {
    if (!(e instanceof Unauthorized)) toast(e.message, true);
  }
}

$('#snap-now').addEventListener('click', async (ev) => {
  ev.target.disabled = true;
  await action('snapshot', {}, 'Snapshot requested');
  setTimeout(() => { ev.target.disabled = false; refreshStatus(); }, 2500);
});
$('#light-toggle').addEventListener('click', async () => { await action('light', { on: !lightOn }); refreshStatus(); });
// Refresh takes a fresh local frame (not uploaded) and shows it as soon as it exists.
$('#refresh').addEventListener('click', async () => {
  const before = $('#photo').dataset.seq;
  await action('preview', {});
  for (let i = 0; i < 6; i++) {
    await new Promise((r) => setTimeout(r, 300));
    await refreshStatus();
    if ($('#photo').dataset.seq !== before) break;
  }
  route();
});
$('#reboot').addEventListener('click', () => {
  const b = $('#reboot');
  if (b.dataset.armed) {
    action('reboot', {}, 'Rebooting… the page reconnects by itself.');
    delete b.dataset.armed;
  } else {
    b.dataset.armed = '1';
    b.lastChild.textContent = ' Confirm?';
    setTimeout(() => { delete b.dataset.armed; b.lastChild.textContent = ' Reboot'; }, 4000);
  }
});
$('#logout').addEventListener('click', async () => { await action('logout'); showLogin(); });
$('#photo').addEventListener('click', () => window.open(`${API}/snapshot.jpg`, '_blank'));
$('#photo').addEventListener('error', () => {
  if (liveOn) {
    toast('Live view unavailable (another viewer, or the camera is busy)', true);
    setLive(false);
  }
});
$('#live').addEventListener('click', () => setLive(!liveOn));
// Leaving the page or hiding the tab frees the single stream slot on the camera.
document.addEventListener('visibilitychange', () => { if (document.hidden && liveOn) setLive(false); });
window.addEventListener('hashchange', route);

setInterval(() => { if (!$('#app').hidden) refreshStatus(); }, 5000);

(async () => {
  try {
    const session = await api('session');
    if (session.authenticated) showApp();
    else showLogin(session.setup_required, session.setup_window_open);
  } catch {
    showLogin();
  }
})();
