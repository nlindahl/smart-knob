import createPreview from './preview.js';
let preview, ptr = 0, last = new Uint8Array(), imported = '', message = 'USB disconnected', port;
let reader, writer, input = [], sequence = 1, pending = new Map(), busy = false, state = [];
let receiveTask, disconnecting = false;
let diagnostic = '', diagnosticAt = 0, diagnosticsAvailable = true;
export const ready = createPreview().then(m => { preview = m; preview._sk_preview_init(); ptr = preview._malloc(24576); });
function load(bytes, view) {
  if (bytes.length !== last.length || bytes.some((b, i) => b !== last[i])) {
    preview.HEAPU8.set(bytes, ptr);
    if (!preview._sk_preview_load(ptr, bytes.length)) throw new Error('LVGL rejected the view bundle');
    last = bytes.slice();
  }
  if (!preview._sk_preview_select(view)) throw new Error('Preview view does not exist');
}
export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const b of bytes) { crc ^= b; for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1)); }
  return (~crc) >>> 0;
}
export function frame(op, seq, payload = new Uint8Array()) {
  if (payload.length > 512) throw new Error('Payload too large');
  const b = new Uint8Array(12 + payload.length), d = new DataView(b.buffer);
  b.set([83,75,1,op]); d.setUint16(4,seq,true); d.setUint16(6,payload.length,true); b.set(payload,8);
  d.setUint32(8+payload.length,crc32(b.subarray(2,8+payload.length)),true);
  return b;
}
export function parse(buffer) {
  const frames = [];
  while (buffer.length >= 2) {
    if (buffer[0] !== 83 || buffer[1] !== 75) { buffer.shift(); continue; }
    if (buffer.length < 8) break;
    const len = buffer[6] | buffer[7] << 8;
    if (buffer[2] !== 1 || len > 512) { buffer.shift(); continue; }
    if (buffer.length < len+12) break;
    const b = Uint8Array.from(buffer.slice(0,len+12)), d = new DataView(b.buffer);
    if (crc32(b.subarray(2,len+8)) !== d.getUint32(len+8,true)) { buffer.shift(); continue; }
    frames.push({op:b[3],seq:d.getUint16(4,true),payload:b.slice(8,len+8)});
    buffer.splice(0,len+12);
  }
  return frames;
}
async function receive() {
  try {
    while (!disconnecting && port?.readable) {
      reader = port.readable.getReader();
      try {
        while (true) {
          const {value,done} = await reader.read(); if (done) break;
          input.push(...value);
          for (const f of parse(input)) {
            const p = pending.get(f.seq);
            if (!p || f.op !== (p.op | 0x80)) continue;
            pending.delete(f.seq); clearTimeout(p.timer);
            if (f.payload[0] === 0) p.resolve(f.payload.slice(1));
            else p.reject(new Error(`Device: ${['ok','invalid request','busy','invalid bundle','storage failed','hardware unavailable'][f.payload[0]] || 'unknown error'}`));
          }
        }
      } finally { reader.releaseLock(); reader = undefined; }
    }
  } catch (e) { message = `USB disconnected: ${e.message}`; }
  finally {
    for (const p of pending.values()) { clearTimeout(p.timer); p.reject(new Error('USB disconnected')); }
    pending.clear();
    if (writer) { writer.releaseLock(); writer = undefined; }
    if (port) { try { await port.close(); } catch {} port = undefined; }
    disconnecting = false;
  }
}
async function call(op, payload = new Uint8Array()) {
  if (!writer) throw new Error('Connect USB first');
  const seq = sequence++ & 65535;
  const bytes = frame(op,seq,payload);
  let rejectWrite;
  const response = new Promise((resolve,reject) => {
    rejectWrite = reject;
    const timer = setTimeout(() => { pending.delete(seq); reject(new Error('Device response timed out')); },10000);
    pending.set(seq,{op,resolve,reject,timer});
  });
  try { await writer.write(bytes); } catch (e) { const p = pending.get(seq); if (p) clearTimeout(p.timer); pending.delete(seq); rejectWrite(e); }
  return response;
}
async function withDevice(action) {
  if (busy) { message = 'Wait for the current device operation'; return; }
  busy = true;
  try { await action(); } catch (e) { message = e.message; } finally { busy = false; }
}
async function upload(bytes, save) {
  await withDevice(async () => {
    const begin = new Uint8Array(8), d = new DataView(begin.buffer);
    d.setUint32(0,bytes.length,true); d.setUint32(4,crc32(bytes),true);
    await call(2,begin);
    for (let offset = 0; offset < bytes.length; offset += 508) {
      const slice = bytes.slice(offset,offset+508), payload = new Uint8Array(4+slice.length);
      new DataView(payload.buffer).setUint32(0,offset,true); payload.set(slice,4);
      await call(3,payload); message = `Uploading ${Math.round(100*(offset+slice.length)/bytes.length)}%`;
    }
    await call(4);
    if (save) { message = 'Saving views…'; await call(9); }
    message = save ? 'Views uploaded and saved' : 'Views active for live testing';
  });
}
async function connect() {
  await withDevice(async () => {
    if (!navigator.serial) throw new Error('Web Serial is unavailable. Use Chrome/Edge on HTTPS or localhost, or use knobctl.');
    if (port) { message = 'USB already connected'; return; }
    port = await navigator.serial.requestPort();
    try { await port.open({baudRate:115200,bufferSize:16384}); } catch (e) { port = undefined; throw e; }
    input = []; state = []; disconnecting = false; writer = port.writable.getWriter();
    diagnostic = ''; diagnosticAt = 0; diagnosticsAvailable = true;
    receiveTask = receive();
    const name = new TextDecoder().decode(await call(1));
    message = name;
  });
}
setInterval(async () => {
  if (!writer || busy) return;
  busy = true;
  try {
    const bytes = await call(6);
    if (bytes.length === 28) {
      const d = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
      state = [d.getUint16(0,true),d.getInt32(2,true),d.getFloat32(6,true),bytes[10],bytes[11],d.getFloat32(16,true),d.getFloat32(20,true),d.getUint16(24,true)];
      if ((bytes[10] & 1) && diagnosticsAvailable && Date.now() - diagnosticAt >= 2000) {
        diagnosticAt = Date.now();
        try { diagnostic = new TextDecoder().decode(await call(12)); }
        catch (e) {
          if (e.message === 'Device: invalid request') diagnosticsAvailable = false;
          else throw e;
        }
      }
    }
  } catch (e) { message = e.message; } finally { busy = false; }
},500);
globalThis.smartknob = {
  render(bytes,view,position,sub,ms,flash) { load(bytes,view); const p = preview._sk_preview_render(position,sub,ms,flash); return preview.HEAPU8.slice(p,p+240*240*4); },
  simulate(bytes,view,angle,pressed,longPress) { load(bytes,view); const p = preview._sk_preview_step(angle,pressed ? 1 : 0,longPress ? 1 : 0); return preview.HEAPF32.slice(p/4,p/4+3); },
  reset() { preview._sk_preview_reset(); },
  ring() { const p = preview._sk_preview_leds(); return preview.HEAPU8.slice(p,p+216); },
  download(name,bytes,mime) { const url = URL.createObjectURL(new Blob([bytes.slice()],{type:mime})); const a = document.createElement('a'); a.href=url; a.download=name; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000); },
  import_project() { const input = document.createElement('input'); input.type='file'; input.accept='.json,application/json'; input.onchange = async () => { if (input.files[0]) { if (input.files[0].size > 262144) { message='Project file is too large'; return; } imported = await input.files[0].text(); } }; input.click(); },
  take_import() { const json = imported; imported=''; return json; },
  connect,
  install_firmware() {
    withDevice(async () => {
      // No other device operation can start while the reader and port shut down.
      disconnecting = true;
      if (reader) await reader.cancel();
      await receiveTask;
      location.assign(new URL('./install.html', location.href));
    });
  },
  upload(bytes,save) { upload(bytes.slice(),save); },
  command(op,bytes) { const data=bytes.slice(); withDevice(async () => { await call(op,data); message=op===7 ? 'Calibration started; leave the knob untouched' : 'Command accepted'; }); },
  status() { return message; },
  telemetry() { return Float32Array.from(state); },
  diagnostics() { return diagnostic; },
};
