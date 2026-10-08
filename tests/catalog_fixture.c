/* SPDX-License-Identifier: MIT */
#include "catalog_fixture.h"

#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "harness.h"
#include "system/fs.h"
#include "system/path.h"

void t_catalog_write(const char *json)
{
    static const char *root;
    if (!root)
        root = t_tempdir();
    char *directory = path_join(root, "catalog-\xc3\xa9");
    char *hax_directory = path_join(directory, "hax");
    EXPECT(fs_mkdir_p(hax_directory) == 0);
    t_env_set("XDG_CACHE_HOME", directory);
    char *path = path_join(hax_directory, "catalog.json");
    EXPECT(fs_write_atomic(path, json, strlen(json), 0) == 0);
    free(path);
    free(hax_directory);
    free(directory);
}
