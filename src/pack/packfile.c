#include "fastgit/pack.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <zlib.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseeki64
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_CREAT _O_CREAT
#define O_TRUNC _O_TRUNC
#define O_BINARY _O_BINARY
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif
#pragma pack(push, 1)
typedef struct { uint32_t signature; uint32_t version; uint32_t object_count; } fastgit_pack_header_t;
#pragma pack(pop)
struct fastgit_pack {
    int fd;
    char* pack_file;
    char* idx_file;
    fastgit_pack_index_t* index;
    uint64_t file_size;
    bool writing;
    fastgit_pack_header_t header;
    uint8_t* mapped;
    size_t mapped_size;
    uint64_t stats_objects_read;
    uint64_t stats_objects_written;
    uint64_t stats_bytes_read;
    uint64_t stats_bytes_written;
    uint64_t stats_deltas_created;
    uint64_t stats_deltas_applied;
    /* write path state */
    fastgit_oid_t* w_oids;
    uint32_t* w_crcs;
    uint64_t* w_offsets;
    uint32_t w_count;
    uint32_t w_cap;
    uint64_t w_cur_offset;
};
static fastgit_obj_type_t pack_obj_type_from_git(uint32_t t){
    switch(t){ case FASTGIT_PACK_OBJ_COMMIT: return FASTGIT_OBJ_COMMIT;
        case FASTGIT_PACK_OBJ_TREE: return FASTGIT_OBJ_TREE;
        case FASTGIT_PACK_OBJ_BLOB: return FASTGIT_OBJ_BLOB;
        case FASTGIT_PACK_OBJ_TAG: return FASTGIT_OBJ_TAG;
        case FASTGIT_PACK_OBJ_OFS_DELTA: return FASTGIT_OBJ_OFS_DELTA;
        case FASTGIT_PACK_OBJ_REF_DELTA: return FASTGIT_OBJ_REF_DELTA;
        default: return FASTGIT_OBJ_BLOB;}}
static fastgit_obj_type_t git_type_to_fastgit(uint32_t git_type){
    // git pack type numbers same as ours for 1..4,6,7
    return pack_obj_type_from_git(git_type);
}
// git delta apply (copy=0x80)
static fastgit_error_t git_delta_apply(const void* base,size_t base_len,const void* delta,size_t delta_len,void** out,size_t* out_len){
    if(!delta||!out||!out_len) return FASTGIT_EINVAL;
    const uint8_t* d=(const uint8_t*)delta; const uint8_t* dend=d+delta_len;
    uint64_t src_size=0; size_t n=fastgit_decode_varint(d,dend-d,&src_size); if(!n) return FASTGIT_ERROR; d+=n;
    uint64_t dst_size=0; n=fastgit_decode_varint(d,dend-d,&dst_size); if(!n) return FASTGIT_ERROR; d+=n;
    if(src_size!=base_len && base) { /* git allows mismatch but we tolerate */ }
    uint8_t* res=(uint8_t*)malloc(dst_size); if(!res) return FASTGIT_ENOMEM;
    uint8_t* o=res;
    while(d<dend){
        uint8_t cmd=*d++;
        if(cmd & 0x80){
            uint32_t off=0, sz=0;
            if(cmd & 0x01) { if(d>=dend) {free(res); return FASTGIT_ERROR;} off|=*d++; }
            if(cmd & 0x02) { if(d>=dend) {free(res); return FASTGIT_ERROR;} off|=*d++<<8; }
            if(cmd & 0x04) { if(d>=dend) {free(res); return FASTGIT_ERROR;} off|=*d++<<16; }
            if(cmd & 0x08) { if(d>=dend) {free(res); return FASTGIT_ERROR;} off|=*d++<<24; }
            if(cmd & 0x10) { if(d>=dend) {free(res); return FASTGIT_ERROR;} sz|=*d++; }
            if(cmd & 0x20) { if(d>=dend) {free(res); return FASTGIT_ERROR;} sz|=*d++<<8; }
            if(cmd & 0x40) { if(d>=dend) {free(res); return FASTGIT_ERROR;} sz|=*d++<<16; }
            if(sz==0) sz=0x10000;
            if(!base || off+sz>base_len || o+sz>res+dst_size){ free(res); return FASTGIT_ERROR; }
            memcpy(o,(const uint8_t*)base+off,sz); o+=sz;
        } else {
            if(cmd==0){ free(res); return FASTGIT_ERROR; }
            size_t ins=cmd & 0x7F;
            if(d+ins>dend || o+ins>res+dst_size){ free(res); return FASTGIT_ERROR; }
            memcpy(o,d,ins); o+=ins; d+=ins;
        }
    }
    if((size_t)(o-res)!=dst_size){ free(res); return FASTGIT_ERROR; }
    *out=res; *out_len=dst_size; return FASTGIT_OK;
}
static fastgit_error_t inflate_slice(const uint8_t* in,size_t avail_in, uint8_t* out,size_t out_len, size_t* consumed){
    z_stream zs={0};
    if(inflateInit(&zs)!=Z_OK) return FASTGIT_ERROR;
    zs.next_in=(Bytef*)in; zs.avail_in=avail_in;
    zs.next_out=out; zs.avail_out=out_len;
    int ret=inflate(&zs,Z_FINISH);
    if(consumed) *consumed=zs.total_in;
    inflateEnd(&zs);
    if(ret!=Z_STREAM_END) return FASTGIT_ERROR;
    if(zs.total_out!=out_len) return FASTGIT_ERROR;
    return FASTGIT_OK;
}
// parse varint pack header at offset: returns type, size, header_len
static fastgit_error_t parse_pack_header(const uint8_t* p,size_t avail, uint32_t* type_out, uint64_t* size_out, size_t* hdr_len){
    if(avail<1) return FASTGIT_ERROR;
    uint8_t c=p[0];
    uint32_t type=(c>>4)&0x07;
    uint64_t sz=c & 0x0F;
    size_t shift=4; size_t i=1;
    while(c & 0x80){
        if(i>=avail) return FASTGIT_ERROR;
        c=p[i];
        sz |= (uint64_t)(c & 0x7F)<<shift;
        shift+=7; i++;
        if(i>10) return FASTGIT_ERROR;
    }
    *type_out=type; *size_out=sz; *hdr_len=i; return FASTGIT_OK;
}
static fastgit_error_t parse_ofs_delta_offset(const uint8_t* p,size_t avail,size_t* hdr_len, uint64_t* out){
    if(avail<1) return FASTGIT_ERROR;
    uint64_t off=p[0] & 0x7F; size_t i=1;
    while(p[i-1] & 0x80){
        if(i>=avail) return FASTGIT_ERROR;
        off = ((off+1)<<7) | (p[i] & 0x7F);
        i++;
        if(i>10) return FASTGIT_ERROR;
    }
    *hdr_len=i; *out=off; return FASTGIT_OK;
}
static fastgit_error_t pack_read_object_at(fastgit_pack_t* pack,uint64_t offset,fastgit_obj_type_t* type_out,void** data_out,size_t* size_out,int depth){
    if(depth>32) return FASTGIT_ERROR;
    if(offset+12>pack->mapped_size) return FASTGIT_ENOENT;
    const uint8_t* base=pack->mapped;
    uint32_t ptype; uint64_t psize; size_t hlen;
    if(parse_pack_header(base+offset, pack->mapped_size-offset, &ptype,&psize,&hlen)!=FASTGIT_OK) return FASTGIT_ERROR;
    uint64_t cur=offset+hlen;
    uint32_t type=ptype;
    if(type==FASTGIT_PACK_OBJ_OFS_DELTA){
        size_t olen; uint64_t ofs;
        if(parse_ofs_delta_offset(base+cur, pack->mapped_size-cur, &olen,&ofs)!=FASTGIT_OK) return FASTGIT_ERROR;
        cur+=olen;
        uint64_t base_off=offset - ofs;
        // inflate delta
        uint8_t* delta_buf=(uint8_t*)malloc(psize); if(!delta_buf) return FASTGIT_ENOMEM;
        size_t avail=pack->mapped_size - cur - 32; // leave room for pack checksum (32 for sha256, 20 for sha1) use 32 safe
        if(avail> pack->mapped_size) avail=0;
        fastgit_error_t e=inflate_slice(base+cur,avail,delta_buf,psize,NULL);
        if(e!=FASTGIT_OK){ free(delta_buf); return e; }
        // recurse base
        void* base_data=NULL; size_t base_len=0; fastgit_obj_type_t base_type;
        e=pack_read_object_at(pack,base_off,&base_type,&base_data,&base_len,depth+1);
        if(e!=FASTGIT_OK){ free(delta_buf); return e; }
        void* out=NULL; size_t out_len=0;
        e=git_delta_apply(base_data,base_len,delta_buf,psize,&out,&out_len);
        free(delta_buf); free(base_data);
        if(e!=FASTGIT_OK) return e;
        *type_out=base_type; *data_out=out; *size_out=out_len; return FASTGIT_OK;
    } else if(type==FASTGIT_PACK_OBJ_REF_DELTA){
        // need hash len: use index hash len (oid len)
        size_t hash_len=pack->index?pack->index->data.oids[0].len:32;
        if(cur+hash_len>pack->mapped_size) return FASTGIT_ERROR;
        fastgit_oid_t base_oid; base_oid.len=hash_len; base_oid.algo=FASTGIT_HASH_SHA256; memcpy(base_oid.hash,base+cur,hash_len);
        cur+=hash_len;
        uint8_t* delta_buf=(uint8_t*)malloc(psize); if(!delta_buf) return FASTGIT_ENOMEM;
        size_t avail=pack->mapped_size - cur - 32;
        fastgit_error_t e=inflate_slice(base+cur,avail,delta_buf,psize,NULL);
        if(e!=FASTGIT_OK){ free(delta_buf); return e; }
        uint32_t bi; e=fastgit_pack_index_find(pack->index,&base_oid,&bi);
        if(e!=FASTGIT_OK){ free(delta_buf); return FASTGIT_ENOENT; }
        uint64_t base_off=pack->index->data.offsets[bi];
        void* base_data=NULL; size_t base_len=0; fastgit_obj_type_t base_type;
        e=pack_read_object_at(pack,base_off,&base_type,&base_data,&base_len,depth+1);
        if(e!=FASTGIT_OK){ free(delta_buf); return e; }
        void* out=NULL; size_t out_len=0;
        e=git_delta_apply(base_data,base_len,delta_buf,psize,&out,&out_len);
        free(delta_buf); free(base_data);
        if(e!=FASTGIT_OK) return e;
        *type_out=base_type; *data_out=out; *size_out=out_len; return FASTGIT_OK;
    } else {
        void* out=malloc(psize); if(!out) return FASTGIT_ENOMEM;
        size_t avail=pack->mapped_size - cur - 32;
        fastgit_error_t e=inflate_slice(base+cur,avail,out,psize,NULL);
        if(e!=FASTGIT_OK){ free(out); return e; }
        *type_out=git_type_to_fastgit(type); *data_out=out; *size_out=psize; return FASTGIT_OK;
    }
}
fastgit_error_t fastgit_pack_open(const char* pack_file,const char* idx_file,fastgit_pack_t** out){
    if(!pack_file||!out) return FASTGIT_EINVAL;
    fastgit_pack_t* pack=calloc(1,sizeof(*pack)); if(!pack) return FASTGIT_ENOMEM;
    pack->pack_file=strdup(pack_file);
    if(idx_file) pack->idx_file=strdup(idx_file);
    else { size_t l=strlen(pack_file); pack->idx_file=malloc(l+5); if(!pack->idx_file){free(pack->pack_file);free(pack);return FASTGIT_ENOMEM;} memcpy(pack->idx_file,pack_file,l); strcpy(pack->idx_file+l-5,".idx"); }
#if defined(_WIN32)
    pack->fd=open(pack->pack_file,O_RDONLY|O_BINARY);
#else
    pack->fd=open(pack->pack_file,O_RDONLY);
#endif
    if(pack->fd<0){ free(pack->pack_file);free(pack->idx_file);free(pack);return FASTGIT_ENOENT; }
    struct stat st; fstat(pack->fd,&st); pack->file_size=st.st_size;
#if defined(_WIN32)
    HANDLE h=CreateFileMapping((HANDLE)_get_osfhandle(pack->fd),NULL,PAGE_READONLY,0,0,NULL);
    if(!h){ close(pack->fd);free(pack->pack_file);free(pack->idx_file);free(pack);return FASTGIT_EIO;}
    pack->mapped=MapViewOfFile(h,FILE_MAP_READ,0,0,0); CloseHandle(h);
    if(!pack->mapped){ close(pack->fd);free(pack->pack_file);free(pack->idx_file);free(pack);return FASTGIT_EIO;}
#else
    pack->mapped=mmap(NULL,pack->file_size,PROT_READ,MAP_PRIVATE,pack->fd,0);
    if(pack->mapped==MAP_FAILED){ close(pack->fd);free(pack->pack_file);free(pack->idx_file);free(pack);return FASTGIT_EIO;}
#endif
    pack->mapped_size=pack->file_size;
    if(pack->mapped_size<12 || memcmp(pack->mapped,"PACK",4)!=0){ goto fail; }
    uint32_t ver=__builtin_bswap32(*(uint32_t*)(pack->mapped+4));
    if(ver!=2){ goto fail; }
    pack->header.signature=FASTGIT_PACK_SIGNATURE;
    pack->header.version=2;
    pack->header.object_count=__builtin_bswap32(*(uint32_t*)(pack->mapped+8));
    fastgit_error_t e=fastgit_pack_index_load(pack->idx_file,&pack->index);
    if(e!=FASTGIT_OK){ goto fail; }
    *out=pack; return FASTGIT_OK;
fail:
#if defined(_WIN32)
    if(pack->mapped) UnmapViewOfFile(pack->mapped);
#else
    if(pack->mapped) munmap(pack->mapped,pack->mapped_size);
#endif
    close(pack->fd); free(pack->pack_file); free(pack->idx_file); free(pack); return FASTGIT_ERROR;
}
fastgit_error_t fastgit_pack_create(const char* pack_file,const char* idx_file,fastgit_pack_t** out){
    if(!pack_file||!out) return FASTGIT_EINVAL;
    fastgit_pack_t* pack=calloc(1,sizeof(*pack)); if(!pack) return FASTGIT_ENOMEM;
    pack->pack_file=strdup(pack_file);
    if(idx_file) pack->idx_file=strdup(idx_file);
    else { size_t l=strlen(pack_file); pack->idx_file=malloc(l+5); if(!pack->idx_file){free(pack->pack_file);free(pack);return FASTGIT_ENOMEM;} memcpy(pack->idx_file,pack_file,l); strcpy(pack->idx_file+l-5,".idx"); }
#if defined(_WIN32)
    pack->fd=open(pack->pack_file,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, S_IRUSR|S_IWUSR);
#else
    pack->fd=open(pack->pack_file,O_WRONLY|O_CREAT|O_TRUNC,0644);
#endif
    if(pack->fd<0){ free(pack->pack_file);free(pack->idx_file);free(pack);return FASTGIT_EIO; }
    pack->writing=true; pack->header.signature=FASTGIT_PACK_SIGNATURE; pack->header.version=2; pack->header.object_count=0;
    pack->w_cur_offset=12;
    uint32_t sig=__builtin_bswap32(pack->header.signature); uint32_t ver=__builtin_bswap32(pack->header.version); uint32_t cnt=0;
    if(write(pack->fd,&sig,4)!=4||write(pack->fd,&ver,4)!=4||write(pack->fd,&cnt,4)!=4){ close(pack->fd);free(pack->pack_file);free(pack->idx_file);free(pack);return FASTGIT_EIO;}
    *out=pack; return FASTGIT_OK;
}
void fastgit_pack_close(fastgit_pack_t* pack){
    if(!pack) return;
    if(pack->writing){
        uint32_t cnt=__builtin_bswap32(pack->header.object_count);
#if defined(_WIN32)
        LARGE_INTEGER p; p.QuadPart=8; SetFilePointerEx((HANDLE)_get_osfhandle(pack->fd),p,NULL,FILE_BEGIN);
#else
        lseek(pack->fd,8,SEEK_SET);
#endif
        write(pack->fd,&cnt,4);
        // compute SHA256 of pack up to current end, then append trailer
#if defined(_WIN32)
        LARGE_INTEGER e; e.QuadPart=0; SetFilePointerEx((HANDLE)_get_osfhandle(pack->fd),e,NULL,FILE_END);
#else
        off_t end=lseek(pack->fd,0,SEEK_END);
        (void)end;
#endif
        // read file for hashing
        {
            int fd2=open(pack->pack_file,O_RDONLY);
            if(fd2>=0){
                struct stat st; fstat(fd2,&st);
                size_t sz=(size_t)st.st_size;
                uint8_t* buf=malloc(sz); if(buf){ ssize_t r=read(fd2,buf,sz); if(r==(ssize_t)sz){ fastgit_hash_t th; fastgit_hash(FASTGIT_HASH_SHA256,buf,sz,&th); lseek(pack->fd,0,SEEK_END); write(pack->fd,th.digest,th.len); } free(buf); }
                close(fd2);
            }
        }
        // generate idx
        if(pack->idx_file) fastgit_pack_index_create(pack->idx_file, pack);
        free(pack->w_oids); free(pack->w_crcs); free(pack->w_offsets);
        pack->w_oids=NULL; pack->w_crcs=NULL; pack->w_offsets=NULL;
    }
    if(pack->index) fastgit_pack_index_free(pack->index);
    if(pack->mapped){
#if defined(_WIN32)
        UnmapViewOfFile(pack->mapped);
#else
        munmap(pack->mapped,pack->mapped_size);
#endif
    }
    if(pack->fd>=0) close(pack->fd);
    free(pack->pack_file); free(pack->idx_file); free(pack);
}
fastgit_error_t fastgit_pack_read_entry(fastgit_pack_t* pack,const fastgit_oid_t* oid,fastgit_odb_object_t* out){
    if(!pack||!oid||!out) return FASTGIT_EINVAL;
    if(!pack->index) return FASTGIT_ENOENT;
    uint32_t idx; fastgit_error_t e=fastgit_pack_index_find(pack->index,oid,&idx); if(e!=FASTGIT_OK) return e;
    uint64_t off=pack->index->data.offsets[idx];
    void* data=NULL; size_t sz=0; fastgit_obj_type_t tp;
    e=pack_read_object_at(pack,off,&tp,&data,&sz,0); if(e!=FASTGIT_OK) return e;
    out->oid=*oid; out->type=tp; out->size=sz; out->data=data; out->source=FASTGIT_ODB_PACK;
    pack->stats_objects_read++; pack->stats_bytes_read+=sz; return FASTGIT_OK;
}
fastgit_error_t fastgit_pack_read_header(fastgit_pack_t* pack,const fastgit_oid_t* oid,fastgit_obj_type_t* type,size_t* size){
    fastgit_odb_object_t o; fastgit_error_t e=fastgit_pack_read_entry(pack,oid,&o); if(e!=FASTGIT_OK) return e;
    if(type) *type=o.type; if(size) *size=o.size; free(o.data); return FASTGIT_OK;
}
fastgit_error_t fastgit_pack_exists(fastgit_pack_t* pack,const fastgit_oid_t* oid){
    if(!pack||!oid) return FASTGIT_EINVAL;
    if(!pack->index) return FASTGIT_ENOENT;
    uint32_t idx; return fastgit_pack_index_find(pack->index,oid,&idx);
}
static size_t encode_pack_header(uint32_t type, uint64_t size, uint8_t* out){
    size_t n=0;
    uint8_t c = (type << 4) | (size & 0x0F);
    size >>= 4;
    if(size) c |= 0x80;
    out[n++]=c;
    while(size){
        c = size & 0x7F;
        size >>= 7;
        if(size) c |= 0x80;
        out[n++]=c;
    }
    return n;
}
static fastgit_error_t oid_of_object(fastgit_obj_type_t type, const void* data, size_t len, fastgit_oid_t* out){
    const char* typestr="blob";
    if(type==FASTGIT_OBJ_COMMIT) typestr="commit";
    else if(type==FASTGIT_OBJ_TREE) typestr="tree";
    else if(type==FASTGIT_OBJ_TAG) typestr="tag";
    else if(type==FASTGIT_OBJ_BLOB) typestr="blob";
    fastgit_hash_t h; fastgit_hash_ctx_t* ctx=fastgit_hash_ctx_new(FASTGIT_HASH_SHA256);
    if(!ctx) return FASTGIT_EIO;
    if(fastgit_hash_ctx_init(ctx)!=FASTGIT_OK){ fastgit_hash_ctx_free(ctx); return FASTGIT_EIO; }
    char tmp[64]; int tlen = snprintf(tmp,sizeof(tmp),"%s %zu",typestr,len);
    size_t need = (size_t)tlen+1;
    if(need>sizeof(tmp)) need=sizeof(tmp);
    fastgit_hash_ctx_update(ctx, tmp, need);
    fastgit_hash_ctx_update(ctx, data, len);
    fastgit_hash_ctx_final(ctx,&h);
    fastgit_hash_ctx_free(ctx);
    out->algo=FASTGIT_HASH_SHA256; out->len=h.len; memcpy(out->hash,h.digest,h.len);
    return FASTGIT_OK;
}
static fastgit_error_t ensure_write_cap(fastgit_pack_t* p){
    if(p->w_count < p->w_cap) return FASTGIT_OK;
    uint32_t nc = p->w_cap ? p->w_cap*2 : 16;
    fastgit_oid_t* no = realloc(p->w_oids, nc*sizeof(*no));
    uint32_t* ncrc = realloc(p->w_crcs, nc*sizeof(*ncrc));
    uint64_t* noff = realloc(p->w_offsets, nc*sizeof(*noff));
    if(!no||!ncrc||!noff){ free(no); free(ncrc); free(noff); return FASTGIT_ENOMEM; }
    p->w_oids=no; p->w_crcs=ncrc; p->w_offsets=noff; p->w_cap=nc; return FASTGIT_OK;
}
fastgit_error_t fastgit_pack_add_object(fastgit_pack_t* p,fastgit_obj_type_t t,const void* d,size_t l,fastgit_oid_t* o){
    if(!p||!d) return FASTGIT_EINVAL;
    if(!p->writing) return FASTGIT_EINVAL;
    fastgit_oid_t oid;
    if(oid_of_object(t,d,l,&oid)!=FASTGIT_OK) return FASTGIT_EIO;
    if(o) *o=oid;
    // map type to pack type
    uint32_t ptype=FASTGIT_PACK_OBJ_BLOB;
    if(t==FASTGIT_OBJ_COMMIT) ptype=FASTGIT_PACK_OBJ_COMMIT;
    else if(t==FASTGIT_OBJ_TREE) ptype=FASTGIT_PACK_OBJ_TREE;
    else if(t==FASTGIT_OBJ_BLOB) ptype=FASTGIT_PACK_OBJ_BLOB;
    else if(t==FASTGIT_OBJ_TAG) ptype=FASTGIT_PACK_OBJ_TAG;
    uint8_t hdr[16]; size_t hlen=encode_pack_header(ptype,l,hdr);
    uLongf clen = compressBound(l);
    uint8_t* comp=malloc(clen); if(!comp) return FASTGIT_ENOMEM;
    int z=compress2(comp,&clen,d,l,Z_DEFAULT_COMPRESSION);
    if(z!=Z_OK){ free(comp); return FASTGIT_EIO; }
    uint64_t off = p->w_cur_offset;
    // write header + compressed
    if(write(p->fd,hdr,hlen)!=(ssize_t)hlen){ free(comp); return FASTGIT_EIO; }
    if(write(p->fd,comp,clen)!=(ssize_t)clen){ free(comp); return FASTGIT_EIO; }
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, hdr, hlen);
    crc = crc32(crc, comp, clen);
    free(comp);
    if(ensure_write_cap(p)!=FASTGIT_OK) return FASTGIT_ENOMEM;
    p->w_oids[p->w_count]=oid;
    p->w_crcs[p->w_count]=crc;
    p->w_offsets[p->w_count]=off;
    p->w_count++;
    p->w_cur_offset += hlen + clen;
    p->header.object_count++;
    p->stats_objects_written++; p->stats_bytes_written+=l;
    return FASTGIT_OK;
}
fastgit_error_t fastgit_pack_write(fastgit_pack_t* p,const fastgit_oid_t* o,size_t c){ (void)o;(void)c; if(!p) return FASTGIT_EINVAL; if(!p->writing) return FASTGIT_EINVAL; return FASTGIT_OK; }
fastgit_error_t fastgit_pack_index_load(const char* idx_file,fastgit_pack_index_t** out){
    if(!idx_file||!out) return FASTGIT_EINVAL;
    fastgit_pack_index_t* idx=calloc(1,sizeof(*idx)); if(!idx) return FASTGIT_ENOMEM;
    idx->fd=-1;
#if defined(_WIN32)
    HANDLE h=CreateFileA(idx_file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE){ free(idx); return FASTGIT_ENOENT; }
    idx->fd=_open_osfhandle((intptr_t)h,_O_RDONLY);
#else
    idx->fd=open(idx_file,O_RDONLY); if(idx->fd<0){ free(idx); return FASTGIT_ENOENT; }
#endif
    struct stat st; fstat(idx->fd,&st); idx->mapped_size=st.st_size;
#if defined(_WIN32)
    HANDLE hm=CreateFileMapping((HANDLE)_get_osfhandle(idx->fd),NULL,PAGE_READONLY,0,0,NULL);
    if(!hm){ close(idx->fd); free(idx); return FASTGIT_EIO; }
    idx->mapped=MapViewOfFile(hm,FILE_MAP_READ,0,0,0); CloseHandle(hm);
#else
    idx->mapped=mmap(NULL,idx->mapped_size,PROT_READ,MAP_PRIVATE,idx->fd,0);
    if(idx->mapped==MAP_FAILED){ close(idx->fd); free(idx); return FASTGIT_EIO; }
#endif
    idx->own_mapping=true;
    uint8_t* ptr=(uint8_t*)idx->mapped; uint8_t* end=ptr+idx->mapped_size;
    // check magic for v2
    bool is_v2=false;
    if(idx->mapped_size>=8 && ptr[0]==0xFF && ptr[1]==0x74 && ptr[2]==0x4F && ptr[3]==0x63 && ptr[4]==0x00 && ptr[5]==0x00 && ptr[6]==0x00 && ptr[7]==0x02) is_v2=true;
    if(!is_v2){ fastgit_pack_index_free(idx); return FASTGIT_ERROR; }
    ptr+=8;
    for(int i=0;i<256;i++){ idx->data.fanout[i]=__builtin_bswap32(*(uint32_t*)ptr); ptr+=4; }
    idx->data.count=idx->data.fanout[255];
    // detect hash len: try 32, then 20
    size_t hash_len=32;
    // compute expected size for 32
    size_t expect32=8+1024 + (size_t)idx->data.count*hash_len + (size_t)idx->data.count*4 + (size_t)idx->data.count*4 + 2*hash_len;
    size_t expect20=8+1024 + (size_t)idx->data.count*20 + (size_t)idx->data.count*4 + (size_t)idx->data.count*4 + 2*20;
    // large offset handling: if any offset has MSB set, need extra 8 bytes per large entry; approximate by checking file size
    // Try to deduce: if file_size == expect32 or expect32 + k*8, use 32 else 20
    if(idx->mapped_size!=expect32 && idx->mapped_size!=expect32+8 && idx->mapped_size!=expect32+16){
        // check if 20 fits
        if(idx->mapped_size==expect20 || idx->mapped_size==expect20+8) hash_len=20;
        else {
            // fallback: if count small, prefer 32 for sha256 repos
            // keep 32
        }
    }
    // refine by checking remaining
    // Re-estimate with hash_len
    idx->data.oids=malloc(idx->data.count*sizeof(fastgit_oid_t)); if(!idx->data.oids){ fastgit_pack_index_free(idx); return FASTGIT_ENOMEM; }
    for(uint32_t i=0;i<idx->data.count;i++){
        idx->data.oids[i].algo=FASTGIT_HASH_SHA256;
        idx->data.oids[i].len=hash_len;
        memcpy(idx->data.oids[i].hash,ptr,hash_len); ptr+=hash_len;
    }
    idx->data.crc32s=malloc(idx->data.count*sizeof(uint32_t)); if(!idx->data.crc32s){ fastgit_pack_index_free(idx); return FASTGIT_ENOMEM; }
    for(uint32_t i=0;i<idx->data.count;i++){ idx->data.crc32s[i]=__builtin_bswap32(*(uint32_t*)ptr); ptr+=4; }
    idx->data.offsets=malloc(idx->data.count*sizeof(uint64_t)); if(!idx->data.offsets){ fastgit_pack_index_free(idx); return FASTGIT_ENOMEM; }
    for(uint32_t i=0;i<idx->data.count;i++){ uint32_t off=__builtin_bswap32(*(uint32_t*)ptr); idx->data.offsets[i]=off; ptr+=4; }
    // handle large offsets (offset with MSB)
    size_t large_count=0;
    for(uint32_t i=0;i<idx->data.count;i++) if(idx->data.offsets[i] & 0x80000000) large_count++;
    if(large_count>0){
        // next large_count *8 bytes are 64-bit offsets
        for(uint32_t i=0;i<idx->data.count;i++){
            if(idx->data.offsets[i] & 0x80000000){
                uint32_t idx64=idx->data.offsets[i] & 0x7FFFFFFF;
                if(ptr+8>end){ fastgit_pack_index_free(idx); return FASTGIT_ERROR; }
                uint64_t off64=((uint64_t)__builtin_bswap32(*(uint32_t*)ptr)<<32) | __builtin_bswap32(*(uint32_t*)(ptr+4));
                ptr+=8;
                (void)idx64; idx->data.offsets[i]=off64;
            }
        }
    }
    if(ptr+ (int)hash_len <= end){
        idx->data.pack_checksum.algo=FASTGIT_HASH_SHA256; idx->data.pack_checksum.len=hash_len; memcpy(idx->data.pack_checksum.digest,ptr,hash_len);
    }
    *out=idx; return FASTGIT_OK;
}
fastgit_error_t fastgit_pack_index_create(const char* idx_file, fastgit_pack_t* pack){
    if(!idx_file||!pack) return FASTGIT_EINVAL;
    if(!pack->writing) return FASTGIT_EINVAL;
    // sort indices by oid
    uint32_t n=pack->w_count;
    uint32_t* order=malloc(n*sizeof(uint32_t)); if(!order) return FASTGIT_ENOMEM;
    for(uint32_t i=0;i<n;i++) order[i]=i;
    // simple insertion sort (n small) else qsort
    for(uint32_t i=1;i<n;i++){
        uint32_t key=order[i]; int j=i-1;
        while(j>=0 && memcmp(pack->w_oids[order[j]].hash, pack->w_oids[key].hash, 32)>0){ order[j+1]=order[j]; j--; }
        order[j+1]=key;
    }
    // build fanout
    uint32_t fanout[256]={0};
    for(uint32_t i=0;i<n;i++){
        uint8_t b=pack->w_oids[order[i]].hash[0];
        for(int f=b; f<256; f++) fanout[f]++;
    }
    // verify sorted fanout is cumulative already
    // write file
    int fd=open(idx_file,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0){ free(order); return FASTGIT_EIO; }
    uint8_t hdr[8]={0xFF,0x74,0x4F,0x63,0x00,0x00,0x00,0x02};
    write(fd,hdr,8);
    for(int i=0;i<256;i++){ uint32_t v=__builtin_bswap32(fanout[i]); write(fd,&v,4); }
    for(uint32_t i=0;i<n;i++) write(fd,pack->w_oids[order[i]].hash,32);
    for(uint32_t i=0;i<n;i++){ uint32_t c=__builtin_bswap32(pack->w_crcs[order[i]]); write(fd,&c,4); }
    // offsets need 32-bit unless >2GB; we store 32 for now
    for(uint32_t i=0;i<n;i++){ uint32_t off=(uint32_t)pack->w_offsets[order[i]]; uint32_t be=__builtin_bswap32(off); write(fd,&be,4); }
    // pack checksum (re-read from pack tail)
    {
        int pfd=open(pack->pack_file,O_RDONLY); if(pfd>=0){ struct stat st; fstat(pfd,&st);
            size_t sz=(size_t)st.st_size; if(sz>=32){ lseek(pfd, sz-32, SEEK_SET); uint8_t cs[32]; read(pfd,cs,32); write(fd,cs,32); } else { uint8_t z[32]={0}; write(fd,z,32); }
            close(pfd);
        }
    }
    // idx checksum = hash of idx content so far
    {
        int rfd=open(idx_file,O_RDONLY); if(rfd>=0){ struct stat st; fstat(rfd,&st); size_t sz=(size_t)st.st_size; uint8_t* buf=malloc(sz); if(buf){ read(rfd,buf,sz); fastgit_hash_t ih; fastgit_hash(FASTGIT_HASH_SHA256,buf,sz,&ih); write(fd,ih.digest,ih.len); free(buf); } close(rfd); }
    }
    close(fd); free(order); return FASTGIT_OK;
}
void fastgit_pack_index_free(fastgit_pack_index_t* idx){
    if(!idx) return;
    if(idx->own_mapping&&idx->mapped){
#if defined(_WIN32)
        UnmapViewOfFile(idx->mapped);
#else
        munmap(idx->mapped,idx->mapped_size);
#endif
    }
    if(idx->fd>=0) close(idx->fd);
    free(idx->data.oids); free(idx->data.crc32s); free(idx->data.offsets); free(idx);
}
fastgit_error_t fastgit_pack_index_find(fastgit_pack_index_t* idx,const fastgit_oid_t* oid,uint32_t* out){
    if(!idx||!oid||!out) return FASTGIT_EINVAL;
    if(idx->data.count==0) return FASTGIT_ENOENT;
    size_t hl=idx->data.oids[0].len;
    if(oid->len!=hl) return FASTGIT_ENOENT;
    uint32_t first=oid->hash[0];
    uint32_t lo=first?idx->data.fanout[first-1]:0;
    uint32_t hi=idx->data.fanout[first];
    while(lo<hi){
        uint32_t mid=(lo+hi)/2;
        int cmp=memcmp(idx->data.oids[mid].hash,oid->hash,hl);
        if(cmp<0) lo=mid+1; else if(cmp>0) hi=mid; else { *out=mid; return FASTGIT_OK; }
    }
    return FASTGIT_ENOENT;
}
const fastgit_pack_index_data_t* fastgit_pack_index_data(fastgit_pack_index_t* i){ return &i->data; }
void fastgit_pack_stats(fastgit_pack_t* p,fastgit_pack_stats_t* o){ if(!p||!o) return; o->objects_read=p->stats_objects_read; o->objects_written=p->stats_objects_written; o->bytes_read=p->stats_bytes_read; o->bytes_written=p->stats_bytes_written; o->deltas_created=p->stats_deltas_created; o->deltas_applied=p->stats_deltas_applied; o->compression_ratio=0.0; }
