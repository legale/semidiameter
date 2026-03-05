# RADIUS UDP Proxy

A high-performance, single-threaded RADIUS UDP proxy for Linux that keeps the incoming interface, reuses socket pools, and optionally remaps IDs/authenticators when a shared secret is provided.

## Overview
- **Interface-aware forwarding:** Each NAS listener socket is created with `IP_PKTINFO` so responses can be returned on the same interface and source IP the NAS used, preventing asymmetric routing issues.
- **Slot-based routing:** `slot.c` manages a ring buffer of 256‑entry slots per UDP socket pool. Each slot tracks the original NAS request (ID, Authenticator, source address, interface index, etc.) so the response can be restored to the NAS that issued it.
- **Socket pool:** The proxy pre-allocates up to eight UDP sockets (`-n` flag) to talk to the RADIUS server. Each pool exposes 256 possible RADIUS IDs to avoid per-request socket creation.
- **Authenticator handling:** `md5.c` exposes the MD5/HMAC helpers used in `main.c` to recalculate Request/Response Authenticators and the Message-Authenticator whenever the proxy rewrites IDs in remapping mode.

## Requirements
- Linux with `epoll` and `IP_PKTINFO` support (the proxy is designed for IPv4 only).
- A C compiler (`cc`, `gcc`, `clang`, or `musl-gcc` for static builds) and a POSIX shell for the test helper script.

## Building
The `Makefile` builds the proxy and the companion tests in one step.

```bash
make          # builds radius_proxy + tests
make debug    # enables ASan/UBsan with -O0
make clang    # builds with clang instead of the default CC
make static   # builds a static binary with musl-gcc
make clean    # removes all generated binaries
```

## Running
```
./radius_proxy [-s secret] [-n pools] listen_ip:port radius_ip:port [...]
```
- `listen_ip:port` is the NAS-facing address; you can provide up to four of them (MAX_NAS is 4).
- `radius_ip:port` is the downstream RADIUS server that receives remapped requests.
- `-s secret` enables remapping mode. The proxy will rewrite each NAS ID into a pool-local ID, recalc the Request/Response Authenticators, and update the Message-Authenticator using HMAC-MD5.
- Without `-s`, the proxy operates transparently and relies on `slot_alloc` to guarantee each UDP socket pool only uses an ID that is free.
- `-n pools` adjusts how many UDP socket pools (and corresponding slot ranges) the proxy uses; the default is 4, and the cap is 8.

Log lines include timestamps and clarify whether a request is remapped or transparent, the pool used, and the source NAS address.

## Testing
`make test` runs `bash scripts/run_tests.sh` to start:
1. `tests/mock_radius` — a lightweight echoing server bound to port `18121`.
2. `./radius_proxy` listening on `127.0.0.1:18120` and proxying to `127.0.0.1:18121`.

`run_tests.sh` accepts an optional mode argument:
- `bash scripts/run_tests.sh unit` runs only `tests/unit_test`, which exercises `slot_alloc`/`slot_find` for wraparound, pool collisions, and slot reuse.
- `bash scripts/run_tests.sh stress` runs `tests/stress_test 127.0.0.1 18120`, issuing 100k Access-Request packets from 10 clients and asserting the echo replies match.
- `bash scripts/run_tests.sh perf` runs `tests/perf_test 127.0.0.1 18120` to measure throughput and latency over 1M packets, printing total requests, time, and latency p99/avg.

You can also run the helpers directly once `make` has built them:
```bash
make unit
make stress
make perf
```

The tests rely only on the proxy binary, the mock server, and standard sockets so they can run on any Linux box with `bash`.
