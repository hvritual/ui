/* A developer-owned chart module, independent of engine/native implementations.
 * ECharts-like setOption subset: line data, fixed y axis, zoom and theme styles.
 * Every sample in the visible domain becomes a real polyline vertex, no binning.
 */
(function(global) {
  "use strict";
  function register(registry) {
    registry.register("developer.charts.line",(size,option)=>{
      if(!option || !Array.isArray(option.series) || option.series.length!==1 ||
         option.series[0].type!=="line")throw Error("CHART_SERIES");
      const data=option.series[0].data;
      if(!Array.isArray(data)||data.length<2||data.length>4096)throw Error("CHART_DATA_BUDGET");
      for(let i=0;i<data.length;i++)if(!Number.isFinite(data[i]))throw Error("CHART_DATA");
      const min=option.yAxis && option.yAxis.min,max=option.yAxis && option.yAxis.max;
      if(!Number.isFinite(min)||!Number.isFinite(max)||min>=max||!Number.isFinite(max-min))
        throw Error("CHART_AXIS");
      const zoom=option.dataZoom || {start:0,end:100};
      if(!Number.isFinite(zoom.start)||!Number.isFinite(zoom.end)||zoom.start<0||zoom.end>100||zoom.start>=zoom.end)
        throw Error("CHART_ZOOM");
      const first=Math.floor((data.length-1)*zoom.start/100),last=Math.ceil((data.length-1)*zoom.end/100);
      if(size.width<64||size.height<64)throw Error("CHART_SIZE");
      const points=[];
      for(let i=first;i<=last;i++) {
        const ratio=Math.max(0,Math.min(1,(data[i]-min)/(max-min)));
        points.push({x:24+Math.round((size.width-48)*(i-first)/(last-first)),
          y:size.height-24-Math.round((size.height-48)*ratio)});
      }
      return [
        {kind:"polyline",points,styleRef:4},
        {kind:"line",x1:16,y1:8,x2:16,y2:size.height-16,thickness:1,styleRef:5},
        {kind:"line",x1:16,y1:size.height-16,x2:size.width-8,y2:size.height-16,thickness:1,styleRef:5}
      ];
    });
  }
  function init(registry,parent,rect) {
    let instance=null,currentRect={...rect};
    return Object.freeze({
      setOption(option) {
        if(instance)instance.update(option,currentRect);
        else instance=registry.mount("developer.charts.line",parent,currentRect,option,2);
      },
      resize(rect,option) {
        if(!instance)throw Error("CHART_NOT_MOUNTED");
        instance.update(option,rect);currentRect={...rect};
      }
    });
  }
  Object.defineProperty(global,"PocketCharts",{value:Object.freeze({register,init})});
})(globalThis);
