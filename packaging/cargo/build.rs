fn main() {
    let manifest = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let root = std::path::Path::new(&manifest).join("../..").canonicalize().unwrap();
    let dst = cmake::Config::new(&root)
        .define("FASTGIT_BUILD_TESTS", "OFF")
        .define("FASTGIT_BUILD_BENCHMARKS", "OFF")
        .define("FASTGIT_BUILD_SHARED", "OFF")
        .build();
    println!("cargo:rustc-link-search=native={}/lib", dst.display());
    println!("cargo:rustc-link-lib=static=fastgit");
    println!("cargo:rustc-link-lib=z");
    if pkg_config::probe_library("openssl").is_ok() {
        println!("cargo:rustc-link-lib=ssl");
        println!("cargo:rustc-link-lib=crypto");
    }
}
