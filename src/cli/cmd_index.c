#include "fastgit/cli.h"
#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/index.h"
#include "fastgit/odb.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int fastgit_cmd_add(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "usage: fastgit add <pathspec>...\n");
        return 1;
    }
    fastgit_repository_t* repo;
    fastgit_error_t err = fastgit_repository_open(".", &repo);
    if (err != FASTGIT_OK) {
        fprintf(stderr, "fatal: not a fastgit repository: %s\n", fastgit_error_string(err));
        return 1;
    }
    fastgit_index_t* idx = fastgit_repository_index(repo);
    const char** paths = malloc((size_t)argc * sizeof(char*));
    if (!paths) { fastgit_repository_free(repo); return 1; }
    size_t npaths = 0;
    for (int i = 0; i < argc; i++) {
        const char* p = argv[i];
        if (p[0] == '-' && p[1] != '\0') {
            if (strcmp(p, "--") == 0) continue;
            continue;
        }
        if (strcmp(p, "--") == 0) continue;
        paths[npaths++] = p;
    }
    if (npaths == 0) { free((void*)paths); fastgit_repository_free(repo); fprintf(stderr, "nothing specified\n"); return 1; }
    if (npaths > 1) {
        err = fastgit_index_add_many(idx, paths, npaths);
        if (err != FASTGIT_OK) {
            fprintf(stderr, "error: add failed: %s\n", fastgit_error_string(err));
            fastgit_repository_free(repo);
            return 1;
        }
    } else {
        for (size_t i = 0; i < npaths; i++) {
            err = fastgit_index_add(idx, paths[i]);
            if (err != FASTGIT_OK) {
                fprintf(stderr, "error: pathspec '%s' did not match any files: %s\n", paths[i], fastgit_error_string(err));
                free((void*)paths);
                fastgit_repository_free(repo);
                return 1;
            }
        }
    }
    free((void*)paths);
    err = fastgit_index_write(idx);
    if (err != FASTGIT_OK) {
        fprintf(stderr, "error: could not write index: %s\n", fastgit_error_string(err));
        fastgit_repository_free(repo);
        return 1;
    }
    fastgit_repository_free(repo);
    return 0;
}

int fastgit_cmd_commit(int argc, char **argv) {
    const char* msg = NULL;
    const char* msg_file = NULL;
    bool amend = false;
    for (int i=0;i<argc;i++) {
        if (strcmp(argv[i],"-m")==0 && i+1<argc) msg=argv[++i];
        else if (strcmp(argv[i],"--amend")==0) amend=true;
        else if (strncmp(argv[i],"-m",2)==0) msg=argv[i]+2;
    }
    if (!msg) msg = "commit via fastgit";
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
    fastgit_index_t* idx = fastgit_repository_index(repo);
    fastgit_oid_t tree_oid;
    if (fastgit_index_write_tree(idx, &tree_oid)!=FASTGIT_OK) { fprintf(stderr,"fatal: write-tree failed\n"); fastgit_repository_free(repo); return 1; }
    fastgit_oid_t parent_oid; bool has_parent = (fastgit_reference_lookup(repo,"HEAD",&parent_oid)==FASTGIT_OK || fastgit_rev_parse(repo,"HEAD",&parent_oid)==FASTGIT_OK);
    fastgit_signature_t *author=NULL,*committer=NULL;
    fastgit_signature_default(&author); fastgit_signature_default(&committer);
    fastgit_commit_t cmt = {0};
    cmt.tree = tree_oid;
    if (has_parent) { cmt.parent_count=1; cmt.parents=&parent_oid; }
    cmt.author=author; cmt.committer=committer; cmt.message=(char*)msg;
    fastgit_object_t* cobj=NULL;
    if (fastgit_commit_create(&cmt,&cobj)!=FASTGIT_OK) { fprintf(stderr,"fatal: commit create failed\n"); fastgit_signature_free(author); fastgit_signature_free(committer); fastgit_repository_free(repo); return 1; }
    fastgit_buf_t buf; fastgit_object_serialize(cobj, FASTGIT_HASH_SHA256, &buf);
    fastgit_oid_t oid; fastgit_hash(FASTGIT_HASH_SHA256, buf.data, buf.len, (fastgit_hash_t*)&oid); oid.algo=FASTGIT_HASH_SHA256; oid.len=32;
    fastgit_odb_t* odb=fastgit_repository_odb(repo);
    fastgit_oid_t woid;
    {
        size_t hdr = 0; while (hdr < buf.len && ((uint8_t*)buf.data)[hdr] != '\0') hdr++;
        const void* content = hdr < buf.len ? (uint8_t*)buf.data + hdr + 1 : buf.data;
        size_t clen = hdr < buf.len ? buf.len - hdr - 1 : buf.len;
        fastgit_odb_write(odb, FASTGIT_OBJ_COMMIT, content, clen, &woid);
    }
    fastgit_reference_update(repo, "HEAD", &woid, msg);
    char hex[129]; fastgit_oid_to_hex(&woid,hex,sizeof(hex));
    printf("[%s %s] %s\n", "main", hex, msg);
    free(buf.data); fastgit_object_free(cobj);
    fastgit_repository_free(repo); (void)amend; (void)msg_file;
    return 0;
}

int fastgit_cmd_hash_object(int argc, char **argv) {
    const char* type_name = "blob";
    bool write = false;
    bool use_stdin = false;
    int file_start = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0) write = true;
        else if (strcmp(argv[i], "--stdin") == 0) use_stdin = true;
        else if (strcmp(argv[i], "-t") == 0 && i+1 < argc) { type_name = argv[i+1]; i++; }
        else if (argv[i][0]=='-') { /* ignore unknown */ }
        else { file_start = i; break; }
    }
    int nfiles = use_stdin ? 1 : (argc - file_start);
    if (nfiles <= 0 && !use_stdin) {
        fprintf(stderr, "usage: fastgit hash-object [-t <type>] [-w] [--stdin] [--] <file>...\n");
        return 1;
    }
    fastgit_obj_type_t type = fastgit_obj_type_from_name(type_name);
    fastgit_repository_t* repo = NULL;
    fastgit_odb_t* odb = NULL;
    if (write) {
        if (fastgit_repository_open(".", &repo) != FASTGIT_OK) {
            fprintf(stderr, "fatal: not a fastgit repository (and -w requires one)\n");
            return 1;
        }
        odb = fastgit_repository_odb(repo);
    }
    int ret = 0;
    for (int fi = 0; fi < nfiles; fi++) {
        const char* path = use_stdin ? "-" : argv[file_start + fi];
        void* data = NULL; size_t len = 0;
        if (use_stdin) {
            size_t cap = 8192; data = malloc(cap); len = 0;
            size_t n; while ((n = fread((char*)data+len,1,cap-len,stdin))>0) { len+=n; if(len==cap){ cap*=2; data=realloc(data,cap); } }
            if (!data) { ret=1; break; }
        } else {
            FILE* f = fopen(path, "rb");
            if (!f) { fprintf(stderr, "fatal: could not open '%s'\n", path); ret=1; continue; }
            fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
            if (sz<0) sz=0; len=(size_t)sz; data=malloc(len?len:1);
            if (len) fread(data,1,len,f); fclose(f);
        }
        fastgit_object_t* obj=NULL;
        fastgit_error_t err = fastgit_object_parse(type, data, len, &obj);
        free(data);
        if (err!=FASTGIT_OK) { fprintf(stderr,"error: parse failed\n"); ret=1; continue; }
        fastgit_oid_t oid;
        err = fastgit_object_hash(obj, FASTGIT_HASH_SHA256, &oid);
        if (err!=FASTGIT_OK) { fastgit_object_free(obj); ret=1; continue; }
        if (write && odb) {
            fastgit_oid_t woid;
            fastgit_odb_write(odb, type, obj->data, obj->size, &woid);
        }
        char hex[129]; fastgit_oid_to_hex(&oid, hex, sizeof(hex));
        printf("%s\n", hex);
        fastgit_object_free(obj);
    }
    if (repo) fastgit_repository_free(repo);
    return ret;
}

int fastgit_cmd_cat_file(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "usage: fastgit cat-file [-p|-t|-s] <object>\n");
        return 1;
    }
    const char* opt = NULL; const char* objname = NULL;
    for (int i=0;i<argc;i++) {
        if (argv[i][0]=='-') opt=argv[i];
        else objname=argv[i];
    }
    if (!objname) { fprintf(stderr,"fatal: need object\n"); return 1; }
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a fastgit repository\n"); return 1; }
    fastgit_oid_t oid;
    if (fastgit_rev_parse(repo,objname,&oid)!=FASTGIT_OK && fastgit_oid_from_hex(objname,&oid)!=FASTGIT_OK) { fprintf(stderr,"fatal: invalid object name %s\n",objname); fastgit_repository_free(repo); return 1; }
    fastgit_odb_object_t o;
    fastgit_error_t err = fastgit_odb_read(fastgit_repository_odb(repo), &oid, &o);
    if (err!=FASTGIT_OK) { fprintf(stderr,"fatal: object %s not found\n",objname); fastgit_repository_free(repo); return 1; }
    if (opt && strcmp(opt,"-t")==0) printf("%s\n", fastgit_obj_type_name(o.type));
    else if (opt && strcmp(opt,"-s")==0) printf("%zu\n", o.size);
    else fwrite(o.data,1,o.size,stdout);
    free(o.data); fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_write_tree(int argc, char **argv) {
    (void)argc; (void)argv;
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    fastgit_oid_t oid; if(fastgit_index_write_tree(fastgit_repository_index(repo),&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
    char hex[129]; fastgit_oid_to_hex(&oid,hex,sizeof(hex)); printf("%s\n",hex); fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_read_tree(int argc, char **argv) {
    if(argc<1){fprintf(stderr,"usage: fastgit read-tree <tree-ish>\n");return 1;}
    fastgit_repository_t* repo=NULL;
    if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    fastgit_oid_t oid; if(fastgit_rev_parse(repo,argv[0],&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
    fastgit_index_read_tree(fastgit_repository_index(repo),&oid);
    fastgit_index_write(fastgit_repository_index(repo));
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_commit_tree(int argc, char **argv) {
    if(argc<1){fprintf(stderr,"usage: fastgit commit-tree <tree> [-p <parent>] [-m <msg>]\n");return 1;}
    fastgit_repository_t* repo=NULL; fastgit_repository_open(".", &repo);
    fastgit_oid_t tree; fastgit_oid_from_hex(argv[0],&tree);
    const char* msg="commit-tree"; fastgit_oid_t parent; bool has_p=false;
    for(int i=1;i<argc;i++) if(strcmp(argv[i],"-p")==0 && i+1<argc) {fastgit_oid_from_hex(argv[++i],&parent); has_p=true;} else if(strcmp(argv[i],"-m")==0 && i+1<argc) msg=argv[++i];
    fastgit_signature_t *a=NULL,*c=NULL; fastgit_signature_default(&a); fastgit_signature_default(&c);
    fastgit_commit_t ct={0}; ct.tree=tree; if(has_p){ct.parent_count=1; ct.parents=&parent;} ct.author=a; ct.committer=c; ct.message=(char*)msg;
    fastgit_object_t* obj=NULL; fastgit_commit_create(&ct,&obj);
    fastgit_buf_t buf; fastgit_object_serialize(obj,FASTGIT_HASH_SHA256,&buf);
    fastgit_odb_t* odb=repo? fastgit_repository_odb(repo):NULL;
    fastgit_oid_t woid; if(odb) fastgit_odb_write(odb,FASTGIT_OBJ_COMMIT,buf.data,buf.len,&woid); else { fastgit_hash(FASTGIT_HASH_SHA256,buf.data,buf.len,(fastgit_hash_t*)&woid); woid.algo=FASTGIT_HASH_SHA256; woid.len=32; }
    char hex[129]; fastgit_oid_to_hex(&woid,hex,sizeof(hex)); printf("%s\n",hex);
    free(buf.data); fastgit_object_free(obj); if(repo) fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_update_index(int argc, char **argv) {
    fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    fastgit_index_t* idx=fastgit_repository_index(repo);
    for(int i=0;i<argc;i++) if(argv[i][0]!='-') fastgit_index_add(idx, argv[i]);
    fastgit_index_write(idx); fastgit_repository_free(repo); return 0;
}
