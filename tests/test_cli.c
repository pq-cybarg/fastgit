#include "fastgit/cli.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

int main(void) {
    printf("Testing CLI...\n");

    char* argv[] = { "fastgit", "init", "/tmp/test" };
    fastgit_cli_t* cli;
    fastgit_error_t err = fastgit_cli_new(3, argv, &cli);
    assert(err == FASTGIT_OK);
    fastgit_cli_free(cli);

    size_t count;
    const fastgit_cmd_def_t* cmds = fastgit_cli_commands(&count);
    assert(cmds != NULL);
    assert(count > 30);

    bool found_init = false;
    bool found_hash = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(cmds[i].name, "init") == 0) found_init = true;
        if (strcmp(cmds[i].name, "hash-object") == 0) found_hash = true;
    }
    assert(found_init);
    assert(found_hash);

    fastgit_config_entry_t* entries;
    size_t entry_count;
    err = fastgit_config_list(&entries, &entry_count);
    assert(err == FASTGIT_OK);
    fastgit_config_free(entries, entry_count);

    printf("All CLI tests passed!\n");
    return 0;
}
