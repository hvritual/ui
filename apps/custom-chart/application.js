/* Public SDK + reusable JS module; offline values, no device control. */
(function () {
  "use strict";
  const base = 0xfffaf8f4;
  let chart, targetHeight, ticks = 0, updates = 0;
  PocketUI.define({
    start(ui, target) {
      targetHeight = target.height;
      ui.defineTheme({id:1, foregroundToken:2, fontSize:22, colors:{
        "1":base, "2":0xff332b26, "3":0xfff0dfc8, "4":0xff2a7f98, "5":0xff65564c
      }});
      ui.styleRule(1,1,2,0); ui.styleRule(2,3,2,0);
      ui.styleRule(4,4,2,0); ui.styleRule(5,5,2,0);
      ui.selectTheme(1);
      const root = ui.createView(0, {x:0,y:0,width:1024,height:target.height}, 1);
      ui.createText(root, {x:64,y:38,width:880,height:44}, 5001, 1);
      const registry = PocketCustom.install(ui);
      PocketBarChart.register(registry);
      chart = registry.mount("example.chart.bars", root,
        {x:64,y:160,width:896,height:target.height - 240}, {values:[20,45,70,35,85,55]}, 2);
      ui.navigation.push(1, root);
      ui.publish({selected:0,itemCount:6,actions:0,theme:1,background:base});
      return true;
    },
    action() {},
    tick(ui) {
      // Deterministic next-tick simulation. The first snapshot precedes the update.
      if (++ticks === 8) {
        chart.update({values:[80,30,50,90,25,65]},
          {x:96,y:176,width:832,height:targetHeight - 272});
        updates++;
        ui.publish({selected:1,itemCount:6,actions:updates,theme:1,background:base});
      }
      return true;
    },
    inspect() { return {updates, primitives:chart ? chart.primitiveCount : 0}; }
  });
})();
