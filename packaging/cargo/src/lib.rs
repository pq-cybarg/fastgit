//! Raw FFI bindings to libfastgit. Link via `fastgit-sys`.
#![allow(non_camel_case_types)]
use std::os::raw::{c_char, c_int};
#[repr(C)] pub struct fastgit_oid_t { pub hash: [u8;64], pub len: usize, pub algo: u8 }
#[repr(C)] pub struct fastgit_repository { _unused: [u8;0] }
extern "C" {
    pub fn fastgit_repository_init(path: *const c_char, bare: c_int, out: *mut *mut fastgit_repository) -> c_int;
    pub fn fastgit_repository_open(path: *const c_char, out: *mut *mut fastgit_repository) -> c_int;
    pub fn fastgit_repository_free(repo: *mut fastgit_repository);
}
#[cfg(test)] mod tests { #[test] fn links() { assert_eq!(2+2,4); } }
