use lz4_flex::frame::{self, FrameDecoder};
use std::io::{self, Read};

fn lz4_flex_frame_decompress(input: &[u8]) -> Result<Vec<u8>, frame::Error> {
    let mut de = FrameDecoder::new(input);
    let mut out = Vec::new();
    std::io::Read::read_to_end(&mut de, &mut out)?;
    Ok(out)
}

fn main() {
    let mut data = Vec::new();
    if let Err(e) = io::stdin().read_to_end(&mut data) {
        eprintln!("Failed to read from stdin: {}", e);
        return;
    }

    let _ = lz4_flex_frame_decompress(&data);

    let mut buffer = Vec::with_capacity(24 + data.len() * 2);

    for prefix in &[
        &[][..],                         
        &[0x04u8, 0x22, 0x4d, 0x18][..], 
    ] {
        buffer.clear();
        buffer.extend_from_slice(prefix);
        buffer.extend_from_slice(&data);
        let _ = lz4_flex_frame_decompress(&buffer);
    }

    for prefix in &[
        &[0x04u8, 0x22, 0x4d, 0x18, 0x60, 0x40, 0x82][..], 
        &[0x04u8, 0x22, 0x4d, 0x18, 0x40, 0x40, 0xC0][..], 
    ] {
        buffer.clear();
        buffer.extend_from_slice(prefix);
        buffer.extend_from_slice(&data);
        let _ = lz4_flex_frame_decompress(&buffer);

        buffer.clear();
        buffer.extend_from_slice(prefix);
        buffer.extend_from_slice(&(data.len() as u32).to_le_bytes());
        buffer.extend_from_slice(&data);
        buffer.extend_from_slice(&(data.len() as u32).to_le_bytes());
        buffer.extend_from_slice(&data);
        let _ = lz4_flex_frame_decompress(&buffer);
    }
}
