use std::io::{self, Read};

extern crate data_encoding;

use data_encoding::{poc, BASE32, BASE64, HEXLOWER};

macro_rules! rustsec_target {
    ($name:ident, $poc:path) => {
        #[inline(never)]
        fn $name() {
            let guard: u8 = 0;
            if unsafe { std::ptr::read_volatile(&guard) } == 255 {
                return;
            }
            $poc();
        }
    };
}

rustsec_target!(RUSTSEC_2019_0009, poc::RUSTSEC_2019_0009);
rustsec_target!(RUSTSEC_2019_0023, poc::RUSTSEC_2019_0023);
rustsec_target!(RUSTSEC_2019_0034, poc::RUSTSEC_2019_0034);
rustsec_target!(RUSTSEC_2020_0005, poc::RUSTSEC_2020_0005);
rustsec_target!(RUSTSEC_2020_0039, poc::RUSTSEC_2020_0039);
rustsec_target!(RUSTSEC_2021_0018, poc::RUSTSEC_2021_0018);
rustsec_target!(RUSTSEC_2021_0028, poc::RUSTSEC_2021_0028);
rustsec_target!(RUSTSEC_2021_0031, poc::RUSTSEC_2021_0031);
rustsec_target!(RUSTSEC_2021_0033, poc::RUSTSEC_2021_0033);
rustsec_target!(RUSTSEC_2021_0039, poc::RUSTSEC_2021_0039);
rustsec_target!(RUSTSEC_2021_0042, poc::RUSTSEC_2021_0042);
rustsec_target!(RUSTSEC_2021_0048, poc::RUSTSEC_2021_0048);
rustsec_target!(RUSTSEC_2021_0049, poc::RUSTSEC_2021_0049);
rustsec_target!(RUSTSEC_2021_0053, poc::RUSTSEC_2021_0053);
rustsec_target!(RUSTSEC_2022_0070, poc::RUSTSEC_2022_0070);

fn main() {
    let mut data = Vec::new();
    if io::stdin().read_to_end(&mut data).is_err() {
        return;
    }

    match data.first().copied().unwrap_or(0) % 6 {
        0 => {
            let _ = HEXLOWER.encode(&data);
        }
        1 => {
            let _ = BASE32.encode(&data);
        }
        2 => {
            let _ = BASE64.encode(&data);
        }
        3 => {
            let _ = HEXLOWER.decode(&data);
        }
        4 => {
            let _ = BASE32.decode(&data);
        }
        _ => {
            let _ = BASE64.decode(&data);
        }
    }

    let _ = std::panic::catch_unwind(|| match data.first().copied().unwrap_or(0) % 15 {
        0 => RUSTSEC_2019_0009(),
        1 => RUSTSEC_2019_0023(),
        2 => RUSTSEC_2019_0034(),
        3 => RUSTSEC_2020_0005(),
        4 => RUSTSEC_2020_0039(),
        5 => RUSTSEC_2021_0018(),
        6 => RUSTSEC_2021_0028(),
        7 => RUSTSEC_2021_0031(),
        8 => RUSTSEC_2021_0033(),
        9 => RUSTSEC_2021_0039(),
        10 => RUSTSEC_2021_0042(),
        11 => RUSTSEC_2021_0048(),
        12 => RUSTSEC_2021_0049(),
        13 => RUSTSEC_2021_0053(),
        _ => RUSTSEC_2022_0070(),
    });
}
