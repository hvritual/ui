/* PocketCustom Surface v0 — experimental component composition, no renderer ABI.
 * Explicit bound: rectangles and axis-aligned lines only. Rectangles are mapped
 * to current public View primitives; arbitrary polyline/arc/shader is NOT admitted.
 * This module is trusted app code, NOT an untrusted-code sandbox.
 */
(function (global) {
  "use strict";
  if (typeof global.PocketUI !== "object" || !global.PocketUI ||
      global.PocketUI.version !== "0.1-experimental") throw Error("POCKET_CUSTOM_SDK_REQUIRED");
  if (global.PocketCustom !== undefined) throw Error("POCKET_CUSTOM_DUPLICATE");
  const MAX_FACTORIES=8, MAX_INSTANCES=4, MAX_PRIMITIVES=24;
  const registry=new Map(),instances=[];
  function number(n,min,max,code){
    if(!Number.isSafeInteger(n)||n<min||n>max)throw Error("POCKET_CUSTOM_"+code);
    return n;
  }
  function region(box,maxW,maxH){
    if(!box||typeof box!=="object"||Array.isArray(box))throw Error("POCKET_CUSTOM_RECT");
    const x=number(box.x,0,maxW,"X"),y=number(box.y,0,maxH,"Y");
    const width=number(box.width,1,maxW,"WIDTH"),height=number(box.height,1,maxH,"HEIGHT");
    if(width>maxW-x||height>maxH-y)throw Error("POCKET_CUSTOM_BOUNDS");
    return Object.freeze({x,y,width,height});
  }
  function style(n){return number(n,1,65535,"STYLE");}
  function createSurface(ui,parent,bounds){
    const size=region({x:0,y:0,width:bounds.width,height:bounds.height},65536,65536);
    const entries=[];
    const root=ui.createView(parent,bounds,1);
    const api=Object.freeze({
      width:size.width,height:size.height,
      rect(box,styleRef){
        if(entries.length>=MAX_PRIMITIVES)throw Error("POCKET_CUSTOM_PRIMITIVE_BUDGET");
        const shape=region(box,size.width,size.height);
        const pigment=style(styleRef);
        const handle=ui.createView(root,shape,pigment);
        entries.push(handle);
        return entries.length;
      },
      move(id,box){
        const index=number(id,1,entries.length,"PRIMITIVE_ID")-1;
        const shape=region(box,size.width,size.height);
        ui.setLayout(entries[index],shape);
        return true;
      },
      horizontal(x,y,length,thickness,styleRef){
        return this.rect({x,y,width:length,height:thickness},styleRef);
      },
      vertical(x,y,length,thickness,styleRef){
        return this.rect({x,y,width:thickness,height:length},styleRef);
      },
      used(){return entries.length;}
    });
    return {root,api};
  }
  function register(name,factory){
    if(typeof name!=="string"||name.length>64||!/^([a-z][a-z0-9-]*\.)+[a-z][a-z0-9-]*$/.test(name))
      throw Error("POCKET_CUSTOM_NAME");
    if(typeof factory!=="function")throw Error("POCKET_CUSTOM_FACTORY");
    if(registry.has(name))throw Error("POCKET_CUSTOM_ALREADY_REGISTERED");
    if(registry.size>=MAX_FACTORIES)throw Error("POCKET_CUSTOM_FACTORY_BUDGET");
    registry.set(name,factory);
    return true;
  }
  function mount(name,ui,parent,bounds,props){
    const factory=registry.get(name);
    if(!factory)throw Error("POCKET_CUSTOM_UNKNOWN");
    if(instances.length>=MAX_INSTANCES)throw Error("POCKET_CUSTOM_INSTANCE_BUDGET");
    if(!ui||typeof ui.createView!=="function"||typeof ui.setLayout!=="function")throw Error("POCKET_CUSTOM_UI_REQUIRED");
    number(parent,1,Number.MAX_SAFE_INTEGER,"PARENT");
    const box=region(bounds,65536,65536);
    const surface=createSurface(ui,parent,box);
    const widget=factory(surface.api,props);
    if(!widget||typeof widget.update!=="function")throw Error("POCKET_CUSTOM_UPDATE_REQUIRED");
    const object=Object.freeze({
      root:surface.root,
      update(next){return widget.update(next);},
      primitives(){return surface.api.used();}
    });
    instances.push(object);
    return object;
  }
  Object.defineProperty(global,"PocketCustom",{
    value:Object.freeze({register,mount,version:"0.1-experimental",limits:Object.freeze({factories:8,instances:4,primitives:24})}),
    enumerable:true,configurable:false,writable:false
  });
})(globalThis);
