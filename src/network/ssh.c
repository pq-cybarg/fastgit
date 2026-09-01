#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>

struct fastgit_ssh_transport {
    fastgit_transport_t base;
    char* host;
    char* user;
    int port;
    void* session;
};

fastgit_error_t fastgit_ssh_transport_new(fastgit_remote_t* remote, fastgit_transport_t** out) {
    (void)remote;
    (void)out;
    return FASTGIT_EUNSUPPORTED;
}
