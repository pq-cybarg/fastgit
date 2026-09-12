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

int fastgit_cmd_status(int argc, char **argv) {
    (void)argc; (void)argv;
    fastgit_repository_t* repo;
    fastgit_error_t err = fastgit_repository_open(".", &repo);
    if (err != FASTGIT_OK) {
        fprintf(stderr, "fatal: not a fastgit repository: %s\n", fastgit_error_string(err));
        return 1;
    }
    fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
    fastgit_status_entry_t* entries;
    size_t count;
    err = fastgit_status(wt, &entries, &count);
    if (err != FASTGIT_OK) {
        fprintf(stderr, "error: status failed: %s\n", fastgit_error_string(err));
        fastgit_repository_free(repo);
        return 1;
    }
    for (size_t i = 0; i < count; i++) {
        if (entries[i].worktree_status==0 && entries[i].index_status==0) continue;
        const char* status_str = "";
        switch (entries[i].worktree_status) {
            case FASTGIT_STATUS_WT_NEW: status_str = "??"; break;
            case FASTGIT_STATUS_WT_MODIFIED: status_str = " M"; break;
            case FASTGIT_STATUS_WT_DELETED: status_str = " D"; break;
            default: status_str = "  "; break;
        }
        printf("%s %s\n", status_str, entries[i].path);
    }
    fastgit_status_free(entries, count);
    fastgit_repository_free(repo);
    return 0;
}

int fastgit_cmd_diff(int argc, char **argv) {
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
    const char* path = argc>=1 && argv[0][0]!='-' ? argv[0] : NULL;
    fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
    char* out=NULL; fastgit_diff_worktree(wt, path, &out);
    if (out) { printf("%s", out); free(out); }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_log(int argc, char **argv) {
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
    fastgit_oid_t oid;
    const char* rev = argc>0? argv[0]: "HEAD";
    if (rev[0]=='-' ) rev="HEAD";
    if (fastgit_rev_parse(repo, rev, &oid)!=FASTGIT_OK) { fprintf(stderr,"fatal: ambiguous argument '%s'\n", rev); fastgit_repository_free(repo); return 1; }
    int limit=20; bool oneline=false;
    for(int i=0;i<argc;i++) if(strcmp(argv[i],"--oneline")==0) oneline=true;
    for(int n=0;n<limit;n++) {
        fastgit_object_t* obj=NULL;
        if (fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK) break;
        const fastgit_commit_t* c = fastgit_commit_parse(obj);
        char hex[129]; fastgit_oid_to_hex(&oid,hex,sizeof(hex));
        if (oneline) printf("%s %s\n", hex, c&&c->message?c->message:"");
        else {
            printf("commit %s\n", hex);
            if (c && c->author) printf("Author: %s <%s>\n", c->author->name, c->author->email);
            if (c && c->message) printf("\n    %s\n\n", c->message);
        }
        fastgit_oid_t next; bool has_next=false;
        if (c && c->parent_count>0) { next=c->parents[0]; has_next=true; }
        fastgit_object_free(obj);
        if (!has_next) break;
        oid=next;
    }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_ls_files(int argc, char **argv) {
    bool show_stage=false; const char* pathspec=NULL;
    for(int i=0;i<argc;i++){
        if(strcmp(argv[i],"--stage")==0 || strcmp(argv[i],"-s")==0) show_stage=true;
        else if(argv[i][0]!='-' && !pathspec) pathspec=argv[i];
    }
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
    fastgit_index_t* idx = fastgit_repository_index(repo);
    size_t n = fastgit_index_entry_count(idx);
    for(size_t i=0;i<n;i++) { const fastgit_index_entry_t* e = fastgit_index_entry_by_index(idx,i); if(!e) continue; if(pathspec && strcmp(e->path,pathspec)!=0) continue; if(show_stage){ char hex[129]; fastgit_oid_to_hex(&e->oid,hex,sizeof(hex)); printf("%06o %s %u\t%s\n", e->mode, hex, e->stage, e->path); } else printf("%s\n", e->path); }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_ls_tree(int argc, char **argv) {
    if (argc<1){fprintf(stderr,"usage: fastgit ls-tree <tree-ish>\n");return 1;}
    bool recursive=false; const char* treeish=NULL;
    for(int i=0;i<argc;i++){
        if(strcmp(argv[i],"-r")==0 || strcmp(argv[i],"--recursive")==0) recursive=true;
        else if(argv[i][0]!='-' && !treeish) treeish=argv[i];
        else if(argv[i][0]!='-' && treeish) { /* ignore extra pathspec for now */ }
    }
    if(!treeish) treeish=argv[argc-1];
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    fastgit_oid_t oid; if(fastgit_rev_parse(repo,treeish,&oid)!=FASTGIT_OK){fprintf(stderr,"fatal: bad tree %s\n",treeish);fastgit_repository_free(repo);return 1;}
    fastgit_object_t* obj=NULL; if(fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
    fastgit_obj_type_t t=fastgit_object_type(obj);
    if(t==FASTGIT_OBJ_COMMIT){ const fastgit_commit_t* c=fastgit_commit_parse(obj); if(c) oid=c->tree; fastgit_object_free(obj); if(fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}}
    if(!recursive){
        size_t cnt=fastgit_tree_entry_count(obj);
        for(size_t i=0;i<cnt;i++){ const fastgit_tree_entry_t* e=fastgit_tree_entry_by_index(obj,i); char hex[129]; fastgit_oid_to_hex(&e->oid,hex,sizeof(hex)); const char* tname = (e->mode == 040000 || (e->mode & 0170000) == 0040000) ? fastgit_obj_type_name(FASTGIT_OBJ_TREE) : fastgit_obj_type_name(FASTGIT_OBJ_BLOB); printf("%06o %s %s\t%s\n", e->mode, tname, hex, e->path); }
        fastgit_object_free(obj); fastgit_repository_free(repo); return 0;
    }
    typedef struct { fastgit_oid_t oid; char prefix[1024]; } ls_stack_t;
    ls_stack_t stack[256]; int sp=0;
    stack[sp].oid=oid; stack[sp].prefix[0]='\0'; sp++;
    struct pending { fastgit_oid_t oid; char prefix[1024]; struct pending* next; } *pending=NULL;
    size_t rcnt=fastgit_tree_entry_count(obj);
    for(size_t i=0;i<rcnt;i++){
        const fastgit_tree_entry_t* e=fastgit_tree_entry_by_index(obj,i);
        bool is_tree = (e->mode == 040000 || (e->mode & 0170000) == 0040000);
        if (!is_tree) {
            char hex[129]; fastgit_oid_to_hex(&e->oid,hex,sizeof(hex));
            printf("%06o %s %s\t%s\n", e->mode, fastgit_obj_type_name(FASTGIT_OBJ_BLOB), hex, e->path);
        }
        if(is_tree){
            char full[2048]; if(stack[0].prefix[0]) snprintf(full,sizeof(full),"%s/%s",stack[0].prefix,e->path); else snprintf(full,sizeof(full),"%s",e->path);
            struct pending* p=malloc(sizeof(*p)); p->oid=e->oid; snprintf(p->prefix,sizeof(p->prefix),"%s",full); p->next=pending; pending=p;
        }
    }
    fastgit_object_free(obj);
    while(pending){
        struct pending* cur=pending; pending=pending->next;
        fastgit_object_t* tobj=NULL;
        if(fastgit_object_lookup(repo,&cur->oid,&tobj)!=FASTGIT_OK){ free(cur); continue; }
        size_t ccnt=fastgit_tree_entry_count(tobj);
        for(size_t i=0;i<ccnt;i++){
            const fastgit_tree_entry_t* e=fastgit_tree_entry_by_index(tobj,i);
            bool is_tree = (e->mode == 040000 || (e->mode & 0170000) == 0040000);
            if (!is_tree) {
                char hex[129]; fastgit_oid_to_hex(&e->oid,hex,sizeof(hex));
                char full[2048]; snprintf(full,sizeof(full),"%s/%s",cur->prefix,e->path);
                printf("%06o %s %s\t%s\n", e->mode, fastgit_obj_type_name(FASTGIT_OBJ_BLOB), hex, full);
            }
            if(is_tree){
                char full[2048]; snprintf(full,sizeof(full),"%s/%s",cur->prefix,e->path);
                struct pending* p=malloc(sizeof(*p)); p->oid=e->oid; snprintf(p->prefix,sizeof(p->prefix),"%s",full); p->next=pending; pending=p;
            }
        }
        fastgit_object_free(tobj); free(cur);
    }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_show(int argc, char **argv) {
    const char* rev=argc>0?argv[0]:"HEAD";
    fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    fastgit_oid_t oid; if(fastgit_rev_parse(repo,rev,&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
    fastgit_object_t* obj=NULL; if(fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
    fwrite(fastgit_object_data(obj),1,fastgit_object_size(obj),stdout);
    fastgit_object_free(obj); fastgit_repository_free(repo); return 0;
}
