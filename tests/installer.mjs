// Browser integration tests. ESP Web Tools is real; only the USB/chip boundary is mocked.
import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const base = process.env.SITE_URL || 'http://127.0.0.1:8081/smart-knob/';
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH || undefined,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
});

async function pageWithUSB(mode = 'success') {
  const page = await browser.newPage();
  page.setDefaultTimeout(10000);
  await page.addInitScript(mode => {
    window.usbCalls = [];
    const port = {
      readable: null, writable: null,
      async open() {
        if (this.readable) throw new Error('Port already open');
        window.usbCalls.push('open');
        this.readable = new ReadableStream();
        this.writable = new WritableStream();
      },
      async close() {
        if (this.readable?.locked || this.writable?.locked) throw new Error('Port still locked');
        window.usbCalls.push('close');
        this.readable = this.writable = null;
      },
      async setSignals() {},
      getInfo() { return { usbVendorId: 0x303a, usbProductId: 0x1001 }; },
      addEventListener() {}, removeEventListener() {},
    };
    Object.defineProperty(navigator, 'serial', { configurable: true, value: {
      async requestPort() {
        window.usbCalls.push('pick');
        if (mode === 'cancel') throw new DOMException('No port selected', 'NotFoundError');
        return port;
      },
    } });
    // The upstream flasher exposes this instance for debugging. Replace hardware I/O,
    // while retaining the real dialog, manifest loading, download, and flash flow.
    Object.defineProperty(window, 'esploader', { configurable: true, set(loader) {
      loader.main = async () => { await port.open(); loader.chip = { CHIP_NAME: mode === 'wrong-chip' ? 'ESP32-C3' : 'ESP32-S3' }; };
      loader.flashId = async () => {};
      loader.after = async () => {};
      loader.eraseFlash = async () => { window.usbCalls.push('erase'); };
      loader.writeFlash = async options => {
        window.usbCalls.push('write');
        if (mode === 'disconnect') throw new Error('USB disconnected during write');
        const part = options.fileArray[0];
        window.flashed = { offset: part.address, eraseAll: options.eraseAll,
          hash: Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', part.data)), b => b.toString(16).padStart(2, '0')).join('') };
        options.reportProgress(0, 1, 2);
        options.reportProgress(0, 2, 2);
      };
    } });
  }, mode);
  return page;
}

async function ready(page) {
  await page.goto(new URL('install.html', base).href);
  await page.waitForFunction(() => document.querySelector('#status').textContent.startsWith('Firmware verified.'));
}

async function startFlash(page) {
  await ready(page);
  assert.equal(await page.locator('#install').isDisabled(), true);
  await page.locator('#hardware').check();
  await page.locator('#install').click();
  const dialog = page.locator('ewt-install-dialog');
  await dialog.locator('ew-list-item').filter({ hasText: 'Install SmartKnob' }).click();
  assert.equal(await dialog.locator('ew-checkbox').evaluate(el => !!el.checked), false, 'Full erase must default off');
  await dialog.getByRole('button', { name: 'Next', exact: true }).click();
  await dialog.getByRole('button', { name: 'Install', exact: true }).click();
  return dialog;
}

try {
  const page = await pageWithUSB();
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await startFlash(page);
  await page.waitForFunction(() => window.flashed);
  const result = await page.evaluate(() => ({ flashed, calls: usbCalls, expected: document.querySelector('#checksum').textContent }));
  assert.equal(result.flashed.offset, 0);
  assert.equal(result.flashed.eraseAll, false);
  assert.equal(result.flashed.hash, result.expected, 'Flasher must receive the verified binary bytes');
  assert(!result.calls.includes('erase'));
  await page.waitForTimeout(500);
  assert.deepEqual(errors, []);
  await page.close();

  const lost = await pageWithUSB('disconnect');
  const lostDialog = await startFlash(lost);
  await lost.waitForFunction(() => document.querySelector('ewt-install-dialog')?.shadowRoot.querySelector('ewt-page-message')?.shadowRoot?.textContent.includes('USB disconnected during write'));
  assert(await lostDialog.locator('ewt-page-message').isVisible());
  await lost.close();

  const wrong = await pageWithUSB('wrong-chip');
  const wrongDialog = await startFlash(wrong);
  await wrong.waitForFunction(() => document.querySelector('ewt-install-dialog')?.shadowRoot.querySelector('ewt-page-message')?.shadowRoot?.textContent.includes('Your ESP32-C3 board is not supported.'));
  assert(await wrongDialog.locator('ewt-page-message').isVisible());
  assert(!(await wrong.evaluate(() => usbCalls)).includes('write'));
  await wrong.close();

  const corrupt = await browser.newPage();
  await corrupt.route('**/firmware.bin', route => route.fulfill({ body: 'corrupt binary' }));
  await corrupt.goto(new URL('install.html', base).href);
  await corrupt.getByText(/Firmware checksum verification failed/).waitFor();
  assert.equal(await corrupt.locator('#installer').isHidden(), true);
  await corrupt.unroute('**/firmware.bin');
  await corrupt.locator('#retry').click();
  await corrupt.waitForFunction(() => document.querySelector('#status').textContent.startsWith('Firmware verified.'));
  await corrupt.close();

  const unsupported = await browser.newPage();
  await unsupported.addInitScript(() => { delete Object.getPrototypeOf(navigator).serial; });
  await ready(unsupported);
  await unsupported.locator('esp-web-install-button[install-unsupported]').waitFor();
  assert.equal(await unsupported.locator('#download').isVisible(), true);
  await unsupported.close();

  const insecure = await browser.newPage();
  await insecure.addInitScript(() => Object.defineProperty(window, 'isSecureContext', { value: false }));
  await insecure.goto(new URL('install.html', base).href);
  await insecure.getByText(/USB flashing and checksum verification require HTTPS/).waitFor();
  assert.equal(await insecure.locator('#install').isDisabled(), true);
  await insecure.close();

  const missing = await browser.newPage();
  await missing.route('**/firmware/latest.json', route => route.fulfill({ status: 404, body: '{}' }));
  await missing.goto(new URL('install.html', base).href);
  await missing.getByText(/Firmware request failed \(404\)/).waitFor();
  assert.equal(await missing.locator('#install').isDisabled(), true);
  await missing.close();

  const cancel = await pageWithUSB('cancel');
  await ready(cancel);
  await cancel.locator('#hardware').check();
  await cancel.locator('#install').click();
  await cancel.waitForFunction(() => usbCalls.includes('pick'));
  assert(!(await cancel.evaluate(() => usbCalls)).includes('open'));
  await cancel.close();

  const handoff = await browser.newPage();
  await handoff.goto(base);
  await handoff.waitForFunction(() => !document.querySelector('#loading'));
  await handoff.evaluate(async () => {
    const { frame } = await import('./bridge.js');
    let input;
    const port = {
      async open() {
        this.readable = new ReadableStream({ start(controller) { input = controller; },
          cancel() { localStorage.setItem('handoff-reader', 'canceled'); } });
        this.writable = new WritableStream({ write(bytes) {
          const seq = new DataView(bytes.buffer, bytes.byteOffset).getUint16(4, true);
          input.enqueue(frame(bytes[3] | 0x80, seq, new Uint8Array([0, ...new TextEncoder().encode('Test SmartKnob')])));
        } });
      },
      async close() {
        if (this.readable.locked || this.writable.locked) throw new Error('USB streams still locked');
        this.readable = this.writable = null;
        localStorage.setItem('handoff-port', 'closed');
      },
    };
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port }, configurable: true });
    await smartknob.connect();
    smartknob.install_firmware();
  });
  await handoff.waitForURL(new URL('install.html', base).href);
  assert.equal(await handoff.evaluate(() => localStorage.getItem('handoff-reader')), 'canceled');
  assert.equal(await handoff.evaluate(() => localStorage.getItem('handoff-port')), 'closed');
  await handoff.close();

  console.log('Installer passed: verified bytes, offset, preserve-data default, USB loss, wrong chip, corruption/retry, unsupported/insecure browsers, missing build, canceled picker, editor USB handoff');
} finally {
  await browser.close();
}
