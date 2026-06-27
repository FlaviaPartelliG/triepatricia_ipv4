# Tema 6 - Roteador IP por Longest Prefix Match (Trie de Patricia)
# Build with GNU make inside WSL/Linux. Requires gcc with pthread and the
# sanitizer runtimes (libasan, libtsan, libubsan - shipped with gcc).

CC      ?= gcc
CSTD    := -std=c11
WARN    := -Wall -Wextra -Werror
OPT     := -O2
INC     := -Iinclude
CFLAGS  := $(CSTD) $(WARN) $(OPT) $(INC) -g
LDLIBS  := -pthread

# sanitizer flags reused by the test targets
SANFLAGS := $(CSTD) $(WARN) -O1 -g $(INC) -fno-omit-frame-pointer
ASAN     := -fsanitize=address,undefined
TSAN     := -fsanitize=thread

LIBSRC := $(wildcard src/*.c)

BIN     := bin
DATA    := data/prefixes.txt
GEN_N   ?= 100000

TOOLS := $(BIN)/gen_prefixes $(BIN)/router_sim $(BIN)/bench $(BIN)/bench_struct
TESTS := test_ipv4 test_patricia test_multibit test_rcu test_concurrent

.PHONY: all gen test asan tsan stress bench graphs valgrind clean dirs help
.DELETE_ON_ERROR:

all: dirs $(TOOLS)          ## build all tools (warning-free)

dirs:
	@mkdir -p $(BIN) data bench report

# each tool is linked against the whole library (unused code is harmless)
$(BIN)/%: tools/%.c $(LIBSRC) | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

gen: $(BIN)/gen_prefixes    ## generate data/prefixes.txt (GEN_N prefixes)
	$(BIN)/gen_prefixes $(DATA) $(GEN_N) 1 256

# ---- correctness: unit + concurrency tests under ASan + UBSan ----
test: dirs                  ## build & run all tests under ASan+UBSan
	@set -e; for t in $(TESTS); do \
	  echo "=== build $$t (asan+ubsan) ==="; \
	  $(CC) $(SANFLAGS) $(ASAN) tests/$$t.c $(LIBSRC) -o $(BIN)/$$t.asan $(LDLIBS); \
	done
	$(BIN)/test_ipv4.asan
	$(BIN)/test_patricia.asan
	$(BIN)/test_multibit.asan
	$(BIN)/test_rcu.asan
	$(BIN)/test_concurrent.asan 1000 4 8000 5000 300

asan: test                  ## alias for `test`

# ---- data-race proof: the test of fire under ThreadSanitizer ----
tsan: dirs                  ## run the concurrent test of fire under TSan (10^5 prefixes)
	$(CC) $(SANFLAGS) $(TSAN) tests/test_concurrent.c $(LIBSRC) \
	  -o $(BIN)/test_concurrent.tsan $(LDLIBS)
	$(BIN)/test_concurrent.tsan 2000 8 40000 100000 800

stress: dirs                ## heavier/longer concurrent run under TSan
	$(CC) $(SANFLAGS) $(TSAN) tests/test_concurrent.c $(LIBSRC) \
	  -o $(BIN)/test_concurrent.tsan $(LDLIBS)
	$(BIN)/test_concurrent.tsan 5000 8 200000 100000 3000

# ---- experimental data for the report ----
bench: all gen              ## run benchmarks, emit CSVs in bench/
	$(BIN)/bench --table $(DATA) --threads "1,2,4,8,16" --seconds 3 --writer on  --out bench/throughput_writer.csv
	$(BIN)/bench --table $(DATA) --threads "1,2,4,8,16" --seconds 3 --writer off --out bench/throughput_nowriter.csv
	$(BIN)/bench_struct --table $(DATA) --packets 10000000 --out bench/structcmp.csv

graphs:                     ## render PNGs from the benchmark CSVs
	python3 scripts/plot.py

valgrind: all gen           ## leak check the forwarder
	valgrind --leak-check=full --error-exitcode=1 \
	  $(BIN)/router_sim --table $(DATA) --packets 200000 --threads 1 --show 0

clean:                      ## remove build artifacts (keeps data/prefixes.txt)
	rm -rf $(BIN) bench/*.csv bench/*.png

help:                       ## list targets
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	  awk 'BEGIN{FS=":.*?## "}{printf "  %-10s %s\n", $$1, $$2}'
