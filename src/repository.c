#include "fastgit/fastgit.h"
#include "fastgit/object.h"
#include "fastgit/odb.h"
#include "fastgit/index.h"
#include "fastgit/worktree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdatomic.h>

struct fastgit_repository {
    char* path;
    char* gitdir;
    bool bare;
    fastgit_odb_t* odb;
    fastgit_index_t* index;
    fastgit_worktree_t* worktree;
};

static _Atomic uint64_t g_objects_read = 0;
static _Atomic uint64_t g_objects_written = 0;
static _Atomic uint64_t g_bytes_read = 0;
static _Atomic uint64_t g_bytes_written = 0;

const char* fastgit_version(void) {
    return "0.1.0";
}

const char* fastgit_error_string(fastgit_error_t err) {
    switch (err) {
        case FASTGIT_OK: return "ok";
        case FASTGIT_ERROR: return "generic error";
        case FASTGIT_ENOMEM: return "out of memory";
        case FASTGIT_EINVAL: return "invalid argument";
        case FASTGIT_ENOENT: return "not found";
        case FASTGIT_EEXIST: return "already exists";
        case FASTGIT_EIO: return "I/O error";
        case FASTGIT_EBUSY: return "busy";
        case FASTGIT_EAGAIN: return "try again";
        case FASTGIT_EOVERFLOW: return "overflow";
        case FASTGIT_EUNSUPPORTED: return "unsupported";
        default: return "unknown";
    }
}

void fastgit_stats_reset(void) {
    atomic_store(&g_objects_read, 0);
    atomic_store(&g_objects_written, 0);
    atomic_store(&g_bytes_read, 0);
    atomic_store(&g_bytes_written, 0);
}
fastgit_stats_t fastgit_stats_get(void) {
    fastgit_stats_t s;
    s.objects_read = atomic_load(&g_objects_read);
    s.objects_written = atomic_load(&g_objects_written);
    s.bytes_read = atomic_load(&g_bytes_read);
    s.bytes_written = atomic_load(&g_bytes_written);
    s.cpu_time_ms = 0;
    s.wall_time_ms = 0;
    return s;
}
void fastgit_stats_add_read(uint64_t n, uint64_t bytes) {
    atomic_fetch_add(&g_objects_read, n);
    atomic_fetch_add(&g_bytes_read, bytes);
}
void fastgit_stats_add_written(uint64_t n, uint64_t bytes) {
    atomic_fetch_add(&g_objects_written, n);
    atomic_fetch_add(&g_bytes_written, bytes);
}

static void ensure_dir(const char* p) {
#if defined(_WIN32)
    mkdir(p);
#else
    mkdir(p, 0755);
#endif
}

fastgit_error_t fastgit_repository_init(const char* path, bool bare, fastgit_repository_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;
    fastgit_repository_t* repo = calloc(1, sizeof(*repo));
    if (!repo) return FASTGIT_ENOMEM;
    repo->path = strdup(path);
    repo->bare = bare;
    if (!repo->path) { free(repo); return FASTGIT_ENOMEM; }
    size_t gl = strlen(path) + 8;
    repo->gitdir = malloc(gl);
    if (!repo->gitdir) { free(repo->path); free(repo); return FASTGIT_ENOMEM; }
    if (bare) snprintf(repo->gitdir, gl, "%s", path);
    else snprintf(repo->gitdir, gl, "%s/.git", path);

    if (!bare) ensure_dir(repo->path);
    ensure_dir(repo->gitdir);
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s/objects", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/objects/pack", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/objects/info", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/refs", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/refs/heads", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/refs/tags", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/info", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/hooks", repo->gitdir); ensure_dir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/branches", repo->gitdir); ensure_dir(tmp);
    // HEAD
    {
        char head_path[4096]; snprintf(head_path, sizeof(head_path), "%s/HEAD", repo->gitdir);
        FILE* f = fopen(head_path, "w");
        if (f) { fputs("ref: refs/heads/main\n", f); fclose(f); }
    }
    // config (SHA256 objectFormat for git interop)
    {
        char cfg_path[4096]; snprintf(cfg_path, sizeof(cfg_path), "%s/config", repo->gitdir);
        FILE* f = fopen(cfg_path, "w");
        if (f) {
            fprintf(f, "[core]\n\trepositoryformatversion = 1\n\tfilemode = true\n\tbare = %s\n\tlogallrefupdates = %s\n", bare ? "true" : "false", bare ? "false" : "true");
            fputs("[extensions]\n\tobjectFormat = sha256\n", f);
            fclose(f);
        }
    }
    // description
    {
        char desc_path[4096]; snprintf(desc_path, sizeof(desc_path), "%s/description", repo->gitdir);
        FILE* f = fopen(desc_path, "w");
        if (f) { fputs("Unnamed repository; edit this file 'description' to name the repository.\n", f); fclose(f); }
    }
    // info/exclude
    {
        char excl_path[4096]; snprintf(excl_path, sizeof(excl_path), "%s/info/exclude", repo->gitdir);
        FILE* f = fopen(excl_path, "w");
        if (f) {
            fputs("# git ls-files --others --exclude-from=.git/info/exclude\n# Lines that start with '#' are comments.\n", f);
            fclose(f);
        }
    }
    // packed-refs (empty, git expects it optionally)
    {
        char pr_path[4096]; snprintf(pr_path, sizeof(pr_path), "%s/packed-refs", repo->gitdir);
        FILE* f = fopen(pr_path, "w");
        if (f) { fputs("# pack-refs with: peeled fully-peeled sorted\n", f); fclose(f); }
    }

    char odb_path[4096];
    snprintf(odb_path, sizeof(odb_path), "%s/objects", repo->gitdir);
    fastgit_error_t err = fastgit_odb_new(odb_path, &repo->odb);
    if (err != FASTGIT_OK) { free(repo->path); free(repo->gitdir); free(repo); return err; }
    char idx_path[4096];
    snprintf(idx_path, sizeof(idx_path), "%s/index", repo->gitdir);
    if (fastgit_index_open(idx_path, &repo->index) != FASTGIT_OK) {
        if (fastgit_index_new(&repo->index) == FASTGIT_OK) {
            free(repo->index->path);
            repo->index->path = strdup(idx_path);
        } else {
            repo->index = NULL;
        }
    }
    // worktree only for non-bare – share the same index object so bench_full Add→Diff is coherent
    if (!bare) {
        fastgit_worktree_new(path, &repo->worktree);
        if (repo->worktree && repo->index) {
            fastgit_worktree_attach_index(repo->worktree, repo->index);
        }
    }
    *out = repo;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_repository_open(const char* path, fastgit_repository_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;
    fastgit_repository_t* repo = calloc(1, sizeof(*repo));
    if (!repo) return FASTGIT_ENOMEM;
    repo->path = strdup(path);
    if (!repo->path) { free(repo); return FASTGIT_ENOMEM; }
    size_t gl = strlen(path) + 8;
    repo->gitdir = malloc(gl);
    if (!repo->gitdir) { free(repo->path); free(repo); return FASTGIT_ENOMEM; }
    snprintf(repo->gitdir, gl, "%s/.git", path);
    struct stat st;
    if (stat(repo->gitdir, &st) != 0) {
        // try bare: must have objects dir to be a repo, otherwise not found
        char bare_odb[4096];
        snprintf(bare_odb, sizeof(bare_odb), "%s/objects", path);
        if (stat(bare_odb, &st) != 0) {
            free(repo->gitdir);
            free(repo->path);
            free(repo);
            return FASTGIT_ENOENT;
        }
        free(repo->gitdir);
        repo->gitdir = strdup(path);
        repo->bare = true;
    }
    char odb_path[4096];
    snprintf(odb_path, sizeof(odb_path), "%s/objects", repo->gitdir);
    fastgit_error_t err = fastgit_odb_open(odb_path, &repo->odb);
    if (err != FASTGIT_OK) { free(repo->path); free(repo->gitdir); free(repo); return err; }
    char idx_path[4096];
    snprintf(idx_path, sizeof(idx_path), "%s/index", repo->gitdir);
    if (fastgit_index_open(idx_path, &repo->index) != FASTGIT_OK) {
        if (fastgit_index_new(&repo->index) == FASTGIT_OK) {
            free(repo->index->path);
            repo->index->path = strdup(idx_path);
        }
    }
    if (!repo->bare) {
        fastgit_worktree_open(path, &repo->worktree);
        if (repo->worktree && repo->index) {
            fastgit_worktree_attach_index(repo->worktree, repo->index);
        }
    }
    *out = repo;
    return FASTGIT_OK;
}

const char* fastgit_repository_path(fastgit_repository_t* repo) { return repo ? repo->path : NULL; }
const char* fastgit_repository_gitdir(fastgit_repository_t* repo) { return repo ? repo->gitdir : NULL; }

fastgit_error_t fastgit_repository_free(fastgit_repository_t* repo) {
    if (!repo) return FASTGIT_OK;
    if (repo->worktree) fastgit_worktree_free(repo->worktree);
    if (repo->index) fastgit_index_free(repo->index);
    if (repo->odb) fastgit_odb_free(repo->odb);
    free(repo->path);
    free(repo->gitdir);
    free(repo);
    return FASTGIT_OK;
}

fastgit_odb_t* fastgit_repository_odb(fastgit_repository_t* repo) { return repo ? repo->odb : NULL; }
fastgit_index_t* fastgit_repository_index(fastgit_repository_t* repo) { return repo ? repo->index : NULL; }
fastgit_worktree_t* fastgit_repository_worktree(fastgit_repository_t* repo) { return repo ? repo->worktree : NULL; }

// object shims
fastgit_error_t fastgit_object_free(fastgit_object_t* obj) {
    if (!obj) return FASTGIT_OK;
    if (obj->free_data && obj->data) obj->free_data(obj->data);
    else free(obj->data);
    free(obj);
    return FASTGIT_OK;
}
fastgit_obj_type_t fastgit_object_type(fastgit_object_t* obj) { return obj ? obj->type : 0; }
const fastgit_oid_t* fastgit_object_id(fastgit_object_t* obj) { return obj ? &obj->oid : NULL; }
const void* fastgit_object_data(fastgit_object_t* obj) { return obj ? obj->data : NULL; }
size_t fastgit_object_size(fastgit_object_t* obj) { return obj ? obj->size : 0; }

fastgit_error_t fastgit_object_lookup(fastgit_repository_t* repo, const fastgit_oid_t* oid, fastgit_object_t** out) {
    if (!repo || !oid || !out) return FASTGIT_EINVAL;
    fastgit_odb_object_t o;
    fastgit_error_t err = fastgit_odb_read(repo->odb, oid, &o);
    if (err != FASTGIT_OK) return err;
    fastgit_object_t* obj = calloc(1, sizeof(*obj));
    if (!obj) { free(o.data); return FASTGIT_ENOMEM; }
    obj->type = o.type; obj->size = o.size; obj->data = o.data; obj->free_data = free; obj->oid = *oid;
    *out = obj; return FASTGIT_OK;
}

fastgit_error_t fastgit_blob_create_from_buffer(const void* data, size_t len, fastgit_object_t** out) {
    return fastgit_object_parse(FASTGIT_OBJ_BLOB, data, len, out);
}

#include <dirent.h>
#include <unistd.h>
#include "fastgit/hash.h"

static fastgit_error_t ref_path_for_name(fastgit_repository_t* repo, const char* name, char* out, size_t out_len) {
    if (!repo || !name || !out) return FASTGIT_EINVAL;
    if (strncmp(name, "refs/", 5)==0) snprintf(out, out_len, "%s/%s", repo->gitdir, name);
    else if (strcmp(name, "HEAD")==0) snprintf(out, out_len, "%s/HEAD", repo->gitdir);
    else snprintf(out, out_len, "%s/refs/heads/%s", repo->gitdir, name);
    return FASTGIT_OK;
}
static fastgit_error_t read_ref_file(const char* path, char* hex, size_t hex_len) {
    FILE* f=fopen(path,"r"); if(!f) return FASTGIT_ENOENT;
    if(!fgets(hex, (int)hex_len, f)){ fclose(f); return FASTGIT_EIO; }
    fclose(f);
    size_t l=strlen(hex); while(l>0 && (hex[l-1]=='\n'||hex[l-1]=='\r')) hex[--l]=0;
    if (strncmp(hex,"ref: ",5)==0) return FASTGIT_EAGAIN; // symbolic
    return FASTGIT_OK;
}
fastgit_error_t fastgit_reference_lookup(fastgit_repository_t* repo, const char* name, fastgit_oid_t* out) {
    if(!repo||!name||!out) return FASTGIT_EINVAL;
    char path[4096]; ref_path_for_name(repo,name,path,sizeof(path));
    // resolve symbolic HEAD / refs
    for(int depth=0; depth<8; depth++){
        char hex[256]={0};
        fastgit_error_t er = read_ref_file(path, hex, sizeof(hex));
        if(er==FASTGIT_EAGAIN){
            FILE* f=fopen(path,"r"); if(!f) return FASTGIT_ENOENT;
            char line[512]={0}; fgets(line,sizeof(line),f); fclose(f);
            char* p=line+5; while(*p==' ') p++; size_t ll=strlen(p); while(ll>0 && (p[ll-1]=='\n'||p[ll-1]=='\r')) p[--ll]=0;
            // p is like refs/heads/main
            snprintf(path,sizeof(path),"%s/%s", repo->gitdir, p);
            continue;
        }
        if(er!=FASTGIT_OK){
            // try packed-refs
            char pr[4096]; snprintf(pr,sizeof(pr),"%s/packed-refs", repo->gitdir);
            FILE* pf=fopen(pr,"r"); if(!pf) return er;
            char line[512];
            while(fgets(line,sizeof(line),pf)){
                if(line[0]=='#'||line[0]=='^') continue;
                char ohex[128], rname[256];
                if(sscanf(line,"%127s %255s", ohex, rname)==2){
                    const char* want=name;
                    char full[512];
                    if(strncmp(name,"refs/",5)!=0 && strcmp(name,"HEAD")!=0){ snprintf(full,sizeof(full),"refs/heads/%s", name); want=full; }
                    if(strcmp(rname,want)==0 || strcmp(rname,name)==0){
                        fclose(pf);
                        return fastgit_oid_from_hex(ohex, out);
                    }
                }
            }
            fclose(pf);
            return FASTGIT_ENOENT;
        }
        return fastgit_oid_from_hex(hex, out);
    }
    return FASTGIT_EIO;
}
static void mkdir_p(const char* path){
    char tmp[4096]; strncpy(tmp, path, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
    size_t len = strlen(tmp);
    if(len==0) return;
    if(tmp[len-1]=='/') tmp[len-1]=0;
    for(char* p = tmp+1; *p; p++){
        if(*p=='/'){
            *p=0;
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p='/';
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}
static fastgit_error_t ensure_ref_dir(const char* ref_path){
    char dir[4096]; strncpy(dir, ref_path, sizeof(dir)); dir[sizeof(dir)-1]=0;
    char* slash=strrchr(dir,'/'); if(!slash) return FASTGIT_OK;
    *slash=0;
    mkdir_p(dir);
    return FASTGIT_OK;
}
static fastgit_error_t append_reflog(fastgit_repository_t* repo, const char* name, const fastgit_oid_t* old_oid, const fastgit_oid_t* new_oid, const char* msg){
    char log_path[4096];
    const char* rname=name;
    char full[512];
    if(strcmp(name,"HEAD")==0) rname="HEAD";
    else if(strncmp(name,"refs/",5)!=0){ snprintf(full,sizeof(full),"refs/heads/%s", name); rname=full; }
    else rname=name;
    snprintf(log_path,sizeof(log_path),"%s/logs/%s", repo->gitdir, rname);
    char dir[4096]; snprintf(dir,sizeof(dir),"%s/logs/%s", repo->gitdir, rname);
    char* sl=strrchr(dir,'/'); if(sl){ *sl=0; mkdir_p(dir); }
    FILE* f=fopen(log_path,"a");
    if(!f) return FASTGIT_OK; // reflog optional
    char old_hex[129]={0}, new_hex[129]={0};
    size_t hex_width = 64;
    if(old_oid) hex_width = old_oid->len ? old_oid->len*2 : 64;
    else if(new_oid) hex_width = new_oid->len ? new_oid->len*2 : 64;
    if(old_oid) fastgit_oid_to_hex(old_oid, old_hex, sizeof(old_hex));
    else memset(old_hex,'0',hex_width);
    if(new_oid) fastgit_oid_to_hex(new_oid, new_hex, sizeof(new_hex));
    else memset(new_hex,'0',hex_width);
    // signature
    fastgit_signature_t* sig=NULL;
    fastgit_signature_default(&sig);
    int64_t when = sig?sig->when:0;
    int off = sig?sig->offset:0;
    const char* sname = sig&&sig->name?sig->name:"fastgit";
    const char* semail = sig&&sig->email?sig->email:"fastgit@local";
    char tz[16]; snprintf(tz,sizeof(tz),"%+05d", off);
    if(sig) fastgit_signature_free(sig);
    fprintf(f,"%s %s %s <%s> %lld %s\t%s\n", old_hex, new_hex, sname, semail, (long long)when, tz, msg?msg:"update");
    fclose(f);
    return FASTGIT_OK;
}
fastgit_error_t fastgit_reference_create(fastgit_repository_t* repo, const char* name, const fastgit_oid_t* oid, bool force, const char* log_message){
    if(!repo||!name||!oid) return FASTGIT_EINVAL;
    // if updating HEAD symbolic, follow to target ref
    const char* eff = name;
    char resolved[512]="";
    if(strcmp(name,"HEAD")==0){
        char hp[4096]; snprintf(hp,sizeof(hp),"%s/HEAD", repo->gitdir);
        FILE* hf=fopen(hp,"r");
        if(hf){ char line[512]={0}; if(fgets(line,sizeof(line),hf)){ if(strncmp(line,"ref: ",5)==0){ char* p=line+5; while(*p==' ') p++; size_t ll=strlen(p); while(ll>0&&(p[ll-1]=='\n'||p[ll-1]=='\r')) p[--ll]=0; snprintf(resolved,sizeof(resolved),"%s",p); eff=resolved; } } fclose(hf); }
    }
    char path[4096]; ref_path_for_name(repo,eff,path,sizeof(path));
    if(!force){
        struct stat st; if(stat(path,&st)==0) return FASTGIT_EEXIST;
    }
    ensure_ref_dir(path);
    fastgit_oid_t old; bool had_old = fastgit_reference_lookup(repo,name,&old)==FASTGIT_OK;
    char hex[129]={0}; fastgit_oid_to_hex(oid, hex, sizeof(hex));
    FILE* f=fopen(path,"w"); if(!f) return FASTGIT_EIO;
    fprintf(f,"%s\n", hex); fclose(f);
    append_reflog(repo,name, had_old?&old:NULL, oid, log_message?log_message:"create");
    return FASTGIT_OK;
}
fastgit_error_t fastgit_reference_update(fastgit_repository_t* repo, const char* name, const fastgit_oid_t* oid, const char* log_message){
    return fastgit_reference_create(repo,name,oid,true,log_message?log_message:"update");
}
fastgit_error_t fastgit_reference_remove(fastgit_repository_t* repo, const char* name){
    if(!repo||!name) return FASTGIT_EINVAL;
    char path[4096]; ref_path_for_name(repo,name,path,sizeof(path));
    fastgit_oid_t old; bool had=fastgit_reference_lookup(repo,name,&old)==FASTGIT_OK;
    if(unlink(path)!=0) return FASTGIT_ENOENT;
    append_reflog(repo,name, had?&old:NULL, NULL, "delete");
    return FASTGIT_OK;
}
static void collect_refs_recursive(const char* base, const char* rel, char*** out, size_t* count, size_t* cap, const char* pattern){
    char dir[4096]; snprintf(dir,sizeof(dir),"%s/%s", base, rel);
    DIR* d=opendir(dir); if(!d) return;
    struct dirent* e;
    while((e=readdir(d))){
        if(strcmp(e->d_name,".")==0||strcmp(e->d_name,"..")==0) continue;
        char child_rel[4096]; if(rel[0]) snprintf(child_rel,sizeof(child_rel),"%s/%s", rel, e->d_name); else snprintf(child_rel,sizeof(child_rel),"%s", e->d_name);
        char full[4096]; snprintf(full,sizeof(full),"%s/%s", base, child_rel);
        struct stat st; if(stat(full,&st)!=0) continue;
        if(S_ISDIR(st.st_mode)){
            collect_refs_recursive(base, child_rel, out, count, cap, pattern);
        } else {
            char refname[4096]; snprintf(refname,sizeof(refname),"refs/%s", child_rel);
            if(pattern && strstr(refname, pattern)==NULL && strstr(child_rel, pattern)==NULL) continue;
            if(*count>=*cap){ *cap=*cap?*cap*2:32; *out=realloc(*out,*cap*sizeof(char*)); }
            (*out)[(*count)++]=strdup(refname);
        }
    }
    closedir(d);
}
fastgit_error_t fastgit_reference_list(fastgit_repository_t* repo, const char* pattern, char*** out, size_t* count){
    if(!repo||!out||!count) return FASTGIT_EINVAL;
    *out=NULL; *count=0; size_t cap=0;
    char base[4096]; snprintf(base,sizeof(base),"%s/refs", repo->gitdir);
    collect_refs_recursive(base, "", out, count, &cap, pattern);
    // also packed-refs
    char pr[4096]; snprintf(pr,sizeof(pr),"%s/packed-refs", repo->gitdir);
    FILE* f=fopen(pr,"r");
    if(f){
        char line[512];
        while(fgets(line,sizeof(line),f)){
            if(line[0]=='#'||line[0]=='^') continue;
            char ohex[128], rname[256]; if(sscanf(line,"%127s %255s", ohex, rname)!=2) continue;
            if(strncmp(rname,"refs/",5)!=0) continue;
            if(pattern && strstr(rname,pattern)==NULL) continue;
            // dedup: check already has file
            bool dup=false; for(size_t i=0;i<*count;i++) if(strcmp((*out)[i], rname)==0) {dup=true;break;}
            if(dup) continue;
            if(*count>=cap){ cap=cap?cap*2:32; *out=realloc(*out,cap*sizeof(char*)); }
            (*out)[(*count)++]=strdup(rname);
        }
        fclose(f);
    }
    return FASTGIT_OK;
}
void fastgit_reference_list_free(char** list, size_t count){ if(!list) return; for(size_t i=0;i<count;i++) free(list[i]); free(list); }
fastgit_error_t fastgit_rev_parse_single(fastgit_repository_t* repo, const char* spec, fastgit_oid_t* out){
    if(!repo||!spec||!out) return FASTGIT_EINVAL;
    if(strcmp(spec,"HEAD")==0) return fastgit_reference_lookup(repo,"HEAD",out);
    // try direct hex
    if(fastgit_oid_from_hex(spec,out)==FASTGIT_OK) return FASTGIT_OK;
    // try refs/heads/<spec>
    if(fastgit_reference_lookup(repo,spec,out)==FASTGIT_OK) return FASTGIT_OK;
    char full[512]; snprintf(full,sizeof(full),"refs/heads/%s", spec);
    if(fastgit_reference_lookup(repo,full,out)==FASTGIT_OK) return FASTGIT_OK;
    snprintf(full,sizeof(full),"refs/tags/%s", spec);
    if(fastgit_reference_lookup(repo,full,out)==FASTGIT_OK) return FASTGIT_OK;
    return FASTGIT_ENOENT;
}
fastgit_error_t fastgit_rev_parse(fastgit_repository_t* repo, const char* spec, fastgit_oid_t* out){
    // handle HEAD~N, HEAD^ etc minimal: support ~ and ^ suffix
    if(!repo||!spec||!out) return FASTGIT_EINVAL;
    char base[512]={0}; int tilde=-1;
    const char* t=strchr(spec,'~'); const char* c=strchr(spec,'^');
    const char* sep = t? t : c;
    if(sep){
        size_t blen=sep-spec; if(blen>=sizeof(base)) return FASTGIT_EINVAL;
        memcpy(base,spec,blen); base[blen]=0;
        if(*sep=='~'){
            tilde = sep[1]? atoi(sep+1):1;
        } else {
            tilde = 0; // ^ means first parent for now
            // ^N not fully handled
        }
    } else {
        return fastgit_rev_parse_single(repo,spec,out);
    }
    fastgit_oid_t cur;
    fastgit_error_t er=fastgit_rev_parse_single(repo, base[0]?base:"HEAD", &cur);
    if(er!=FASTGIT_OK) return er;
    for(int i=0;i<tilde;i++){
        fastgit_object_t* obj=NULL;
        if(fastgit_object_lookup(repo,&cur,&obj)!=FASTGIT_OK) return FASTGIT_ENOENT;
        if(fastgit_object_type(obj)!=FASTGIT_OBJ_COMMIT){ fastgit_object_free(obj); return FASTGIT_EINVAL; }
        const fastgit_commit_t* cmt = fastgit_commit_parse(obj);
        if(!cmt || cmt->parent_count==0){ fastgit_object_free(obj); return FASTGIT_ENOENT; }
        cur=cmt->parents[0];
        fastgit_object_free(obj);
    }
    *out=cur; return FASTGIT_OK;
}

fastgit_error_t fastgit_clone(const char* url, const char* path, const char* ref) {
    if (!url || !path) return FASTGIT_EINVAL;
    // file:// or local path clone via direct copy + fetch
    bool is_file = (strncmp(url,"file://",7)==0) || (url[0]=='/' || strncmp(url,"./",2)==0 || strncmp(url,"../",3)==0) || (access(url, F_OK)==0);
    if (is_file) {
        const char* src = url;
        if (strncmp(url,"file://",7)==0) src = url+7;
        // init destination
        fastgit_repository_t* dst=NULL;
        fastgit_error_t er = fastgit_repository_init(path, false, &dst);
        if (er != FASTGIT_OK) return er;
        fastgit_repository_free(dst);
        // reuse fetch logic via temp repo open
        fastgit_repository_t* repo=NULL;
        er = fastgit_repository_open(path, &repo);
        if (er != FASTGIT_OK) return er;
        // use existing file fetch helpers: directly copy objects/refs
        // open source repo
        fastgit_repository_t* src_repo=NULL;
        // src may be <path>/.git or <path>
        char src_git[4096];
        char probe[4096]; snprintf(probe,sizeof(probe),"%s/.git",src);
        struct stat st; if (stat(probe,&st)==0) snprintf(src_git,sizeof(src_git),"%s",probe);
        else if (stat(src,&st)==0 && S_ISDIR(st.st_mode)) {
            // if src is bare (has objects), use as gitdir
            char o[4096]; snprintf(o,sizeof(o),"%s/objects",src);
            if (stat(o,&st)==0) snprintf(src_git,sizeof(src_git),"%s",src);
            else snprintf(src_git,sizeof(src_git),"%s/.git",src);
        } else snprintf(src_git,sizeof(src_git),"%s",src);
        // simple copy via fastgit_fetch using file://
        char file_url[4096]; snprintf(file_url,sizeof(file_url),"file://%s",src);
        er = fastgit_fetch(repo, file_url, NULL);
        // checkout HEAD if ref specified or default
        if (er==FASTGIT_OK) {
            const char* want = ref ? ref : "HEAD";
            fastgit_oid_t oid;
            if (fastgit_rev_parse(repo, want, &oid)==FASTGIT_OK) {
                // update HEAD and checkout
                fastgit_worktree_t* wt = fastgit_repository_worktree(repo);
                // get commit tree
                fastgit_object_t* obj=NULL;
                if (fastgit_object_lookup(repo,&oid,&obj)==FASTGIT_OK) {
                    fastgit_oid_t tree_oid = oid;
                    if (fastgit_object_type(obj)==FASTGIT_OBJ_COMMIT) {
                        const fastgit_commit_t* c = fastgit_commit_parse(obj);
                        if (c) tree_oid = c->tree;
                    }
                    fastgit_object_free(obj);
                    // checkout tree and update index
                    fastgit_index_t* idx = fastgit_repository_index(repo);
                    if (idx) {
                        fastgit_index_read_tree(idx, &tree_oid);
                        fastgit_index_write(idx);
                    }
                    if (wt) {
                        fastgit_checkout_tree(wt, &tree_oid, true);
                    }
                }
            }
        }
        fastgit_repository_free(repo);
        (void)src_git;
        return er;
    }
    return FASTGIT_EUNSUPPORTED;
}
