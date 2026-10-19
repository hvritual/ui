/* PocketCharts experimental ECharts-like bounded series API.
 * Developer JS holds data; native Chart owns 1000-point raster, no 1000 Views.
 * Public operations only, no framebuffer/canvas/raw __pocketCall access.
 */
(function(global){
 "use strict";
 if(typeof global.PocketUI!=="object"||!global.PocketUI||
    global.PocketUI.version!=="0.1-experimental")
    throw Error("POCKET_CHART_SDK_REQUIRED");
 if(global.PocketCharts!==undefined)throw Error("POCKET_CHART_DUPLICATE");
 const MAX_POINTS=1000,MAX_SERIES=4,MAX_INSTANCES=2;
 const modes=Object.freeze({line:1,bar:2,scatter:3,bubble:4});
 const alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 const adapters=new Map();
 let instances=0;
 function finite(n,where){if(typeof n!=="number"||!Number.isFinite(n))throw Error("POCKET_CHART_"+where);return n;}
 function options(value){
  if(!value||typeof value!=="object"||Array.isArray(value))throw Error("POCKET_CHART_OPTIONS");
  let mode=modes[value.type],points=value.data;
  if(!mode&&adapters.has(value.type)){
   const entry=adapters.get(value.type);
   points=entry.transform(value.data);
   mode=modes[entry.base];
  }
  if(!mode)throw Error("POCKET_CHART_TYPE");
  if(!Array.isArray(points)||points.length<1||points.length>MAX_POINTS)
    throw Error("POCKET_CHART_POINT_BUDGET");
  if(typeof value.color!=="number"||!Number.isInteger(value.color)||
     value.color<0||value.color>0xffffffff)throw Error("POCKET_CHART_COLOR");
  const domain=value.xAxis||{};
  const ordinate=value.yAxis||{};
  function range(rangeValue,lower,upper,axis){
   const start=rangeValue.min===undefined?lower:finite(rangeValue.min,axis);
   const end=rangeValue.max===undefined?upper:finite(rangeValue.max,axis);
   if(!(end>start)||end-start>1e12)throw Error("POCKET_CHART_AXIS");
   return [start,end];
  }
  const [xmin,xmax]=range(domain,0,10000,"X_AXIS");
  const [ymin,ymax]=range(ordinate,0,10000,"Y_AXIS");
  const raw=new Uint8Array(points.length*5);
  function coordinate(n,start,end,what){
   finite(n,what);
   if(n<start||n>end)throw Error("POCKET_CHART_OUT_OF_RANGE");
   const mapped=Math.round(10000*(n-start)/(end-start));
   if(!Number.isSafeInteger(mapped)||mapped<0||mapped>10000)throw Error("POCKET_CHART_AXIS_RANGE");
   return mapped;
  }
  for(let i=0;i<points.length;i++){
   let p=points[i];
   if(typeof p==="number")p={x:points.length===1?xmin:xmin+i*(xmax-xmin)/(points.length-1),y:p};
   if(!p||typeof p!=="object"||Array.isArray(p))throw Error("POCKET_CHART_POINT");
   const x=coordinate(p.x,xmin,xmax,"X");
   const y=coordinate(p.y,ymin,ymax,"Y");
   const z=mode===4?Math.round(finite(p.size,"SIZE")):0;
   if(z<0||z>255||!Number.isSafeInteger(z))throw Error("POCKET_CHART_SIZE");
   const o=i*5;
   raw[o]=x&255;raw[o+1]=(x>>>8)&255;raw[o+2]=y&255;raw[o+3]=(y>>>8)&255;raw[o+4]=z;
  }
  let encoded="";
  for(let i=0;i<raw.length;i+=3){
   const a=raw[i],b=i+1<raw.length?raw[i+1]:0,c=i+2<raw.length?raw[i+2]:0;
   encoded+=alphabet[a>>>2]+alphabet[((a&3)<<4)|(b>>>4)];
   encoded+=i+1<raw.length?alphabet[((b&15)<<2)|(c>>>6)]:"=";
   encoded+=i+2<raw.length?alphabet[c&63]:"=";
  }
  if(encoded.length>6668)throw Error("POCKET_CHART_PAYLOAD_BUDGET");
  return {mode,color:value.color,encoded,points:points.length};
 }
 function registerSeries(name,base,transform){
  if(typeof name!=="string"||!/^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$/.test(name)||
     name.length>64||modes[name]||adapters.has(name))throw Error("POCKET_CHART_SERIES_NAME");
  if(adapters.size>=MAX_SERIES)throw Error("POCKET_CHART_SERIES_BUDGET");
  if(typeof transform!=="function"||!modes[base])throw Error("POCKET_CHART_SERIES_ADAPTER");
  adapters.set(name,{base,transform});return true;
 }
 function mount(ui,parent,bounds,configuration){
  if(instances>=MAX_INSTANCES)throw Error("POCKET_CHART_INSTANCE_BUDGET");
  if(!ui||typeof ui.createChart!=="function"||typeof ui.setChartData!=="function")
    throw Error("POCKET_CHART_UI");
  if(!bounds||!Number.isSafeInteger(bounds.width)||!Number.isSafeInteger(bounds.height)||
     bounds.width<64||bounds.width>768||bounds.height<64||bounds.height>320)
    throw Error("POCKET_CHART_BOUNDS");
  const initial=options(configuration); // entire dataset validated BEFORE chart creation
  const handle=ui.createChart(parent,bounds,1);
  ui.setChartData(handle,initial.mode,initial.color,initial.encoded);
  instances++;
  let size=initial.points;
  return Object.freeze({
   handle,
   setOption(next){
    const data=options(next); // malformed updates never touch Native
    ui.setChartData(handle,data.mode,data.color,data.encoded);
    size=data.points;return true;
   },
   pointCount(){return size;}
  });
 }
 Object.defineProperty(global,"PocketCharts",{
  value:Object.freeze({mount,registerSeries,version:"0.2-experimental",
    limits:Object.freeze({series:4,instances:2,pointsPerSeries:MAX_POINTS,
                         canvasWidth:768,canvasHeight:320})}),
  enumerable:true,configurable:false,writable:false
 });
})(globalThis);
