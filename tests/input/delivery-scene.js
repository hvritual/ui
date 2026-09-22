// Real PocketJS contact-delivery fixture for P3-02.
if (ui.__host !== 'linux-headless' || ui.__hostAbi !== 1) throw new Error('host');
ui.setProp(1,64,0xff181410);
const box=ui.createNode(0);
ui.setProp(box,24,1);ui.setProp(box,28,0);ui.setProp(box,25,0);
ui.setProp(box,1,100);ui.setProp(box,2,100);ui.setProp(box,64,0xff224466);
ui.insertBefore(1,box,0);
let lastTouch=0,lastHit=-1,frames=0;
globalThis.frame=function(buttons,analog,contacts,hits){
  frames++;
  lastTouch=(contacts&&contacts.length)?(contacts[0]>>>0):0;
  lastHit=(hits&&hits.length)?hits[0]:-1;
};
globalThis.inspect=function(op){
  if(op===1)return lastTouch|0;
  if(op===2)return lastHit|0;
  if(op===3)return box|0;
  if(op===4)return frames|0;
  return -1;
};
