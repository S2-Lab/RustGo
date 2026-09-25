use std::ffi::{CString};
use libc::{read, c_void, size_t};
use std::env;
use std::os::unix::ffi::OsStringExt;

const MAX_CMDLINE_LEN: usize = 100000;
const MAX_CMDLINE_PAR: usize = 50000;


pub fn argv_init() -> Vec<CString> {
    let mut in_buf = vec![0u8; MAX_CMDLINE_LEN];
    let mut ret = Vec::with_capacity(MAX_CMDLINE_PAR);
    
    // Collect current command line arguments
    let mut args: Vec<CString> = env::args_os().map(|arg| CString::new(arg.into_vec()).unwrap()).collect();

    // Read input from stdin
    unsafe {
        let read_ret = read(0, in_buf.as_mut_ptr() as *mut c_void, (MAX_CMDLINE_LEN - 2) as size_t);
        if read_ret < 0 {
            return args;
        }

        let mut ptr = 0;

        while ptr < read_ret as usize {
            let mut end = ptr;
            while end < read_ret as usize && !in_buf[end].is_ascii_whitespace() {
                end += 1;
            }

            let arg = CString::new(&in_buf[ptr..end]).unwrap();
            ret.push(arg);

            ptr = end + 1;
            while ptr < read_ret as usize && in_buf[ptr].is_ascii_whitespace() {
                ptr += 1;
            }
        }
    }

    // Append stdin arguments to the command line arguments
    args.extend(ret);

    args
}


pub fn argv_stdin() -> Vec<String> {
    let mut in_buf = vec![0u8; MAX_CMDLINE_LEN];
    let mut ret = Vec::with_capacity(MAX_CMDLINE_PAR);

    // Collect current command line arguments, skipping the first one
    let mut args: Vec<String> = env::args_os()
        .skip(1) // Skip the first argument (binary name)
        .map(|arg| arg.into_string().unwrap())
        .collect();

    // Read input from stdin
    unsafe {
        let read_ret = read(0, in_buf.as_mut_ptr() as *mut c_void, (MAX_CMDLINE_LEN - 2) as size_t);
        if read_ret < 0 {
            return args;
        }

        let mut ptr = 0;

        while ptr < read_ret as usize {
            let mut end = ptr;
            while end < read_ret as usize && !in_buf[end].is_ascii_whitespace() {
                end += 1;
            }

            let arg = CString::new(&in_buf[ptr..end]).unwrap();
            ret.push(arg);

            ptr = end + 1;
            while ptr < read_ret as usize && in_buf[ptr].is_ascii_whitespace() {
                ptr += 1;
            }
        }
    }

    // Append stdin arguments to the command line arguments
    args.extend(ret.into_iter().map(|cstr| cstr.into_string().unwrap()));

    args
}


pub fn argv_only_stdin() -> Vec<String> {
    let mut in_buf = vec![0u8; MAX_CMDLINE_LEN];
    let mut ret = Vec::with_capacity(MAX_CMDLINE_PAR);

    // Read input from stdin
    unsafe {
        let read_ret = read(0, in_buf.as_mut_ptr() as *mut c_void, (MAX_CMDLINE_LEN - 2) as size_t);
        if read_ret < 0 {
            return vec![]; // Return an empty Vec if read fails
        }

        let mut ptr = 0;

        while ptr < read_ret as usize {
            let mut end = ptr;
            while end < read_ret as usize && !in_buf[end].is_ascii_whitespace() {
                end += 1;
            }

            let arg = CString::new(&in_buf[ptr..end]).unwrap();
            ret.push(arg);

            ptr = end + 1;
            while ptr < read_ret as usize && in_buf[ptr].is_ascii_whitespace() {
                ptr += 1;
            }
        }
    }

    // Convert the collected CString arguments to String and return
    ret.into_iter().map(|cstr| cstr.into_string().unwrap()).collect()
}