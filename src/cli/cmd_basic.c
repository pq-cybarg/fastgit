#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
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
