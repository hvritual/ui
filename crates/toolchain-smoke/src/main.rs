use std::ffi::{c_char, c_int, CStr, CString};

extern "C" {
    fn ui_js_evaluate(
        source: *const c_char,
        output: *mut c_char,
        output_capacity: usize,
        error: *mut c_char,
        error_capacity: usize,
    ) -> c_int;
    fn ui_js_value_size() -> u32;
}

fn evaluate(source: &str) -> Result<String, String> {
    let source = CString::new(source).map_err(|e| e.to_string())?;
    let mut output = [0 as c_char; 2048];
    let mut error = [0 as c_char; 2048];
    // Both buffers live across the call. C bounds writes and NUL-terminates them;
    // JSValue never crosses this FFI boundary.
    let status = unsafe {
        ui_js_evaluate(
            source.as_ptr(),
            output.as_mut_ptr(),
            output.len(),
            error.as_mut_ptr(),
            error.len(),
        )
    };
    let buffer = if status == 0 { &output } else { &error };
    let text = unsafe { CStr::from_ptr(buffer.as_ptr()) }
        .to_string_lossy()
        .into_owned();
    if status == 0 { Ok(text) } else { Err(text) }
}

fn expect_value(name: &str, script: &str, expected: &str) -> Result<(), String> {
    let value = evaluate(script)?;
    if value != expected {
        return Err(format!("{name}: expected {expected}, received {value}"));
    }
    println!("PASS {name}");
    Ok(())
}

fn expect_error(name: &str, script: &str, fragment: &str) -> Result<(), String> {
    match evaluate(script) {
        Err(error) if error.contains(fragment) => {
            println!("PASS {name}");
            Ok(())
        }
        result => Err(format!("{name}: expected error containing {fragment}, received {result:?}")),
    }
}

fn run() -> Result<(), String> {
    if std::mem::size_of::<usize>() != 4 {
        return Err("not a 32-bit executable".into());
    }
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    if arguments == ["--self-test-failure"] {
        return expect_value("intentional-failure", "globalThis.__result = 41", "42");
    }
    if !arguments.is_empty() {
        return Err("usage: toolchain-smoke [--self-test-failure]".into());
    }
    expect_value("arithmetic", "globalThis.__result = 6 * 7", "42")?;
    expect_value("native-callback", "globalThis.__result = nativeAdd(20, 22)", "42")?;
    expect_value("json-unicode", r#"globalThis.__result = JSON.parse('{"drink":"咖啡","cups":2}').drink + ':' + 2"#, "咖啡:2")?;
    expect_value("typed-array", "globalThis.__result = new Uint16Array([40, 2]).reduce((a,b) => a+b)", "42")?;
    expect_value("promise-jobs", "globalThis.__result = 0; Promise.resolve(42).then(v => { globalThis.__result = v; })", "42")?;
    expect_error("exception", "throw new Error('expected-error')", "expected-error")?;
    expect_error("syntax-error", "let = ;", "SyntaxError")?;
    expect_error("interrupt", "while (true) {}", "interrupted")?;
    expect_error("memory-limit", "globalThis.__result = new ArrayBuffer(64 * 1024 * 1024)", "out of memory")?;
    for _ in 0..100 {
        let value = evaluate("globalThis.__result = nativeAdd(40, 2)")?;
        if value != "42" { return Err("lifecycle: wrong result".into()); }
    }
    println!("PASS lifecycle-100");
    println!("SMOKE_OK tests=10 pointer_bits=32 jsvalue_bytes={}", unsafe { ui_js_value_size() });
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("SMOKE_FAILED {error}");
        std::process::exit(1);
    }
}
