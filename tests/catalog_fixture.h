/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_CATALOG_FIXTURE_H
#define HAX_TESTS_CATALOG_FIXTURE_H

/* Point XDG_CACHE_HOME at an owned Unicode scratch directory and replace its catalog snapshot.
 * Repeated calls reuse the directory; catalog memoization remains under the test's control. */
void t_catalog_write(const char *json);

#endif /* HAX_TESTS_CATALOG_FIXTURE_H */
