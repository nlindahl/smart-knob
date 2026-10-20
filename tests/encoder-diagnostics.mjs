// Check new diagnostic polling and compatibility with firmware predating opcode 12.
import assert from 'node:assert/strict';
import { chromium } from 'playwright';
const base = process.env.SITE_URL || 'http://127.0.0.1:8081/smart-knob/';
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH || undefined,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
});
try {
  for (const available of [true, false]) {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.goto(base);
    await page.waitForFunction(() => !document.querySelector('#loading'));
    await page.evaluate(async available => {
      const { frame } = await import('./bridge.js');
      let input;
      window.diagnosticRequests = 0;
      const port = {
        async open() {
          this.readable = new ReadableStream({ start(controller) { input = controller; } });
          this.writable = new WritableStream({ write(bytes) {
            const op = bytes[3], seq = new DataView(bytes.buffer, bytes.byteOffset).getUint16(4, true);
            let reply = new Uint8Array([0]);
            if (op === 1) reply = new Uint8Array([0, ...new TextEncoder().encode('Test SmartKnob')]);
            if (op === 6) {
              reply = new Uint8Array(29);
              reply[11] = available ? 0 : 1; // Warnings must be visible without a hard fault.
            }
            if (op === 12) {
              window.diagnosticRequests++;
              reply = available
                ? new Uint8Array([0, ...new TextEncoder().encode('Encoder: spi_rc=0 raw=123456 crc=16/17 loss=0 field=1 reads=10 valid=0 spi_err=0 crc_err=10 status_err=0')])
                : new Uint8Array([1]);
            }
            input.enqueue(frame(op | 0x80, seq, reply));
          } });
        },
        async close() { this.readable = this.writable = null; },
      };
      Object.defineProperty(navigator, 'serial', { configurable: true, value: { requestPort: async () => port } });
      await smartknob.connect();
    }, available);
    await page.waitForFunction(() => window.diagnosticRequests > 0);
    if (available) {
      await page.waitForFunction(() => smartknob.diagnostics().includes('raw=123456'));
      assert.equal(await page.evaluate(() => smartknob.telemetry()[3]), 0);
    } else {
      await page.waitForTimeout(2500);
      assert.equal(await page.evaluate(() => window.diagnosticRequests), 1);
      assert.equal(await page.evaluate(() => smartknob.status()), 'Test SmartKnob');
    }
    assert.deepEqual(errors, []);
    await page.close();
  }
  console.log('Encoder diagnostics passed: warning polling without a fault, raw data, older-firmware compatibility');
} finally { await browser.close(); }
