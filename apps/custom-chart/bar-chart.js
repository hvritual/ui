/* Independent reusable module. Factories receive size and data, never native handles. */
(function (global) {
  "use strict";
  function register(registry) {
    registry.register("example.chart.bars", (size, props) => {
      if (!props || !Array.isArray(props.values) || props.values.length < 1 ||
          props.values.length > 30 || props.values.some(n => !Number.isFinite(n) || n < 0 || n > 100))
        throw Error("BAR_CHART_VALUES");
      const count = props.values.length;
      const step = Math.floor((size.width - 32) / count);
      const plotHeight = size.height - 32;
      if (step < 4 || plotHeight < 1) throw Error("BAR_CHART_SIZE");
      const shapes = props.values.map((value, i) => {
        const height = Math.round(plotHeight * value / 100);
        return {kind:"rectangle", x:24 + i * step, y:8 + plotHeight - height,
          width:step - 4, height, styleRef:4};
      });
      shapes.push({kind:"line", x1:16, y1:8, x2:16, y2:size.height - 16, thickness:2, styleRef:5});
      shapes.push({kind:"line", x1:16, y1:size.height - 16, x2:size.width - 8,
        y2:size.height - 16, thickness:2, styleRef:5});
      return shapes;
    });
  }
  Object.defineProperty(global, "PocketBarChart", {value:Object.freeze({register})});
})(globalThis);
