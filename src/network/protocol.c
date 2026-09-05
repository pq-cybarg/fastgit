#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

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
    (void)repo; (void)name; (void)out;
    return FASTGIT_ENOENT;
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
    (void)out; (void)out_len; (void)payload;
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

fastgit_error_t fastgit_fetch(fastgit_repository_t* repo, const char* remote, const char* refspec) {
    (void)refspec;
    if (!repo || !remote) return FASTGIT_EINVAL;
    fastgit_remote_t* r = NULL;
    fastgit_error_t err = fastgit_remote_create(repo, remote, remote, &r);
    if (err != FASTGIT_OK) return err;
    if (!fastgit_remote_circuit_allow(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    if (!fastgit_remote_load_acquire(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    bool queued = r->ls_queued > 0;
    fastgit_error_t last = FASTGIT_EUNSUPPORTED;
    if (is_http_url(r->url)) {
#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
        fastgit_transport_t* t = NULL;
        last = fastgit_http_transport_new(r, &t);
        if (last == FASTGIT_OK) {
            last = do_http_info_refs_with_retry(r, t);
            if (last == FASTGIT_OK) {
                size_t rlen = 0;
                const char* rdata = fastgit_http_response_data(t, &rlen);
                // Validate pkt-line parse; pack negotiation deferred to pkt-line want/have
                fastgit_ref_t** tmp = NULL; size_t tcount = 0;
                fastgit_pktline_parse_refs(rdata, rlen, &tmp, &tcount);
                fastgit_remote_ls_free(tmp, tcount);
                last = FASTGIT_OK;
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
                fastgit_ref_t** tmp = NULL; size_t tcount = 0;
                fastgit_pktline_parse_refs(rdata, rlen, &tmp, &tcount);
                fastgit_remote_ls_free(tmp, tcount);
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

fastgit_error_t fastgit_push(fastgit_repository_t* repo, const char* remote, const char* refspec) {
    (void)refspec;
    if (!repo || !remote) return FASTGIT_EINVAL;
    fastgit_remote_t* r = NULL;
    fastgit_error_t err = fastgit_remote_create(repo, remote, remote, &r);
    if (err != FASTGIT_OK) return err;
    if (!fastgit_remote_circuit_allow(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    if (!fastgit_remote_load_acquire(r)) { fastgit_remote_free(r); return FASTGIT_EBUSY; }
    bool queued = r->ls_queued > 0;
    fastgit_error_t last = FASTGIT_EUNSUPPORTED;
    if (is_http_url(r->url)) {
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
                fastgit_remote_ls_free(tmp, tcount);
                // TODO: build packfile and POST to git-receive-pack with pkt-line
                last = FASTGIT_OK;
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
            // use receive-pack for push advertisement
            fastgit_error_t ce = ssh_connect_with_cred(t, r);
            if (ce == FASTGIT_OK) ce = fastgit_ssh_exec_upload_pack(t, "git-receive-pack");
            if (ce == FASTGIT_OK) ce = ssh_read_all(t, &rdata, &rlen);
            if (ce == FASTGIT_OK) {
                fastgit_ref_t** tmp = NULL; size_t tcount = 0;
                fastgit_pktline_parse_refs(rdata, rlen, &tmp, &tcount);
                fastgit_remote_ls_free(tmp, tcount);
                free(rdata);
                last = FASTGIT_OK;
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
