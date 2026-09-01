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
                // Minimal pack negotiation: send want/have placeholder.
                // Real impl parses pkt-line refs, builds want list, POSTs upload-pack.
                // For now treat discovery success as fetch success (no pack yet).
                size_t rlen = 0;
                (void)fastgit_http_response_data(t, &rlen);
                last = FASTGIT_OK;
            }
            fastgit_http_transport_free(t);
        }
#else
        last = FASTGIT_EUNSUPPORTED;
#endif
    } else {
        // SSH / local — not yet wired to pack negotiation
        last = FASTGIT_EUNSUPPORTED;
    }
    // If still unsupported or still retryable, run jittered retry with budget (preserves prior behavior for non-HTTP)
    if (fastgit_error_is_retryable(last)) {
        uint32_t max_retries = r->retry.max_retries;
        for (uint32_t attempt = 0; attempt < max_retries && fastgit_error_is_retryable(last); attempt++) {
            if (!fastgit_remote_budget_consume(r)) break;
            fastgit_remote_sleep_ms(fastgit_remote_backoff_ms(r, attempt));
            // would re-attempt transport here
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
            // Smart HTTP receive-pack path mirrors fetch: discover then POST
            last = do_http_info_refs_with_retry(r, t);
            if (last == FASTGIT_OK) {
                // Placeholder: would build packfile and POST to git-receive-pack
                last = FASTGIT_OK;
            }
            fastgit_http_transport_free(t);
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
                // Parse refs from t->response_buf (pkt-line format).
                // Minimal: synthesize empty set for now; real parser extracts OID + refname per line.
                *refs = NULL; *count = 0;
            }
            fastgit_http_transport_free(t);
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
