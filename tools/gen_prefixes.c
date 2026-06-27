/* gen_prefixes.c - write a synthetic BGP-like routing table to disk.
 *
 * Usage: gen_prefixes [path] [count] [seed] [n_ifaces]
 * Defaults: data/prefixes.txt 100000 1 256
 *
 * This is run by `make gen` so the 10^5-prefix table exists before any test or
 * benchmark, satisfying Tema 6's "table already made when the project runs".
 */
#include "prefixgen.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "data/prefixes.txt";
    size_t count     = (argc > 2) ? strtoul(argv[2], NULL, 10) : 100000;
    unsigned seed    = (argc > 3) ? (unsigned)strtoul(argv[3], NULL, 10) : 1u;
    int n_ifaces     = (argc > 4) ? atoi(argv[4]) : 256;

    if (count == 0) { fprintf(stderr, "count must be > 0\n"); return 1; }

    printf("Generating %lu prefixes (seed=%u, ifaces=%d) -> %s\n",
           (unsigned long)count, seed, n_ifaces, path);

    if (!prefixgen_write_file(path, count, seed, n_ifaces)) {
        fprintf(stderr, "failed to generate/write table to %s\n", path);
        return 1;
    }

    printf("OK: wrote %lu unique prefixes\n", (unsigned long)count);
    return 0;
}
