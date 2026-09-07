#include "fastgit/object.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct fastgit_tag {
    fastgit_object_t base;
    fastgit_oid_t object;
    fastgit_obj_type_t object_type;
    char* name;
    fastgit_signature_t* tagger;
    char* message;
    char* gpg_signature;
};

static void tag_data_free(void* p) {
    struct fastgit_tag* t = (struct fastgit_tag*)p;
    if (!t) return;
    free(t->name);
    free(t->message);
    free(t->gpg_signature);
    free(t);
}

fastgit_error_t fastgit_tag_create(const fastgit_tag_t* tag, fastgit_object_t** out) {
    if (!tag || !out) return FASTGIT_EINVAL;

    fastgit_object_t* obj = calloc(1, sizeof(fastgit_object_t));
    if (!obj) return FASTGIT_ENOMEM;
    struct fastgit_tag* t = calloc(1, sizeof(struct fastgit_tag));
    if (!t) { free(obj); return FASTGIT_ENOMEM; }

    t->object = tag->object;
    t->object_type = tag->object_type;
    t->name = tag->name ? strdup(tag->name) : NULL;
    t->tagger = tag->tagger;
    t->message = tag->message ? strdup(tag->message) : NULL;
    t->gpg_signature = tag->gpg_signature ? strdup(tag->gpg_signature) : NULL;

    obj->type = FASTGIT_OBJ_TAG;
    obj->size = 0;
    obj->data = t;
    obj->free_data = tag_data_free;

    *out = obj;
    return FASTGIT_OK;
}

const fastgit_tag_t* fastgit_tag_parse(const fastgit_object_t* obj) {
    if (!obj || obj->type != FASTGIT_OBJ_TAG) return NULL;
    return (const fastgit_tag_t*)obj->data;
}

void fastgit_tag_free(fastgit_tag_t* tag) {
    if (!tag) return;
    free(tag->name);
    free(tag->message);
    free(tag->gpg_signature);
    if (tag->tagger) fastgit_signature_free(tag->tagger);
}

static size_t tag_serialize_size(const struct fastgit_tag* t) {
    size_t size = 0;
    char oid_hex[129];
    fastgit_oid_to_hex(&t->object, oid_hex, sizeof(oid_hex));
    size += 7 + strlen(oid_hex) + 1;
    size += 5 + strlen(fastgit_obj_type_name(t->object_type)) + 1;
    size += 4 + strlen(t->name) + 1;

    if (t->tagger) {
        char *tagger_buf = NULL;
        size_t tagger_len = 0;
        fastgit_signature_to_buf(t->tagger, &tagger_buf, &tagger_len);
        size += 7 + tagger_len + 1;
        free(tagger_buf);
    }

    if (t->message) size += 1 + strlen(t->message);
    if (t->gpg_signature) size += 1 + strlen(t->gpg_signature);

    return size;
}

static __attribute__((unused)) void tag_serialize_write(const struct fastgit_tag* t, uint8_t* buf, size_t* pos) {
    char oid_hex[129];
    fastgit_oid_to_hex(&t->object, oid_hex, sizeof(oid_hex));
    memcpy(buf + *pos, "object ", 7);
    *pos += 7;
    size_t len = strlen(oid_hex);
    memcpy(buf + *pos, oid_hex, len);
    *pos += len;
    buf[(*pos)++] = '\n';

    const char* type_name = fastgit_obj_type_name(t->object_type);
    memcpy(buf + *pos, "type ", 5);
    *pos += 5;
    len = strlen(type_name);
    memcpy(buf + *pos, type_name, len);
    *pos += len;
    buf[(*pos)++] = '\n';

    memcpy(buf + *pos, "tag ", 4);
    *pos += 4;
    len = strlen(t->name);
    memcpy(buf + *pos, t->name, len);
    *pos += len;
    buf[(*pos)++] = '\n';

    if (t->tagger) {
        char *tagger_buf = NULL;
        size_t tagger_len = 0;
        fastgit_signature_to_buf(t->tagger, &tagger_buf, &tagger_len);
        memcpy(buf + *pos, "tagger ", 7);
        *pos += 7;
        memcpy(buf + *pos, tagger_buf, tagger_len);
        *pos += tagger_len;
        buf[(*pos)++] = '\n';
        free(tagger_buf);
    }

    buf[(*pos)++] = '\n';

    if (t->message) {
        len = strlen(t->message);
        memcpy(buf + *pos, t->message, len);
        *pos += len;
    }

    if (t->gpg_signature) {
        buf[(*pos)++] = '\n';
        memcpy(buf + *pos, "-----BEGIN PGP SIGNATURE-----", 29);
        *pos += 29;
        buf[(*pos)++] = '\n';
        len = strlen(t->gpg_signature);
        memcpy(buf + *pos, t->gpg_signature, len);
        *pos += len;
        buf[(*pos)++] = '\n';
        memcpy(buf + *pos, "-----END PGP SIGNATURE-----", 27);
        *pos += 27;
    }
}

size_t fastgit_tag_content_size(const struct fastgit_tag* t) {
    return tag_serialize_size(t);
}
void fastgit_tag_content_write(const struct fastgit_tag* t, uint8_t* buf, size_t* pos) {
    tag_serialize_write(t, buf, pos);
}
