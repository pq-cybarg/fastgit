#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
#include <curl/curl.h>
#endif

struct fastgit_http_transport {
    fastgit_transport_t base;
    char* url;
#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
    void* curl_handle;
    struct curl_slist* headers;
    char* cached_token;
    uint64_t token_expiry_ms;
    uint64_t last_retry_after_ms;
    char* user_agent;
    long last_http_code;
    char* response_buf;
    size_t response_len;
    size_t response_cap;
    char* header_buf;
    size_t header_len;
#endif
};

#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
static uint64_t now_ms_http(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static uint64_t parse_retry_after_ms(const char* value) {
    if (!value) return 0;
    while (*value && isspace((unsigned char)*value)) value++;
    if (!*value) return 0;
    // Numeric seconds?
    char* end = NULL;
    long secs = strtol(value, &end, 10);
    if (end != value) {
        // Check if remaining is only ws
        while (*end && isspace((unsigned char)*end)) end++;
        if (*end == '\0' && secs >= 0) {
            return (uint64_t)secs * 1000ULL;
        }
    }
    // HTTP-date form not parsed; treat as 0 (use exponential backoff)
    return 0;
}

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)userdata;
    size_t total = size * nmemb;
    if (t->response_len + total + 1 > t->response_cap) {
        size_t need = t->response_len + total + 1;
        size_t cap = t->response_cap ? t->response_cap * 2 : 8192;
        while (cap < need) cap *= 2;
        char* nb = realloc(t->response_buf, cap);
        if (!nb) return 0;
        t->response_buf = nb;
        t->response_cap = cap;
    }
    memcpy(t->response_buf + t->response_len, ptr, total);
    t->response_len += total;
    t->response_buf[t->response_len] = '\0';
    return total;
}

static size_t header_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)userdata;
    size_t total = size * nitems;
    // Look for Retry-After
    const char* hdr = "Retry-After:";
    size_t hlen = strlen(hdr);
    if (total > hlen) {
        bool match = true;
        for (size_t i = 0; i < hlen; i++) {
            if (tolower((unsigned char)buffer[i]) != tolower((unsigned char)hdr[i])) { match = false; break; }
        }
        if (match) {
            const char* val = buffer + hlen;
            // val may include total-hlen chars; copy to temp
            size_t vlen = total - hlen;
            // Trim trailing \r\n
            while (vlen > 0 && (val[vlen-1] == '\r' || val[vlen-1] == '\n')) vlen--;
            char tmp[256];
            size_t copy = vlen < sizeof(tmp)-1 ? vlen : sizeof(tmp)-1;
            memcpy(tmp, val, copy);
            tmp[copy] = '\0';
            t->last_retry_after_ms = parse_retry_after_ms(tmp);
        }
    }
    // Also capture WWW-Authenticate expiry hints if present (simple)
    return total;
}

static void http_reset_buffers(struct fastgit_http_transport* t) {
    t->response_len = 0;
    if (t->response_buf) t->response_buf[0] = '\0';
    t->last_retry_after_ms = 0;
    t->last_http_code = 0;
}
#endif

fastgit_error_t fastgit_http_transport_new(fastgit_remote_t* remote, fastgit_transport_t** out) {
    if (!remote || !out) return FASTGIT_EINVAL;
#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
    const char* url = fastgit_remote_url(remote);
    if (!url) return FASTGIT_EINVAL;
    struct fastgit_http_transport* t = calloc(1, sizeof(*t));
    if (!t) return FASTGIT_ENOMEM;
    t->base.type = FASTGIT_TRANSPORT_HTTP;
    t->base.handle = NULL;
    t->url = strdup(url);
    if (!t->url) { free(t); return FASTGIT_ENOMEM; }
    t->user_agent = strdup("fastgit/0.1.0 (libcurl)");
    if (!t->user_agent) { free(t->url); free(t); return FASTGIT_ENOMEM; }
    // Global init once
    static bool curl_inited = false;
    if (!curl_inited) { curl_global_init(CURL_GLOBAL_DEFAULT); curl_inited = true; }
    CURL* curl = curl_easy_init();
    if (!curl) { free(t->url); free(t->user_agent); free(t); return FASTGIT_EIO; }
    t->curl_handle = curl;
    t->headers = NULL;
    t->headers = curl_slist_append(t->headers, "Content-Type: application/x-git-upload-pack-request");
    t->headers = curl_slist_append(t->headers, "Accept: application/x-git-upload-pack-result");
    // Low-speed limits to avoid hanging on degraded links (GitHub Aug17 issue: stuck conns)
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, t);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, t->headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, t->user_agent);
    // Accept gzip
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    *out = (fastgit_transport_t*)t;
    return FASTGIT_OK;
#else
    (void)remote; (void)out;
    return FASTGIT_EUNSUPPORTED;
#endif
}

void fastgit_http_transport_free(fastgit_transport_t* transport) {
    if (!transport) return;
#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    if (t->curl_handle) curl_easy_cleanup((CURL*)t->curl_handle);
    if (t->headers) curl_slist_free_all(t->headers);
    free(t->url);
    free(t->user_agent);
    free(t->cached_token);
    free(t->response_buf);
    free(t->header_buf);
    free(t);
#else
    (void)transport;
#endif
}

#if defined(FASTGIT_HAVE_CURL) && FASTGIT_HAVE_CURL
uint64_t fastgit_http_last_retry_after_ms(fastgit_transport_t* transport) {
    if (!transport) return 0;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    return t->last_retry_after_ms;
}

bool fastgit_http_has_valid_token(fastgit_transport_t* transport) {
    if (!transport) return false;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    if (!t->cached_token) return false;
    if (t->token_expiry_ms == 0) return true;
    return now_ms_http() + 30000ULL < t->token_expiry_ms; // 30s clock skew margin
}

fastgit_error_t fastgit_http_set_token(fastgit_transport_t* transport, const char* token, uint64_t expiry_ms) {
    if (!transport || !token) return FASTGIT_EINVAL;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    char* copy = strdup(token);
    if (!copy) return FASTGIT_ENOMEM;
    free(t->cached_token);
    t->cached_token = copy;
    t->token_expiry_ms = expiry_ms;
    // Update Authorization header
    if (t->headers) curl_slist_free_all(t->headers);
    t->headers = NULL;
    t->headers = curl_slist_append(t->headers, "Content-Type: application/x-git-upload-pack-request");
    t->headers = curl_slist_append(t->headers, "Accept: application/x-git-upload-pack-result");
    char auth[8192];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    t->headers = curl_slist_append(t->headers, auth);
    curl_easy_setopt((CURL*)t->curl_handle, CURLOPT_HTTPHEADER, t->headers);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_http_get_info_refs(fastgit_transport_t* transport, const char* service) {
    if (!transport) return FASTGIT_EINVAL;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    CURL* curl = (CURL*)t->curl_handle;
    char url[4096];
    const char* svc = service ? service : "git-upload-pack";
    snprintf(url, sizeof(url), "%s/info/refs?service=%s", t->url, svc);
    http_reset_buffers(t);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_POST, 0L);
    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) return FASTGIT_EIO;
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    t->last_http_code = code;
    if (code == 429 || code == 503) return FASTGIT_EAGAIN;
    if (code == 401 || code == 403) return FASTGIT_EBUSY; // auth, circuit breaker path
    if (code >= 500) return FASTGIT_EAGAIN;
    if (code >= 400) return FASTGIT_ERROR;
    if (code < 200 || code >= 300) return FASTGIT_ERROR;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_http_post_upload_pack(fastgit_transport_t* transport, const void* body, size_t len) {
    if (!transport || !body) return FASTGIT_EINVAL;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    CURL* curl = (CURL*)t->curl_handle;
    char url[4096];
    snprintf(url, sizeof(url), "%s/git-upload-pack", t->url);
    http_reset_buffers(t);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)len);
    // Git smart HTTP uses x-git-upload-pack-result
    struct curl_slist* hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/x-git-upload-pack-request");
    hdrs = curl_slist_append(hdrs, "Accept: application/x-git-upload-pack-result");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, t->headers);
    if (rc != CURLE_OK) return FASTGIT_EIO;
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    t->last_http_code = code;
    if (code == 429 || code == 503) return FASTGIT_EAGAIN;
    if (code >= 500) return FASTGIT_EAGAIN;
    if (code >= 400) return FASTGIT_ERROR;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_http_post_receive_pack(fastgit_transport_t* transport, const void* body, size_t len) {
    if (!transport || !body) return FASTGIT_EINVAL;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    CURL* curl = (CURL*)t->curl_handle;
    char url[4096];
    snprintf(url, sizeof(url), "%s/git-receive-pack", t->url);
    http_reset_buffers(t);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)len);
    struct curl_slist* hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/x-git-receive-pack-request");
    hdrs = curl_slist_append(hdrs, "Accept: application/x-git-receive-pack-result");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, t->headers);
    if (rc != CURLE_OK) return FASTGIT_EIO;
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    t->last_http_code = code;
    if (code == 429 || code == 503) return FASTGIT_EAGAIN;
    if (code >= 500) return FASTGIT_EAGAIN;
    if (code >= 400) return FASTGIT_ERROR;
    return FASTGIT_OK;
}

const char* fastgit_http_response_data(fastgit_transport_t* transport, size_t* out_len) {
    if (!transport) return NULL;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    if (out_len) *out_len = t->response_len;
    return t->response_buf ? t->response_buf : "";
}

long fastgit_http_last_status(fastgit_transport_t* transport) {
    if (!transport) return 0;
    struct fastgit_http_transport* t = (struct fastgit_http_transport*)transport;
    return t->last_http_code;
}
#else
const char* fastgit_http_response_data(fastgit_transport_t* transport, size_t* out_len) {
    (void)transport; if (out_len) *out_len = 0; return NULL;
}
long fastgit_http_last_status(fastgit_transport_t* transport) { (void)transport; return 0; }
#endif
