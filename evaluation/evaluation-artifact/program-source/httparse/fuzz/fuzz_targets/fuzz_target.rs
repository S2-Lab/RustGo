use std::io::{self, Read};

fn main() {
    let mut data = Vec::new();
    io::stdin().read_to_end(&mut data).expect("Failed to read input");

    let mut headers = [httparse::EMPTY_HEADER; 16];
    let mut req = httparse::Request::new(&mut headers);

    match req.parse(&data) {
        Ok(httparse::Status::Complete(_)) => println!("Complete request parsed."),
        Ok(httparse::Status::Partial) => println!("Partial request parsed."),
        Err(e) => eprintln!("Parse error: {:?}", e),
    }
}
