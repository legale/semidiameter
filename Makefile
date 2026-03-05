CC ?= cc
CFLAGS += -O2
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -Werror
CFLAGS += -Wshadow
CFLAGS += -Wconversion
CFLAGS += -Wformat=2
CFLAGS += -Wundef
CFLAGS += -Wpointer-arith
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wmissing-prototypes
CFLAGS += -Wmissing-declarations
CFLAGS += -Wcast-align
CFLAGS += -Wwrite-strings
CFLAGS += -Wno-unused-parameter

BIN = radius_proxy
TEST_BINS = tests/unit_test tests/stress_test tests/perf_test tests/mock_radius

all: $(BIN) $(TEST_BINS)

$(BIN): main.c slot.c md5.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(if $(STRIP_BIN),strip $@)

tests/unit_test: tests/unit_test.c slot.c md5.c
	$(CC) $(CFLAGS) -o $@ $^

tests/stress_test: tests/stress_test.c
	$(CC) $(CFLAGS) -o $@ $<

tests/perf_test: tests/perf_test.c
	$(CC) $(CFLAGS) -o $@ $<

tests/mock_radius: tests/mock_radius.c
	$(CC) $(CFLAGS) -o $@ $<

static: CC = musl-gcc
static: CFLAGS += -Wno-sign-conversion
static: LDFLAGS += -static
static: STRIP_BIN = 1
static: $(BIN)

debug: CFLAGS += -O0 -g3 -fsanitize=address -fsanitize=undefined
debug: all

leak: CC = gcc
leak: CFLAGS += -O0 -g3 -fsanitize=address -fno-omit-frame-pointer
leak: LDFLAGS += -fsanitize=address
leak: clean all
	ASAN_OPTIONS=detect_leaks=1 ./tests/unit_test
	ASAN_OPTIONS=detect_leaks=1 bash scripts/run_tests.sh leak 10

clang: CC = clang
clang: all

test: all
	bash scripts/run_tests.sh

unit: tests/unit_test
	./tests/unit_test

stress: tests/stress_test tests/mock_radius radius_proxy
	bash scripts/run_tests.sh stress

perf: tests/perf_test tests/mock_radius radius_proxy
	bash scripts/run_tests.sh perf

profile: tests/perf_test tests/mock_radius radius_proxy
	bash scripts/run_tests.sh profile 1000

clean:
	rm -f $(BIN) $(TEST_BINS) callgrind.out*

.PHONY: all static debug clang test unit stress perf leak clean
