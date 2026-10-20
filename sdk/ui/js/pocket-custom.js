/* PocketCustom Surface v0. Trusted development modules, using public UI only.
 * Geometry is bounded; this is neither a renderer nor an untrusted-code sandbox.
 */
(function (global) {
  "use strict";
  if (!global.PocketUI) throw Error("POCKET_CUSTOM_UI_REQUIRED");
  if (global.PocketCustom !== undefined) throw Error("POCKET_CUSTOM_DUPLICATE_SDK");
  const installed = new WeakSet();
  const LIMIT = 32;
  function reject(ok, code) { if (!ok) throw Error("POCKET_CUSTOM_" + code); }
  function integer(n, low, high) {
    reject(Number.isSafeInteger(n) && n >= low && n <= high, "COORDINATE");
    return n;
  }
  function exact(value, keys) {
    reject(value && typeof value === "object" && !Array.isArray(value) &&
      Object.keys(value).length === keys.length && keys.every(k => Object.prototype.hasOwnProperty.call(value, k)),
      "FIELDS");
  }
  function box(value) {
    exact(value, ["x", "y", "width", "height"]);
    return Object.freeze({x:integer(value.x, 0, 65536), y:integer(value.y, 0, 65536),
      width:integer(value.width, 1, 65536), height:integer(value.height, 1, 65536)});
  }
  function install(ui) {
    reject(ui && typeof ui === "object" && typeof ui.createView === "function" &&
      typeof ui.setLayout === "function" && typeof ui.invalidateLayout === "function", "CONTEXT");
    reject(!installed.has(ui), "DUPLICATE_INSTALL");
    installed.add(ui);
    const factories = new Map();
    let busy = false;
    function exclusive(fn) {
      reject(!busy, "REENTRANT");
      busy = true;
      try { return fn(); } finally { busy = false; }
    }
    function prepare(factory, bounds, props) {
      const list = factory(Object.freeze({width:bounds.width, height:bounds.height}), props);
      reject(Array.isArray(list) && list.length >= 1 && list.length <= LIMIT, "PRIMITIVE_BUDGET");
      let pointBudget=4096;
      // Copy every record before any host mutation; callers cannot retain mutable geometry.
      return Array.from(list, primitive => {
        reject(primitive && typeof primitive === "object", "PRIMITIVE");
        if(primitive.kind === "polyline") {
          exact(primitive,["kind","points","styleRef"]);
          reject(typeof ui.setPolyline === "function", "DRAW_HOST_REQUIRED");
          reject(bounds.width<=1024 && bounds.height<=800 && bounds.width*bounds.height<=262144,
            "SURFACE_BUDGET");
          const points=primitive.points;
          reject(Array.isArray(points) && points.length>=2 && points.length<=pointBudget,"POINT_BUDGET");
          pointBudget-=points.length;
          const copied=Array.from(points,p=>{
            exact(p,["x","y"]);
            return Object.freeze({x:integer(p.x,0,bounds.width-1),y:integer(p.y,0,bounds.height-1)});
          });
          return Object.freeze({kind:"polyline",styleRef:integer(primitive.styleRef,1,65535),
            geometry:Object.freeze({x:0,y:0,width:bounds.width,height:bounds.height}),points:Object.freeze(copied)});
        }
        let x, y, width, height;
        if (primitive.kind === "rectangle") {
          exact(primitive, ["kind", "x", "y", "width", "height", "styleRef"]);
          x = primitive.x; y = primitive.y; width = primitive.width; height = primitive.height;
        } else {
          reject(primitive.kind === "line", "PRIMITIVE");
          exact(primitive, ["kind", "x1", "y1", "x2", "y2", "thickness", "styleRef"]);
          const x1 = integer(primitive.x1, 0, bounds.width);
          const x2 = integer(primitive.x2, 0, bounds.width);
          const y1 = integer(primitive.y1, 0, bounds.height);
          const y2 = integer(primitive.y2, 0, bounds.height);
          const thickness = integer(primitive.thickness, 1, 65536);
          reject((x1 === x2) !== (y1 === y2), "AXIS_LINE");
          x = Math.min(x1, x2); y = Math.min(y1, y2);
          // A half-open axis segment extends thickness toward positive x or y.
          width = x1 === x2 ? thickness : Math.abs(x2 - x1);
          height = y1 === y2 ? thickness : Math.abs(y2 - y1);
        }
        const geometry = Object.freeze({x:integer(x, 0, bounds.width), y:integer(y, 0, bounds.height),
          width:integer(width, 0, bounds.width), height:integer(height, 0, bounds.height)});
        reject(geometry.x + geometry.width <= bounds.width &&
          geometry.y + geometry.height <= bounds.height, "OUT_OF_BOUNDS");
        return Object.freeze({kind:primitive.kind, styleRef:integer(primitive.styleRef, 1, 65535), geometry});
      });
    }
    return Object.freeze({
      register(name, factory) {
        return exclusive(() => {
          reject(typeof name === "string" && name.length <= 96 &&
            /^[a-z][a-z0-9]*(?:[.-][a-z][a-z0-9]*)+$/.test(name), "NAMESPACE");
          reject(typeof factory === "function", "FACTORY");
          reject(!factories.has(name), "DUPLICATE_COMPONENT");
          reject(factories.size < LIMIT, "REGISTRY_BUDGET");
          factories.set(name, factory);
        });
      },
      mount(name, parent, rect, props, backgroundStyleRef) {
        return exclusive(() => {
          reject(factories.has(name), "UNKNOWN_COMPONENT");
          integer(parent, 1, Number.MAX_SAFE_INTEGER);
          integer(backgroundStyleRef, 1, 65535);
          const factory = factories.get(name);
          let bounds = box(rect);
          let records = prepare(factory, bounds, props);
          const root = ui.createView(parent, bounds, backgroundStyleRef);
          const handles = records.map(p => {
            const handle=ui.createView(root,p.geometry,p.kind==="polyline"?backgroundStyleRef:p.styleRef);
            if(p.kind==="polyline")ui.setPolyline(handle,p.geometry,p.points,p.styleRef);
            return handle;
          });
          let failed = false;
          // Native owner/page teardown remains authoritative. No independent destroy or handles.
          return Object.freeze({
            primitiveCount:records.length,
            update(nextProps, nextRect) {
              return exclusive(() => {
                reject(!failed, "FAILED_INSTANCE");
                const nextBounds = nextRect === undefined ? bounds : box(nextRect);
                const next = prepare(factory, nextBounds, nextProps);
                reject(next.length === records.length && next.every((p, i) =>
                  p.kind === records[i].kind && p.styleRef === records[i].styleRef), "TOPOLOGY");
                try {
                  ui.setLayout(root, nextBounds);
                  next.forEach((p, i) => {
                    ui.setLayout(handles[i], p.geometry);
                    if(p.kind==="polyline")ui.setPolyline(handles[i],p.geometry,p.points,p.styleRef);
                  });
                  ui.invalidateLayout();
                } catch (error) {
                  // No rollback command exists. Never reuse a partially failed or stale instance.
                  failed = true;
                  throw error;
                }
                bounds = nextBounds; records = next;
              });
            }
          });
        });
      }
    });
  }
  Object.defineProperty(global, "PocketCustom", {value:Object.freeze({install, version:"0-experimental",
    maxPrimitives:LIMIT}), enumerable:true, configurable:false, writable:false});
})(globalThis);
