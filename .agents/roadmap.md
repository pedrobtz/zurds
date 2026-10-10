# zurds: roadmap

Status: draft 2026-10-10. Implements [design.md](design.md).
Target: v0.1.0, the first CRAN release.
Tracking: v0.1.0 #1, CRAN-submission #15.

Section references (§) point to design.md. The stages follow design §18,
with two changes: the walk (§18 Stage 1, size L) is split into its data
and its code-carrying halves, and the data builder now comes before
extraction, because `rds_extract()` builds through it (§4).

Rules that hold for every stage:

- A stage is done when its exit criteria pass in CI on every r-actions
  leg, not when the code is written.
- The walk is R-free from its first commit: `src/zrd_walk.c` compiles
  with `-DZRD_STANDALONE`, so §19 criterion 4 is structural.
- Gates need canaries. A gate (symbol check, limits, fuzz, drift) is
  trusted once it has been seen to fail on purpose.
- A stage that settles a decision (§16) or an open question (§17) edits
  design.md in the same pull request.
- A stage's state is not written in its heading, so issue links keep
  their anchors.

## Stages

| Stage | Title | Size | Issue | Done | Pull request |
| --- | --- | --- | --- | --- | --- |
| 0 | Package identity | S | #2 | 2026-10-10 | [#17](https://github.com/pedrobtz/zurds/pull/17) |
| 1 | Source and fixtures | M | #3 | 2026-10-10 | [#18](https://github.com/pedrobtz/zurds/pull/18) |
| 2 | The walk: data items in XDR | L | #4 | 2026-10-10 | [#19](https://github.com/pedrobtz/zurds/pull/19) |
| 3 | The walk: code, environments and ALTREP | M | #5 | 2026-10-10 | [#20](https://github.com/pedrobtz/zurds/pull/20) |
| 4 | Binary and ASCII formats, rds_info() and rds_str() | M | #6 | 2026-10-10 | [#21](https://github.com/pedrobtz/zurds/pull/21) |
| 5 | Table of contents for .rda files | M | #7 | 2026-10-10 | [#22](https://github.com/pedrobtz/zurds/pull/22) |
| 6 | The scanner and rds_deps() | M | #8 | 2026-10-10 | [#23](https://github.com/pedrobtz/zurds/pull/23) |
| 7 | The data-only builder and rds_read_data() | M | #9 | 2026-10-10 | [#24](https://github.com/pedrobtz/zurds/pull/24) |
| 8 | Extraction | M | #10 | 2026-10-10 | [#25](https://github.com/pedrobtz/zurds/pull/25) |
| 9 | Lazy-load databases | M | #11 | 2026-10-10 | [#26](https://github.com/pedrobtz/zurds/pull/26) |
| 10 | Hardening: fuzzing, hostile inputs, drift, benchmarks | M | #12 | 2026-10-10 | [#27](https://github.com/pedrobtz/zurds/pull/27) |
| 11 | Documentation | S | #13 | | |
| 12 | v0.1.0 readiness | S | #14 | | |

## Stage 0 — Package identity

**Goal.** The skeleton becomes a member of the `zu*` family (§3): a real
`DESCRIPTION`, a shared object that exports one symbol, `zufast` linked
and exercised, and the questions §3 leaves to this stage answered.

**Work.**
- `DESCRIPTION`: real `Title` and `Description`; `Authors@R` (Pedro
  Baltazar, `aut`, `cre`, `cph`); `URL` and `BugReports`;
  `Depends: R (>= 4.1)`; `Language: en-GB`;
  `LinkingTo: zufast (>= 0.1.0)`; `Suggests: testthat (>= 3.0.0), withr`;
  `Version: 0.0.0.9000`.
- Providers: `zufast` is not on CRAN (checked 2026-10-10). Develop with
  `Remotes: pedrobtz/zufast`; Stage 12 removes it.
- `LICENSE` / `LICENSE.md` name the real copyright holder. No vendored
  code (§3).
- `.Rbuildignore`: `^\.agents$` (done), `^tools$`, and object-file
  patterns.
- `NEWS.md`: `# zurds (development version)`, without the template's
  "Initial CRAN submission" line until Stage 12.
  *Deviation 2026-10-10:* the heading is `# zurds 0.0.0.9000`. R 4.6's
  `NEWS.md` parser reads no version from "(development version)" and
  `R CMD check` notes "No news entries found".
- Replace `src/zurds.c` with `src/init.c`: `R_init_zurds` registering
  routines, `R_useDynamicSymbols(FALSE)`, `R_forceSymbols(TRUE)`;
  `src/Makevars` with `PKG_CFLAGS = $(C_VISIBILITY)`.
- Include `zufast.h` and call one function from it (a `zuf_load_be32()`
  round trip), so the `LinkingTo` is exercised rather than declared.
- `zurds_info()` (D8): package version, the `zufast` header version
  built against, the default limits (§11), and which compressions this
  R can read (zstd where built).
- `tools/check-symbols`: fails on any exported symbol other than
  `R_init_zurds` and on `stdout`, `stderr`, `printf`, `abort`, `exit`
  and the `assert` symbols. Planted canary seen to fail.
- The `zurds_error` condition constructor and its subclasses (§10), used
  by everything after.
- Check the `zrd_` / `ZRD_` prefix against every sibling's `src/` and
  `inst/include/`; re-check CRAN for a package that inspects or scans
  RDS files without loading them (§3). Record both, with the command and
  the date, in design.md.
- Package `CLAUDE.md` from `templates/package-CLAUDE.md` in
  `pedrobtz/packages`, linking this roadmap and the v0.1.0 issue.
- Replace the template test with `test-init.R` and `test-info.R` in the
  same commit, so `tests/testthat/` is never empty.
- design.md header: from *RFC, Proposed* to *Adopted* with the date.

**Exit criteria.**
- `R CMD check --as-cran` passes on all r-actions legs.
- `tools/check-symbols` passes, and its canary has been seen to fail.
- `zurds_info()` reports the `zufast` version and the compressions.
- The prefix check and the CRAN search are recorded in design.md.
- `CLAUDE.md` exists; `DESCRIPTION` contains no template text.

**Design.** §3, §10, §13, D7, D8.

## Stage 1 — Source and fixtures

**Goal.** zurds can read the decompressed bytes of any file R writes,
in blocks, through R's own connections, and a fixture matrix written by
R itself exists for every stage after this one to test against.

**Work.**
- `src/zrd_source.c`: a source over a connection opened with `gzfile()`
  (gzip, bzip2, xz, zstd where built), read in 64 KiB blocks with
  `R_ReadConnection()`; a source over a raw vector; one interface for
  both.
- **Spike first:** confirm `R_ext/Connections.h` passes
  `R CMD check --as-cran` on every leg, including both r-devel Debian
  containers. If not, switch to the fallback (§13: `readBin()` in R) and
  record the switch in design.md.
- Wrapper detection: `.rda` magic lines (`RDX3`, `RDX2`, `RDA3`, `RDB3`),
  stream headers (`X\n`, `B\n`, `A\n`), the `.rdx`/`.rdb` pair, and the
  `kind =` override (§5).
- `tools/make-fixtures.R`: every type, every format × version (2, 3) ×
  compression, multi-object `.rda` files, a lazy-load database via
  `tools:::makeLazyLoadDB()`, and a manifest with writer version and
  SHA-256. Fixtures small enough for the CRAN tarball live in
  `tests/testthat/fixtures/`; the full matrix is generated in CI on
  oldrel, release and devel.
- Classed errors: `zurds_io_error`, `zurds_decompression_error`,
  `zurds_format_error`.

**Exit criteria.**
- Every fixture is opened and its kind and compression detected,
  through both a path and a raw vector.
- A truncated gzip file fails with `zurds_decompression_error`; a file
  with unknown magic with `zurds_format_error`.
- `R CMD check --as-cran` passes on all legs with the connection API
  (or the fallback is in place and design.md says so).
- The fixture manifest is checked in and regenerated in CI.

**Design.** §1, §4 (source), §13, §14 (fixtures), D3.

## Stage 2 — The walk: data items in XDR

**Goal.** An iterative, R-free walk reads XDR streams of data — vectors,
lists, pairlists, attributes, symbols and references — and emits events,
within limits checked before any length is trusted.

**Work.**
- `src/zrd_walk.c`: the header (§6), flag unpacking (type, object,
  attribute, tag, levels), lengths with the long-vector escape,
  `NILVALUE_SXP` and the other value pseudo-types, `CHARSXP` with
  `NA_character_`, atomic vectors, `VECSXP`/`EXPRSXP`, pairlists with
  attributes and tags, `SYMSXP`, and the `REFSXP` table.
- XDR integers and doubles through `zufast`'s big-endian loads (§3).
- An explicit stack bounded by `max_depth`; `max_items` and `max_size`
  checked before allocation or skip; interrupts every 65,536 items.
- Walk state owned by a finalised external pointer (§12).
- Events: begin item (type, flags, length, offset, depth), end item.
- Compiles with `-DZRD_STANDALONE` in a CI step that links no R.
- Each rule cross-referenced to the `serialize.c` function it mirrors.
- Errors: `zurds_parse_error` (offset, path) and the `zurds_limit_error`
  subclasses.
- *Deviation 2026-10-10:* `tools/make-fixtures.R` builds the `data_frame`
  fixture's integer column with `c()`, not `1:3`: R serializes a compact
  sequence as ALTREP, which is Stage 3's. `compact_seq` stays the ALTREP
  fixture.

**Exit criteria.**
- Every data-only XDR fixture is walked to its end with the item count
  and byte count expected.
- A vector claiming 2^52 elements, a `REFSXP` beyond the table, and
  nesting past `max_depth` fail with the right classed errors before
  allocating; each limit has been seen to trip.
- The standalone build passes in CI.

**Design.** §4 (walk), §6, §11, §12, D2.

## Stage 3 — The walk: code, environments and ALTREP

**Goal.** The walk skips every remaining item kind correctly, so any
XDR stream R writes can be walked without building it.

**Work.**
- Closures, promises, language objects, `DOTSXP`, environments (with
  enclosure, frame, hash table, locked bit), the global, base, empty,
  namespace and package environment pseudo-types, persistent references,
  external pointers, weak references, `S4SXP`.
- Bytecode by its own grammar (`BCREPDEF`, `BCREPREF`, `ATTRLANGSXP`,
  `ATTRLISTSXP`, and the rest of `ReadBC()`).
- `ALTREP_SXP`: class name, package and base type, then the state.
- An unknown pseudo-type is a `zurds_parse_error`, never a skip.

**Exit criteria.**
- Every XDR fixture in the matrix, including byte-compiled closures,
  environments with cycles, and ALTREP compact sequences, is walked to
  its end.
- The standalone build still passes.

**Design.** §6, §11.

## Stage 4 — Binary and ASCII formats, rds_info() and rds_str()

**Goal.** All three stream formats walk through one token reader per
format, and the first two user-facing functions exist.

**Work.**
- Token readers for native binary (with byte-order handling) and ASCII
  (numbers through `zufast`, escaped strings, `NA`, `Inf`, `NaN`).
- `rds_info(x)`: kind, format, version, writer and minimum reader
  versions, native encoding (version 3), compression.
- `rds_str(x, max_level = 2)`: a `str()`-like summary built from events,
  no objects built.
- roxygen docs with examples on the bundled fixtures.

**Exit criteria.**
- Every fixture in all three formats walks to the same event sequence
  as its XDR twin.
- `rds_info()` agrees with the manifest for every fixture.
- A truncated ASCII stream fails with `zurds_parse_error`.
- `rds_str()` output is covered by snapshot tests.

**Design.** §2, §5, §6.

## Stage 5 — Table of contents for .rda files

**Goal.** `rds_toc()` lists the objects of an `.rda` without building
any of them.

**Work.**
- `rds_toc(x)` returning the §5 data frame (`name`, `type`, `class`,
  `length`, `dim`, `size`, `offset`; `risky` filled in Stage 6).
- Offsets in the uncompressed stream, cached by path, size and
  modification time for Stage 8 (§4).
- `RDX2` (format version 2) files, per open question 3.
- Errors: `zurds_not_found` defined for later; an `.rds` passed to
  `rds_toc()` gives `zurds_invalid_argument` with a hint to `rds_str()`.

**Exit criteria.**
- For every `.rda` fixture, `rds_toc()$name` equals `ls()` after
  `load()` into a new environment, and `type`, `class`, `length` and
  `dim` match the loaded objects.
- Memory stays bounded on a generated multi-object file (no object is
  allocated by the walk).

**Design.** §2, §4, §5.

## Stage 6 — The scanner and rds_deps()

**Goal.** zurds reports everything in a file that can run code or reach
outside it, and what a file needs in order to load.

**Work.**
- `rds_scan(x)` returning the §5 findings data frame (`path`, `finding`,
  `detail`, `severity`).
- `rds_is_safe(x)`; the `risky` column of `rds_toc()`.
- `rds_deps(x)`: namespaces, package environments, ALTREP classes and
  their packages, S4 packages.
- One fixture per finding kind; a CVE-2024-27322-style fixture with a
  top-level promise and one nested in a list.
- *Deviation 2026-10-10:* "data-only fixtures" for the scan are those with
  no finding by design §5's list: data, base R's ALTREP classes, symbols,
  language objects and primitives. The formula fixture is not one: it
  keeps the environment it was made in.

**Exit criteria.**
- Every finding kind is detected at its correct path with the right
  severity (§19 criterion 3, first half).
- The CVE-style fixture is reported as `"executes"` at both paths.
- Every data-only fixture scans clean; `rds_is_safe()` is `TRUE` for
  exactly those.

**Design.** §2, §5, §8.

## Stage 7 — The data-only builder and rds_read_data()

**Goal.** A reader written in C builds data-only files into R objects
identical to `readRDS()`, and refuses everything else.

**Work.**
- `src/zrd_build.c`: `NULL`, logical, integer, double, complex,
  character, raw and list vectors, with attributes (classes kept, no
  method called); containers `PROTECT`ed as filled (§12).
- Strings with `Rf_mkCharLenCE()` and their declared encoding; native
  strings converted when the version 3 header names an encoding that
  differs from the session's; invalid UTF-8 an error in strict mode
  (`zurds_encoding_error`), through `zufast`'s validator.
- ALTREP materialised through ordinary-vector state; refused otherwise.
- `rds_read_data(x, strict = TRUE)`: refuses `"executes"` and
  `"reaches_out"` findings with `zurds_unsafe_content` (carrying the
  findings); `strict = FALSE` replaces them with `NULL` and warns once
  per kind.
- List columns of mixed types are built as lists (open question 2).
- Lower `max_size` default for building (4 GiB, §11).

**Exit criteria.**
- Every data-only fixture round-trips `identical()` to `readRDS()` in
  every format, version and compression (§19 criterion 2).
- `rds_read_data()` refuses the CVE-style fixture (§19 criterion 3,
  second half) and never calls `unserialize()` (checked by mocking it
  to fail in the tests).
- Latin-1, UTF-8, bytes and native strings keep their encoding marks.

**Design.** §5, §8, §9, §12, D4, D6.

## Stage 8 — Extraction

**Goal.** One object, list element or data frame column comes out of a
file without the rest being built.

**Work.**
- `rds_extract(x, what)`: a name for `.rda`, or a path such as
  `c("fit", "coefficients")` or `list(3)`; skips to the item, then
  builds it with the data builder.
- Reuses Stage 5's cached offsets when the same file is opened again.
- `use_r = TRUE` (open question 1): after a clean scan of that item
  only, hands its bytes to `unserialize()`, with the findings attached
  to the result. Off by default.
- `zurds_not_found` for a name or path not in the file.

**Exit criteria.**
- For every `.rda` fixture and every top-level name, `rds_extract()`
  is `identical()` to the object after `load()`.
- For list and data frame `.rds` fixtures, extracting each element or
  column equals the corresponding element of `readRDS()`.
- An item with an `"executes"` finding is refused without `use_r`.

**Design.** §2, §4, §5, D4.

## Stage 9 — Lazy-load databases

**Goal.** zurds lists and extracts objects from installed packages and
from knitr or targets caches, without forcing the promises
`lazyLoadDBfetch()` would force.

**Work.**
- Read the `.rdx` index with zurds' own walker (`variables`,
  `references`, `compressed`).
- `.rdb` entries: zlib (`compressed = 1`) and the four-byte length plus
  type byte (`'0'`, `'1'`, `'2'`, `'Z'`), decompressed through R's own
  codecs (§3).
  *Deviation 2026-10-10, approved by the maintainer:* zlib entries go
  through zurds' own bounded decoder (`src/zrd_inflate.c`), since
  `memDecompress()` grows without bound on truncated zlib on R 4.6 and
  earlier without libdeflate; xz entries, raw LZMA2, are wrapped in an
  `.xz` container for `memDecompress()`. Design §7.
- `rds_lazyload_toc(path)` and `rds_lazyload_extract(path, name)`
  through the data builder; top-level promises reported, never forced
  (D5).

**Exit criteria.**
- For the lazy-load fixture, `rds_lazyload_toc()` lists every variable,
  and each data-only one extracts `identical()` to `lazyLoad()`'s value.
- An installed base package's database (`utils` or `stats`) is listed
  without error.
- A promise entry is reported and not evaluated.

**Design.** §2, §5, §7, D5.

## Stage 10 — Hardening: fuzzing, hostile inputs, drift, benchmarks

**Goal.** The walk has survived fuzzing and every hostile input is a
permanent regression test; changes in R's format are caught.

**Work.**
- Fuzz harness with `zufuzz` on the standalone walk, under ASan and
  UBSan, seeded from every fixture, in a scheduled CI job.
  *Deviation 2026-10-10:* `zufuzz` is a package skeleton with no fuzzing
  yet, so the harness (`tools/fuzz/harness.c`: the walk, the scanner and
  payload decoding) runs under r-actions' `fuzz.yml` (libFuzzer, ASan,
  UBSan): two minutes per pull request, thirty nightly, one cached corpus.
  Its canary is `tools/run-fuzz-canary` in `gates.yaml`.
- Hostile inputs (§14), each a test: 2^52-element vector, bad `REFSXP`,
  nesting 10^6 deep, a 10 GiB gzip bomb, an endless `.rda` pairlist, a
  truncated ASCII stream, an unknown pseudo-type.
- Drift check: CI diffs `serialize.c` at each R release against the
  commit recorded in design.md, and fails until reviewed.
- `tools/run-benchmarks` for the §15 targets; results recorded in
  design.md.
- Tests pass with `shuffle = TRUE`; the CRAN suite runs in under 15 s.

**Exit criteria.**
- The fuzz gate has been seen to fail on its canary, then runs clean
  for the agreed budget (§19 criterion 4).
- Every hostile input fails with a classed error and bounded memory.
- The drift check runs in CI and has been seen to fail on a modified
  `serialize.c` (§19 criterion 5).
- Benchmark results for all three §15 targets are recorded.

**Design.** §11, §14, §15.

## Stage 11 — Documentation

**Goal.** A new user can learn what zurds does, and what its safety
guarantee does and does not cover, from the package itself.

**Work.**
- README: purpose, installation, examples of `rds_toc()`, `rds_scan()`,
  `rds_read_data()`.
- Vignette on safely reading files from elsewhere, stating §8's
  guarantee and its limit; `Suggests: knitr, rmarkdown`.
- `_pkgdown.yml` reference index grouped by task; every export has an
  example that runs on a bundled fixture.

**Exit criteria.**
- The pkgdown workflow builds the site without warnings.
- Every exported function has `@return` and a runnable example.
- The vignette builds on all r-actions legs.

**Design.** §1, §5, §8.

## Stage 12 — v0.1.0 readiness

**Goal.** Everything except the submission itself is done.

**Work.**
- `zufast` on CRAN; remove `Remotes:` from `DESCRIPTION`.
- `Version: 0.1.0`; `NEWS.md` heading `# zurds 0.1.0` with
  "Initial CRAN submission."
- `cran-comments.md` listing the r-actions CI legs as the test
  environments.
- Run the `review-pkg-cran` skill and resolve every finding.
- `urlchecker::url_check()` and `spelling::spell_check_package()` clean
  (en-GB wordlist).
- Check every §19 acceptance criterion and tick it off here.

**Exit criteria.**
- `DESCRIPTION` has no `Remotes:`, and `zufast` resolves from CRAN.
- `review-pkg-cran` findings all resolved.
- URL and spelling checks clean.
- A **full-profile** r-actions run is green on the release commit.

**Design.** §13, §19.

## After 0.1.0

- Structural diff of two files and a content hash independent of
  compression and header: small once the event stream is stable (§2,
  open question 4).
- Writing: re-compressing, ASCII to XDR, stripping risky objects into a
  data-only copy. Needs an encoder (§2, §20).
- Partial reads of very large atomic vectors (a row range of a column).
- Proposing the scanner to R Core as `readRDS(safe = TRUE)`: decide
  after 0.1.0 has users (§20).

## Open questions

- **`use_r = TRUE` in `rds_extract()`** (§17 Q1): settled 2026-10-10 in
  Stage 8, as recommended.
- **Mixed-type list columns in strict mode** (§17 Q2): settled 2026-10-10
  in Stage 7: built as lists.
- **`RDX2` / format version 2 in 0.1.0** (§17 Q3): settled 2026-10-10 in
  Stage 5: yes.
- **`zufast` on CRAN.** It is at 0.1.0 on GitHub, not on CRAN
  (2026-10-10). Nothing before Stage 12 waits on it; Stage 12 does.
- **`R_ReadConnection()` on CRAN** (§13): settled 2026-10-10 by Stage 1's
  spike: it passes every leg; no fallback.
