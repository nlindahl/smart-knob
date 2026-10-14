// npm install --prefix tests playwright
// Serve web/ on localhost:8080, then: node tests/browser.mjs
import { chromium } from 'playwright';
import assert from 'node:assert/strict';
import fs from 'node:fs';
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium',
  headless: true,
  args: ['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader'],
});
const page = await browser.newPage({viewport:{width:1440,height:1000}});
const errors = [];
page.on('pageerror', e => errors.push(e.message));
try {
  await page.goto('http://127.0.0.1:8080');
  await page.waitForFunction(() => !document.querySelector('#loading'));
  await page.waitForTimeout(300);
  const project = () => page.evaluate(() => JSON.parse(localStorage.getItem('smartknob.project.v1')));
  // Edit a numeric widget by dragging it on the canvas.
  await page.mouse.move(453,330); await page.mouse.down();
  await page.mouse.move(493,355,{steps:12}); await page.mouse.up();
  await page.waitForTimeout(300);
  let p = await project();
  assert(p.views[0].widgets[2].x > 45, 'Dragging did not move the widget');
  const moved = p.views[0].widgets[2].x;
  await page.waitForTimeout(300); p = await project();
  assert.equal(p.views[0].widgets[2].x,moved,'Widget jumped back after drag');
  // Undo the entire drag, then redo it.
  await page.keyboard.press('Control+z'); await page.waitForTimeout(200);
  assert.equal((await project()).views[0].widgets[2].x,45);
  await page.keyboard.press('Control+y'); await page.waitForTimeout(200);
  assert.equal((await project()).views[0].widgets[2].x,moved);
  // Add a view and verify browser persistence survives a reload.
  await page.mouse.click(25,190); await page.waitForTimeout(200);
  assert.equal((await project()).views.length,7);
  await page.reload(); await page.waitForFunction(() => !document.querySelector('#loading'));
  assert.equal((await project()).views.length,7);
  // Exercise the actual WASM control and LED renderer against a Rust bundle.
  const bytes = [...fs.readFileSync(new URL('../build-host/demo.skv',import.meta.url))];
  const result = await page.evaluate(bytes => {
    const bundle = Uint8Array.from(bytes);
    smartknob.reset();
    smartknob.simulate(bundle,4,0,false,false);
    const sim = smartknob.simulate(bundle,4,0.6,false,false);
    const pixels = smartknob.render(bundle,4,sim[0],sim[1],100,0);
    const leds = smartknob.ring();
    return {position:sim[0],pixels:pixels.length,alphaCorner:pixels[3],alphaCenter:pixels[(120*240+120)*4+3],leds:leds.length,lit:leds.some(v=>v>0)};
  }, bytes);
  assert(result.position > 0); assert.equal(result.pixels,230400);
  assert.equal(result.alphaCorner,0); assert.equal(result.alphaCenter,255);
  assert.equal(result.leds,216); assert(result.lit);
  assert.deepEqual(errors,[]);
  await page.screenshot({path:new URL('../build-host/editor.png',import.meta.url).pathname});
  console.log('Browser: WASM startup, widget drag, undo/redo, view creation, persistence, LVGL pixels and LEDs passed');
} finally { await browser.close(); }
