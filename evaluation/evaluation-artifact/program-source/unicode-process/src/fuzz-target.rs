// The fuzzing harness fuzz test some of the the
// unicode string normalization processing
extern crate unicode_normalization;
use unicode_normalization::poc;
use std::io::{self, Read};
use unicode_normalization::{
    char::{
        canonical_combining_class, compose, decompose_canonical, decompose_compatible,
        is_combining_mark,
    },
    UnicodeNormalization,
};

fn main() {
    let mut buffer = Vec::new();
    if let Err(e) = io::stdin().read_to_end(&mut buffer) {
        eprintln!("Failed to read from stdin: {}", e);
        return;
    }

    if buffer.len() < 2 {
        eprintln!("Input data is too short");
        return;
    }

    let (function_index, string_data) = (
        buffer[0],
        String::from_utf8_lossy(&buffer[1..]).to_string(),
    );

    let mut chars = string_data.chars();

    match function_index % 10 {
        0 => {
            if let (Some(c1), Some(c2)) = (chars.next(), chars.next()) {
                let _ = compose(c1, c2);
            }
        }
        1 => {
            if let Some(c) = chars.next() {
                let _ = canonical_combining_class(c);
            }
        }
        2 => {
            if let Some(c) = chars.next() {
                let _ = is_combining_mark(c);
            }
        }
        3 => {
            let _ = string_data.nfc().collect::<String>();
        }
        4 => {
            let _ = string_data.nfkd().collect::<String>();
        }
        5 => {
            let _ = string_data.nfd().collect::<String>();
        }
        6 => {
            let _ = string_data.nfkc().collect::<String>();
        }
        7 => {
            let _ = string_data.stream_safe().collect::<String>();
        }
        8 => {
            if let Some(c) = chars.next() {
                decompose_canonical(c, |_| {});
            }
        }
        9 => {
            if let Some(c) = chars.next() {
                decompose_compatible(c, |_| {});
            }
        }
        _ => {}
    }
}
