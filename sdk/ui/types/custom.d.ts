/** F7-02A Surface v0 experimental custom components.
 * Built solely from public View rectangles (including axis-aligned lines).
 * Not a native Canvas/Path/Polyline API, not sandboxing untrusted JavaScript.
 */
export {};
declare global {
  interface PocketCustomRect {
    x: number; y: number; width: number; height: number;
  }
  interface PocketCustomSurface {
    readonly width: number;
    readonly height: number;
    rect(rect: PocketCustomRect, styleRef: number): number;
    move(id: number, rect: PocketCustomRect): true;
    horizontal(x: number, y: number, length: number, thickness: number, styleRef: number): number;
    vertical(x: number, y: number, length: number, thickness: number, styleRef: number): number;
    used(): number;
  }
  interface PocketCustomWidget<Props=unknown> {
    update(props: Props): boolean | void;
  }
  interface PocketCustomInstance<Props=unknown> {
    /** The root is owned/destroyed by its containing navigation page. */
    readonly root: PocketHandle;
    update(props: Props): boolean | void;
    primitives(): number;
  }
  const PocketCustom: {
    readonly version: "0.1-experimental";
    readonly limits: {readonly factories:8;readonly instances:4;readonly primitives:24};
    register<Props>(name: string,
      factory: (surface:PocketCustomSurface, props:Props)=>PocketCustomWidget<Props>):true;
    mount<Props>(name:string, ui:PocketUIContext, parent:PocketHandle,
      bounds:PocketCustomRect, props:Props):PocketCustomInstance<Props>;
  };
}
