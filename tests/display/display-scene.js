// Diagnostic fixture using the locked PocketJS HostOps, not a new renderer.
// Property IDs: engine/core/src/spec.rs @ 53a17f6416c3333f1171141bf996695720101ed2.
const W = ui.__viewport.w, H = ui.__viewport.h;
if (W !== 1024 || (H !== 600 && H !== 800)) throw new Error('viewport');
if (!ui.loadFontAtlas(__pak) || ui.measureText('RGB', 0) !== 54) throw new Error('font');
const rgba = (r, g, b) => (0xff000000 | (b << 16) | (g << 8) | r) >>> 0;
function node(type, parent, x, y, w, h) {
  const n = ui.createNode(type);
  ui.setProp(n, 24, 1); ui.setProp(n, 28, x); ui.setProp(n, 25, y);
  ui.setProp(n, 1, w); ui.setProp(n, 2, h); ui.insertBefore(parent, n, 0);
  return n;
}
function box(parent, x, y, w, h, r, g, b) {
  const n = node(0, parent, x, y, w, h); ui.setProp(n, 64, rgba(r, g, b)); return n;
}
function text(x, y, value) {
  const n = node(1, 1, x, y, value.length * 18, 24);
  ui.setProp(n, 96, rgba(255, 255, 255)); ui.setProp(n, 97, 0);
  ui.setProp(n, 99, 24); ui.setText(n, value); return n;
}
ui.setProp(1, 64, rgba(16, 20, 28));
box(1, 0, 0, W, 1, 255, 255, 255); box(1, 0, H - 1, W, 1, 255, 255, 255);
box(1, 0, 0, 1, H, 255, 255, 255); box(1, W - 1, 0, 1, H, 255, 255, 255);
box(1, 1, 1, 8, 8, 255, 0, 0); box(1, W - 9, 1, 8, 8, 0, 255, 0);
box(1, 1, H - 9, 8, 8, 0, 0, 255); box(1, W - 9, H - 9, 8, 8, 255, 255, 0);
text(32, 28, 'FRAMEBUFFER ' + W + 'X' + H);
text(32, 72, 'RED'); text(344, 72, 'GREEN'); text(656, 72, 'BLUE');
box(1, 32, 104, 280, 96, 255, 0, 0); box(1, 344, 104, 280, 96, 0, 255, 0);
box(1, 656, 104, 280, 96, 0, 0, 255);
text(32, 232, 'CLIP'); text(344, 232, 'IMAGE'); text(504, 232, 'GRAY');
const clip = box(1, 32, 272, 280, 160, 48, 48, 48); ui.setProp(clip, 30, 1);
box(clip, 240, 50, 100, 70, 255, 255, 0);
const pixels = new Uint8Array([255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255]);
const texture = ui.uploadTexture(pixels.buffer, 2, 2, 3);
if (texture < 0) throw new Error('image');
const image = node(2, 1, 344, 272, 128, 128); ui.setImage(image, texture);
for (let i = 0; i < 16; i++) box(1, 504 + i * 27, 272, 27, 128, i * 17, i * 17, i * 17);
text(32, H - 112, 'RGB 1:1');
box(1, 32, H - 68, W - 64, 20, 48, 48, 48);
const marker = box(1, 32, H - 68, 4, 20, 255, 255, 255);
let turn = 0;
globalThis.frame = function() { turn++; ui.setProp(marker, 28, 32 + (turn * 8) % (W - 68)); };
