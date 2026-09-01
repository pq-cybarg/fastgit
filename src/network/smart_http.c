#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>

struct fastgit_http_transport {
    fastgit_transport_t base;
    char* url;
    void* curl_handle;
};

fastgit_error_t fastgit_http_transport_new(fastgit_remote_t* remote, fastgit_transport_t** out) {
    (void)remote;
    (void)out;
    return FASTGIT_EUNSUPPORTED;
}
