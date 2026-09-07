#include "fastgit/object.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static void commit_data_free(void* p) {
    fastgit_commit_t* c = (fastgit_commit_t*)p;
    if (!c) return;
    free(c->parents);
    free(c->message);
    free(c->encoding);
    free(c->gpg_signature);
    if (c->author) fastgit_signature_free(c->author);
    if (c->committer) fastgit_signature_free(c->committer);
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

static fastgit_error_t parse_signature_buf(const char* buf, size_t len, fastgit_signature_t** out) {
    // buf is "Name <email> timestamp tz"  (no leading "author ")
    const char* lt = memchr(buf, '<', len);
    const char* gt = lt ? memchr(lt, '>', len - (lt - buf)) : NULL;
    if (!lt || !gt) return FASTGIT_EINVAL;
    size_t name_len = lt - buf;
    while (name_len > 0 && buf[name_len - 1] == ' ') name_len--;
    size_t email_len = gt - lt - 1;
    const char* rest = gt + 1;
    while (rest < buf + len && *rest == ' ') rest++;
    // rest: "<timestamp> <tz>"
    char* tmp = strndup(rest, (buf + len) - rest);
    if (!tmp) return FASTGIT_ENOMEM;
    char* sp = strrchr(tmp, ' ');
    if (!sp) { free(tmp); return FASTGIT_EINVAL; }
    *sp = '\0';
    const char* when_s = tmp;
    const char* tz_s = sp + 1;
    int64_t when = atoll(when_s);
    int tz = 0;
    if (tz_s[0] == '+' || tz_s[0] == '-') {
        int sign = tz_s[0] == '-' ? -1 : 1;
        int v = atoi(tz_s + 1);
        int hh = v / 100;
        int mm = v % 100;
        tz = sign * (hh * 60 + mm);
    }
    char* name = strndup(buf, name_len);
    char* email = strndup(lt + 1, email_len);
    free(tmp);
    if (!name || !email) { free(name); free(email); return FASTGIT_ENOMEM; }
    fastgit_error_t err = fastgit_signature_new(name, email, when, tz, out);
    free(name); free(email);
    return err;
}

static fastgit_error_t commit_parse_raw(const void* data, size_t len, fastgit_commit_t** out) {
    if (!data || !out) return FASTGIT_EINVAL;
    const char* buf = (const char*)data;
    size_t pos = 0;
    fastgit_commit_t* c = calloc(1, sizeof(*c));
    if (!c) return FASTGIT_ENOMEM;
    // tree
    if (len < 5 || strncmp(buf, "tree ", 5) != 0) { free(c); return FASTGIT_EINVAL; }
    pos = 5;
    const char* nl = memchr(buf + pos, '\n', len - pos);
    if (!nl) { free(c); return FASTGIT_EINVAL; }
    size_t hex_len = nl - (buf + pos);
    char hex[129]; if (hex_len >= sizeof(hex)) { free(c); return FASTGIT_EINVAL; }
    memcpy(hex, buf + pos, hex_len); hex[hex_len] = '\0';
    if (fastgit_oid_from_hex(hex, &c->tree) != FASTGIT_OK) { free(c); return FASTGIT_EINVAL; }
    pos = (nl - buf) + 1;
    // parents (0+)
    while (pos + 7 <= len && strncmp(buf + pos, "parent ", 7) == 0) {
        pos += 7;
        nl = memchr(buf + pos, '\n', len - pos);
        if (!nl) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
        hex_len = nl - (buf + pos);
        if (hex_len >= sizeof(hex)) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
        memcpy(hex, buf + pos, hex_len); hex[hex_len] = '\0';
        fastgit_oid_t poid;
        if (fastgit_oid_from_hex(hex, &poid) != FASTGIT_OK) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
        fastgit_oid_t* np = realloc(c->parents, (c->parent_count + 1) * sizeof(fastgit_oid_t));
        if (!np) { fastgit_commit_free(c); return FASTGIT_ENOMEM; }
        c->parents = np;
        c->parents[c->parent_count++] = poid;
        pos = (nl - buf) + 1;
    }
    // author
    if (pos + 7 > len || strncmp(buf + pos, "author ", 7) != 0) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
    pos += 7;
    nl = memchr(buf + pos, '\n', len - pos);
    if (!nl) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
    if (parse_signature_buf(buf + pos, nl - (buf + pos), &c->author) != FASTGIT_OK) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
    pos = (nl - buf) + 1;
    // committer
    if (pos + 10 > len || strncmp(buf + pos, "committer ", 10) != 0) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
    pos += 10;
    nl = memchr(buf + pos, '\n', len - pos);
    if (!nl) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
    if (parse_signature_buf(buf + pos, nl - (buf + pos), &c->committer) != FASTGIT_OK) { fastgit_commit_free(c); return FASTGIT_EINVAL; }
    pos = (nl - buf) + 1;
    // optional encoding / gpgsig headers until blank line
    while (pos < len) {
        if (buf[pos] == '\n') { pos++; break; }
        nl = memchr(buf + pos, '\n', len - pos);
        if (!nl) break;
        size_t ll = nl - (buf + pos);
        if (ll >= 9 && strncmp(buf + pos, "encoding ", 9) == 0) {
            c->encoding = strndup(buf + pos + 9, ll - 9);
        } else if (ll >= 7 && strncmp(buf + pos, "gpgsig ", 7) == 0) {
            // gpgsig may be multiline; for now capture remainder until blank line
            // our serialize writes "\n gpgsig <sig>\n\n" so this branch captures single line
            c->gpg_signature = strndup(buf + pos + 7, ll - 7);
        }
        pos = (nl - buf) + 1;
        if (ll == 0) break;
    }
    // message = remainder
    if (pos < len) {
        c->message = strndup(buf + pos, len - pos);
    } else {
        c->message = strdup("");
    }
    if (!c->message) { fastgit_commit_free(c); return FASTGIT_ENOMEM; }
    *out = c;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_commit_from_raw(const void* data, size_t len, fastgit_commit_t** out);

fastgit_error_t fastgit_commit_from_raw(const void* data, size_t len, fastgit_commit_t** out) {
    return commit_parse_raw(data, len, out);
}

const fastgit_commit_t* fastgit_commit_parse(const fastgit_object_t* obj) {
    if (!obj || obj->type != FASTGIT_OBJ_COMMIT) return NULL;
    if (obj->size == 0) return (const fastgit_commit_t*)obj->data;
    // raw bytes: lazily parse into heap commit (caller must not free long-lived; we cache by returning heap that leaks until object free - acceptable for CLI short-lived)
    // For repository lookup path we convert eagerly, so this path only handles ad-hoc raw objects.
    fastgit_commit_t* c = NULL;
    if (commit_parse_raw(obj->data, obj->size, &c) != FASTGIT_OK) return NULL;
    // store parsed commit back into object for future calls if mutable (cast away const)
    // leak previous raw buffer? caller owns object lifetime, we replace data
    fastgit_object_t* m = (fastgit_object_t*)obj;
    free(m->data);
    m->data = c;
    m->free_data = commit_data_free;
    m->size = 0;
    return (const fastgit_commit_t*)c;
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

size_t fastgit_commit_content_size(const fastgit_commit_t* c); /* forward */
static size_t commit_serialize_size(const fastgit_commit_t* c) {
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

static void commit_serialize_write(const fastgit_commit_t* c, uint8_t* buf, size_t* pos) {
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

size_t fastgit_commit_content_size(const fastgit_commit_t* c) { return commit_serialize_size(c); }
void fastgit_commit_content_write(const fastgit_commit_t* c, uint8_t* buf, size_t* pos) { commit_serialize_write(c, buf, pos); }

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
