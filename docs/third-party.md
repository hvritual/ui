# Third-party source provenance

- PocketJS reference: `pocket-stack/pocketjs@53a17f6416c3333f1171141bf996695720101ed2`, https://github.com/pocket-stack/pocketjs/tree/53a17f6416c3333f1171141bf996695720101ed2 . Not built or vendored in P0.
- Native C-source pin reference: https://github.com/pocket-stack/pocketjs/blob/53a17f6416c3333f1171141bf996695720101ed2/tools/3ds-toolchain.ts . See the QuickJS pin and source list, not the 3DS target flags.
- QuickJS source: `pocket-stack/quickjs-rs@ba5bdd0dc013518768e76cd9e05cd30ed53dd35b`, `libquickjs-sys/embed/quickjs`, VERSION `2026-06-04`.
- The build copies that source tree's unmodified `LICENSE` to `out/quickjs-LICENSE.txt` and hashes it with the archive. Preserve it when redistributing the resulting executable/library.
- Portable runtime interface for P1: https://github.com/pocket-stack/pocketjs/blob/53a17f6416c3333f1171141bf996695720101ed2/engine/quickjs-c/README.md .
- Rust target support reference: https://doc.rust-lang.org/rustc/platform-support.html . Target support does not certify the libc/kernel of the user's board.

No fonts, drink images or other third-party UI assets are included by P0. Future asset licensing is tracked in P4/P8.
