{
  "targets": [{
    "target_name": "fastgit",
    "sources": ["fastgit.cc"],
    "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")", "../../include"],
    "libraries": ["-L<(module_root_dir)/../../build/src", "-lfastgit", "-lz"],
    "conditions": [["OS=='linux'", {"libraries": ["-lcrypto","-lssl"] }]],
    "cflags_cc": ["-std=c++17"]
  }]
}
