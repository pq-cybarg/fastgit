#include "fastgit/fastgit.h"
#include "fastgit/cli.h"
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv) {
    fastgit_cli_t* cli;
    fastgit_error_t err = fastgit_cli_new(argc, argv, &cli);
    if (err != FASTGIT_OK) {
        fprintf(stderr, "fatal: could not initialize CLI: %s\n", fastgit_error_string(err));
        return 1;
    }

    int ret = fastgit_cli_run(cli);
    fastgit_cli_free(cli);
    return ret;
}
