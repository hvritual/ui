/** Surface v0: trusted JS factories, fixed topology, parent-owned lifetime. */
import "./index";
export {};
declare global {
  interface PocketCustomSize { readonly width: number; readonly height: number; }
  interface PocketCustomRectangle extends PocketRect { kind: "rectangle"; styleRef: number; }
  /** Half-open segment; positive-side thickness must fit within the surface. */
  interface PocketCustomLine {
    kind: "line"; x1: number; y1: number; x2: number; y2: number;
    thickness: number; styleRef: number;
  }
  type PocketCustomPrimitive = PocketCustomRectangle | PocketCustomLine;
  type PocketCustomFactory<P> = (size: PocketCustomSize, props: P) => readonly PocketCustomPrimitive[];
  interface PocketCustomInstance<P> {
    readonly primitiveCount: number;
    /** Same count, kinds, styles and handles; validates all geometry before mutation. */
    update(props: P, rect?: PocketRect): void;
  }
  interface PocketCustomRegistry {
    register<P>(namespace: string, factory: PocketCustomFactory<P>): void;
    /** P must match the registered factory's props. Registry does runtime validation. */
    mount<P>(namespace: string, parent: PocketHandle, rect: PocketRect,
      props: P, backgroundStyleRef: number): PocketCustomInstance<P>;
  }
  const PocketCustom: {
    readonly version: "0-experimental";
    readonly maxPrimitives: 32;
    /** Exactly once for each application UI context. No native bridge is installed. */
    install(ui: PocketUIContext): PocketCustomRegistry;
  };
}
