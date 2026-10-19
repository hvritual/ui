/* Reusable developer-owned example: chart bars composed from public View
 * rectangles. No renderer pointers, native plugins, or canvas pixel access.
 */
PocketCustom.register("pocket.bar-chart",function(surface,initial){
  const n=8;
  const bars=[];
  const left=28,right=16,bottom=24,top=14;
  const baseline=surface.height-bottom;
  const chartH=baseline-top;
  const stride=Math.floor((surface.width-left-right)/n);
  const barW=Math.max(1,Math.min(48,stride-14));
  if(chartH<20||stride<18)throw Error("CHART_VIEWPORT_TOO_SMALL");
  function validate(props){
    if(!props||!Array.isArray(props.samples)||props.samples.length!==n)
      throw Error("CHART_SAMPLES_REQUIRED");
    const samples=props.samples.map(value=>{
      if(!Number.isSafeInteger(value)||value<0||value>100)throw Error("CHART_SAMPLE_RANGE");
      return value;
    });
    return samples;
  }
  function position(i,sample){
    const height=Math.max(2,Math.floor(sample*chartH/100));
    return {x:left+i*stride,y:baseline-height,width:barW,height};
  }
  let values=validate(initial);
  surface.horizontal(left-10,baseline,surface.width-left-right+8,2,3);
  surface.vertical(left-10,top,chartH+2,2,3);
  for(let i=0;i<n;i++)bars.push(surface.rect(position(i,values[i]),4));
  return {
    update(next) {
      const fresh=validate(next); // Validate *all* samples before any mutation.
      for(let i=0;i<n;i++)surface.move(bars[i],position(i,fresh[i]));
      values=fresh;return true;
    }
  };
});
