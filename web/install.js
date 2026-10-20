const byId = id => document.getElementById(id);
const installer = byId('installer');
const install = byId('install');
const status = byId('status');
let ready = false;
let objectURLs = [];
const supported = window.isSecureContext && 'serial' in navigator;

function updateButton() { install.disabled = !ready || !byId('hardware').checked; }
byId('hardware').addEventListener('change', updateButton);

async function getJSON(url) {
  const response = await fetch(url, { cache: 'no-store' });
  if (!response.ok) throw new Error(`Firmware request failed (${response.status})`);
  return response.json();
}

function localFile(path, base) {
  const url = new URL(path, base);
  if (url.origin !== location.origin || !url.href.startsWith(new URL('./', base).href)) {
    throw new Error('Firmware files must belong to the same build');
  }
  return url;
}

function objectURL(blob) {
  const url = URL.createObjectURL(blob);
  objectURLs.push(url);
  return url;
}

async function load() {
  ready = false;
  updateButton();
  installer.hidden = true;
  byId('retry').hidden = true;
  for (const id of ['download', 'checksums', 'manifest']) byId(id).hidden = true;
  objectURLs.forEach(URL.revokeObjectURL);
  objectURLs = [];
  status.textContent = 'Loading installer and verifying firmware…';
  try {
    await import('./dist/esp-web-tools/install-button.js');
    if (!window.isSecureContext) {
      installer.hidden = false;
      status.textContent = 'USB flashing and checksum verification require HTTPS or localhost. Open the hosted installer or use a firmware release download.';
      return;
    }
    const catalogURL = new URL('./firmware/latest.json', location.href);
    const catalog = await getJSON(catalogURL);
    const manifestURL = localFile(catalog.manifest, catalogURL);
    const metadataURL = localFile(catalog.details, catalogURL);
    const [manifest, details] = await Promise.all([getJSON(manifestURL), getJSON(metadataURL)]);
    if (!/^[a-f0-9]{40}$/.test(details.commit) || !/^[a-f0-9]{64}$/.test(details.sha256)
        || catalog.version !== manifest.version || catalog.version !== details.version
        || manifest.new_install_prompt_erase !== true || manifest.new_install_improv_wait_time !== 0
        || manifest.builds.length !== 1 || manifest.builds[0].chipFamily !== 'ESP32-S3'
        || manifest.builds[0].parts.length !== 1 || manifest.builds[0].parts[0].offset !== 0
        || details.offset !== 0 || details.hardwareValidated !== false) {
      throw new Error('Firmware metadata does not match the Dev Kit v0.1 install layout');
    }
    const binaryURL = localFile(manifest.builds[0].parts[0].path, manifestURL);
    const response = await fetch(binaryURL, { cache: 'no-store' });
    if (!response.ok) throw new Error(`Firmware download failed (${response.status})`);
    const bytes = await response.arrayBuffer();
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
    if (bytes.byteLength !== details.size || hash !== details.sha256) throw new Error('Firmware checksum verification failed; installation is disabled');
    // Flash exactly the bytes verified above, even if a deployment changes mid-session.
    manifest.builds[0].parts[0].path = objectURL(new Blob([bytes], { type: 'application/octet-stream' }));
    installer.manifest = objectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }));
    byId('version').textContent = details.version;
    byId('source').href = `https://github.com/nlindahl/smart-knob/commit/${details.commit}`;
    byId('source').textContent = details.commit.slice(0, 12);
    byId('checksum').textContent = hash;
    for (const [id, url] of [['download', binaryURL], ['checksums', new URL('SHA256SUMS', manifestURL)], ['manifest', manifestURL]]) {
      byId(id).href = url.href;
      byId(id).hidden = false;
    }
    ready = true;
    installer.hidden = false;
    updateButton();
    status.textContent = supported
      ? 'Firmware verified. Confirm your hardware, then connect your knob to start.'
      : 'Firmware verified. USB flashing needs desktop Chrome or Edge on HTTPS or localhost. Downloads remain available.';
  } catch (error) {
    status.textContent = `Unable to prepare installer: ${error.message}. Retry, or use a firmware release download.`;
    byId('retry').hidden = false;
  }
}

byId('retry').addEventListener('click', load);
addEventListener('pagehide', () => objectURLs.forEach(URL.revokeObjectURL));
load();
