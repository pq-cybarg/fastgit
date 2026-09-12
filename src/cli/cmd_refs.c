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
#include <unistd.h>
#include <sys/stat.h>

int fastgit_cmd_branch(int argc, char **argv) {
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
    if (argc==0) {
        char** list; size_t cnt;
        fastgit_reference_list(repo,NULL,&list,&cnt);
        for(size_t i=0;i<cnt;i++) {
            const char* n=list[i];
            if(strncmp(n,"refs/heads/",11)==0) n+=11;
            else if(strncmp(n,"heads/",6)==0) n+=6;
            if(strstr(list[i],"heads/")) printf("  %s\n", n);
        }
        fastgit_reference_list_free(list,cnt);
    } else if (argc>=1 && strcmp(argv[0],"-d")==0 && argc>=2) {
        char ref[512]; snprintf(ref,sizeof(ref),"refs/heads/%s",argv[1]);
        fastgit_reference_remove(repo,ref);
    } else {
        const char* name=argv[0]; const char* start = argc>=2? argv[1]:"HEAD";
        fastgit_oid_t oid; if(fastgit_rev_parse(repo,start,&oid)!=FASTGIT_OK){fprintf(stderr,"fatal: bad start %s\n",start); fastgit_repository_free(repo); return 1;}
        char ref[512]; snprintf(ref,sizeof(ref),"refs/heads/%s",name);
        fastgit_reference_create(repo,ref,&oid,false,"branch");
    }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_tag(int argc, char **argv) {
    fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    if(argc==0){ char** l; size_t n; fastgit_reference_list(repo,"refs/tags/",&l,&n); for(size_t i=0;i<n;i++) printf("%s\n", l[i]+10); fastgit_reference_list_free(l,n); fastgit_repository_free(repo); return 0; }
    const char* tname=argv[0]; fastgit_oid_t oid; const char* target=argc>=2?argv[1]:"HEAD";
    if(fastgit_rev_parse(repo,target,&oid)!=FASTGIT_OK){fastgit_repository_free(repo);return 1;}
    char ref[512]; snprintf(ref,sizeof(ref),"refs/tags/%s",tname);
    fastgit_reference_create(repo,ref,&oid,false,"tag"); fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_reset(int argc, char **argv) {
    if(argc<1){fprintf(stderr,"usage: fastgit reset [<commit>]\n");return 1;}
    fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    fastgit_oid_t oid; if(fastgit_rev_parse(repo,argv[0],&oid)!=FASTGIT_OK){fprintf(stderr,"fatal: bad rev\n");fastgit_repository_free(repo);return 1;}
    fastgit_reference_update(repo,"HEAD",&oid,"reset");
    fastgit_object_t* obj=NULL; if(fastgit_object_lookup(repo,&oid,&obj)==FASTGIT_OK){
        fastgit_oid_t tree=oid; const fastgit_commit_t* c=fastgit_commit_parse(obj); if(c) tree=c->tree;
        fastgit_index_read_tree(fastgit_repository_index(repo),&tree); fastgit_index_write(fastgit_repository_index(repo));
        fastgit_object_free(obj);
    }
    fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_rm(int argc, char **argv) {
    fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)!=FASTGIT_OK){fprintf(stderr,"fatal: not a git repository\n");return 1;}
    for(int i=0;i<argc;i++) if(argv[i][0]!='-'){ fastgit_index_remove(fastgit_repository_index(repo), argv[i],0); unlink(argv[i]); }
    fastgit_index_write(fastgit_repository_index(repo)); fastgit_repository_free(repo); return 0;
}

int fastgit_cmd_mv(int argc, char **argv) {
    if(argc<2){fprintf(stderr,"usage: fastgit mv <src> <dst>\n");return 1;}
    const char* src=argv[0]; const char* dst=argv[1];
    if(rename(src,dst)!=0){perror("rename"); return 1;}
    fastgit_repository_t* repo=NULL; if(fastgit_repository_open(".", &repo)==FASTGIT_OK){
        fastgit_index_remove(fastgit_repository_index(repo),src,0);
        fastgit_index_add(fastgit_repository_index(repo),dst);
        fastgit_index_write(fastgit_repository_index(repo));
        fastgit_repository_free(repo);
    }
    return 0;
}

int fastgit_cmd_checkout(int argc, char **argv) {
    if (argc<1) { fprintf(stderr,"usage: fastgit checkout <branch> [<paths>...]\n"); return 1; }
    int arg_off=0;
    const char* target = argv[0];
    int path_start=-1;
    if (strcmp(target,"--")==0) {
        arg_off=1;
        path_start=1;
        target=NULL;
    } else if (argc>=2 && strcmp(argv[1],"--")==0) {
        path_start=2;
    }
    fastgit_repository_t* repo=NULL;
    if (fastgit_repository_open(".", &repo)!=FASTGIT_OK) { fprintf(stderr,"fatal: not a git repository\n"); return 1; }
    if (path_start>=0) {
        fastgit_index_t* idx = fastgit_repository_index(repo);
        (void)fastgit_repository_odb(repo);
        const char* wt_path = fastgit_repository_worktree(repo) ? "." : ".";
        (void)wt_path;
        for(int i=path_start;i<argc;i++) {
            const char* pth = argv[i];
            fastgit_index_entry_t* ent=NULL;
            if (fastgit_index_find(idx, pth, 0, &ent)!=FASTGIT_OK || !ent) { fprintf(stderr,"error: pathspec '%s' did not match\n", pth); continue; }
            fastgit_object_t* blob=NULL;
            if (fastgit_object_lookup(repo,&ent->oid,&blob)==FASTGIT_OK) {
                size_t sz = fastgit_object_size(blob); const void* data = fastgit_object_data(blob);
                char* dup=strdup(pth); char* sl=strrchr(dup,'/');
                if (sl) { *sl='\0';
                    char tmp2[4096]; strncpy(tmp2,dup,sizeof(tmp2)-1); tmp2[sizeof(tmp2)-1]=0;
                    for(char* p=tmp2+1;*p;p++) if(*p=='/'){*p=0; mkdir(tmp2,0755); *p='/';}
                    mkdir(tmp2,0755);
                }
                free(dup);
                FILE* f=fopen(pth,"wb"); if(f){ fwrite(data,1,sz,f); fclose(f); }
                fastgit_object_free(blob);
            }
        }
        fastgit_repository_free(repo); return 0;
    }
    if (strcmp(target,"--")==0) { fastgit_repository_free(repo); return 1; }
    (void)arg_off;
    fastgit_oid_t oid;
    if (fastgit_rev_parse(repo,target,&oid)!=FASTGIT_OK) {
        fprintf(stderr,"error: pathspec '%s' did not match\n", target);
        fastgit_repository_free(repo); return 1;
    }
    fastgit_object_t* obj=NULL;
    if (fastgit_object_lookup(repo,&oid,&obj)!=FASTGIT_OK) { fastgit_repository_free(repo); return 1; }
    fastgit_oid_t tree_oid = oid;
    const fastgit_commit_t* c = fastgit_commit_parse(obj);
    if (c) tree_oid = c->tree;
    fastgit_index_t* idx = fastgit_repository_index(repo);
    fastgit_index_read_tree(idx, &tree_oid);
    fastgit_index_write(idx);
    fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
    fastgit_checkout_head(wt, true);
    fastgit_reference_update(repo,"HEAD",&oid,target);
    fastgit_object_free(obj); fastgit_repository_free(repo); return 0;
}
