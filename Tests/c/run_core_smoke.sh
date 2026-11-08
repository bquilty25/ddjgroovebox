#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the SloopCore C API smoke test; screenshots and the flash image go to .build-c/smoke.
set -e
cd "$(dirname "$0")/../.."
mkdir -p .build-c/smoke
cc -c -O2 -w -fwrapv -fno-strict-aliasing -ISources/SloopCore/include -Ivendor/sloop/build/gen \
    -Ivendor/sloop/firmware/src -Ivendor/sloop/firmware/hal Sources/SloopCore/sloop_core.c -o .build-c/sloop_core.o
cc -O2 -w -ISources/SloopCore/include Tests/c/core_smoke.c .build-c/sloop_core.o -lm -o .build-c/core_smoke
./.build-c/core_smoke .build-c/smoke
