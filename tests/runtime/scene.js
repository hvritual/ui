// Native HostOps fixture, not a Vue/JSX app or a replacement UI core.
// Numeric values come from the pinned engine/core/src/spec.rs.
if (ui.__host !== 'linux-headless' || ui.__hostAbi !== 1 || __simHz !== 60)
  throw new Error('host contract mismatch');
if (new Uint8Array(__pak)[2] !== 3) throw new Error('pack missing');
ui.setProp(1, 64, 0xff332211);
const box = ui.createNode(0);
ui.setProp(box, 1, 80); ui.setProp(box, 2, 40);
ui.setProp(box, 64, 0xff0000ff);
ui.insertBefore(1, box, 0);
const text = ui.createNode(1);
ui.setText(text, 'Coffee 咖啡'); ui.replaceText(text, '咖啡 Coffee');
ui.insertBefore(1, text, 0);
ui.setFocus(box); ui.setFocus(0);
let turns = 0, jobs = 0, lastTouch = 0, lastTouchCount = 0, lastHit = 0;
globalThis.frame = function(buttons, analog, contacts, hits) {
  if (buttons === 4) throw new Error('synthetic guest failure');
  if (new Uint8Array(__pak)[2] !== 3) throw new Error('pack lifetime');
  turns++;
  lastTouch = contacts[0] || 0;
  lastTouchCount = contacts.length;
  lastHit = (hits && hits[0]) || 0;
  Promise.resolve().then(() => {
    jobs++;
    ui.setProp(box, 1, 120);
    ui.setProp(box, 64, 0xff00ff00);
  });
};
globalThis.inspect = function(op) {
  if (op === 1) return turns;
  if (op === 2) return jobs;
  if (op === 3) return box;
  if (op === 4) return lastTouch;
  if (op === 5) { ui.removeChild(1, box); ui.destroyNode(box); return 1; }
  if (op === 6) return lastTouchCount;
  if (op === 7) return lastHit;
  return -1;
};
