use std::str;
use std::io::{self, Read};

fn main() {
    let mut data = Vec::new();
    io::stdin().read_to_end(&mut data).expect("Failed to read input");
    let _ = serde_yaml::from_slice::<serde_yaml::Value>(&data);
  }
  