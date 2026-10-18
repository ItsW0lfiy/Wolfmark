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
            if env::var_os("WOLFMARK_TEST_ALLOW_CANCEL").is_none() {
                assert!(!args.iter().any(|arg| arg == "--cancel-file"));
            }
            if let Ok(expected) = env::var("WOLFMARK_TEST_EXPECT_INSTALL_DIR") {
                let actual = args
                    .windows(2)
                    .find(|pair| pair[0] == "--install-dir")
                    .unwrap();
                assert_eq!(actual[1], expected);
                let operation = args
                    .windows(2)
                    .find(|pair| pair[0] == "--operation")
                    .unwrap();
                assert_eq!(operation[1], "update");
                let cancel = args
                    .windows(2)
                    .find(|pair| pair[0] == "--cancel-file")
                    .unwrap();
                assert!(!cancel[1].is_empty() && !cancel[1].starts_with("--"));
            }
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
