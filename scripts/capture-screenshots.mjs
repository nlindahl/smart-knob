// Run after building and serving web/: see docs/screenshots/README.md.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const { chromium } = await import(process.env.PLAYWRIGHT_MODULE ||
  new URL('../tests/node_modules/playwright/index.mjs', import.meta.url).href);
const root = fileURLToPath(new URL('..', import.meta.url));
const output = path.join(root, 'docs/screenshots');
const base = process.env.EDITOR_URL || 'http://127.0.0.1:8080';
const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium',
  headless: true,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
});
const errors = [];
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, deviceScaleFactor: 1 });
page.on('pageerror', e => errors.push(e.message));
await fs.mkdir(output, { recursive: true });
try {
  await page.goto(base);
  await page.waitForFunction(() => !document.querySelector('#loading'));
  // Put the rotation slider at a nonzero value so its bindings are visible.
  await page.mouse.click(340, 569);
  await page.keyboard.press('Control+a');
  await page.keyboard.type('144');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(400);
  await page.screenshot({ path: path.join(output, 'editor-overview.png') });

  await page.mouse.click(55, 124); // Bounded preset.
  await page.mouse.click(1250, 173); // Motor feedback inspector.
  await page.waitForTimeout(350);
  await page.screenshot({ path: path.join(output, 'editor-motor-feedback.png') });
  await page.mouse.click(1250, 173); // Close motor inspector.
  await page.waitForTimeout(350); // Wait for collapse animation before locating LEDs.
  await page.mouse.click(1250, 193); // Open LED inspector.
  await page.waitForTimeout(350);
  await page.screenshot({ path: path.join(output, 'editor-led-ring.png') });

  const bundle = [...await fs.readFile(path.join(root, 'web/capture-demo.skv'))];
  const names = ['Free spin', 'Fine detents', 'Coarse detents', 'Bounded', 'Magnetic', 'Switch'];
  const positions = [12, 24, 5, 64, 5, 1];
  const notes = ['Smooth, continuous rotation', '6° detents · adjustable strength', '30° detents · distinct steps', '0–100 range · physical end stops', 'Attraction at positions 0, 5 and 10', 'Two positions · tactile snap'];
  const views = await page.evaluate(({ bundle, names, positions, notes }) => {
    const bytes = Uint8Array.from(bundle);
    return names.map((name, i) => {
      const pixels = smartknob.render(bytes, i + 1, positions[i], 0, 1000, 0);
      const leds = [...smartknob.ring()];
      const canvas = document.createElement('canvas');
      canvas.width = canvas.height = 240;
      canvas.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(pixels), 240, 240), 0, 0);
      return { name, position: positions[i], note: notes[i], screen: canvas.toDataURL('image/png'), leds };
    });
  }, { bundle, names, positions, notes });

  // A browser-rendered hardware mockup. Screen textures and ring colors above
  // are from the firmware's actual LVGL / LED WASM implementation.
  const mockup = await browser.newPage({ viewport: { width: 1440, height: 1200 }, deviceScaleFactor: 1.5 });
  await mockup.setContent(`<!doctype html><html><head><meta charset="utf-8"><style>
    *{box-sizing:border-box}body{margin:0;background:#0c1119;color:#edf3fa;font-family:Arial,Helvetica,sans-serif}
    main{padding:52px 58px 32px}.eyebrow{color:#61d5bb;letter-spacing:2.8px;font-size:12px;font-weight:700}
    h1{font-size:38px;letter-spacing:-1px;margin:12px 0}header p{color:#95a5b8;font-size:16px;margin:0}
    .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:24px;margin-top:32px}
    article{background:linear-gradient(145deg,#18222f,#111923);border:1px solid #273343;border-radius:20px;text-align:center;padding:18px 20px 23px}
    .meta{display:flex;justify-content:space-between;color:#92a2b6;font-size:11px;letter-spacing:1.4px;text-transform:uppercase;margin-bottom:6px}
    .device{position:relative;width:308px;height:308px;margin:0 auto 14px;border-radius:50%;background:repeating-conic-gradient(#3a4654 0deg 1deg,#1c242e 1deg 3deg);box-shadow:0 15px 27px #0009,inset 0 2px 3px #8e9aa866,inset 0 -3px 5px #000}
    .body{position:absolute;inset:6px;border-radius:50%;background:linear-gradient(140deg,#4b5664,#1c242e 42%,#0d131b 72%,#343e49);box-shadow:inset 0 0 0 1px #78849255}
    .cap{position:absolute;inset:26px;border-radius:50%;background:linear-gradient(135deg,#394450,#10161e 48%,#3b4652);box-shadow:0 0 0 2px #070c12,0 0 0 3px #56627355}
    .screen{position:absolute;width:240px;height:240px;left:34px;top:34px;border-radius:50%;box-shadow:0 0 0 1px #05090d}
    canvas{position:absolute;inset:0;width:308px;height:308px;pointer-events:none}
    h2{font-size:20px;margin:12px 0 7px;font-weight:600}.note{color:#a2b1c2;font-size:13px;margin:0}
    footer{display:flex;justify-content:space-between;margin-top:25px;font-size:12px;color:#8292a6}
    footer strong{color:#b5c3d3;font-weight:400}
  </style></head><body><main><header><div class="eyebrow">SMARTKNOB DEV KIT v0.1</div><h1>Six views. Six different feels.</h1><p>Actual LVGL display renders, paired with the 72-LED ring.</p></header><section class="grid">${views.map((v, i) => `<article><div class="meta"><span>View 0${i + 1}</span><span>240 × 240</span></div><div class="device"><div class="body"></div><div class="cap"></div><img class="screen" src="${v.screen}" alt="${v.name} view"><canvas width="616" height="616" id="ring-${i}"></canvas></div><h2>${v.name}</h2><p class="note">${v.note}</p></article>`).join('')}</section><footer><strong>Simulated hardware preview · housing and LED glow are illustrative</strong><span>Screen pixels rendered by the firmware UI library</span></footer></main></body></html>`);
  await mockup.evaluate(views => {
    views.forEach((v, i) => {
      const ctx = document.querySelector(`#ring-${i}`).getContext('2d');
      ctx.scale(2, 2);
      for (let j = 0; j < 72; j++) {
        const angle = j * Math.PI * 2 / 72 - Math.PI / 2;
        const rgb = v.leds.slice(j * 3, j * 3 + 3).map(c => Math.round(Math.sqrt(c / 255) * 255));
        const css = `rgb(${rgb.join(',')})`;
        ctx.shadowColor = css; ctx.shadowBlur = 7;
        ctx.fillStyle = css; ctx.beginPath();
        ctx.arc(154 + Math.cos(angle) * 140, 154 + Math.sin(angle) * 140, 2.25, 0, Math.PI * 2);
        ctx.fill();
      }
    });
  }, views);
  await mockup.screenshot({ path: path.join(output, 'smartknob-views.png'), fullPage: true });
  for (let i = 0; i < views.length; i++) {
    await fs.writeFile(path.join(output, `view-${i + 1}.png`), Buffer.from(views[i].screen.split(',')[1], 'base64'));
  }
  if (errors.length) throw new Error(errors.join('\n'));
  console.log(`Captured 3 editor screenshots, 6 LVGL screens, and the SmartKnob mockup in ${output}`);
} finally {
  await browser.close();
}
