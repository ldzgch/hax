/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "diag.h"
#include "files.h"
#include "harness.h"

static void test_diag_sequence(void)
{
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    EXPECT(saved >= 0);
    FILE *tmp = t_tmpfile();
    EXPECT(tmp != NULL);
    EXPECT(dup2(fileno(tmp), STDERR_FILENO) >= 0);

    unsigned long before = hax_diag_sequence();
    hax_warn("sequence test");
    EXPECT(hax_diag_sequence() == before + 1);

    EXPECT(dup2(saved, STDERR_FILENO) >= 0);
    close(saved);
    fclose(tmp);
}

int main(void)
{
    test_diag_sequence();

    T_REPORT();
}
