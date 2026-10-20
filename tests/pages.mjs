// Verify the published editor under a repository URL prefix, not just /.
import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const url = process.env.SITE_URL || 'http://127.0.0.1:8081/smart-knob/';
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH || undefined,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
});
try {
  const page = await browser.newPage();
  const errors = [];
  const wasm = [];
  page.on('pageerror', e => errors.push(e.message));
  page.on('response', response => {
    if (response.status() >= 400) errors.push(`${response.status()} ${response.url()}`);
    if (response.url().endsWith('.wasm')) wasm.push(response);
  });
  const response = await page.goto(url);
  assert.equal(response.status(), 200);
  await page.waitForFunction(() => !document.querySelector('#loading'), null, { timeout: 30000 });
  assert.equal(await page.title(), 'SmartKnob Studio');
  assert.equal(await page.locator('canvas').count(), 1);
  assert.equal(wasm.length, 2, 'Both Rust and LVGL WASM modules must load');
  for (const module of wasm) {
    assert(module.url().startsWith(url), 'WASM asset escaped the Pages repository prefix');
    assert.equal(module.headers()['content-type'].split(';')[0], 'application/wasm');
  }
  assert.equal(await page.evaluate(() => window.isSecureContext), true);
  assert.equal(await page.evaluate(() => 'serial' in navigator), true);
  assert.equal(await page.evaluate(() => smartknob.ring().length), 216);
  assert.deepEqual(errors, []);
  console.log(`Pages smoke test passed: ${url}`);
} finally {
  await browser.close();
}
