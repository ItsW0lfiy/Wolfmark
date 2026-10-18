// A recording maintenance double for the native/NSIS startup contract tests.
use std::{env, fs::OpenOptions, io::Write, path::Path, process};

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let mut calls = OpenOptions::new()
        .create(true)
        .append(true)
        .open(env::var_os("WOLFMARK_TEST_CORE_CALLS").unwrap())
        .unwrap();
    writeln!(calls, "{args:?}").unwrap();
    if args.iter().any(String::is_empty) {
        eprintln!("Empty argument reached the maintenance core");
        process::exit(2);
    }
    if args.first().is_some_and(|arg| arg == "detect") {
        assert_eq!(args.get(1).map(String::as_str), Some("--manifest"));
        assert!(Path::new(&args[2]).is_file());
        println!("{}", env::var("WOLFMARK_TEST_CORE_STATE").unwrap());
    } else {
        if args.first().is_some_and(|arg| arg == "install") {
            assert!(!args.iter().any(|arg| arg == "--cancel-file"));
        }
        println!("{{\"changed\":false}}");
    }
    process::exit(
        env::var("WOLFMARK_TEST_CORE_EXIT")
            .unwrap()
            .parse()
            .unwrap(),
    );
}
