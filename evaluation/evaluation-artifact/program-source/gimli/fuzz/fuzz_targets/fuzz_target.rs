use gimli::{
    read::{BaseAddresses, CieOrFde, EhFrame, UnwindContext, UnwindSection},
    LittleEndian,
};
use std::io::{self, Read};

fn main() {
    let mut buffer = Vec::new();
    if let Err(e) = io::stdin().read_to_end(&mut buffer) {
        eprintln!("Failed to read input: {}", e);
        std::process::exit(1);
    }

    let eh_frame = EhFrame::new(&buffer, LittleEndian);

    let mut ctx = Box::new(UnwindContext::new());
    let bases = BaseAddresses::default()
        .set_eh_frame(0)
        .set_eh_frame_hdr(0)
        .set_text(0)
        .set_got(0);

    let mut entries = eh_frame.entries(&bases);
    while let Ok(Some(entry)) = entries.next() {
        match entry {
            CieOrFde::Cie(_) => continue,
            CieOrFde::Fde(partial) => {
                if let Ok(fde) = partial.parse(EhFrame::cie_from_offset) {
                    if let Ok(mut table) = fde.rows(&eh_frame, &bases, &mut ctx) {
                        while let Ok(Some(_row)) = table.next_row() {
                            // Do nothing
                        }
                    }
                }
            }
        };
    }
}
