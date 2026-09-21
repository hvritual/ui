use std::{env, path::Path};

fn main() {
    println!("cargo:rerun-if-env-changed=QUICKJS_LIB_DIR");
    let directory = env::var("QUICKJS_LIB_DIR").expect("run make build to compile pinned QuickJS first");
    let archive = Path::new(&directory).join("libquickjs_smoke.a");
    assert!(archive.is_file(), "missing QuickJS archive: {}", archive.display());
    println!("cargo:rerun-if-changed={}", archive.display());
    println!("cargo:rustc-link-search=native={directory}");
    println!("cargo:rustc-link-lib=static=quickjs_smoke");
    for library in ["m", "dl", "pthread", "atomic"] {
        println!("cargo:rustc-link-lib={library}");
    }
}
