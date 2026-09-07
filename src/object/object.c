#include "fastgit/object.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

fastgit_error_t fastgit_object_parse(fastgit_obj_type_t type, const void* data, size_t len, fastgit_object_t** out) {
    fastgit_object_t* obj = calloc(1, sizeof(fastgit_object_t));
    if (!obj) return FASTGIT_ENOMEM;

    obj->type = type;
    obj->size = len;
    obj->data = malloc(len);
    if (!obj->data) {
        free(obj);
        return FASTGIT_ENOMEM;
    }
    memcpy(obj->data, data, len);
    obj->free_data = free;

    *out = obj;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_object_raw_parse(const fastgit_oid_t* oid, fastgit_obj_type_t type, const void* data, size_t len, fastgit_object_raw_t* out) {
    if (!out) return FASTGIT_EINVAL;
    out->oid = *oid;
    out->type = type;
    out->size = len;
    out->data = malloc(len);
    if (!out->data) return FASTGIT_ENOMEM;
    memcpy(out->data, data, len);
    return FASTGIT_OK;
}

void fastgit_object_raw_free(fastgit_object_raw_t* raw) {
    if (raw && raw->data) {
        free(raw->data);
        raw->data = NULL;
    }
}

fastgit_error_t fastgit_object_serialize(const fastgit_object_t* obj, uint8_t hash_algo __attribute__((unused)), fastgit_buf_t* out) {
    if (!obj || !out) return FASTGIT_EINVAL;

    static const char* type_names[] = {
        [FASTGIT_OBJ_BLOB] = "blob",
        [FASTGIT_OBJ_TREE] = "tree",
        [FASTGIT_OBJ_COMMIT] = "commit",
        [FASTGIT_OBJ_TAG] = "tag",
    };

    const char* type_name = (obj->type < 5) ? type_names[obj->type] : "unknown";

    size_t content_len = 0;
    uint8_t* content_buf = NULL;
    bool need_free_content = false;

    // Dispatch to typed serializers if object holds structured data (size==0 indicates structured)
    if (obj->type == FASTGIT_OBJ_COMMIT && obj->size == 0) {
        const fastgit_commit_t* c = (const fastgit_commit_t*)obj->data;
        if (c) {
            extern size_t fastgit_commit_content_size(const fastgit_commit_t* c);
            extern void fastgit_commit_content_write(const fastgit_commit_t* c, uint8_t* buf, size_t* pos);
            content_len = fastgit_commit_content_size(c);
            content_buf = malloc(content_len);
            if (!content_buf) return FASTGIT_ENOMEM;
            size_t pos = 0;
            fastgit_commit_content_write(c, content_buf, &pos);
            need_free_content = true;
        }
    } else if (obj->type == FASTGIT_OBJ_TREE && obj->size == 0) {
        const struct fastgit_tree* t = (const struct fastgit_tree*)obj->data;
        if (t) {
            extern size_t fastgit_tree_content_size(const struct fastgit_tree* t);
            extern void fastgit_tree_content_write(const struct fastgit_tree* t, uint8_t* buf, size_t* pos);
            content_len = fastgit_tree_content_size(t);
            content_buf = malloc(content_len);
            if (!content_buf) return FASTGIT_ENOMEM;
            size_t pos = 0;
            fastgit_tree_content_write(t, content_buf, &pos);
            need_free_content = true;
        }
    } else if (obj->type == FASTGIT_OBJ_TAG && obj->size == 0) {
        const struct fastgit_tag* tg = (const struct fastgit_tag*)obj->data;
        if (tg) {
            extern size_t fastgit_tag_content_size(const struct fastgit_tag* tg);
            extern void fastgit_tag_content_write(const struct fastgit_tag* tg, uint8_t* buf, size_t* pos);
            content_len = fastgit_tag_content_size(tg);
            content_buf = malloc(content_len);
            if (!content_buf) return FASTGIT_ENOMEM;
            size_t pos = 0;
            fastgit_tag_content_write(tg, content_buf, &pos);
            need_free_content = true;
        }
    } else {
        content_len = obj->size;
        content_buf = (uint8_t*)obj->data;
        need_free_content = false;
    }

    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_name, content_len);
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        if (need_free_content) free(content_buf);
        return FASTGIT_ERROR;
    }

    size_t total_len = header_len + 1 + content_len;
    void* buf = malloc(total_len);
    if (!buf) {
        if (need_free_content) free(content_buf);
        return FASTGIT_ENOMEM;
    }

    memcpy(buf, header, header_len);
    ((uint8_t*)buf)[header_len] = '\0';
    memcpy((uint8_t*)buf + header_len + 1, content_buf, content_len);
    if (need_free_content) free(content_buf);

    out->data = buf;
    out->len = total_len;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_object_hash(const fastgit_object_t* obj, uint8_t hash_algo, fastgit_oid_t* out) {
    fastgit_buf_t buf;
    fastgit_error_t err = fastgit_object_serialize(obj, hash_algo, &buf);
    if (err != FASTGIT_OK) return err;

    fastgit_hash_t hash;
    err = fastgit_hash(hash_algo, buf.data, buf.len, &hash);
    free((void*)buf.data);

    if (err != FASTGIT_OK) return err;

    out->algo = hash.algo;
    out->len = hash.len;
    memcpy(out->hash, hash.digest, hash.len);
    return FASTGIT_OK;
}

fastgit_oid_t* fastgit_oid_dup(const fastgit_oid_t* oid) {
    if (!oid) return NULL;
    fastgit_oid_t* copy = malloc(sizeof(fastgit_oid_t));
    if (!copy) return NULL;
    *copy = *oid;
    return copy;
}

void fastgit_oid_free(fastgit_oid_t* oid) {
    free(oid);
}

int fastgit_oid_cmp(const fastgit_oid_t* a, const fastgit_oid_t* b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    if (a->algo != b->algo) return (int)a->algo - (int)b->algo;
    size_t min_len = a->len < b->len ? a->len : b->len;
    int cmp = memcmp(a->hash, b->hash, min_len);
    if (cmp != 0) return cmp;
    return (int)a->len - (int)b->len;
}

bool fastgit_oid_equal(const fastgit_oid_t* a, const fastgit_oid_t* b) {
    return fastgit_oid_cmp(a, b) == 0;
}

void fastgit_oid_to_hex(const fastgit_oid_t* oid, char* out, size_t out_len) {
    if (!oid || !out || out_len < oid->len * 2 + 1) return;
    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < oid->len; i++) {
        out[i * 2] = hex[oid->hash[i] >> 4];
        out[i * 2 + 1] = hex[oid->hash[i] & 0x0F];
    }
    out[oid->len * 2] = '\0';
}

fastgit_error_t fastgit_oid_from_hex(const char* hex, fastgit_oid_t* out) {
    if (!hex || !out) return FASTGIT_EINVAL;
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return FASTGIT_EINVAL;

    out->len = hex_len / 2;
    if (out->len > FASTGIT_HASH_MAX_DIGEST) return FASTGIT_EOVERFLOW;
    /* hash agility: infer algo from hex length */
    if (out->len == 20) out->algo = FASTGIT_HASH_SHA1;
    else if (out->len == 32) {
        /* could be SHA256 or SHA3-256; default SHA256 (length-extension resistant SHA3 preferred explicitly) */
        out->algo = FASTGIT_HASH_SHA256;
    } else if (out->len == 48) out->algo = FASTGIT_HASH_SHA384;
    else if (out->len == 64) out->algo = FASTGIT_HASH_SHA3_512;
    else out->algo = FASTGIT_HASH_SHA256;

    for (size_t i = 0; i < out->len; i++) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        uint8_t val = 0;

        if (high >= '0' && high <= '9') val = (high - '0') << 4;
        else if (high >= 'a' && high <= 'f') val = (high - 'a' + 10) << 4;
        else if (high >= 'A' && high <= 'F') val = (high - 'A' + 10) << 4;
        else return FASTGIT_EINVAL;

        if (low >= '0' && low <= '9') val |= (low - '0');
        else if (low >= 'a' && low <= 'f') val |= (low - 'a' + 10);
        else if (low >= 'A' && low <= 'F') val |= (low - 'A' + 10);
        else return FASTGIT_EINVAL;

        out->hash[i] = val;
    }
    return FASTGIT_OK;
}

const char* fastgit_obj_type_name(fastgit_obj_type_t type) {
    static const char* names[] = {
        [FASTGIT_OBJ_BLOB] = "blob",
        [FASTGIT_OBJ_TREE] = "tree",
        [FASTGIT_OBJ_COMMIT] = "commit",
        [FASTGIT_OBJ_TAG] = "tag",
        [FASTGIT_OBJ_OFS_DELTA] = "ofs-delta",
        [FASTGIT_OBJ_REF_DELTA] = "ref-delta",
    };
    return (type < 8) ? names[type] : "unknown";
}

fastgit_obj_type_t fastgit_obj_type_from_name(const char* name) {
    if (!name) return FASTGIT_OBJ_BLOB;
    if (strcmp(name, "blob") == 0) return FASTGIT_OBJ_BLOB;
    if (strcmp(name, "tree") == 0) return FASTGIT_OBJ_TREE;
    if (strcmp(name, "commit") == 0) return FASTGIT_OBJ_COMMIT;
    if (strcmp(name, "tag") == 0) return FASTGIT_OBJ_TAG;
    if (strcmp(name, "ofs-delta") == 0) return FASTGIT_OBJ_OFS_DELTA;
    if (strcmp(name, "ref-delta") == 0) return FASTGIT_OBJ_REF_DELTA;
    return FASTGIT_OBJ_BLOB;
}

void fastgit_signature_to_buf(const fastgit_signature_t* sig, char** out, size_t* out_len) {
    if (!sig) return;
    char timebuf[32];
    snprintf(timebuf, sizeof(timebuf), "%lld %+05d", (long long)sig->when, sig->offset);
    size_t needed = strlen(sig->name) + 1 + strlen(sig->email) + 3 + strlen(timebuf) + 1;
    *out = malloc(needed);
    if (*out) {
        snprintf(*out, needed, "%s <%s> %s", sig->name, sig->email, timebuf);
        *out_len = strlen(*out);
    }
}
