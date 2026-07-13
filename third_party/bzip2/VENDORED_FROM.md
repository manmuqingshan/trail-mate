# bzip2 / libbzip2

- Upstream: https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
- Version: 1.0.8, 2019-07-13
- Copyright: Copyright (C) 1996-2019 Julian R Seward
- License: See `LICENSE`.

This vendored copy keeps the upstream source files unchanged. The additional
`src/bz_internal_error.c` file supplies the host assertion hook required when
building libbzip2 with `BZ_NO_STDIO=1`. Trail Mate firmware uses only the
low-level decompression stream API for Reticulum Resource payloads.
