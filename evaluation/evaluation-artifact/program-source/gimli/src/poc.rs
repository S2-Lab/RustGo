use std::fs::OpenOptions;
use std::io::Write;
use std::fs;
use std::time::{SystemTime, UNIX_EPOCH};
use std::panic::{catch_unwind, AssertUnwindSafe};
use alloc::boxed::Box;
use std::string::String;

/*#################################### */
/*######## RUSTSEC_2019_0009 ######### */
/*#################################### */
extern crate smallvec;
use smallvec::*;

pub fn RUSTSEC_2019_0009(){
    log("RUSTSEC_2019_0009");
    let mut v: SmallVec<[u8; 2]> = SmallVec::new();
    v.push(0);
    v.push(1);
    v.push(2);
    assert_eq!(v.capacity(), 4);
    v.grow(4);
    println!("{:?}", v);
}

/*#################################### */
/*######## RUSTSEC_2019_0023 ######### */
/*#################################### */
extern crate string_interner;
use string_interner::{DefaultStringInterner, Sym};

fn clone_and_drop() -> (DefaultStringInterner, Sym) {
    let mut old = DefaultStringInterner::new();
    let foo = old.get_or_intern("foo");

    let new = old.clone();
    let _bar = old.get_or_intern("bar");

    (new, foo)
}

pub fn RUSTSEC_2019_0023() {
    log("RUSTSEC_2019_0023");
    let (mut new, foo) = clone_and_drop();
    assert_eq!(
        new.get_or_intern("foo"),
        foo,
        "`foo` should represent the string \"foo\" so they should be equal"
    );
}

/*#################################### */
/*######## RUSTSEC_2019_0034 ######### */
/*#################################### */
extern crate http; // 0.1.19
use http::header::HeaderMap;
struct RUSTSEC_2019_0034_DropDetector{
    data: Box<u32>,
}
impl RUSTSEC_2019_0034_DropDetector {
    fn new(value: u32) -> RUSTSEC_2019_0034_DropDetector {
        RUSTSEC_2019_0034_DropDetector {
            data: Box::new(value),
        }
    }
}

pub fn RUSTSEC_2019_0034() {
    log("RUSTSEC_2019_0034");
    {
        println!("Failing to drop Drain causes double-free");
        
        let mut map = HeaderMap::with_capacity(32);
        map.insert("1", RUSTSEC_2019_0034_DropDetector::new(1));
        map.insert("2", RUSTSEC_2019_0034_DropDetector::new(2));
        
        let mut drain = map.drain();
        drain.next();
        std::mem::forget(drain);
    }
    
    {
        println!("Drop drain without consuming it leaks memory");
        
        let mut map = HeaderMap::with_capacity(32);
        map.insert("3", RUSTSEC_2019_0034_DropDetector::new(3));
        map.insert("4", RUSTSEC_2019_0034_DropDetector::new(4));
        
        let mut drain = map.drain();
        drain.next();
    }
}

/*#################################### */
/*######## RUSTSEC_2020_0005 ######### */
/*#################################### */
extern crate cbox;
use cbox::*;
use std::ops::Deref;

pub fn RUSTSEC_2020_0005() {
    log("RUSTSEC_2020_0005");
    let p = vec![1, 2, 3].as_mut_ptr();
    let semibox: CSemiBox<'static, str> = CSemiBox::new(p as _);
    drop(semibox);
    println!("Hello World")
}

/*#################################### */
/*######## RUSTSEC_2020_0039 ######### */
/*#################################### */
extern crate simple_slab;
use simple_slab::Slab;

#[derive(Debug, PartialEq)]
struct RUSTSEC_2020_0039_DropDetector(u32);

impl Drop for RUSTSEC_2020_0039_DropDetector {
    fn drop(&mut self) {
        println!("Dropping {}", self.0);
    }
}

pub fn RUSTSEC_2020_0039() {
    log("RUSTSEC_2020_0039");
    let mut slab = Slab::with_capacity(2);
    slab.insert(RUSTSEC_2020_0039_DropDetector(123));
    slab.insert(RUSTSEC_2020_0039_DropDetector(456));
    let removed = slab.remove(0);
    let ss = slab[0].0;
}

/*#################################### */
/*######## RUSTSEC_2021_0018 ######### */
/*#################################### */
extern crate qwutils;
use qwutils::*;
struct RUSTSEC_2021_0018_DropDetector{
    data: Box<u32>,
}
impl RUSTSEC_2021_0018_DropDetector {
    fn new(value: u32) -> RUSTSEC_2021_0018_DropDetector {
        RUSTSEC_2021_0018_DropDetector {
            data: Box::new(value),
        }
    }
}

impl Clone for RUSTSEC_2021_0018_DropDetector {
    fn clone(&self) -> Self {
        panic!("RUSTSEC_2021_0018_DropDetector {} panic on clone()", self.data);
    }
}

pub fn RUSTSEC_2021_0018() {
    log("RUSTSEC_2021_0018");
    std::panic::set_hook(Box::new(|_| {}));

    let mut a = vec![RUSTSEC_2021_0018_DropDetector::new(1), RUSTSEC_2021_0018_DropDetector::new(2)];
    let b = [RUSTSEC_2021_0018_DropDetector::new(3)];

    let _ = catch_unwind(AssertUnwindSafe(|| {
        a.insert_slice_clone(0, &b);
    }));
}



/*#################################### */
/*######## RUSTSEC_2021_0028 ######### */
/*#################################### */
extern crate toodee;

use toodee::TooDee;

struct RUSTSEC_2021_0028_DropDetector { data: Box<u32> }
impl RUSTSEC_2021_0028_DropDetector {
    fn new(value: u32) -> RUSTSEC_2021_0028_DropDetector {
        RUSTSEC_2021_0028_DropDetector { data: Box::new(value) }
    }
}

struct PanickingIterator();
impl Iterator for PanickingIterator {
    type Item = RUSTSEC_2021_0028_DropDetector;
    fn next(&mut self) -> Option<Self::Item> { panic!("Iterator panicked"); }
}
impl ExactSizeIterator for PanickingIterator {
    fn len(&self) -> usize { 1 }
}

pub fn RUSTSEC_2021_0028() {
    log("RUSTSEC_2021_0028");
    std::panic::set_hook(Box::new(|_| {}));

    let vec = vec![
        RUSTSEC_2021_0028_DropDetector::new(1),
        RUSTSEC_2021_0028_DropDetector::new(2),
        RUSTSEC_2021_0028_DropDetector::new(3),
    ];
    let mut toodee: TooDee<_> = TooDee::from_vec(1, 3, vec);

    let _ = catch_unwind(AssertUnwindSafe(|| {
        toodee.insert_row(0, PanickingIterator());
    }));
}




/*#################################### */
/*######## RUSTSEC_2021_0031 ######### */
/*#################################### */
extern crate nano_arena;
use std::alloc::{dealloc, Layout};
use nano_arena::{Arena, ArenaAccess, Idx};
use std::{borrow::Borrow, cell::Cell};

struct MyIdx {
    idx1: Idx,
    idx2: Idx,
    state: Cell<bool>
}

impl MyIdx {
    fn new(idx1: Idx, idx2: Idx) -> Self {
        MyIdx { idx1, idx2, state: Cell::new(false) }
    }
}

// A borrow implementation that alternatingly returns two different indexes.
impl Borrow<Idx> for MyIdx {
    fn borrow(&self) -> &Idx {
        self.state.set(!self.state.get());
        if self.state.get() {
            &self.idx1
        } else {
            &self.idx2
        }
    }
}

pub fn RUSTSEC_2021_0031() {
    log("RUSTSEC_2021_0031");
    let mut arena = Arena::new();
    let idx1 = arena.alloc(1);
    let idx2 = arena.alloc(2);

    let custom_idx = MyIdx::new(idx1.clone(), idx2.clone());

    let (mutable_ref_one, mut split_arena) = arena.split_at(custom_idx).unwrap();
    let mutable_ref_two : &mut i32 = split_arena.get_mut(&idx1).unwrap();
    println!("{:p}, {:p}", mutable_ref_one, mutable_ref_two);
    let ref_one_ptr = &*mutable_ref_two as *const i32;
    let x_mut_ptr = ref_one_ptr as *mut i32;
    unsafe {
        dealloc(x_mut_ptr as *mut u8, Layout::new::<i32>());
    }
    // drop(mutable_ref_two);
    // println!("{:p}, {:p}", mutable_ref_one, mutable_ref_two);
    println!("{:p}", mutable_ref_two);
    // assert!(mutable_ref_one != mutable_ref_two);
}




/*#################################### */
/*######## RUSTSEC_2021_0033 ######### */
/*#################################### */
use stack_dst::StackA;
struct RUSTSEC_2021_0033_DropDetector{
    data: Box<u32>,
}
impl RUSTSEC_2021_0033_DropDetector {
    fn new(value: u32) -> RUSTSEC_2021_0033_DropDetector {
        RUSTSEC_2021_0033_DropDetector {
            data: Box::new(value),
        }
    }
}


impl Clone for RUSTSEC_2021_0033_DropDetector {
    fn clone(&self) -> Self { panic!("Panic in clone()") }
}

pub fn RUSTSEC_2021_0033() {
    log("RUSTSEC_2021_0033");
    std::panic::set_hook(Box::new(|_| {}));

    let mut stack = StackA::<[RUSTSEC_2021_0033_DropDetector], [usize; 9]>::new();
    stack.push_stable([RUSTSEC_2021_0033_DropDetector::new(1)], |p| p);
    stack.push_stable([RUSTSEC_2021_0033_DropDetector::new(2)], |p| p);

    println!("Popping off second drop detector");
    let _second_drop = stack.pop();

    println!("Pushing panicky-clone");
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let _ = stack.push_cloned(&[RUSTSEC_2021_0033_DropDetector::new(3)]);
    }));
}




/*#################################### */
/*######## RUSTSEC_2021_0039 ######### */
/*#################################### */
extern crate endian_trait;
use endian_trait::Endian;

#[derive(Debug)]
struct Foo(Box<Option<i32>>);

impl Endian for Foo {
    fn to_be(self) -> Self {
        println!(
            "PANIC BY MISTAKE {}",
            self.0.as_ref().as_ref().unwrap()
        );
        self
    }
    fn to_le(self) -> Self { self }
    fn from_be(self) -> Self { self }
    fn from_le(self) -> Self { self }
}

pub fn RUSTSEC_2021_0039() {
    log("RUSTSEC_2021_0039");
    std::panic::set_hook(Box::new(|_| {}));

    let mut foo = [Foo(Box::new(None))];
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let x = (&mut foo).to_be(); 
        dbg!(x);                    
    }));
}



/*#################################### */
/*######## RUSTSEC_2021_0042 ######### */
/*#################################### */
use insert_many::InsertMany;
struct RUSTSEC_2021_0042_DropDetector {
    data: Box<u32>,
}

impl RUSTSEC_2021_0042_DropDetector {
    fn new(value: u32) -> RUSTSEC_2021_0042_DropDetector {
        RUSTSEC_2021_0042_DropDetector { data: Box::new(value), }
    }
}

struct MyCollection();

impl IntoIterator for MyCollection {
    type Item = RUSTSEC_2021_0042_DropDetector;
    type IntoIter = RUSTSEC_2021_0042_PanickingIterator;

    fn into_iter(self) -> Self::IntoIter { RUSTSEC_2021_0042_PanickingIterator() }
}

struct RUSTSEC_2021_0042_PanickingIterator();

impl Iterator for RUSTSEC_2021_0042_PanickingIterator {
    type Item = RUSTSEC_2021_0042_DropDetector;

    fn next(&mut self) -> Option<Self::Item> { panic!("Iterator panicked"); }
}

impl ExactSizeIterator for RUSTSEC_2021_0042_PanickingIterator {
    fn len(&self) -> usize { 1 }
}


pub fn RUSTSEC_2021_0042() {
    log("RUSTSEC_2021_0042");
    let mut v = vec![RUSTSEC_2021_0042_DropDetector::new(1), RUSTSEC_2021_0042_DropDetector::new(2)];
    let _ = catch_unwind(AssertUnwindSafe(|| {
        v.insert_many(0, MyCollection());
    }));
}




/*######## RUSTSEC_2021_0048 ######### */
extern crate stackvector;

use stackvector::StackVec;

#[allow(unused_parens, unused_variables, unused_imports)]
struct IncorrectIterator(u32);

impl IncorrectIterator {
    pub fn new() -> Self { IncorrectIterator(0) }
}

impl Iterator for IncorrectIterator {
    type Item = u8;

    fn next(&mut self) -> Option<Self::Item> {
        self.0 += 1;
        if (self.0 >= 20) { None } else { Some(0x41) }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let lower_bound = 20;
        let upper_bound = Some(0);
        (lower_bound, upper_bound)
    }
}

pub fn RUSTSEC_2021_0048() {
    log("RUSTSEC_2021_0048");
    let mut stack_vec = StackVec::<[u8; 4]>::new();
    let i : i32 = 42;
    std::panic::set_hook(Box::new(|_| {}));
    let _ = catch_unwind(AssertUnwindSafe(|| {
        stack_vec.extend(IncorrectIterator::new());
    }));
}

/*#################################### */
/*######## RUSTSEC_2021_0049 ######### */
/*#################################### */
extern crate through;
use through::through;

pub fn RUSTSEC_2021_0049() {
    log("RUSTSEC_2021_0049");
    let mut hello = String::from("Hello");

    let _ = catch_unwind(AssertUnwindSafe(|| {
        let object = through(&mut hello, |mut s| {
            s.push_str(" World!");
            s
        });
    }));
}


/*#################################### */
/*######## RUSTSEC_2021_0053 ######### */   
/*#################################### */
extern crate algorithmica;
use algorithmica::sort::merge_sort::sort;   

pub fn RUSTSEC_2021_0053() {
    log("RUSTSEC_2021_0053");
    let mut arr = vec![
        String::from("Hello"),
        String::from("World"),
        String::from("Rust"),
    ];

    algorithmica::sort::merge_sort::sort(&mut arr);
}



/*#################################### */
/*######## RUSTSEC_2022_0070 ######### */
/*#################################### */
extern crate secp256k1;
use secp256k1::Secp256k1;

fn make_bad_secp() -> Secp256k1::<secp256k1::AllPreallocated<'static>> {
    // in principle `Box` is not needed but if it's not here it won't show up as a crash.
    let mut array = Box::new([secp256k1::ffi::types::AlignedType::ZERO; 1024]);
    secp256k1::Secp256k1::<secp256k1::AllPreallocated<'static>>::preallocated_gen_new(&mut *array).unwrap()
}

pub fn RUSTSEC_2022_0070() {
    log("RUSTSEC_2022_0070");
    let mut array = [secp256k1::ffi::types::AlignedType::ZERO; 1024];
    let secp = secp256k1::Secp256k1::<secp256k1::All>::preallocated_gen_new(&mut array).unwrap();
    let secret = secp256k1::SecretKey::from_slice(b"release the nasal daemons!!!!!!!").unwrap();
    let pubkey = secp256k1::PublicKey::from_secret_key(&secp, &secret);
}




//Original Loggging
use std::fs::{File};
use std::io::{Read, BufReader};
use std::io::BufRead;

pub fn log(function_name: &str) {
    let log_file_path = "reaching-log.txt";
    if let Ok(file) = File::open(log_file_path) {
        let reader = BufReader::new(file);
        if reader.lines().flatten().any(|line| line.contains(function_name)) {
            return; 
        }
    }
    let now_ms = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis();
    let log_entry = format!("[{}] : {} ms since start\n", function_name, now_ms);
    
    if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(log_file_path) {
        let _ = file.write_all(log_entry.as_bytes());
    }
}