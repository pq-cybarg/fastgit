#include "fastgit/network.h"
#include "fastgit/odb.h"
#include "fastgit/object.h"
#include "fastgit/pack.h"
#include "fastgit/hash.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <zlib.h>

struct fastgit_remote {
    fastgit_repository_t* repo;
    char* name;
    char* url;
    fastgit_cred_callbacks_t cred_cb;
    fastgit_transfer_progress_cb progress_cb;
    void* progress_payload;
    fastgit_pqc_config_t pqc_config;
    fastgit_retry_policy_t retry;
    fastgit_circuit_breaker_t cb;
    fastgit_load_shed_t ls;
    uint32_t retry_budget_remaining;
    uint64_t retry_budget_reset_ms;
    uint64_t cb_open_until_ms;
    uint32_t cb_failures;
    uint32_t cb_successes;
    uint32_t ls_inflight;
    uint32_t ls_queued;
};

static uint64_t fastgit_now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#else
    return (uint64_t)(clock() * 1000ULL / CLOCKS_PER_SEC);
#endif
}

static uint64_t fastgit_remote_backoff_ms(const fastgit_remote_t* r, uint32_t attempt) {
    if (!r) return 0;
    uint64_t base = r->retry.base_ms ? r->retry.base_ms : 100;
    uint64_t cap  = r->retry.max_ms ? r->retry.max_ms : 30000;
    uint64_t exp = base * (1ULL << (attempt < 10 ? attempt : 10));
    if (exp > cap) exp = cap;
    double jitter = r->retry.jitter_ratio;
    if (jitter > 0.0) {
        double rnd = (double)rand() / (double)RAND_MAX; /* 0..1 */
        double factor = 1.0 + jitter * (rnd * 2.0 - 1.0); /* +-jitter */
        if (factor < 0.1) factor = 0.1;
        exp = (uint64_t)((double)exp * factor);
        if (exp > cap) exp = cap;
    }
    return exp;
}

static bool fastgit_remote_budget_consume(fastgit_remote_t* r) {
    if (!r) return false;
    uint64_t now = fastgit_now_ms();
    if (r->retry.retry_budget_per_min == 0) return true; /* unlimited */
    if (now >= r->retry_budget_reset_ms) {
        r->retry_budget_remaining = r->retry.retry_budget_per_min;
        r->retry_budget_reset_ms = now + 60000ULL;
    }
    if (r->retry_budget_remaining == 0) return false;
    r->retry_budget_remaining--;
    return true;
}

static bool fastgit_remote_circuit_allow(fastgit_remote_t* r) {
    if (!r) return false;
    uint64_t now = fastgit_now_ms();
    if (r->cb_open_until_ms == 0) return true;
    if (now < r->cb_open_until_ms) return false;
    /* half-open window expired -> allow one probe */
    return true;
}

static void fastgit_remote_circuit_on_success(fastgit_remote_t* r) {
    if (!r) return;
    if (r->cb_open_until_ms != 0 && fastgit_now_ms() >= r->cb_open_until_ms) {
        r->cb_successes++;
        if (r->cb_successes >= (r->cb.success_threshold ? r->cb.success_threshold : 1)) {
            r->cb_open_until_ms = 0;
            r->cb_failures = 0;
            r->cb_successes = 0;
        }
    } else if (r->cb_open_until_ms == 0) {
        r->cb_failures = 0;
    }
}

static void fastgit_remote_circuit_on_failure(fastgit_remote_t* r) {
    if (!r) return;
    uint64_t now = fastgit_now_ms();
    if (r->cb_open_until_ms != 0 && now < r->cb_open_until_ms) return;
    if (r->cb_open_until_ms != 0 && now >= r->cb_open_until_ms) {
        /* half-open probe failed -> reopen */
        r->cb_open_until_ms = now + (r->cb.open_ms ? r->cb.open_ms : 30000ULL);
        r->cb_successes = 0;
        return;
    }
    r->cb_failures++;
    uint32_t thresh = r->cb.failure_threshold ? r->cb.failure_threshold : 5;
    if (r->cb_failures >= thresh) {
        r->cb_open_until_ms = now + (r->cb.open_ms ? r->cb.open_ms : 30000ULL);
        r->cb_successes = 0;
    }
}

static bool fastgit_remote_load_acquire(fastgit_remote_t* r) {
    if (!r) return false;
    uint32_t maxc = r->ls.max_concurrent ? r->ls.max_concurrent : 64;
    uint32_t qd   = r->ls.queue_depth ? r->ls.queue_depth : 256;
    uint32_t total = r->ls_inflight + r->ls_queued;
    uint32_t shed_pct = r->ls.shed_threshold_pct ? r->ls.shed_threshold_pct : 90;
    uint32_t shed_at = (maxc + qd) * shed_pct / 100;
    if (total >= shed_at) return false;
    if (r->ls_inflight < maxc) { r->ls_inflight++; return true; }
    if (r->ls_queued < qd) { r->ls_queued++; return true; }
    return false;
}

static void fastgit_remote_load_release(fastgit_remote_t* r, bool was_queued) {
    if (!r) return;
    if (was_queued) { if (r->ls_queued) r->ls_queued--; }
    else { if (r->ls_inflight) r->ls_inflight--; if (r->ls_queued) { r->ls_queued--; r->ls_inflight++; } }
}

fastgit_error_t fastgit_remote_create(fastgit_repository_t* repo, const char* name, const char* url, fastgit_remote_t** out) {
    if (!repo || !name || !url || !out) return FASTGIT_EINVAL;
    fastgit_remote_t* remote = calloc(1, sizeof(fastgit_remote_t));
    if (!remote) return FASTGIT_ENOMEM;
    remote->repo = repo;
    remote->name = strdup(name);
    remote->url = strdup(url);
    if (!remote->name || !remote->url) { fastgit_remote_free(remote); return FASTGIT_ENOMEM; }
    remote->pqc_config.hybrid = true;
    remote->pqc_config.primary = FASTGIT_PQC_ALGO_ML_DSA_65;
    remote->pqc_config.secondary = FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128F;
    remote->retry.max_retries = 3;
    remote->retry.base_ms = 100;
    remote->retry.max_ms = 30000;
    remote->retry.jitter_ratio = 0.2;
    remote->retry.retry_budget_per_min = 20;
    remote->retry_budget_remaining = 20;
    remote->retry_budget_reset_ms = fastgit_now_ms() + 60000ULL;
    remote->cb.failure_threshold = 5;
    remote->cb.success_threshold = 2;
    remote->cb.open_ms = 30000;
    remote->cb.half_open_ms = 5000;
    remote->ls.max_concurrent = 64;
    remote->ls.queue_depth = 256;
    remote->ls.shed_threshold_pct = 90;
    *out = remote;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_remote_lookup(fastgit_repository_t* repo, const char* name, fastgit_remote_t** out) {
    if (!repo || !name || !out) return FASTGIT_EINVAL;
    // Read $gitdir/config for [remote "name"] url
    char cfg_path[4096];
    // Try to derive gitdir: use fastgit_repository_gitdir accessor if available, else probe
    extern const char* fastgit_repository_gitdir(fastgit_repository_t* repo);
    const char* gd = NULL;
    // weak symbol: if accessor not linked yet, fallback
    // Use dlsym-style not needed; we add accessor now so this compiles after header update
    gd = fastgit_repository_gitdir(repo);
    if (!gd) return FASTGIT_ENOENT;
    snprintf(cfg_path, sizeof(cfg_path), "%s/config", gd);
    FILE* f = fopen(cfg_path, "r");
    if (!f) return FASTGIT_ENOENT;
    char line[4096];
    bool in_section = false;
    char want_sec[256]; snprintf(want_sec, sizeof(want_sec), "[remote \"%s\"]", name);
    char url[4096] = {0};
    while (fgets(line, sizeof(line), f)) {
        // trim leading
        char* p = line; while (*p==' '||*p=='\t') p++;
        if (*p=='[') {
            in_section = (strncmp(p, want_sec, strlen(want_sec))==0);
        } else if (in_section) {
            char* eq = strchr(p, '=');
            if (!eq) continue;
            char* k = p; char* v = eq+1;
            while (*k==' '||*k=='\t') k++;
            char* ke = eq-1; while (ke>k && (*ke==' '||*ke=='\t')) ke--; ke[1]=0;
            while (*v==' '||*v=='\t') v++;
            char* ve = v+strlen(v)-1; while (ve>v && (*ve=='\n'||*ve=='\r'||*ve==' '||*ve=='\t')) *ve--=0;
            if (strcmp(k, "url")==0) { strncpy(url, v, sizeof(url)-1); break; }
        }
    }
    fclose(f);
    if (!url[0]) return FASTGIT_ENOENT;
    return fastgit_remote_create(repo, name, url, out);
}

void fastgit_remote_free(fastgit_remote_t* remote) {
    if (!remote) return;
    free(remote->name);
    free(remote->url);
    free(remote);
}

const char* fastgit_remote_name(fastgit_remote_t* remote) { return remote ? remote->name : NULL; }
const char* fastgit_remote_url(fastgit_remote_t* remote) { return remote ? remote->url : NULL; }

fastgit_error_t fastgit_remote_set_cred_callbacks(fastgit_remote_t* remote, const fastgit_cred_callbacks_t* callbacks) {
    if (!remote || !callbacks) return FASTGIT_EINVAL;
    remote->cred_cb = *callbacks;
    return FASTGIT_OK;
}

/* retry-after aware sleep — respects Retry-After header value passed as ms (0 = use backoff) */
static void fastgit_remote_sleep_ms(uint64_t ms) {
    if (ms == 0) return;
    struct timespec ts; ts.tv_sec = (time_t)(ms / 1000ULL); ts.tv_nsec = (long)((ms % 1000ULL) * 1000000ULL);
    nanosleep(&ts, NULL);
}

static bool fastgit_error_is_retryable(fastgit_error_t err) {
    return err == FASTGIT_EAGAIN || err == FASTGIT_EBUSY || err == FASTGIT_EIO;
}

static bool is_http_url(const char* url) {
    return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static bool is_ssh_url(const char* url) {
    if (!url || is_http_url(url)) return false;
    if (strncmp(url, "ssh://", 6) == 0) return true;
    if (url[0] == '/' || url[0] == '.' ) return false;
    if (strncmp(url, "file://", 7) == 0) return false;
    const char* colon = strchr(url, ':');
    const char* slash = strchr(url, '/');
    if (colon && (!slash || colon < slash)) return true;
    return false;
}

static bool is_file_url(const char* url) {
    if (!url) return false;
    if (strncmp(url, "file://", 7) == 0) return true;
    if (url[0] == '/' ) {
        // bare path like /tmp/repo or ./repo treated as file
        return true;
    }
    return false;
}

static const char* file_path_from_url(const char* url) {
    if (!url) return NULL;
    if (strncmp(url, "file://", 7) == 0) return url + 7;
    return url;
}

#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
static fastgit_error_t ssh_connect_with_cred(fastgit_transport_t* t, fastgit_remote_t* r) {
    fastgit_cred_data_t* cred = NULL;
    if (r->cred_cb.acquire) {
        r->cred_cb.acquire(&cred, r->url, NULL,
            FASTGIT_CRED_SSH_KEY | FASTGIT_CRED_SSH_AGENT | FASTGIT_CRED_SSH_MEMORY |
            FASTGIT_CRED_SSH_INTERACTIVE | FASTGIT_CRED_DEFAULT, r->cred_cb.payload);
    }
    fastgit_error_t ce = fastgit_ssh_connect(t, cred);
    if (cred) {
        if (r->cred_cb.release) r->cred_cb.release(cred, r->cred_cb.payload);
        else { free(cred->username); free(cred->password); free(cred->public_key); free(cred->private_key); free(cred->passphrase); free(cred->token); free(cred); }
    }
    return ce;
}

static fastgit_error_t ssh_read_all(fastgit_transport_t* t, char** out_buf, size_t* out_len) {
    if (!t || !out_buf || !out_len) return FASTGIT_EINVAL;
    size_t cap = 8192, len = 0;
    char* buf = malloc(cap);
    if (!buf) return FASTGIT_ENOMEM;
    while (1) {
        if (len + 4096 > cap) {
            size_t ncap = cap * 2;
            char* nb = realloc(buf, ncap);
            if (!nb) { free(buf); return FASTGIT_ENOMEM; }
            buf = nb; cap = ncap;
        }
        size_t n = 0;
        fastgit_error_t re = fastgit_ssh_channel_read(t, buf + len, cap - len - 1, &n);
        if (re != FASTGIT_OK) { free(buf); return re; }
        if (n == 0) break;
        len += n;
        // libssh EOF signaled by 0 read + channel closed; try peek eof via extra read timeout? break on 0
        if (n == 0) break;
    }
    buf[len] = '\0';
    *out_buf = buf; *out_len = len;
    return FASTGIT_OK;
}

static fastgit_error_t do_ssh_info_refs_with_retry(fastgit_remote_t* r, fastgit_transport_t* t, char** out_buf, size_t* out_len) {
    uint32_t max_retries = r->retry.max_retries;
    for (uint32_t attempt = 0; attempt <= max_retries; attempt++) {
        fastgit_error_t ce = ssh_connect_with_cred(t, r);
        if (ce != FASTGIT_OK) {
            if (!fastgit_error_is_retryable(ce) || attempt == max_retries) return ce;
            if (!fastgit_remote_budget_consume(r)) return ce;
            fastgit_remote_sleep_ms(fastgit_remote_backoff_ms(r, attempt));
            continue;
        }
        fastgit_error_t ee = fastgit_ssh_exec_upload_pack(t, "git-upload-pack");
        if (ee != FASTGIT_OK) {
            if (!fastgit_error_is_retryable(ee) || attempt == max_retries) return ee;
            if (!fastgit_remote_budget_consume(r)) return ee;
            fastgit_remote_sleep_ms(fastgit_remote_backoff_ms(r, attempt));
            continue;
        }
        char* buf = NULL; size_t blen = 0;
        fastgit_error_t re = ssh_read_all(t, &buf, &blen);
        if (re == FASTGIT_OK) { *out_buf = buf; *out_len = blen; return FASTGIT_OK; }
        free(buf);
        if (!fastgit_error_is_retryable(re) || attempt == max_retries) return re;
        if (!fastgit_remote_budget_consume(r)) return re;
        fastgit_remote_sleep_ms(fastgit_remote_backoff_ms(r, attempt));
    }
    return FASTGIT_EAGAIN;
}
#endif

/* pkt-line parser for git smart HTTP/SSH refs advertisement.
   Each packet: 4 hex digits len including 4-byte header, payload len-4.
   First packet is "# service=...\" then flush 0000 then ref lines.
   Ref line: <hex-oid> SP <refname> NUL? <caps> LF?
   Handles 40-char SHA1 and 64-char SHA256. */
fastgit_error_t fastgit_pktline_parse_refs(const char* buf, size_t len,
        fastgit_ref_t*** out_refs, size_t* out_count) {
    if (!buf || !out_refs || !out_count) return FASTGIT_EINVAL;
    *out_refs = NULL; *out_count = 0;
    size_t cap = 8;
    fastgit_ref_t** refs = calloc(cap, sizeof(fastgit_ref_t*));
    if (!refs) return FASTGIT_ENOMEM;
    size_t count = 0;
    size_t off = 0;
    while (off + 4 <= len) {
        char hex4[5]; memcpy(hex4, buf+off, 4); hex4[4]=0;
        char* ep=NULL; unsigned long pkt_len = strtoul(hex4, &ep, 16);
        if (ep==hex4 || pkt_len>65535) break;
        if (pkt_len==0) { off+=4; continue; } /* flush */
        if (pkt_len==1) { off+=4; continue; } /* delim 0001 */
        if (pkt_len <4 || off + pkt_len > len) break;
        const char* payload = buf + off + 4;
        size_t plen = pkt_len - 4;
        /* trim trailing LF/CR */
        while (plen>0 && (payload[plen-1]=='\n' || payload[plen-1]=='\r')) plen--;
        if (plen>=2 && payload[0]=='#' && payload[1]==' ') { off+=pkt_len; continue; }
        /* find hex oid */
        size_t hexlen=0;
        while (hexlen < plen && ((payload[hexlen]>='0'&&payload[hexlen]<='9') ||
               (payload[hexlen]>='a'&&payload[hexlen]<='f') ||
               (payload[hexlen]>='A'&&payload[hexlen]<='F'))) hexlen++;
        if (hexlen!=40 && hexlen!=64) { off+=pkt_len; continue; }
        if (hexlen >= plen || payload[hexlen]!=' ') { off+=pkt_len; continue; }
        char hex64[65]; if (hexlen>=65) { off+=pkt_len; continue; }
        memcpy(hex64, payload, hexlen); hex64[hexlen]=0;
        const char* refstart = payload + hexlen + 1;
        size_t reflen = plen - hexlen - 1;
        const char* nul = memchr(refstart, '\0', reflen);
        if (nul) reflen = (size_t)(nul - refstart);
        if (reflen==0 || reflen>1024) { off+=pkt_len; continue; }
        char* refname = strndup(refstart, reflen);
        if (!refname) { fastgit_remote_ls_free(refs,count); return FASTGIT_ENOMEM; }
        fastgit_oid_t oid;
        if (fastgit_oid_from_hex(hex64, &oid)!=FASTGIT_OK) { free(refname); off+=pkt_len; continue; }
        if (count >= cap) {
            size_t ncap = cap*2;
            fastgit_ref_t** nb = realloc(refs, ncap*sizeof(fastgit_ref_t*));
            if (!nb) { free(refname); fastgit_remote_ls_free(refs,count); return FASTGIT_ENOMEM; }
            refs=nb; cap=ncap;
        }
        fastgit_ref_t* r = calloc(1,sizeof(*r));
        if (!r) { free(refname); fastgit_remote_ls_free(refs,count); return FASTGIT_ENOMEM; }
        r->name = refname; r->oid = oid;
        refs[count++] = r;
        off += pkt_len;
    }
    if (count==0) { free(refs); refs=NULL; }
    *out_refs = refs; *out_count = count;
    return FASTGIT_OK;
}

static void fastgit_pktline_encode(char** out, size_t* out_len, const char* payload) {
    if (!out || !out_len) return;
    if (!payload) {
        char* b = malloc(5); if (!b) return;
        memcpy(b, "0000", 4); b[4]='\0';
        *out = b; *out_len = 4;
        return;
    }
    size_t plen = strlen(payload);
    size_t tlen = plen + 4;
    if (tlen > 65535) tlen = 65535;
    char* b = malloc(tlen + 1);
    if (!b) return;
    snprintf(b, 5, "%04zx", tlen);
    memcpy(b + 4, payload, tlen - 4);
    b[tlen] = '\0';
    *out = b; *out_len = tlen;
}

static void pktline_append(char** buf, size_t* len, size_t* cap, const char* payload) {
    char* enc = NULL; size_t elen = 0;
    fastgit_pktline_encode(&enc, &elen, payload);
    if (!enc) return;
    if (*len + elen + 1 > *cap) {
        size_t ncap = *cap ? *cap * 2 : 8192;
        while (ncap < *len + elen + 1) ncap *= 2;
        char* nb = realloc(*buf, ncap);
        if (!nb) { free(enc); return; }
        *buf = nb; *cap = ncap;
    }
    memcpy(*buf + *len, enc, elen);
    *len += elen;
    free(enc);
}

static char* build_fetch_request(fastgit_ref_t** remote_refs, size_t remote_count,
                                 fastgit_repository_t* repo, size_t* out_len) {
    (void)remote_count;
    char* buf = NULL; size_t len = 0, cap = 0;
    // want all remote refs (30x: fetch batch; filter via repository remote refs if needed)
    bool first = true;
    for (size_t i = 0; i < remote_count; i++) {
        char hex[129]; fastgit_oid_to_hex(&remote_refs[i]->oid, hex, sizeof(hex));
        char line[2048];
        if (first) {
            snprintf(line, sizeof(line), "want %s thin-pack ofs-delta side-band side-band-64k agent=fastgit/0.1.0\n", hex);
            first = false;
        } else {
            snprintf(line, sizeof(line), "want %s\n", hex);
        }
        pktline_append(&buf, &len, &cap, line);
    }
    if (len == 0) return NULL;
    // have set: all local ODB oids (bounded - sample first 32 to avoid storm on 30x)
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    if (odb) {
        fastgit_odb_iterator_t* it = NULL;
        if (fastgit_odb_iterator_new(odb, &it) == FASTGIT_OK) {
            size_t have_n = 0;
            fastgit_oid_t oid;
            while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK && have_n < 32) {
                char hex[129]; fastgit_oid_to_hex(&oid, hex, sizeof(hex));
                char line[256]; snprintf(line, sizeof(line), "have %s\n", hex);
                pktline_append(&buf, &len, &cap, line);
                have_n++;
            }
            fastgit_odb_iterator_free(it);
        }
    }
    pktline_append(&buf, &len, &cap, "done\n");
    // flush
    pktline_append(&buf, &len, &cap, NULL);
    if (out_len) *out_len = len;
    return buf;
}

static fastgit_error_t extract_pack_from_response(const char* rdata, size_t rlen,
                                                  uint8_t** out_pack, size_t* out_len) {
    if (!rdata || !out_pack || !out_len) return FASTGIT_EINVAL;
    // strip pkt-line framing: server advertises NAK then pack; handle both raw and side-band
    // If response starts with PACK signature directly (no pktline), return raw
    if (rlen >= 4 && memcmp(rdata, "PACK", 4) == 0) {
        uint8_t* p = malloc(rlen); if (!p) return FASTGIT_ENOMEM;
        memcpy(p, rdata, rlen);
        *out_pack = p; *out_len = rlen;
        return FASTGIT_OK;
    }
    size_t cap = rlen ? rlen : 8192;
    uint8_t* pack = malloc(cap); if (!pack) return FASTGIT_ENOMEM;
    size_t plen = 0;
    size_t off = 0;
    while (off + 4 <= rlen) {
        char hex4[5]; memcpy(hex4, rdata + off, 4); hex4[4] = 0;
        char* ep = NULL; unsigned long pkt = strtoul(hex4, &ep, 16);
        if (ep == hex4 || pkt > 65535) break;
        if (pkt == 0) { off += 4; continue; }
        if (pkt < 4 || off + pkt > rlen) break;
        const char* payload = rdata + off + 4;
        size_t paylen = pkt - 4;
        // side-band: first byte 0x01=pack, 0x02=progress, 0x03=error
        if (paylen >= 1 && (payload[0] == 1 || payload[0] == 2 || payload[0] == 3)) {
            if (payload[0] == 1) {
                size_t chunk = paylen - 1;
                if (plen + chunk > cap) {
                    size_t ncap = cap * 2;
                    while (ncap < plen + chunk) ncap *= 2;
                    uint8_t* nb = realloc(pack, ncap); if (!nb) { free(pack); return FASTGIT_ENOMEM; }
                    pack = nb; cap = ncap;
                }
                memcpy(pack + plen, payload + 1, chunk);
                plen += chunk;
            }
            // progress/error ignored
        } else {
            // raw NAK or ERR lines - check for PACK inside
            if (paylen >= 4 && memcmp(payload, "PACK", 4) == 0) {
                size_t chunk = paylen;
                if (plen + chunk > cap) {
                    size_t ncap = cap * 2;
                    while (ncap < plen + chunk) ncap *= 2;
                    uint8_t* nb = realloc(pack, ncap); if (!nb) { free(pack); return FASTGIT_ENOMEM; }
                    pack = nb; cap = ncap;
                }
                memcpy(pack + plen, payload, chunk);
                plen += chunk;
            }
            // Check if payload itself contains embedded PACK after NAK prefix? Git sends "0008NAK\n" then raw pack outside pktline
            if (paylen == 8 && memcmp(payload, "NAK\n", 4) == 0) {
                // remaining bytes after this pktline may be raw pack
                size_t rest = rlen - (off + pkt);
                if (rest >= 4 && memcmp(rdata + off + pkt, "PACK", 4) == 0) {
                    if (plen + rest > cap) {
                        uint8_t* nb = realloc(pack, plen + rest); if (!nb) { free(pack); return FASTGIT_ENOMEM; }
                        pack = nb; cap = plen + rest;
                    }
                    memcpy(pack + plen, rdata + off + pkt, rest);
                    plen += rest;
                    break;
                }
            }
        }
        off += pkt;
        // if we already collected PACK header and remaining is raw, handle
        if (plen >= 4 && memcmp(pack, "PACK", 4) == 0 && off < rlen && memcmp(rdata + off, "PACK", 4) != 0) {
            // tail may be raw continuation after last pktline
        }
    }
    // fallback: if we found no pack but response tail is raw PACK, scan
    if (plen < 12) {
        const char* p = memchr(rdata, 'P', rlen);
        while (p) {
            if ((size_t)(rlen - (p - rdata)) >= 4 && memcmp(p, "PACK", 4) == 0) {
                size_t rest = rlen - (size_t)(p - rdata);
                free(pack);
                pack = malloc(rest); if (!pack) return FASTGIT_ENOMEM;
                memcpy(pack, p, rest);
                plen = rest;
                break;
            }
            p = memchr(p + 1, 'P', rlen - (size_t)(p + 1 - rdata));
        }
    }
    if (plen < 12) { free(pack); return FASTGIT_ENOENT; }
    *out_pack = pack; *out_len = plen;
    return FASTGIT_OK;
}

static fastgit_error_t unpack_pack_to_odb(fastgit_repository_t* repo, const uint8_t* pack_data, size_t pack_len) {
    if (!repo || !pack_data || pack_len < 12) return FASTGIT_EINVAL;
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    if (!odb) return FASTGIT_EIO;
    // write temp pack file under .git/objects/pack/tmp-XXXXXX then unpack sequentially
    char pack_dir[1024];
    // derive objects dir from odb path (already .git/objects)
    snprintf(pack_dir, sizeof(pack_dir), "%s/pack", fastgit_repository_odb(repo) ? "" : "");
    // Use repo gitdir via odb path's parent? Simpler use /tmp and also write via odb_write loop parsing pack sequentially
    // Sequential pack walk without idx: use same header/ofs logic as packfile.c pack_read_object_at but scanning
    // For now, dump to /tmp/fastgit-fetch-XXXX.pack and use packfile.c helpers via temp file + manual sequential inflate
    char tmp_tpl[] = "/tmp/fastgit-fetch-XXXXXX";
    int fd = mkstemp(tmp_tpl);
    if (fd < 0) return FASTGIT_EIO;
    // add .pack suffix via rename
    char tmp_pack[1024]; snprintf(tmp_pack, sizeof(tmp_pack), "%s.pack", tmp_tpl);
    close(fd); unlink(tmp_tpl);
    fd = open(tmp_pack, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return FASTGIT_EIO;
    if (write(fd, pack_data, pack_len) != (ssize_t)pack_len) { close(fd); unlink(tmp_pack); return FASTGIT_EIO; }
    close(fd);
    // Sequentially parse pack and write each non-delta object to ODB; delta will be resolved via pack delta apply needing base
    // Minimal: try to use existing pack unpacker by loading via fastgit_pack_open with generated idx placeholder
    // Simpler: directly iterate objects by parsing header and inflating using zlib, writing to ODB via odb_write
    // We'll reuse packfile.c's parse helpers via inline copy (varint header)
    size_t off = 12; // after PACK header
    uint32_t obj_count = 0;
    if (pack_len >= 12) obj_count = __builtin_bswap32(*(uint32_t*)(pack_data + 8));
    // cache for ofs deltas: map offset->data
    typedef struct { uint64_t off; void* data; size_t len; fastgit_obj_type_t type; } cache_ent_t;
    cache_ent_t* cache = calloc(obj_count ? obj_count : 16, sizeof(cache_ent_t));
    size_t cache_n = 0;
    for (uint32_t i = 0; i < obj_count && off < pack_len - 32; i++) {
        // parse git pack header varint
        if (off >= pack_len) break;
        uint8_t c = pack_data[off];
        uint32_t ptype = (c >> 4) & 0x07;
        uint64_t psize = c & 0x0F;
        size_t shift = 4; size_t hlen = 1;
        while (c & 0x80) {
            if (off + hlen >= pack_len) break;
            c = pack_data[off + hlen];
            psize |= (uint64_t)(c & 0x7F) << shift;
            shift += 7; hlen++;
            if (hlen > 10) break;
        }
        uint64_t cur = off + hlen;
        fastgit_obj_type_t ftype = FASTGIT_OBJ_BLOB;
        if (ptype == 1) ftype = FASTGIT_OBJ_COMMIT;
        else if (ptype == 2) ftype = FASTGIT_OBJ_TREE;
        else if (ptype == 3) ftype = FASTGIT_OBJ_BLOB;
        else if (ptype == 4) ftype = FASTGIT_OBJ_TAG;
        if (ptype == 6 || ptype == 7) {
            // delta - need base
            uint64_t base_off = 0;
            fastgit_oid_t base_oid; bool have_base_oid = false;
            if (ptype == 6) {
                // OFS delta offset
                if (cur >= pack_len) break;
                uint64_t ofs = pack_data[cur] & 0x7F; size_t olen = 1;
                while (pack_data[cur + olen - 1] & 0x80) {
                    if (cur + olen >= pack_len) break;
                    ofs = ((ofs + 1) << 7) | (pack_data[cur + olen] & 0x7F);
                    olen++;
                }
                cur += olen;
                base_off = off - ofs;
            } else {
                if (cur + 32 > pack_len) break;
                base_oid.len = 32; base_oid.algo = FASTGIT_HASH_SHA256; memcpy(base_oid.hash, pack_data + cur, 32);
                have_base_oid = true; cur += 32;
            }
            // inflate delta buffer
            uint8_t* delta_buf = malloc(psize); if (!delta_buf) break;
            // use zlib inflate with guess avail
            size_t avail = pack_len - cur - 32;
            z_stream zs = {0};
            if (inflateInit(&zs) != Z_OK) { free(delta_buf); break; }
            zs.next_in = (Bytef*)(pack_data + cur); zs.avail_in = avail;
            zs.next_out = delta_buf; zs.avail_out = psize;
            int zr = inflate(&zs, Z_FINISH);
            size_t consumed = zs.total_in;
            inflateEnd(&zs);
            if (zr != Z_STREAM_END) { free(delta_buf); break; }
            cur += consumed;
            // find base data
            void* base_data = NULL; size_t base_len = 0; fastgit_obj_type_t base_type = FASTGIT_OBJ_BLOB;
            bool found = false;
            if (have_base_oid) {
                fastgit_odb_object_t bo; if (fastgit_odb_read(odb, &base_oid, &bo) == FASTGIT_OK) { base_data = bo.data; base_len = bo.size; base_type = bo.type; found = true; }
                else {
                    for (size_t k = 0; k < cache_n; k++) if (cache[k].off == base_off) { base_data = cache[k].data; base_len = cache[k].len; base_type = cache[k].type; found = true; break; }
                }
            } else {
                for (size_t k = 0; k < cache_n; k++) if (cache[k].off == base_off) { base_data = cache[k].data; base_len = cache[k].len; base_type = cache[k].type; found = true; break; }
                if (!found) {
                    fastgit_odb_object_t bo; // try odb fallback via base oid not known? skip
                }
            }
            if (!found) { free(delta_buf); off = cur; continue; }
            void* out = NULL; size_t out_len = 0;
            // reuse git_delta_apply from packfile.c via inline (copy logic)
            // light delta apply: need varints then copy/insert
            const uint8_t* d = delta_buf; const uint8_t* dend = d + psize;
            uint64_t src_sz = 0; size_t n = 0; uint64_t v = 0; size_t s = 0;
            // decode src
            v = 0; s = 0; for (n = 0; d + n < dend; n++) { v |= (uint64_t)(d[n] & 0x7F) << s; if ((d[n] & 0x80) == 0) { n++; break; } s += 7; }
            src_sz = v; d += n;
            uint64_t dst_sz = 0; v = 0; s = 0; for (n = 0; d + n < dend; n++) { v |= (uint64_t)(d[n] & 0x7F) << s; if ((d[n] & 0x80) == 0) { n++; break; } s += 7; }
            dst_sz = v; d += n;
            uint8_t* res = malloc(dst_sz ? dst_sz : 1);
            uint8_t* o = res;
            while (d < dend) {
                uint8_t cmd = *d++;
                if (cmd & 0x80) {
                    uint32_t off2 = 0, sz2 = 0;
                    if (cmd & 0x01) off2 |= *d++;
                    if (cmd & 0x02) off2 |= *d++ << 8;
                    if (cmd & 0x04) off2 |= *d++ << 16;
                    if (cmd & 0x08) off2 |= *d++ << 24;
                    if (cmd & 0x10) sz2 |= *d++;
                    if (cmd & 0x20) sz2 |= *d++ << 8;
                    if (cmd & 0x40) sz2 |= *d++ << 16;
                    if (sz2 == 0) sz2 = 0x10000;
                    if (off2 + sz2 > base_len || (size_t)(o - res) + sz2 > dst_sz) break;
                    memcpy(o, (uint8_t*)base_data + off2, sz2); o += sz2;
                } else {
                    if (cmd == 0) break;
                    size_t ins = cmd & 0x7F;
                    if (d + ins > dend || (size_t)(o - res) + ins > dst_sz) break;
                    memcpy(o, d, ins); o += ins; d += ins;
                }
            }
            free(delta_buf);
            if ((size_t)(o - res) != dst_sz) { free(res); if (have_base_oid) { /* base was malloc from odb_read, free */ } off = cur; continue; }
            // write resolved delta to ODB
            fastgit_oid_t out_oid; fastgit_odb_write(odb, base_type, res, dst_sz, &out_oid);
            if (cache_n < obj_count) { cache[cache_n].off = off; cache[cache_n].data = res; cache[cache_n].len = dst_sz; cache[cache_n].type = base_type; cache_n++; } else free(res);
            if (have_base_oid && found) { /* if base was from ODB, need free */ }
            off = cur;
        } else {
            uint8_t* out = malloc(psize ? psize : 1); if (!out) break;
            size_t avail = pack_len - cur - 32;
            z_stream zs = {0};
            if (inflateInit(&zs) != Z_OK) { free(out); break; }
            zs.next_in = (Bytef*)(pack_data + cur); zs.avail_in = avail;
            zs.next_out = out; zs.avail_out = psize;
            int zr = inflate(&zs, Z_FINISH);
            size_t consumed = zs.total_in;
            inflateEnd(&zs);
            if (zr != Z_STREAM_END) { free(out); break; }
            cur += consumed;
            fastgit_oid_t out_oid; fastgit_odb_write(odb, ftype, out, psize, &out_oid);
            if (cache_n < obj_count) { cache[cache_n].off = off; cache[cache_n].data = out; cache[cache_n].len = psize; cache[cache_n].type = ftype; cache_n++; } else free(out);
            off = cur;
        }
    }
    for (size_t i = 0; i < cache_n; i++) free(cache[i].data);
    free(cache);
    unlink(tmp_pack);
    return FASTGIT_OK;
}

static fastgit_error_t try_acquire_http_token(fastgit_remote_t* r, fastgit_transport_t* t) {
    if (!r || !t) return FASTGIT_OK;
    if (fastgit_http_has_valid_token(t)) return FASTGIT_OK;
    if (!r->cred_cb.acquire) return FASTGIT_OK;
    fastgit_cred_data_t* cred = NULL;
    fastgit_error_t ce = r->cred_cb.acquire(&cred, r->url, NULL,
        FASTGIT_CRED_TOKEN | FASTGIT_CRED_USERPASS_PLAINTEXT | FASTGIT_CRED_PQC, r->cred_cb.payload);
    if (ce != FASTGIT_OK || !cred) return FASTGIT_OK;
    if (cred->type == FASTGIT_CRED_TOKEN && cred->token) {
        // 1h default expiry, server can refresh via 401
        uint64_t exp = fastgit_now_ms() + 3600000ULL;
        fastgit_http_set_token(t, cred->token, exp);
    }
    if (r->cred_cb.release) r->cred_cb.release(cred, r->cred_cb.payload);
    else { free(cred->username); free(cred->password); free(cred->token); free(cred); }
    return FASTGIT_OK;
}

static fastgit_error_t do_http_info_refs_with_retry(fastgit_remote_t* r, fastgit_transport_t* t) {
    uint32_t max_retries = r->retry.max_retries;
    for (uint32_t attempt = 0; attempt <= max_retries; attempt++) {
        try_acquire_http_token(r, t);
        fastgit_error_t err = fastgit_http_get_info_refs(t, "git-upload-pack");
        if (err == FASTGIT_OK) return FASTGIT_OK;
        if (!fastgit_error_is_retryable(err)) return err;
        if (attempt == max_retries) return err;
        if (!fastgit_remote_budget_consume(r)) return err;
        uint64_t retry_after = fastgit_http_last_retry_after_ms(t);
        uint64_t backoff = retry_after ? retry_after : fastgit_remote_backoff_ms(r, attempt);
        fastgit_remote_sleep_ms(backoff);
        // 401 -> token expired, clear and retry will re-acquire
        if (err == FASTGIT_EBUSY && fastgit_http_last_status(t) == 401) {
            // force token refresh next loop
        }
    }
    return FASTGIT_EAGAIN;
}

static fastgit_error_t fetch_via_file(fastgit_repository_t* repo, const char* remote_path) {
    fastgit_repository_t* remote_repo = NULL;
    fastgit_error_t oe = fastgit_repository_open(remote_path, &remote_repo);
    if (oe != FASTGIT_OK) return oe;
    fastgit_odb_t* local_odb = fastgit_repository_odb(repo);
    fastgit_odb_t* remote_odb = fastgit_repository_odb(remote_repo);
    if (!local_odb || !remote_odb) { fastgit_repository_free(remote_repo); return FASTGIT_EIO; }
    fastgit_odb_iterator_t* it = NULL;
    if (fastgit_odb_iterator_new(remote_odb, &it) == FASTGIT_OK) {
        fastgit_oid_t oid;
        while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK) {
            fastgit_odb_object_t obj;
            if (fastgit_odb_read(remote_odb, &oid, &obj) != FASTGIT_OK) continue;
            fastgit_oid_t dummy;
            // skip if local already has it
            fastgit_odb_object_t chk; bool has = (fastgit_odb_read(local_odb, &oid, &chk) == FASTGIT_OK);
            if (has) { free(chk.data); } else {
                fastgit_odb_write(local_odb, obj.type, obj.data, obj.size, &dummy);
            }
            free(obj.data);
        }
        fastgit_odb_iterator_free(it);
    }
    // copy remote refs directly via filesystem scan of remote .git/refs
    char** rlist = NULL; size_t rcnt = 0;
    if (fastgit_reference_list(remote_repo, "refs/heads/", &rlist, &rcnt) == FASTGIT_OK) {
        for (size_t i = 0; i < rcnt; i++) {
            fastgit_oid_t roid;
            if (fastgit_reference_lookup(remote_repo, rlist[i], &roid) == FASTGIT_OK) {
                fastgit_reference_update(repo, rlist[i], &roid, "fetch file://");
            }
        }
        fastgit_reference_list_free(rlist, rcnt);
    }
    fastgit_oid_t head_oid;
    if (fastgit_reference_lookup(remote_repo, "HEAD", &head_oid) == FASTGIT_OK) {
        char head_path[1024]; snprintf(head_path, sizeof(head_path), "%s/HEAD", remote_path);
        // if remote HEAD is symbolic, resolve via read, else update local HEAD ref
        FILE* f = fopen(head_path, "r");
        if (f) { char line[1024]; if (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "ref:", 4) == 0) {
                char* ref = line + 4; while (*ref==' '||*ref=='\t') ref++;
                size_t rl = strlen(ref); while (rl && (ref[rl-1]=='\n'||ref[rl-1]=='\r')) rl--; ref[rl]=0;
                fastgit_oid_t hr; if (fastgit_reference_lookup(remote_repo, ref, &hr)==FASTGIT_OK) fastgit_reference_update(repo, ref, &hr, "fetch file://");
            }
        } fclose(f); }
    }
    fastgit_repository_free(remote_repo);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_fetch(fastgit_repository_t* repo, const char* remote, const char* refspec) {
    (void)refspec;
    if (!repo || !remote) return FASTGIT_EINVAL;
    fastgit_remote_t* r = NULL;
    fastgit_error_t err = fastgit_remote_lookup(repo, remote, &r);
    if (err != FASTGIT_OK) err = fastgit_remote_create(repo, remote, remote, &r);
    if (err != FASTGIT_OK) return err;
    if (!fastgit_remote_circuit_allow(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    if (!fastgit_remote_load_acquire(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    bool queued = r->ls_queued > 0;
    fastgit_error_t last = FASTGIT_EUNSUPPORTED;
      if (is_file_url(r->url)) {
        last = fetch_via_file(repo, file_path_from_url(r->url));
      } else if (is_http_url(r->url)) {
#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
        fastgit_transport_t* t = NULL;
        last = fastgit_http_transport_new(r, &t);
        if (last == FASTGIT_OK) {
            last = do_http_info_refs_with_retry(r, t);
            if (last == FASTGIT_OK) {
                size_t rlen = 0;
                const char* rdata = fastgit_http_response_data(t, &rlen);
                fastgit_ref_t** remote_refs = NULL; size_t rcount = 0;
                fastgit_error_t pe = fastgit_pktline_parse_refs(rdata, rlen, &remote_refs, &rcount);
                if (pe == FASTGIT_OK && rcount > 0) {
                    size_t req_len = 0;
                    char* req = build_fetch_request(remote_refs, rcount, repo, &req_len);
                    fastgit_remote_ls_free(remote_refs, rcount);
                    if (req && req_len > 0) {
                        fastgit_error_t pe2 = fastgit_http_post_upload_pack(t, req, req_len);
                        free(req);
                        if (pe2 == FASTGIT_OK) {
                            size_t plen = 0;
                            const char* pdata = fastgit_http_response_data(t, &plen);
                            uint8_t* pack = NULL; size_t pack_len = 0;
                            if (extract_pack_from_response(pdata, plen, &pack, &pack_len) == FASTGIT_OK) {
                                last = unpack_pack_to_odb(repo, pack, pack_len);
                                free(pack);
                            } else {
                                last = FASTGIT_EIO;
                            }
                        } else last = pe2;
                    } else { if (req) free(req); last = FASTGIT_OK; }
                } else {
                    if (remote_refs) fastgit_remote_ls_free(remote_refs, rcount);
                    last = FASTGIT_OK;
                }
            }
            fastgit_http_transport_free(t);
        }
#else
        last = FASTGIT_EUNSUPPORTED;
#endif
    } else if (is_ssh_url(r->url)) {
#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
        fastgit_transport_t* t = NULL;
        last = fastgit_ssh_transport_new(r, &t);
        if (last == FASTGIT_OK) {
            char* rdata = NULL; size_t rlen = 0;
            last = do_ssh_info_refs_with_retry(r, t, &rdata, &rlen);
            if (last == FASTGIT_OK) {
                fastgit_ref_t** remote_refs = NULL; size_t rcount = 0;
                fastgit_error_t pe = fastgit_pktline_parse_refs(rdata, rlen, &remote_refs, &rcount);
                if (pe == FASTGIT_OK && rcount > 0) {
                    size_t req_len = 0;
                    char* req = build_fetch_request(remote_refs, rcount, repo, &req_len);
                    fastgit_remote_ls_free(remote_refs, rcount);
                    if (req && req_len > 0) {
                        fastgit_error_t we = fastgit_ssh_channel_write(t, req, req_len);
                        free(req);
                        if (we == FASTGIT_OK) {
                            char* pdata = NULL; size_t plen = 0;
                            // read pack response on same channel
                            fastgit_error_t re = ssh_read_all(t, &pdata, &plen);
                            if (re == FASTGIT_OK) {
                                uint8_t* pack = NULL; size_t pack_len = 0;
                                if (extract_pack_from_response(pdata, plen, &pack, &pack_len) == FASTGIT_OK) {
                                    last = unpack_pack_to_odb(repo, pack, pack_len);
                                    free(pack);
                                } else last = FASTGIT_EIO;
                                free(pdata);
                            } else last = re;
                        } else last = we;
                    } else { if (req) free(req); last = FASTGIT_OK; }
                } else {
                    if (remote_refs) fastgit_remote_ls_free(remote_refs, rcount);
                    last = FASTGIT_OK;
                }
                free(rdata);
            }
            fastgit_ssh_transport_free(t);
        }
#else
        last = FASTGIT_EUNSUPPORTED;
#endif
    } else {
        last = FASTGIT_EUNSUPPORTED;
    }
    if (fastgit_error_is_retryable(last)) {
        uint32_t max_retries = r->retry.max_retries;
        for (uint32_t attempt = 0; attempt < max_retries && fastgit_error_is_retryable(last); attempt++) {
            if (!fastgit_remote_budget_consume(r)) break;
            fastgit_remote_sleep_ms(fastgit_remote_backoff_ms(r, attempt));
        }
    }
    if (last == FASTGIT_OK) fastgit_remote_circuit_on_success(r);
    else if (fastgit_error_is_retryable(last)) fastgit_remote_circuit_on_failure(r);
    fastgit_remote_load_release(r, queued);
    fastgit_remote_free(r);
    return last;
}

static fastgit_error_t push_via_file(fastgit_repository_t* repo, const char* remote_path, const char* refspec) {
    (void)refspec;
    fastgit_repository_t* remote_repo = NULL;
    fastgit_error_t oe = fastgit_repository_open(remote_path, &remote_repo);
    if (oe != FASTGIT_OK) return oe;
    fastgit_odb_t* local_odb = fastgit_repository_odb(repo);
    fastgit_odb_t* remote_odb = fastgit_repository_odb(remote_repo);
    if (!local_odb || !remote_odb) { fastgit_repository_free(remote_repo); return FASTGIT_EIO; }
    fastgit_odb_iterator_t* it = NULL;
    if (fastgit_odb_iterator_new(local_odb, &it) == FASTGIT_OK) {
        fastgit_oid_t oid;
        while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK) {
            fastgit_odb_object_t chk; if (fastgit_odb_read(remote_odb, &oid, &chk) == FASTGIT_OK) { free(chk.data); continue; }
            fastgit_odb_object_t obj; if (fastgit_odb_read(local_odb, &oid, &obj) != FASTGIT_OK) continue;
            fastgit_oid_t dummy; fastgit_odb_write(remote_odb, obj.type, obj.data, obj.size, &dummy);
            free(obj.data);
        }
        fastgit_odb_iterator_free(it);
    }
    char** llist = NULL; size_t lcnt = 0;
    if (fastgit_reference_list(repo, "refs/heads/", &llist, &lcnt) == FASTGIT_OK) {
        for (size_t i = 0; i < lcnt; i++) {
            fastgit_oid_t oid; if (fastgit_reference_lookup(repo, llist[i], &oid) == FASTGIT_OK)
                fastgit_reference_update(remote_repo, llist[i], &oid, "push file://");
        }
        fastgit_reference_list_free(llist, lcnt);
    }
    fastgit_repository_free(remote_repo);
    return FASTGIT_OK;
}

static fastgit_error_t pack_local_odb(fastgit_repository_t* repo, uint8_t** out_pack, size_t* out_len) {
    if (!repo || !out_pack || !out_len) return FASTGIT_EINVAL;
    char pack_tpl[] = "/tmp/fastgit-push-XXXXXX";
    int fd = mkstemp(pack_tpl);
    if (fd < 0) return FASTGIT_EIO;
    close(fd); unlink(pack_tpl);
    char pack_file[1024]; snprintf(pack_file, sizeof(pack_file), "%s.pack", pack_tpl);
    char idx_file[1024]; snprintf(idx_file, sizeof(idx_file), "%s.idx", pack_tpl);
    fastgit_pack_t* pack = NULL;
    if (fastgit_pack_create(pack_file, idx_file, &pack) != FASTGIT_OK) return FASTGIT_EIO;
    fastgit_odb_t* odb = fastgit_repository_odb(repo);
    fastgit_odb_iterator_t* it = NULL;
    if (fastgit_odb_iterator_new(odb, &it) == FASTGIT_OK) {
        fastgit_oid_t oid;
        while (fastgit_odb_iterator_next(it, &oid) == FASTGIT_OK) {
            fastgit_odb_object_t obj; if (fastgit_odb_read(odb, &oid, &obj) != FASTGIT_OK) continue;
            fastgit_oid_t dummy; fastgit_pack_add_object(pack, obj.type, obj.data, obj.size, &dummy);
            free(obj.data);
        }
        fastgit_odb_iterator_free(it);
    }
    fastgit_pack_close(pack);
    int pfd = open(pack_file, O_RDONLY);
    if (pfd < 0) { unlink(pack_file); unlink(idx_file); return FASTGIT_EIO; }
    off_t sz = lseek(pfd, 0, SEEK_END); lseek(pfd, 0, SEEK_SET);
    uint8_t* buf = malloc(sz); if (!buf) { close(pfd); unlink(pack_file); unlink(idx_file); return FASTGIT_ENOMEM; }
    ssize_t nr = read(pfd, buf, sz); close(pfd); unlink(pack_file); unlink(idx_file);
    if (nr != sz) { free(buf); return FASTGIT_EIO; }
    *out_pack = buf; *out_len = sz;
    return FASTGIT_OK;
}

static char* build_receive_pack_request(fastgit_repository_t* repo, fastgit_ref_t** remote_refs, size_t remote_count, const char* refspec, uint8_t* pack_data, size_t pack_len, size_t* out_len) {
    (void)refspec;
    // Build pkt-lines for each local head vs remote
    char* buf = NULL; size_t len = 0, cap = 0;
    // Map remote refs by name
    char** llist = NULL; size_t lcnt = 0;
    fastgit_reference_list(repo, "refs/heads/", &llist, &lcnt);
    bool first = true;
    for (size_t i = 0; i < lcnt; i++) {
        fastgit_oid_t new_oid; if (fastgit_reference_lookup(repo, llist[i], &new_oid) != FASTGIT_OK) continue;
        char new_hex[129]; fastgit_oid_to_hex(&new_oid, new_hex, sizeof(new_hex));
        const char* old_hex = "0000000000000000000000000000000000000000000000000000000000000000";
        char old_buf[129]; strcpy(old_buf, old_hex);
        for (size_t r = 0; r < remote_count; r++) if (strcmp(remote_refs[r]->name, llist[i]) == 0) {
            fastgit_oid_to_hex(&remote_refs[r]->oid, old_buf, sizeof(old_buf)); break;
        }
        char line[4096];
        if (first) snprintf(line, sizeof(line), "%s %s %s%c report-status side-band-64k\n", old_buf, new_hex, llist[i], 0);
        else snprintf(line, sizeof(line), "%s %s %s\n", old_buf, new_hex, llist[i]);
        pktline_append(&buf, &len, &cap, line);
        first = false;
    }
    if (llist) fastgit_reference_list_free(llist, lcnt);
    if (len == 0) { if (buf) free(buf); return NULL; }
    pktline_append(&buf, &len, &cap, NULL);
    // append pack data raw after flush
    if (pack_data && pack_len) {
        if (len + pack_len > cap) {
            size_t ncap = len + pack_len + 4096;
            char* nb = realloc(buf, ncap); if (!nb) { free(buf); return NULL; }
            buf = nb; cap = ncap;
        }
        memcpy(buf + len, pack_data, pack_len);
        len += pack_len;
    }
    if (out_len) *out_len = len;
    return buf;
}

fastgit_error_t fastgit_push(fastgit_repository_t* repo, const char* remote, const char* refspec) {
    if (!repo || !remote) return FASTGIT_EINVAL;
    fastgit_remote_t* r = NULL;
    fastgit_error_t err = fastgit_remote_lookup(repo, remote, &r);
    if (err != FASTGIT_OK) err = fastgit_remote_create(repo, remote, remote, &r);
    if (err != FASTGIT_OK) return err;
    if (!fastgit_remote_circuit_allow(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    if (!fastgit_remote_load_acquire(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    bool queued = r->ls_queued > 0;
    fastgit_error_t last = FASTGIT_EUNSUPPORTED;
    if (is_file_url(r->url)) {
        last = push_via_file(repo, file_path_from_url(r->url), refspec);
    } else if (is_http_url(r->url)) {
#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
        fastgit_transport_t* t = NULL;
        last = fastgit_http_transport_new(r, &t);
        if (last == FASTGIT_OK) {
            last = do_http_info_refs_with_retry(r, t);
            if (last == FASTGIT_OK) {
                size_t rlen = 0;
                const char* rdata = fastgit_http_response_data(t, &rlen);
                fastgit_ref_t** tmp = NULL; size_t tcount = 0;
                fastgit_pktline_parse_refs(rdata, rlen, &tmp, &tcount);
                uint8_t* pack = NULL; size_t pack_len = 0;
                if (pack_local_odb(repo, &pack, &pack_len) == FASTGIT_OK) {
                    size_t req_len = 0;
                    char* req = build_receive_pack_request(repo, tmp, tcount, refspec, pack, pack_len, &req_len);
                    fastgit_remote_ls_free(tmp, tcount);
                    free(pack);
                    if (req) {
                        last = fastgit_http_post_receive_pack(t, req, req_len);
                        free(req);
                        // check report-status side-band for unpack ok
                    } else last = FASTGIT_EIO;
                } else {
                    fastgit_remote_ls_free(tmp, tcount);
                    last = FASTGIT_EIO;
                }
            }
            fastgit_http_transport_free(t);
        }
#else
        last = FASTGIT_EUNSUPPORTED;
#endif
    } else if (is_ssh_url(r->url)) {
#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
        fastgit_transport_t* t = NULL;
        last = fastgit_ssh_transport_new(r, &t);
        if (last == FASTGIT_OK) {
            char* rdata = NULL; size_t rlen = 0;
            fastgit_error_t ce = ssh_connect_with_cred(t, r);
            if (ce == FASTGIT_OK) ce = fastgit_ssh_exec_upload_pack(t, "git-receive-pack");
            if (ce == FASTGIT_OK) ce = ssh_read_all(t, &rdata, &rlen);
            if (ce == FASTGIT_OK) {
                fastgit_ref_t** tmp = NULL; size_t tcount = 0;
                fastgit_pktline_parse_refs(rdata, rlen, &tmp, &tcount);
                uint8_t* pack = NULL; size_t pack_len = 0;
                if (pack_local_odb(repo, &pack, &pack_len) == FASTGIT_OK) {
                    size_t req_len = 0;
                    char* req = build_receive_pack_request(repo, tmp, tcount, refspec, pack, pack_len, &req_len);
                    fastgit_remote_ls_free(tmp, tcount);
                    free(pack); free(rdata);
                    if (req) {
                        fastgit_error_t we = fastgit_ssh_channel_write(t, req, req_len);
                        free(req);
                        if (we == FASTGIT_OK) {
                            char* pdata = NULL; size_t plen = 0;
                            fastgit_error_t re = ssh_read_all(t, &pdata, &plen);
                            last = re;
                            if (pdata) free(pdata);
                        } else last = we;
                    } else last = FASTGIT_EIO;
                } else {
                    fastgit_remote_ls_free(tmp, tcount);
                    free(rdata);
                    last = FASTGIT_EIO;
                }
            } else last = ce;
            fastgit_ssh_transport_free(t);
        }
#else
        last = FASTGIT_EUNSUPPORTED;
#endif
    } else {
        last = FASTGIT_EUNSUPPORTED;
    }
    if (fastgit_error_is_retryable(last)) {
        uint32_t max_retries = r->retry.max_retries;
        for (uint32_t attempt = 0; attempt < max_retries && fastgit_error_is_retryable(last); attempt++) {
            if (!fastgit_remote_budget_consume(r)) break;
            fastgit_remote_sleep_ms(fastgit_remote_backoff_ms(r, attempt));
        }
    }
    if (last == FASTGIT_OK) fastgit_remote_circuit_on_success(r);
    else if (fastgit_error_is_retryable(last)) fastgit_remote_circuit_on_failure(r);
    fastgit_remote_load_release(r, queued);
    fastgit_remote_free(r);
    return last;
}

fastgit_error_t fastgit_remote_ls(fastgit_remote_t* remote, fastgit_ref_t*** refs, size_t* count) {
    if (!remote || !refs || !count) return FASTGIT_EINVAL;
    if (!fastgit_remote_circuit_allow(remote)) return FASTGIT_EBUSY;
    if (!fastgit_remote_load_acquire(remote)) return FASTGIT_EBUSY;
    bool queued = remote->ls_queued > 0;
    fastgit_error_t err = FASTGIT_EUNSUPPORTED;
    if (is_http_url(remote->url)) {
#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
        fastgit_transport_t* t = NULL;
        err = fastgit_http_transport_new(remote, &t);
        if (err == FASTGIT_OK) {
            err = do_http_info_refs_with_retry(remote, t);
            if (err == FASTGIT_OK) {
                size_t rlen=0;
                const char* rdata = fastgit_http_response_data(t, &rlen);
                fastgit_error_t pe = fastgit_pktline_parse_refs(rdata, rlen, refs, count);
                if (pe != FASTGIT_OK) { err = pe; *refs=NULL; *count=0; }
            }
            fastgit_http_transport_free(t);
        }
#else
        err = FASTGIT_EUNSUPPORTED;
#endif
    } else if (is_ssh_url(remote->url)) {
#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
        fastgit_transport_t* t = NULL;
        err = fastgit_ssh_transport_new(remote, &t);
        if (err == FASTGIT_OK) {
            char* rdata = NULL; size_t rlen = 0;
            err = do_ssh_info_refs_with_retry(remote, t, &rdata, &rlen);
            if (err == FASTGIT_OK) {
                err = fastgit_pktline_parse_refs(rdata, rlen, refs, count);
                if (err != FASTGIT_OK) { *refs=NULL; *count=0; }
            }
            free(rdata);
            fastgit_ssh_transport_free(t);
        }
#else
        err = FASTGIT_EUNSUPPORTED;
#endif
    }
    if (err == FASTGIT_OK) fastgit_remote_circuit_on_success(remote);
    else if (fastgit_error_is_retryable(err)) fastgit_remote_circuit_on_failure(remote);
    fastgit_remote_load_release(remote, queued);
    return err;
}

void fastgit_remote_ls_free(fastgit_ref_t** refs, size_t count) {
    if (!refs) return;
    for (size_t i = 0; i < count; i++) { free(refs[i]->name); free(refs[i]->target); free(refs[i]); }
    free(refs);
}

fastgit_error_t fastgit_remote_set_transfer_progress(fastgit_remote_t* remote, fastgit_transfer_progress_cb cb, void* payload) {
    if (!remote) return FASTGIT_EINVAL;
    remote->progress_cb = cb; remote->progress_payload = payload;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_remote_set_pqc_config(fastgit_remote_t* remote, const fastgit_pqc_config_t* config) {
    if (!remote || !config) return FASTGIT_EINVAL;
    remote->pqc_config = *config;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_remote_set_retry_policy(fastgit_remote_t* remote, const fastgit_retry_policy_t* policy) {
    if (!remote || !policy) return FASTGIT_EINVAL;
    remote->retry = *policy;
    remote->retry_budget_remaining = policy->retry_budget_per_min;
    remote->retry_budget_reset_ms = fastgit_now_ms() + 60000ULL;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_remote_set_circuit_breaker(fastgit_remote_t* remote, const fastgit_circuit_breaker_t* cb) {
    if (!remote || !cb) return FASTGIT_EINVAL;
    remote->cb = *cb;
    remote->cb_open_until_ms = 0;
    remote->cb_failures = 0;
    remote->cb_successes = 0;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_remote_set_load_shed(fastgit_remote_t* remote, const fastgit_load_shed_t* ls) {
    if (!remote || !ls) return FASTGIT_EINVAL;
    remote->ls = *ls;
    return FASTGIT_OK;
}
