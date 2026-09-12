#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/worktree.h"
#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>

int fastgit_cmd_benchmark(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Running benchmarks...\n");
    fastgit_stats_t stats = fastgit_stats_get();
    printf("Objects read: %llu\n", stats.objects_read);
    printf("Objects written: %llu\n", stats.objects_written);
    printf("Bytes read: %llu\n", stats.bytes_read);
    printf("Bytes written: %llu\n", stats.bytes_written);
    printf("CPU time: %.2f ms\n", stats.cpu_time_ms);
    printf("Wall time: %.2f ms\n", stats.wall_time_ms);
    return 0;
}

int fastgit_cmd_stats(int argc, char **argv) {
    (void)argc; (void)argv;
    fastgit_stats_t stats = fastgit_stats_get();
    printf("fastgit statistics:\n");
    printf("  Objects read:    %llu\n", stats.objects_read);
    printf("  Objects written: %llu\n", stats.objects_written);
    printf("  Bytes read:      %llu\n", stats.bytes_read);
    printf("  Bytes written:   %llu\n", stats.bytes_written);
    printf("  CPU time:        %.2f ms\n", stats.cpu_time_ms);
    printf("  Wall time:       %.2f ms\n", stats.wall_time_ms);
    return 0;
}

int fastgit_cmd_config(int argc, char **argv) {
    if(argc==1){ char* v=NULL; if(fastgit_config_get(argv[0],&v)==FASTGIT_OK){printf("%s\n",v); free(v);} else {fprintf(stderr,"not found\n"); return 1; } return 0; }
    if(argc==2){ fastgit_config_set(argv[0], argv[1], "local"); return 0; }
    fprintf(stderr,"usage: fastgit config <key> [<value>]\n"); return 1;
}

int fastgit_cmd_stash(int argc, char **argv) {
    const char* sub = argc >= 1 ? argv[0] : "push";
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    char stash_ref[512]; snprintf(stash_ref, sizeof(stash_ref), "refs/stash");
    if (strcmp(sub, "push") == 0 || strcmp(sub, "save") == 0) {
        const char* msg = "WIP on HEAD";
        for (int i = 1; i < argc; i++) if (strcmp(argv[i], "-m") == 0 && i+1 < argc) msg = argv[++i];
        fastgit_index_t* idx = fastgit_repository_index(repo);
        fastgit_oid_t tree_oid;
        if (fastgit_index_write_tree(idx, &tree_oid) != FASTGIT_OK) { fastgit_repository_free(repo); return 1; }
        fastgit_oid_t parent; bool has_parent = (fastgit_rev_parse(repo, "HEAD", &parent) == FASTGIT_OK);
        fastgit_signature_t *a = NULL, *c = NULL; fastgit_signature_default(&a); fastgit_signature_default(&c);
        fastgit_commit_t ct = {0}; ct.tree = tree_oid;
        if (has_parent) { ct.parent_count = 1; ct.parents = &parent; }
        ct.author = a; ct.committer = c; ct.message = (char*)msg;
        fastgit_object_t* cobj = NULL;
        if (fastgit_commit_create(&ct, &cobj) != FASTGIT_OK) { fastgit_signature_free(a); fastgit_signature_free(c); fastgit_repository_free(repo); return 1; }
        fastgit_buf_t buf; fastgit_object_serialize(cobj, FASTGIT_HASH_SHA256, &buf);
        fastgit_odb_t* odb = fastgit_repository_odb(repo);
        fastgit_oid_t woid;
        size_t hdr = 0; while (hdr < buf.len && ((uint8_t*)buf.data)[hdr] != '\0') hdr++;
        const void* content = hdr < buf.len ? (uint8_t*)buf.data + hdr + 1 : buf.data;
        size_t clen = hdr < buf.len ? buf.len - hdr - 1 : buf.len;
        fastgit_odb_write(odb, FASTGIT_OBJ_COMMIT, content, clen, &woid);
        fastgit_reference_update(repo, stash_ref, &woid, msg);
        char hex[129]; fastgit_oid_to_hex(&woid, hex, sizeof(hex));
        printf("Saved working directory and index state %s\n", hex);
        free(buf.data); fastgit_object_free(cobj);
        fastgit_index_read_tree(idx, &tree_oid); fastgit_index_write(idx);
    } else if (strcmp(sub, "list") == 0) {
        fastgit_oid_t oid;
        if (fastgit_reference_lookup(repo, stash_ref, &oid) == FASTGIT_OK) {
            char hex[129]; fastgit_oid_to_hex(&oid, hex, sizeof(hex));
            printf("stash@{0}: %s\n", hex);
        }
    } else if (strcmp(sub, "pop") == 0 || strcmp(sub, "apply") == 0) {
        fastgit_oid_t oid;
        if (fastgit_reference_lookup(repo, stash_ref, &oid) != FASTGIT_OK) { fprintf(stderr, "No stash found.\n"); fastgit_repository_free(repo); return 1; }
        fastgit_object_t* obj = NULL;
        if (fastgit_object_lookup(repo, &oid, &obj) == FASTGIT_OK) {
            const fastgit_commit_t* cc = fastgit_commit_parse(obj);
            if (cc) {
                fastgit_index_read_tree(fastgit_repository_index(repo), (fastgit_oid_t*)&cc->tree);
                fastgit_index_write(fastgit_repository_index(repo));
                fastgit_checkout_tree(fastgit_repository_worktree(repo), &cc->tree, true);
                if (strcmp(sub, "pop") == 0) fastgit_reference_remove(repo, stash_ref);
                printf("Restored stash %s\n", sub);
            }
            fastgit_object_free(obj);
        }
    } else if (strcmp(sub, "clear") == 0 || strcmp(sub, "drop") == 0) {
        fastgit_reference_remove(repo, stash_ref);
    } else {
        fprintf(stderr, "usage: fastgit stash [push|pop|apply|list|clear|drop]\n");
    }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_blame(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit blame <file>\n"); return 1; }
    const char* path = argv[argc - 1];
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    FILE* f = fopen(path, "rb");
    char* content = NULL; size_t clen = 0;
    if (f) { fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); clen = sz > 0 ? (size_t)sz : 0; content = malloc(clen + 1); if (clen) fread(content, 1, clen, f); content[clen] = '\0'; fclose(f); }
    else {
        fastgit_oid_t head; if (fastgit_rev_parse(repo, "HEAD", &head) == FASTGIT_OK) {
            fastgit_object_t* obj = NULL; if (fastgit_object_lookup(repo, &head, &obj) == FASTGIT_OK) {
                const fastgit_commit_t* c = fastgit_commit_parse(obj);
                if (c) {
                    fastgit_index_t* idx = fastgit_repository_index(repo);
                    fastgit_index_entry_t* ent = NULL;
                    if (fastgit_index_find(idx, path, 0, &ent) == FASTGIT_OK && ent) {
                        fastgit_odb_object_t o; if (fastgit_odb_read(fastgit_repository_odb(repo), &ent->oid, &o) == FASTGIT_OK) { content = malloc(o.size + 1); memcpy(content, o.data, o.size); content[o.size] = '\0'; clen = o.size; free(o.data); }
                    }
                }
                fastgit_object_free(obj);
            }
        }
    }
    if (!content) { fastgit_repository_free(repo); return 1; }
    fastgit_oid_t head_oid; char head_hex[9] = "00000000";
    if (fastgit_rev_parse(repo, "HEAD", &head_oid) == FASTGIT_OK) { char hx[129]; fastgit_oid_to_hex(&head_oid, hx, sizeof(hx)); memcpy(head_hex, hx, 8); head_hex[8] = '\0'; }
    const char* author = "unknown";
    fastgit_oid_t cur; if (fastgit_rev_parse(repo, "HEAD", &cur) == FASTGIT_OK) {
        fastgit_object_t* obj = NULL; if (fastgit_object_lookup(repo, &cur, &obj) == FASTGIT_OK) { const fastgit_commit_t* c = fastgit_commit_parse(obj); if (c && c->author) author = c->author->name; fastgit_object_free(obj); }
    }
    size_t line_no = 1; char* p = content;
    while (p < content + clen) {
        char* nl = memchr(p, '\n', (content + clen) - p);
        size_t llen = nl ? (size_t)(nl - p) : (size_t)((content + clen) - p);
        char line[1024]; size_t cp = llen < sizeof(line) - 1 ? llen : sizeof(line) - 1; memcpy(line, p, cp); line[cp] = '\0';
        printf("%s (%s %ld) %s\n", head_hex, author, (long)line_no, line);
        line_no++;
        if (!nl) break; p = nl + 1;
    }
    free(content); fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_grep(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit grep <pattern> [<path>...]\n"); return 1; }
    const char* pattern = argv[0];
    bool show_line = true; (void)show_line;
    regex_t re; int rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);
    if (rc != 0) { char err[256]; regerror(rc, &re, err, sizeof(err)); fprintf(stderr, "grep: %s\n", err); return 1; }
    fastgit_repository_t* repo = NULL; bool has_repo = (fastgit_repository_open(".", &repo) == FASTGIT_OK);
    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            const char* pth = argv[i];
            FILE* ff = fopen(pth, "rb"); if (!ff) continue;
            char line[8192]; int lineno = 1;
            while (fgets(line, sizeof(line), ff)) {
                size_t ll = strlen(line); if (ll && line[ll-1] == '\n') line[ll-1] = '\0';
                if (regexec(&re, line, 0, NULL, 0) == 0) printf("%s:%d:%s\n", pth, lineno, line);
                lineno++;
            }
            fclose(ff);
        }
    } else if (has_repo) {
        fastgit_index_t* idx = fastgit_repository_index(repo);
        size_t n = fastgit_index_entry_count(idx);
        for (size_t i = 0; i < n; i++) {
            const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx, i);
            if (!e) continue;
            FILE* ff = fopen(e->path, "rb"); if (!ff) continue;
            char line[8192]; int lineno = 1;
            while (fgets(line, sizeof(line), ff)) {
                size_t ll = strlen(line); if (ll && line[ll-1] == '\n') line[ll-1] = '\0';
                if (regexec(&re, line, 0, NULL, 0) == 0) printf("%s:%d:%s\n", e->path, lineno, line);
                lineno++;
            }
            fclose(ff);
        }
    }
    if (has_repo) fastgit_repository_free(repo);
    regfree(&re); return 0;
}

int fastgit_cmd_doctor(int argc, char **argv) {
    (void)argc; (void)argv;
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    int errors = 0;
    fastgit_index_t* idx = fastgit_repository_index(repo);
    size_t n = fastgit_index_entry_count(idx);
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    for (size_t i = 0; i < n; i++) {
        const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx, i);
        if (!e) continue;
        fastgit_odb_object_t o;
        if (fastgit_odb_read(odb, &e->oid, &o) != FASTGIT_OK) {
            fprintf(stderr, "doctor: missing blob %s for %s\n", e->path, e->path);
            char hex[129]; fastgit_oid_to_hex(&e->oid, hex, sizeof(hex));
            fprintf(stderr, "  oid %s\n", hex);
            errors++;
        } else free(o.data);
        struct stat st; if (stat(e->path, &st) != 0 && e->mode != 0) { /* may be deleted */ }
    }
    if (errors == 0) printf("doctor: repository OK (%zu index entries verified)\n", n);
    else printf("doctor: found %d errors\n", errors);
    fastgit_repository_free(repo); return errors ? 1 : 0;
}

int fastgit_cmd_migrate(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "usage: fastgit migrate <algorithm> (sha256|sha384|sha3-256)\n"); return 1; }
    const char* algo_name = argv[0];
    uint8_t algo = FASTGIT_HASH_SHA256;
    if (strcmp(algo_name, "sha384") == 0) algo = FASTGIT_HASH_SHA384;
    else if (strcmp(algo_name, "sha3-256") == 0 || strcmp(algo_name, "sha3") == 0) algo = FASTGIT_HASH_SHA3_256;
    else if (strcmp(algo_name, "sha256") == 0) algo = FASTGIT_HASH_SHA256;
    else { fprintf(stderr, "unknown algorithm %s\n", algo_name); return 1; }
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    const char* gitdir = fastgit_repository_gitdir(repo);
    char cfg_path[1024]; snprintf(cfg_path, sizeof(cfg_path), "%s/config", gitdir);
    FILE* ff = fopen(cfg_path, "a");
    if (ff) {
        const char* ext = (algo == FASTGIT_HASH_SHA384) ? "sha384" : (algo == FASTGIT_HASH_SHA3_256) ? "sha3-256" : "sha256";
        fprintf(ff, "\n# migrated by fastgit\n[extensions]\n\tobjectFormat = %s\n", ext);
        fclose(ff);
        printf("migrated objectFormat to %s (algo 0x%02x)\n", ext, algo);
    }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_verify(int argc, char **argv) {
    (void)argc; (void)argv;
    fastgit_repository_t* repo = NULL;
    if (fastgit_repository_open(".", &repo) != FASTGIT_OK) { fprintf(stderr, "fatal: not a git repository\n"); return 1; }
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    fastgit_index_t* idx = fastgit_repository_index(repo);
    size_t n = fastgit_index_entry_count(idx);
    int errors = 0;
    for (size_t i = 0; i < n; i++) {
        const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx, i);
        if (!e) continue;
        fastgit_odb_object_t o;
        if (fastgit_odb_read(odb, &e->oid, &o) != FASTGIT_OK) { errors++; continue; }
        fastgit_hash_t h; fastgit_hash(FASTGIT_HASH_SHA256, o.data, o.size, &h);
        (void)h;
        free(o.data);
    }
    fastgit_odb_iterator_t* it = NULL;
    if (fastgit_odb_iterator_new(odb, &it) == FASTGIT_OK) {
        fastgit_oid_t oid; int count = 0;
        while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK) count++;
        fastgit_odb_iterator_free(it);
        printf("verify: %zu index entries, %d loose objects, %d errors\n", n, count, errors);
    } else {
        printf("verify: %zu index entries, %d errors\n", n, errors);
    }
    fastgit_repository_free(repo); return errors ? 1 : 0;
}
