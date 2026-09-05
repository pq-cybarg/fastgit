#include "fastgit/object.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

struct fastgit_commit {
    fastgit_object_t base;
    fastgit_oid_t tree;
    fastgit_oid_t* parents;
    size_t parent_count;
    fastgit_signature_t* author;
    fastgit_signature_t* committer;
    char* message;
    char* encoding;
    char* gpg_signature;
};

static void commit_data_free(void* p) {
    fastgit_commit_t* c = (fastgit_commit_t*)p;
    if (!c) return;
    free(c->parents);
    free(c->message);
    free(c->encoding);
    free(c->gpg_signature);
    /* author/committer not owned — caller frees signatures separately (see bench_full) */
    free(c);
}

fastgit_error_t fastgit_commit_create(const fastgit_commit_t* commit, fastgit_object_t** out) {
    if (!commit || !out) return FASTGIT_EINVAL;

    fastgit_object_t* obj = calloc(1, sizeof(fastgit_object_t));
    if (!obj) return FASTGIT_ENOMEM;
    fastgit_commit_t* c = calloc(1, sizeof(fastgit_commit_t));
    if (!c) { free(obj); return FASTGIT_ENOMEM; }

    c->tree = commit->tree;
    c->parent_count = commit->parent_count;
    if (commit->parent_count > 0) {
        c->parents = malloc(commit->parent_count * sizeof(fastgit_oid_t));
        if (!c->parents) {
            free(c); free(obj);
            return FASTGIT_ENOMEM;
        }
        memcpy(c->parents, commit->parents, commit->parent_count * sizeof(fastgit_oid_t));
    }
    c->author = commit->author;
    c->committer = commit->committer;
    c->message = commit->message ? strdup(commit->message) : NULL;
    c->encoding = commit->encoding ? strdup(commit->encoding) : NULL;
    c->gpg_signature = commit->gpg_signature ? strdup(commit->gpg_signature) : NULL;

    obj->type = FASTGIT_OBJ_COMMIT;
    obj->size = 0;
    obj->data = c;
    obj->free_data = commit_data_free;

    *out = obj;
    return FASTGIT_OK;
}

const fastgit_commit_t* fastgit_commit_parse(const fastgit_object_t* obj) {
    if (!obj || obj->type != FASTGIT_OBJ_COMMIT) return NULL;
    return (const fastgit_commit_t*)obj->data;
}

void fastgit_commit_free(fastgit_commit_t* commit) {
    if (!commit) return;
    free(commit->parents);
    free(commit->message);
    free(commit->encoding);
    free(commit->gpg_signature);
    if (commit->author) fastgit_signature_free(commit->author);
    if (commit->committer) fastgit_signature_free(commit->committer);
}

size_t fastgit_commit_content_size(const struct fastgit_commit* c); /* forward */
static size_t commit_serialize_size(const struct fastgit_commit* c) {
    size_t size = 0;
    size += 5 + c->tree.len * 2 + 1;

    for (size_t i = 0; i < c->parent_count; i++) {
        size += 7 + c->parents[i].len * 2 + 1;
    }

    char *author_buf = NULL, *committer_buf = NULL;
    size_t author_len = 0, committer_len = 0;
    fastgit_signature_to_buf(c->author, &author_buf, &author_len);
    fastgit_signature_to_buf(c->committer, &committer_buf, &committer_len);

    size += 7 + author_len + 1;
    size += 10 + committer_len + 1;

    if (c->encoding) size += 9 + strlen(c->encoding) + 1;
    if (c->message) size += 1 + strlen(c->message);
    if (c->gpg_signature) size += 1 + strlen(c->gpg_signature);

    free(author_buf);
    free(committer_buf);
    return size;
}

static __attribute__((unused)) void commit_serialize_write(const struct fastgit_commit* c, uint8_t* buf, size_t* pos) {
    char oid_hex[129];
    fastgit_oid_to_hex(&c->tree, oid_hex, sizeof(oid_hex));
    memcpy(buf + *pos, "tree ", 5);
    *pos += 5;
    size_t len = strlen(oid_hex);
    memcpy(buf + *pos, oid_hex, len);
    *pos += len;
    buf[(*pos)++] = '\n';

    for (size_t i = 0; i < c->parent_count; i++) {
        fastgit_oid_to_hex(&c->parents[i], oid_hex, sizeof(oid_hex));
        memcpy(buf + *pos, "parent ", 7);
        *pos += 7;
        len = strlen(oid_hex);
        memcpy(buf + *pos, oid_hex, len);
        *pos += len;
        buf[(*pos)++] = '\n';
    }

    char *author_buf = NULL, *committer_buf = NULL;
    size_t author_len = 0, committer_len = 0;
    fastgit_signature_to_buf(c->author, &author_buf, &author_len);
    fastgit_signature_to_buf(c->committer, &committer_buf, &committer_len);

    memcpy(buf + *pos, "author ", 7);
    *pos += 7;
    memcpy(buf + *pos, author_buf, author_len);
    *pos += author_len;
    buf[(*pos)++] = '\n';

    memcpy(buf + *pos, "committer ", 10);
    *pos += 10;
    memcpy(buf + *pos, committer_buf, committer_len);
    *pos += committer_len;
    buf[(*pos)++] = '\n';

    free(author_buf);
    free(committer_buf);

    if (c->encoding) {
        memcpy(buf + *pos, "encoding ", 9);
        *pos += 9;
        len = strlen(c->encoding);
        memcpy(buf + *pos, c->encoding, len);
        *pos += len;
        buf[(*pos)++] = '\n';
    }

    if (c->gpg_signature) {
        buf[(*pos)++] = '\n';
        memcpy(buf + *pos, "gpgsig ", 7);
        *pos += 7;
        len = strlen(c->gpg_signature);
        memcpy(buf + *pos, c->gpg_signature, len);
        *pos += len;
        buf[(*pos)++] = '\n';
    }

    buf[(*pos)++] = '\n';
    if (c->message) {
        len = strlen(c->message);
        memcpy(buf + *pos, c->message, len);
        *pos += len;
    }
}

size_t fastgit_commit_content_size(const struct fastgit_commit* c) { return commit_serialize_size(c); }
void fastgit_commit_content_write(const struct fastgit_commit* c, uint8_t* buf, size_t* pos) { commit_serialize_write(c, buf, pos); }

fastgit_error_t fastgit_signature_new(const char* name, const char* email, int64_t when, int offset, fastgit_signature_t** out) {
    if (!name || !email || !out) return FASTGIT_EINVAL;
    fastgit_signature_t* sig = malloc(sizeof(fastgit_signature_t));
    if (!sig) return FASTGIT_ENOMEM;
    sig->name = strdup(name);
    sig->email = strdup(email);
    sig->when = when;
    sig->offset = offset;
    if (!sig->name || !sig->email) {
        fastgit_signature_free(sig);
        return FASTGIT_ENOMEM;
    }
    *out = sig;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_signature_default(fastgit_signature_t** out) {
    if (!out) return FASTGIT_EINVAL;
    const char* name = getenv("GIT_AUTHOR_NAME");
    const char* email = getenv("GIT_AUTHOR_EMAIL");
    if (!name) name = getenv("USER");
    if (!email) email = "user@localhost";
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    int offset = tm->tm_gmtoff / 60;
    return fastgit_signature_new(name, email, now, offset, out);
}

void fastgit_signature_free(fastgit_signature_t* sig) {
    if (sig) {
        free(sig->name);
        free(sig->email);
        free(sig);
    }
}
