#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
#include <libssh/libssh.h>
#endif

struct fastgit_ssh_transport {
    fastgit_transport_t base;
    char* host;
    char* user;
    int port;
#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
    ssh_session session;
    ssh_channel channel;
    bool connected;
    char* remote_path;
#else
    void* session;
#endif
};

#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
static void parse_ssh_url(const char* url, char** host, char** user, int* port, char** path) {
    // Supports ssh://[user@]host[:port]/path and user@host:path scp-like
    *host = NULL; *user = NULL; *port = 22; *path = NULL;
    if (!url) return;
    if (strncmp(url, "ssh://", 6) == 0) {
        const char* p = url + 6;
        const char* at = strchr(p, '@');
        const char* slash = strchr(p, '/');
        const char* host_start = p;
        if (at && (!slash || at < slash)) {
            *user = strndup(p, at - p);
            host_start = at + 1;
        }
        const char* colon = NULL;
        if (slash) colon = memchr(host_start, ':', slash - host_start);
        else colon = strchr(host_start, ':');
        if (colon) {
            *host = strndup(host_start, colon - host_start);
            *port = atoi(colon + 1);
            if (*port <= 0) *port = 22;
        } else if (slash) {
            *host = strndup(host_start, slash - host_start);
        } else {
            *host = strdup(host_start);
        }
        if (slash) *path = strdup(slash);
        else *path = strdup("/");
    } else {
        // scp-like: user@host:path or host:path
        const char* at = strchr(url, '@');
        const char* colon = strchr(url, ':');
        if (colon) {
            if (at && at < colon) {
                *user = strndup(url, at - url);
                *host = strndup(at + 1, colon - (at + 1));
            } else {
                *host = strndup(url, colon - url);
            }
            *path = strdup(colon + 1);
        } else {
            *host = strdup(url);
            *path = strdup("/");
        }
    }
}
#endif

fastgit_error_t fastgit_ssh_transport_new(fastgit_remote_t* remote, fastgit_transport_t** out) {
    if (!remote || !out) return FASTGIT_EINVAL;
#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
    const char* url = fastgit_remote_url(remote);
    if (!url) return FASTGIT_EINVAL;
    struct fastgit_ssh_transport* t = calloc(1, sizeof(*t));
    if (!t) return FASTGIT_ENOMEM;
    t->base.type = FASTGIT_TRANSPORT_SSH;
    t->base.handle = NULL;
    parse_ssh_url(url, &t->host, &t->user, &t->port, &t->remote_path);
    if (!t->host) { free(t); return FASTGIT_EINVAL; }
    ssh_session sess = ssh_new();
    if (!sess) { free(t->host); free(t->user); free(t->remote_path); free(t); return FASTGIT_EIO; }
    ssh_options_set(sess, SSH_OPTIONS_HOST, t->host);
    if (t->user) ssh_options_set(sess, SSH_OPTIONS_USER, t->user);
    ssh_options_set(sess, SSH_OPTIONS_PORT, &t->port);
    // Timeouts: connect 10s, prevents Aug17-style stuck conns
    long timeout = 10;
    ssh_options_set(sess, SSH_OPTIONS_TIMEOUT, &timeout);
    // Known-hosts strictness off for now; PQC host keys via libssh if built with OQS
    int strict = 0;
    ssh_options_set(sess, SSH_OPTIONS_STRICTHOSTKEYCHECK, &strict);
    t->session = sess;
    t->channel = NULL;
    t->connected = false;
    *out = (fastgit_transport_t*)t;
    return FASTGIT_OK;
#else
    (void)remote; (void)out;
    return FASTGIT_EUNSUPPORTED;
#endif
}

void fastgit_ssh_transport_free(fastgit_transport_t* transport) {
    if (!transport) return;
#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
    struct fastgit_ssh_transport* t = (struct fastgit_ssh_transport*)transport;
    if (t->channel) { ssh_channel_close(t->channel); ssh_channel_free(t->channel); }
    if (t->session) { if (t->connected) ssh_disconnect(t->session); ssh_free(t->session); }
    free(t->host);
    free(t->user);
    free(t->remote_path);
    free(t);
#else
    (void)transport;
#endif
}

#if defined(FASTGIT_HAVE_LIBSSH) && FASTGIT_HAVE_LIBSSH
fastgit_error_t fastgit_ssh_connect(fastgit_transport_t* transport, const fastgit_cred_data_t* cred) {
    if (!transport) return FASTGIT_EINVAL;
    struct fastgit_ssh_transport* t = (struct fastgit_ssh_transport*)transport;
    if (t->connected) return FASTGIT_OK;
    int rc = ssh_connect(t->session);
    if (rc != SSH_OK) return FASTGIT_EIO;
    // Auth: try cred-provided key, then agent, then default
    if (cred && cred->type == FASTGIT_CRED_SSH_KEY && cred->private_key) {
        ssh_key key = NULL;
        if (ssh_pki_import_privkey_base64(cred->private_key, cred->passphrase, NULL, NULL, &key) == SSH_OK) {
            rc = ssh_userauth_try_publickey(t->session, t->user, key);
            ssh_key_free(key);
            if (rc == SSH_AUTH_SUCCESS) { t->connected = true; return FASTGIT_OK; }
        }
    }
    // Agent / default
    rc = ssh_userauth_publickey_auto(t->session, NULL, NULL);
    if (rc == SSH_AUTH_SUCCESS) { t->connected = true; return FASTGIT_OK; }
    // Password fallback if provided
    if (cred && cred->password) {
        rc = ssh_userauth_password(t->session, t->user, cred->password);
        if (rc == SSH_AUTH_SUCCESS) { t->connected = true; return FASTGIT_OK; }
    }
    // Try none
    rc = ssh_userauth_none(t->session, NULL);
    if (rc == SSH_AUTH_SUCCESS) { t->connected = true; return FASTGIT_OK; }
    ssh_disconnect(t->session);
    return FASTGIT_EBUSY; // auth failure -> circuit breaker
}

fastgit_error_t fastgit_ssh_exec_upload_pack(fastgit_transport_t* transport, const char* git_cmd) {
    if (!transport || !git_cmd) return FASTGIT_EINVAL;
    struct fastgit_ssh_transport* t = (struct fastgit_ssh_transport*)transport;
    if (!t->connected) return FASTGIT_EIO;
    if (t->channel) { ssh_channel_close(t->channel); ssh_channel_free(t->channel); t->channel = NULL; }
    ssh_channel ch = ssh_channel_new(t->session);
    if (!ch) return FASTGIT_EIO;
    if (ssh_channel_open_session(ch) != SSH_OK) { ssh_channel_free(ch); return FASTGIT_EIO; }
    char cmd[4096];
    const char* path = t->remote_path ? t->remote_path : "/";
    // Strip leading / for git command
    if (path[0] == '/') path++;
    snprintf(cmd, sizeof(cmd), "%s '%s'", git_cmd, path);
    if (ssh_channel_request_exec(ch, cmd) != SSH_OK) { ssh_channel_close(ch); ssh_channel_free(ch); return FASTGIT_EIO; }
    t->channel = ch;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_ssh_channel_read(fastgit_transport_t* transport, void* buf, size_t len, size_t* out_read) {
    if (!transport || !buf || !out_read) return FASTGIT_EINVAL;
    struct fastgit_ssh_transport* t = (struct fastgit_ssh_transport*)transport;
    if (!t->channel) return FASTGIT_EIO;
    int n = ssh_channel_read(t->channel, buf, (uint32_t)len, 0);
    if (n < 0) return FASTGIT_EIO;
    *out_read = (size_t)n;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_ssh_channel_write(fastgit_transport_t* transport, const void* buf, size_t len) {
    if (!transport || !buf) return FASTGIT_EINVAL;
    struct fastgit_ssh_transport* t = (struct fastgit_ssh_transport*)transport;
    if (!t->channel) return FASTGIT_EIO;
    int n = ssh_channel_write(t->channel, buf, (uint32_t)len);
    if (n < 0 || (size_t)n != len) return FASTGIT_EIO;
    return FASTGIT_OK;
}
#endif
