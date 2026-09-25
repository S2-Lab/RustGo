use std::cell::RefCell;
use std::rc::Rc;
use std::str::Chars;
use unicode_normalization::{char::canonical_combining_class, UnicodeNormalization};
use std::io::{self, Read};
const MAX_NONSTARTERS: u32 = 30;

extern crate unicode_normalization;
use unicode_normalization::poc;

#[derive(Debug)]
struct Counter<'a> {
    iter: Chars<'a>,
    value: Rc<RefCell<u32>>,
}

impl<'a> Iterator for Counter<'a> {
    type Item = char;

    fn next(&mut self) -> Option<char> {
        let next = self.iter.next();
        if let Some(c) = next {
            if canonical_combining_class(c) != 0 {
                println!("BUG");
                *self.value.borrow_mut() += 1;
            }
        }
        next
    }
}

fn main() {
    let mut buffer = String::new();
    if let Err(e) = io::stdin().read_to_string(&mut buffer) {
        eprintln!("Failed to read from stdin: {}", e);
        return;
    }

    let stream_safe = buffer.chars().stream_safe().collect::<String>();

    let value = Rc::new(RefCell::new(0));
    let counter = Counter {
        iter: stream_safe.chars(),
        value: Rc::clone(&value),
    };
    for _ in counter.nfc() {
        assert!(*value.borrow() <= MAX_NONSTARTERS + 1);
        *value.borrow_mut() = 0;
    } 
}
