/** F7-02B experimental bounded chart API (not complete Apache ECharts compatibility).
 * Uses existing PocketUI runtime with opt-in ui.draw.chart capability.
 */
export {};
declare global {
  interface PocketChartPoint {
    x: number;
    y: number;
    /** Bubble size 0..255 (ignored by line/bar/scatter). */
    size?: number;
  }
  interface PocketChartScale {min?:number;max?:number;}
  interface PocketChartOption {
    type: "line" | "bar" | "scatter" | "bubble" | string;
    /** One series contains 1..1000 points. A line/bar can also supply numeric y values. */
    data: ReadonlyArray<PocketChartPoint | number>;
    /** Color is the existing project uint32 packed RGBA semantic token. */
    color: number;
    xAxis?:PocketChartScale;
    yAxis?:PocketChartScale;
  }
  interface PocketChartInstance {
    /** Native Chart component, not a pointer to PixelBuffer. */
    readonly handle: PocketHandle;
    setOption(option:PocketChartOption):true;
    pointCount():number;
  }
  const PocketCharts: {
    readonly version:"0.2-experimental";
    readonly limits: {
      readonly series:4;
      readonly instances:2;
      readonly pointsPerSeries:1000;
      readonly canvasWidth:768;
      readonly canvasHeight:320;
    };
    registerSeries(name:string,base:"line"|"bar"|"scatter"|"bubble",
                   transformer:(input:unknown)=>ReadonlyArray<PocketChartPoint>):true;
    mount(ui:PocketUIContext,parent:PocketHandle,bounds:PocketRect,
          option:PocketChartOption):PocketChartInstance;
  };
}
