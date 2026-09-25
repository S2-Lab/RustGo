use std::io::{self, Write, Read};
fn main() {
    let mut buffer = Vec::new();
    io::stdin().read_to_end(&mut buffer);
    let data: &[u8] = &buffer;
    use chrono::prelude::*;
    if let Ok(data) = std::str::from_utf8(data) {
        let _ = DateTime::parse_from_rfc2822(data);
        let _ = DateTime::parse_from_rfc3339(data);
    }
}
