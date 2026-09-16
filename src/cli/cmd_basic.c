#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include "fastgit/bundle.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int fastgit_cmd_init(int argc, char **argv) {
    const char* path = ".";
    bool bare = false;
    for(int i=0;i<argc;i++){
        if(strcmp(argv[i],"-q")==0 || strcmp(argv[i],"--quiet")==0) continue;
        else if(strcmp(argv[i],"--bare")==0) bare=true;
        else if(argv[i][0]=='-') continue;
        else { path=argv[i]; break; }
    }
    fastgit_repository_t* repo;
    fastgit_error_t err = fastgit_repository_init(path, bare, &repo);
    if (err != FASTGIT_OK) {
        fprintf(stderr, "fatal: could not create repository: %s\n", fastgit_error_string(err));
        return 1;
    }
    fastgit_repository_free(repo);
    printf("Initialized empty fastgit repository in %s/.git/\n", path);
    return 0;
}

int fastgit_cmd_clone(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit clone <repository> [<directory>]\n"); return 1; }
    const char* url = argv[0];
    const char* path = argc >= 2 ? argv[1] : NULL;
    char auto_path[1024] = {0};
    if (!path) {
        const char* slash = strrchr(url, '/');
        const char* base = slash ? slash + 1 : url;
        size_t blen = strlen(base);
        if (blen > 4 && strcmp(base + blen - 4, ".git") == 0) blen -= 4;
        if (blen >= sizeof(auto_path)) blen = sizeof(auto_path) - 1;
        memcpy(auto_path, base, blen);
        auto_path[blen] = '\0';
        if (auto_path[0] == '\0') strcpy(auto_path, "fastgit-clone");
        path = auto_path;
    }
    fastgit_error_t err = fastgit_clone(url, path, NULL);
    if (err != FASTGIT_OK) { fprintf(stderr, "clone failed: %s\n", fastgit_error_string(err)); return 1; }
    return 0;
}

int fastgit_cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("fastgit version %s\n", fastgit_version());
    return 0;
}

int fastgit_cmd_bundle(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit bundle <subcommand>\n"); return 1; }
    const char* sub = argv[0];
    if (strcmp(sub, "create") == 0 && argc >= 3) {
        fastgit_repository_t* repo = NULL;
        if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
        fastgit_bundle_t* bundle = NULL;
        fastgit_error_t err = fastgit_bundle_create(repo, argv[1], argv[2], NULL);
        fastgit_repository_free(repo);
        if (err != FASTGIT_OK) { fprintf(stderr, "bundle create failed: %s\n", fastgit_error_string(err)); return 1; }
        printf("Bundle created at %s\n", argv[1]);
        return 0;
    }
    if (strcmp(sub, "verify") == 0 && argc >= 2) {
        fastgit_bundle_ref_t** refs = NULL; size_t count = 0;
        fastgit_error_t err = fastgit_bundle_verify(argv[1], &refs, &count);
        if (err != FASTGIT_OK) { fprintf(stderr, "bundle verify failed: %s\n", fastgit_error_string(err)); return 1; }
        printf("Bundle verified. Contains %zu refs:\n", count);
        for (size_t i = 0; i < count; i++) {
            char hex[129]; fastgit_oid_to_hex(&refs[i]->oid, hex, sizeof(hex));
            printf("  %s %s\n", hex, refs[i]->name);
        }
        fastgit_bundle_refs_free(refs, count);
        return 0;
    }
    if (strcmp(sub, "unbundle") == 0 && argc >= 2) {
        fastgit_repository_t* repo = NULL;
        if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
        fastgit_error_t err = fastgit_bundle_unbundle(repo, argv[1], argc >= 3 ? argv[2] : NULL);
        fastgit_repository_free(repo);
        if (err != FASTGIT_OK) { fprintf(stderr, "bundle unbundle failed: %s\n", fastgit_error_string(err)); return 1; }
        printf("Bundle unbundled successfully\n");
        return 0;
    }
    fprintf(stderr, "fastgit bundle: unknown subcommand %s\n", sub); return 1;
}
