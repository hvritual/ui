/* Pocket UI Public SDK experimental facade v0.1 — F7-01.
 * Only documented operations are reachable here. Runtime/renderer/IPC transport
 * remain separately owned. This is not a trusted-code sandbox or production ABI.
 */
(function (global) {
  "use strict";
  if (typeof global.__pocketCall !== "function") throw Error("POCKET_UI_HOST_REQUIRED");
  if (global.PocketUI !== undefined) throw Error("POCKET_UI_DUPLICATE_SDK");
  const bridge = global.__pocketCall;
  const TYPES = Object.freeze({View:1, Text:2, Image:3, Button:5});
  const ACTION_QUEUE_LIMIT = 32;
  let defined = false;
  function integer(n, low, high, where) {
    if (!Number.isSafeInteger(n) || n < low || n > high) throw Error("POCKET_UI_INVALID_" + where);
    return n;
  }
  function optional(n, low, high, fallback, where) {
    return n === undefined ? fallback : integer(n, low, high, where);
  }
  function rect(box) {
    if (!box || typeof box !== "object" || Array.isArray(box)) throw Error("POCKET_UI_INVALID_RECT");
    return [
      integer(box.x, -65536, 65536, "X"), integer(box.y, -65536, 65536, "Y"),
      integer(box.width, 0, 65536, "WIDTH"), integer(box.height, 0, 65536, "HEIGHT")
    ];
  }
  function send(op, args) {
    const result = bridge.apply(undefined, [op].concat(args));
    if (!Number.isSafeInteger(result) || result === 0) throw Error("POCKET_UI_HOST_REJECTED:" + op);
    return result;
  }
  function create(kind, parent, box, styleRef, textRef, imageRef) {
    integer(parent, 0, Number.MAX_SAFE_INTEGER, "PARENT");
    const dims = rect(box);
    return send("component.create", [
      TYPES[kind], parent, ...dims,
      optional(styleRef, 0, 65535, 1, "STYLE"),
      optional(textRef, 0, 65535, 0, "TEXTREF"),
      optional(imageRef, 0, 8, 0, "IMAGE")
    ]);
  }
  const ui = Object.freeze({
    createView(parent, box, styleRef) { return create("View", parent, box, styleRef, 0, 0); },
    createText(parent, box, textRef, styleRef) { return create("Text", parent, box, styleRef, textRef, 0); },
    createButton(parent, box, textRef, styleRef, actionId) {
      const component = create("Button", parent, box, styleRef, textRef, 0);
      send("event.bind", [component, integer(actionId, 1, 4095, "ACTION"), 1]);
      return component;
    },
    setText(component, textRef) {
      return send("component.text", [integer(component, 1, Number.MAX_SAFE_INTEGER, "HANDLE"),
        integer(textRef, 0, 65535, "TEXTREF")]);
    },
    setLayout(component, box) {
      return send("component.layout", [integer(component, 1, Number.MAX_SAFE_INTEGER, "HANDLE"), ...rect(box)]);
    },
    setPolyline(component, size, points, styleRef) {
      integer(component,1,Number.MAX_SAFE_INTEGER,"HANDLE");
      const width=integer(size && size.width,1,1024,"DRAW_WIDTH");
      const height=integer(size && size.height,1,800,"DRAW_HEIGHT");
      if(width*height>262144 || !Array.isArray(points) || points.length<2 || points.length>4096)
        throw Error("POCKET_UI_DRAW_BUDGET");
      integer(styleRef,1,65535,"STYLE");
      const chunks=[];
      let chunk="";
      for(let i=0;i<points.length;i++) {
        const p=points[i];
        if(!p || typeof p!=="object" || Object.keys(p).length!==2 ||
           !Object.prototype.hasOwnProperty.call(p,"x") || !Object.prototype.hasOwnProperty.call(p,"y"))
          throw Error("POCKET_UI_DRAW_POINT");
        const x=integer(p.x,0,width-1,"DRAW_X"),y=integer(p.y,0,height-1,"DRAW_Y");
        chunk+=x.toString(16).padStart(4,"0")+y.toString(16).padStart(4,"0");
        if(chunk.length===4096 || i===points.length-1){chunks.push(chunk);chunk="";}
      }
      send("draw.begin",[component,1,width,height,points.length,styleRef]);
      let offset=0;
      for(const text of chunks){send("draw.points",[component,offset,text]);offset+=text.length/8;}
      return send("draw.commit",[component]);
    },
    defineTheme(theme) {
      if (!theme || typeof theme !== "object" || !theme.colors || typeof theme.colors !== "object" ||
          Array.isArray(theme.colors)) throw Error("POCKET_UI_INVALID_THEME");
      const entries = Object.keys(theme.colors).map(key =>
        [integer(Number(key), 1, 65535, "TOKEN"), integer(theme.colors[key], 0, 0xffffffff, "COLOR")]);
      if (entries.length < 1 || entries.length > 14 ||
          new Set(entries.map(e => e[0])).size !== entries.length) throw Error("POCKET_UI_INVALID_TOKENS");
      entries.sort((a,b) => a[0]-b[0]);
      const args = [
        integer(theme.id, 1, 16, "THEME_ID"),
        integer(theme.foregroundToken, 1, 65535, "FOREGROUND_TOKEN"),
        integer(theme.fontSize, 1, 128, "FONT_SIZE")
      ];
      for (const pair of entries) args.push(pair[0], pair[1]);
      return send("theme.define", args);
    },
    styleRule(styleRef, backgroundToken, foregroundToken, radius) {
      return send("style.rule", [
        integer(styleRef, 1, 65535, "STYLE_REF"),
        integer(backgroundToken, 1, 65535, "BACKGROUND_TOKEN"),
        integer(foregroundToken, 1, 65535, "FOREGROUND_TOKEN"),
        integer(radius, 0, 2048, "RADIUS")
      ]);
    },
    selectTheme(themeId) { return send("theme.select", [integer(themeId, 1, 16, "THEME")]); },
    navigation: Object.freeze({
      push(route, root) { return send("navigation.push", [integer(route, 1, 32, "ROUTE"), integer(root, 1, Number.MAX_SAFE_INTEGER, "HANDLE")]); },
      replace(route, root) { return send("navigation.replace", [integer(route, 1, 32, "ROUTE"), integer(root, 1, Number.MAX_SAFE_INTEGER, "HANDLE")]); },
      pop() { return send("navigation.pop", []); }
    }),
    publish(data) {
      if (!data || typeof data !== "object" || Array.isArray(data)) throw Error("POCKET_UI_INVALID_FRAME");
      const fields = [
        ["selected",0,4096,0],["first",0,4096,0],["itemCount",0,4096,0],
        ["locale",0,5,0],["theme",0,15,1],["progress",0,100,0],
        ["completed",0,0xffffffff,0],["actions",0,0xffffffff,0],
        ["scrollX",-65536,65536,0],["dragging",0,1,0],["settling",0,1,0],
        ["background",0,0xffffffff,0xfffaf8f4],["mediaReady",0,1,1]
      ];
      const values = fields.map(f => optional(data[f[0]],f[1],f[2],f[3],f[0]));
      return send("frame.state",values);
    },
    invalidateLayout() { return send("layout.invalidate", []); }
  });
  function define(spec) {
    if (defined) throw Error("POCKET_UI_APP_ALREADY_DEFINED");
    if (!spec || typeof spec.start !== "function" || typeof spec.action !== "function" ||
        (spec.tick !== undefined && typeof spec.tick !== "function") ||
        (spec.inspect !== undefined && typeof spec.inspect !== "function")) throw Error("POCKET_UI_INVALID_APP");
    defined = true;
    const pending = [];
    global.PocketApplication = {
      start(config) {
        if (!config || ![600,800].includes(config.height)) throw Error("POCKET_UI_INVALID_TARGET");
        return spec.start(ui, Object.freeze({width:1024,height:config.height})) !== false;
      },
      event(event) {
        if (!event || event.type !== 1101 || event.phase !== 2 || !Number.isSafeInteger(event.token) ||
            event.token < 1 || event.token > 4095) return 0;
        if (pending.length >= ACTION_QUEUE_LIMIT) throw Error("POCKET_UI_ACTION_OVERFLOW");
        pending.push(event.token);
        return 1;
      },
      tick() {
        // Native actions are queued by F6; no UI tree mutation inside borrowed callbacks.
        let budget = ACTION_QUEUE_LIMIT;
        while (pending.length) {
          if (--budget < 0) throw Error("POCKET_UI_ACTION_BUDGET");
          spec.action(ui, pending.shift());
        }
        return !spec.tick || spec.tick(ui) !== false;
      },
      inspect() { return spec.inspect ? spec.inspect() : {}; }
    };
    return true;
  }
  Object.defineProperty(global, "PocketUI", {value:Object.freeze({define,version:"0.1-experimental"}),
                                            enumerable:true,configurable:false,writable:false});
})(globalThis);
