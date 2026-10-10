# CLAUDE.md

<!-- markdownlint-disable-next-line MD013 -->
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

zurds reads R's serialization format (`.rds`, `.rda`/`.RData`,
`serialize()` raw vectors, lazy-load `.rdb`/`.rdx` databases) without
handing it to R's deserializer. It reports headers, lists the objects in an
`.rda`, summarises structure, extracts one object, scans for anything that
can run code or reach outside the file, and reads data-only files with a C
reader that never calls `unserialize()`. It has no hard runtime
dependencies; `zufast` is `LinkingTo` only. It never re-implements R's
deserializer for closures, environments or bytecode, and reads no other
format.

[`.agents/design.md`](.agents/design.md) is the specification and outranks
this file. [`.agents/roadmap.md`](.agents/roadmap.md) stages the work to
0.1.0, tracked in [v0.1.0 #1](https://github.com/pedrobtz/zurds/issues/1).

## Current state

Stages 0 and 1, 2026-10-10: `zurds_info()`, the condition classes, symbol
hiding and its gate; the source (`R/source.R`, `src/zrd_source.c`,
`src/zrd_reader.c`) that opens a path, raw vector or connection, detects
the kind and compression and reads decompressed bytes in 64 KiB blocks; and
the fixture generator. No walk, scanner or builder yet. See the roadmap's
stage table for what is done.

## Stage tracking

- One parent issue per target version: `v0.1.0` (#1).
- One sub-issue per roadmap stage, titled as the roadmap titles it, with the
  `stage` label.
- Close a stage by merging its pull request, with `Closes #<n>` in the
  body. Never close a stage whose exit criteria are not met; record a
  deviation in the roadmap first.
- The roadmap stays authoritative. The issues record which stages are done.

## Versioning

Develops at `0.0.0.9000` and targets `0.1.0` for its first CRAN release
(Stage 12). Bump to `0.1.0.9000` only after CRAN accepts it.

## Commands

Run from the package root.

```sh
Rscript -e 'devtools::load_all()'                # compile src/ and load
Rscript -e 'devtools::document()'                # roxygen -> NAMESPACE, man/
Rscript -e 'devtools::test()'                    # full testthat suite
Rscript -e 'devtools::test(shuffle = TRUE)'      # order independence
Rscript -e 'devtools::check()'                   # R CMD check
air format .                                     # format R sources
tools/check-symbols <path/to/zurds.so>           # on an R CMD INSTALL build
tools/check-symbols --canary                     # the gate must reject this
Rscript tools/make-fixtures.R                    # rewrite tests/testthat/fixtures
Rscript tools/make-fixtures.R --full DIR         # the full matrix, into DIR
ZURDS_FIXTURES=DIR Rscript -e 'devtools::test()' # the suite against DIR
```

## Architecture

See design §4. Every user-facing function starts with `zrd_open()` in
`R/source.R`: it detects compression from the file's magic bytes, opens
the file with `gzfile()` (which decompresses everything R writes), wraps
the connection or raw vector in a `zrd_source` external pointer, and
detects the kind from the first decompressed bytes. `src/zrd_reader.c` is
the R-free buffered reader the walk will consume; `src/zrd_source.c` is
the R side that fills it with `R_ReadConnection()`. `src/init.c` registers `.Call` routines and is the only
exported symbol. Internal C symbols use the `zrd_` / `ZRD_` prefix. The walk
(`src/zrd_walk.c`, from Stage 2) is R-free and must compile with
`-DZRD_STANDALONE`; the data builder (`src/zrd_build.c`) is the only place
R objects are made from stream content. R-side conditions are built in
`R/conditions.R` by `zrd_abort()`; the class chain lives in
`zrd_condition_classes`. Default limits live in `zrd_default_limits()`.

## Invariants that are easy to break

- `src/Makevars` lists every object by hand (portable make) and sets
  `$(C_VISIBILITY)`. A new `.c` file not listed is silently not built; a
  non-static function without hidden visibility fails `tools/check-symbols`.
- The walk never allocates an R object or calls R. Anything R-specific goes
  behind `#ifndef ZRD_STANDALONE`.
- Limits are checked before a length from the file is trusted, never after.
- An unknown pseudo-type is a parse error, never a skip.
- R's codecs do not fail loudly on truncated input: gzip reads short,
  bzip2 and zstd read nothing, xz warns (through a connection) or returns
  a prefix (through `memDecompress()`). `R/source.R` checks the gzip
  trailer's ISIZE, the xz footer, empty output, and turns codec warnings
  into `zurds_decompression_error`. Read source bytes only through
  `zrd_source_call()`, or a codec warning escapes as a plain warning.
- Gates need canaries: a new gate is trusted only once it has failed on a
  planted defect.

## Testing conventions

- Self-sufficient tests: each builds its inputs inside `test_that()`.
- Global state through `withr::local_*()`; write only under `tempdir()`.
- Assert on condition classes, never message text.
- `devtools::test(shuffle = TRUE)` is part of done.
- The oracle is R itself: fixtures are written by `tools/make-fixtures.R`,
  and data-only reads must be `identical()` to `readRDS()`. Tests iterate
  over `fixture_manifest()` (`helper-fixtures.R`), so they run on the small
  checked-in set by default and on the full matrix when `ZURDS_FIXTURES`
  is set. Never edit `tests/testthat/fixtures/` by hand; regenerate it.

## Definition of done

`devtools::document()` and `devtools::check()` clean (0 errors, warnings,
notes). `devtools::test(shuffle = TRUE)` green. `gctorture(TRUE)` clean when
C changed. CI green on every leg. A user-facing change needs a test, roxygen
documentation and a `NEWS.md` entry.

## Releasing to CRAN

CI through `pedrobtz/r-actions` is the pre-submission check; no
win-builder, macOS builder or R-hub. Fix NOTEs rather than explaining them.
`Remotes: pedrobtz/zufast` stays until `zufast` is on CRAN (Stage 12).

## Editing rules

- roxygen is the source; never edit `man/` or `NAMESPACE` by hand.
- Keep prose short. en-GB spelling. Wrap roxygen at 80 characters.
- `lower_snake_case`; R prefix `rds_` for exports, `zrd_` internal.
- A stage that settles a design decision or open question edits
  `.agents/design.md` in the same pull request.

## Continuous integration

Calls `pedrobtz/r-actions` at `@v1`: `r-cmd-check.yml` (R-CMD-check),
`coverage.yml`, and in `native-checks.yaml` `sanitizers.yml`,
`valgrind.yml`, `lto.yml`, `gctorture.yml`, `rchk.yml` (with
`github-packages: pedrobtz/zufast`), `analyzers.yml`, `cran-special.yml`.
`gates.yaml` runs `tools/check-symbols` and its canary. `fixtures.yaml`
regenerates the fixtures on oldrel, release and devel, checks the
checked-in manifest, and runs the suite on the full matrix.

## Commits and pull requests

Short, imperative, sentence-case subjects. One pull request per roadmap
stage, titled as the stage. Never commit to `main` directly.
