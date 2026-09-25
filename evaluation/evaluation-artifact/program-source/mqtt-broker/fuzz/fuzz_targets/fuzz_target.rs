use bytes::BytesMut;
use mqtt_broker::decoder;
use mqtt_broker::types::ProtocolVersion;
use std::io::{self, Write, Read};


fn main() {
    let mut buffer = Vec::new();
    io::stdin().read_to_end(&mut buffer);
    let data: &[u8] = &buffer;
    let mut bytes = BytesMut::new();
    bytes.extend_from_slice(data);

    let _ = decoder::decode_mqtt(&mut bytes, ProtocolVersion::V500);
    let _ = decoder::decode_mqtt(&mut bytes, ProtocolVersion::V311);
}