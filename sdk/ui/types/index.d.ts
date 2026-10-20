/** Pocket UI F7-01 public SDK v0.1-experimental.
 * Type checking is advisory: this repository currently packages JavaScript,
 * not TypeScript source. The loader's R3 sdk_api=1 is NOT Public SDK 1.0.
 */
export {};
declare global {
  interface PocketRect {
    x: number; y: number; width: number; height: number;
  }
  /** Opaque, runtime-owned handle. Never forge or retain across app restarts. */
  type PocketHandle = number;
  interface PocketTheme {
    id: number;
    foregroundToken: number;
    fontSize: number;
    colors: Record<string, number>;
  }
  interface PocketFrame {
    selected?: number; first?: number; itemCount?: number;
    locale?: number; theme?: number; progress?: number;
    completed?: number; actions?: number; scrollX?: number;
    dragging?: 0 | 1; settling?: 0 | 1;
    background?: number; mediaReady?: 0 | 1;
  }
  interface PocketTarget { readonly width: 1024; readonly height: 600 | 800; }
  /** Mutations are only valid in start/action/tick, never inside F6 event. */
  interface PocketUIContext {
    createView(parent: PocketHandle | 0, rect: PocketRect, styleRef?: number): PocketHandle;
    createText(parent: PocketHandle, rect: PocketRect, textRef: number, styleRef?: number): PocketHandle;
    createButton(parent: PocketHandle, rect: PocketRect, textRef: number, styleRef: number, actionId: number): PocketHandle;
    setText(handle: PocketHandle, textRef: number): number;
    setLayout(handle: PocketHandle, rect: PocketRect): number;
    /** Bounded v1 polyline. 2..4096 integer pixels, <=262144 pixel surface. */
    setPolyline(handle: PocketHandle, size: {width:number;height:number},
      points: readonly {x:number;y:number}[], styleRef: number): number;
    defineTheme(theme: PocketTheme): number;
    styleRule(styleRef: number, backgroundToken: number, foregroundToken: number, radius: number): number;
    selectTheme(themeId: number): number;
    readonly navigation: {
      push(routeId: number, root: PocketHandle): number;
      replace(routeId: number, root: PocketHandle): number;
      pop(): number;
    };
    publish(frame: PocketFrame): number;
    invalidateLayout(): number;
  }
  interface PocketApp {
    start(ui: PocketUIContext, target: PocketTarget): boolean | void;
    action(ui: PocketUIContext, actionId: number): void;
    tick?(ui: PocketUIContext): boolean | void;
    /** Metadata only. Never return private input, preedit or credentials. */
    inspect?(): Record<string, number>;
  }
  const PocketUI: {
    readonly version: "0.1-experimental";
    define(spec: PocketApp): true;
  };
}
