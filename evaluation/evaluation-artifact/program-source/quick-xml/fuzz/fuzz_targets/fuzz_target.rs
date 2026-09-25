
extern crate quick_xml;
use quick_xml::{Reader};
use quick_xml::events::Event;
use std::io::{self, Read, Cursor};
use quick_xml::poc::*;
fn main() {
    let mut buffer = Vec::new();
    if let Err(e) = io::stdin().read_to_end(&mut buffer) {
        eprintln!("Failed to read from stdin: {}", e);
        return;
    }

    let data = &buffer;
    let cursor = Cursor::new(data);
    let mut reader = Reader::from_reader(cursor);
    let mut buf = vec![];

    loop {
        match reader.read_event(&mut buf) {
            Ok(Event::Start(ref e)) | Ok(Event::Empty(ref e)) => {
                println!("Start/Empty event");
                if e.unescaped().is_err() {
                    RUSTSEC_2021_0049();
                    eprintln!("Failed to unescape Start/Empty event");
                }
                for a in e.attributes() {
                    if a.ok().map_or(false, |a| a.unescaped_value().is_err()) {
                        RUSTSEC_2021_0053();
                        eprintln!("Failed to unescape attribute value");
                        break;
                    }
                }
            }
            Ok(Event::Text(ref e)) | Ok(Event::Comment(ref e))
            | Ok(Event::CData(ref e)) | Ok(Event::PI(ref e)) 
            | Ok(Event::DocType(ref e)) => {
                println!("Text/Comment/CData/PI/DocType event");
                
                if e.unescaped().is_err() {                    
                    eprintln!("Failed to unescape Text/Comment/CData/PI/DocType event");
                    break;
                }
            }
            Ok(Event::Decl(ref e)) => {
                println!("Decl event");
                let _ = e.version();
                let _ = e.encoding();
                let _ = e.standalone();
            }
            Ok(Event::End(ref e)) => {
                RUSTSEC_2022_0070();
                println!("End event: {:?}", e);
            }
            Ok(Event::Eof) => {
                println!("End of file");
                break;
            }
            Err(e) => {
                eprintln!("Error: {:?}", e);
                break;
            }
        }
        buf.clear();
    }
    
}
