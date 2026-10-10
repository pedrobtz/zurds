# RFC 0015: `zurds`, inspecting and safely reading R serialization files

- **Status:** Adopted 2026-10-10 (roadmap Stage 0). Proposed as RFC 0015
  on 2026-10-09.
- **Date:** 2026-10-09.
- **Packages:** `zurds` (not yet created; name chosen 2026-10-09 over
  `zurd`, which reads as R's `.Rd` documentation format, and `zurda`,
  which reads as `.rda` only; availability unchecked); consumes `zufast`
  through `LinkingTo`.
- **Source:** R's sources at the GitHub mirror `wch/r-source`, commit
  `a97c000` (2026-10-08): `src/main/serialize.c` (the stream format, the
  pseudo-types 238–255, the flag packing, `checkNotPromise()`, and
  `do_lazyLoadDBfetch()`), `src/main/saveload.c` (the `RDX2`/`RDX3`/
  `RDA3`/`RDB3` magic numbers), `src/main/connections.c`
  (`R_decompress1/2/3` and the `zstdfile` connection), `doc/NEWS.Rd`;
  Duncan Temple Lang's `duncantl/RDAInfo` (last commit 2024-08-08), a
  prototype of the table of contents for `.rda` files; CVE-2024-27322,
  arbitrary code execution through a promise in an RDS stream, fixed in
  R 4.4.0. The reading was of source, not of a running build; section 13
  says what the first stage measures.

## 1. What zurds is

`zurds` reads R's serialization format without handing it to R's
deserializer: to see what a file holds, to pull out one object, to check
whether a file is safe to load, and to load data-only files with a reader
that cannot run code.

One format serves four file kinds:

| Kind | Wrapper around the stream |
| --- | --- |
| `.rds` (`saveRDS()`) | the stream itself, usually gzip-compressed |
| `.rda`, `.RData` (`save()`) | a magic line (`RDX3\n` …), then a pairlist |
| `serialize()` raw vectors | the stream, uncompressed |
| `.rdb` + `.rdx` lazy-load DB | an index; objects compressed singly |

Base R's `readRDS()` and `load()` are all-or-nothing: to learn what a
2 GB `.rda` contains, or whether a model file from a colleague carries
closures and environments, you load all of it.

The one-line statement that governs every decision below:

> **Walk the stream without building it; build only what was asked for,
> and only types that cannot execute.**

## 2. Scope

**In v0.1.0:**

- Header information for every kind: format (XDR, native binary, ASCII),
  version (2 or 3), the R version that wrote it, the minimum reader
  version, native encoding (version 3), compression.
- A table of contents for `.rda` files: name, type, class, length or
  dimensions, uncompressed size, byte offset, depth.
- A structure summary of any stream (`str()`-like) without building it.
- Extracting one top-level object from an `.rda`, or one element of a
  list or one column of a data frame from an `.rds`, skipping the rest.
- A scanner reporting everything that can run code or reach outside
  the file: promises, closures, bytecode, environments other than the
  global and base ones, external pointers, weak references, namespace
  and package references, ALTREP classes, S4 classes.
- `rds_read_data()`: a reader written in C that builds only atomic
  vectors, lists, `NULL`, and attributes (classes included), and refuses
  everything else. It never calls R's `unserialize()`.
- A dependency report: the namespaces, ALTREP classes and S4 packages a
  file would need to load.
- Lazy-load databases: list and extract objects of an installed package
  or a knitr or targets cache.
- Every compression R writes: gzip, bzip2, xz, and zstd where R was built
  with it.

**Later, when asked:**

- Structural diff of two files, and a content hash independent of
  compression and header.
- Writing: re-compressing, converting ASCII to XDR, stripping risky
  objects into a data-only copy.
- Partial reads of very large atomic vectors (a row range of a column).

**Never:**

- Re-implementing R's full deserializer for closures, environments and
  bytecode. Those are exactly the parts that should be loaded by R, after
  a decision, or not at all.
- Other formats (`qs2`, `fst`, Parquet). Different packages.

## 3. Position in the `zu*` family

| | zurds |
| --- | --- |
| Role | format |
| R prefix | `rds_` |
| Info function | `zurds_info()` |
| Root condition class | `zurds_error` |
| Public C prefix | none; internal `zrd_` / `ZRD_` |
| From C | no C API; `LinkingTo` consumer only |
| Hides symbols | yes, from Stage 0 (R3) |
| Vendored code | none |
| `Depends: R` | 4.1 (R2) |
| Language | en-GB (R2) |

The `zrd_` prefix is unused in this repository's documents on
2026-10-09. **Checked 2026-10-10 (Stage 0):** no sibling uses `zrd_` or
`ZRD_` in its C sources or installed headers:

```sh
grep -rlE '\b(zrd_|ZRD_)' --include='*.[ch]' --include='*.cpp' \
  --include='*.hpp' ../*/src ../*/inst/include | grep -v '^../zurds/'
gh search code "zrd_" --owner pedrobtz
```

The first found nothing across the 26 local `zu*` siblings; the second
found only RFC 0015 itself in `pedrobtz/packages`.

**Relationships, as decided rather than as hoped:**

- **`zufast`** owns big-endian loads (`zuf_load_be*`) for XDR, number
  parsing for the ASCII format, UTF-8 validation of `CHARSXP` payloads,
  and xxHash for the later content hash.
- **Compression** comes from R's own connections, not from `zukomp`:
  `gzfile()` already detects gzip, bzip2, xz and (where built) zstd, and
  zurds reads the decompressed bytes through `R_ReadConnection()` in
  fixed-size blocks. This keeps zurds free of codec libraries and makes
  it read exactly what the running R can read. Lazy-load entries are
  decompressed by the same codecs through `memDecompress()`'s C path.
- **`zucbor`** is the template for the walk's limits, conditions and
  hostile-input discipline.

**Consumers.** Anyone receiving `.rds` or `.rda` files from elsewhere
(model files, cached results, data shared by email or in a repository);
reviewers of package data (`R/sysdata.rda`, `data/*.rda`); pipelines that
must not execute code from data; and people with very large `.RData`
files who need one object from them.

**What exists elsewhere.**

- Base R: `readRDS()`, `load()`, `unserialize()`, `lazyLoad()`. Since R
  4.4.0, `unserialize()` refuses a promise at the top level
  (`checkNotPromise()` in `serialize.c`), which closed CVE-2024-27322's
  path, but nested promises, closures and environments still load and
  can run code when used, and `do_lazyLoadDBfetch()` deliberately
  evaluates a top-level promise read from a lazy-load database.
- `RDAInfo` (Duncan Temple Lang, GitHub only): a table of contents and
  selective restore for `.rda` version 3 XDR files. The prior art for
  §2's first two items.
- `librdata` (WizardMac, C) and Python's `rdata` and `pyreadr`: readers of
  data frames and vectors in RDS/RData for other languages; no scanner,
  no lazy-load support.
- Nothing on CRAN inspects or scans these files without loading them.
  **Re-checked 2026-10-10 (Stage 0):** of 25,396 CRAN packages,
  `tools::CRAN_package_db()` titles and descriptions matching `RDS`,
  `.rds`, `RData`, `.rda` or `serializ` gave 91 hits. They are alternative
  serializers (`qs2`, `fst`, `RApiSerialize`, `cereal`), data packages,
  and `RDS` (respondent-driven sampling); none inspects or scans R
  serialization files without loading them.

## 4. Architecture

```text
path / raw vector / connection
      |
      v
  source  (src/zrd_source.c)
   - a connection opened with gzfile() (detects compression), or a raw
     vector; read in 64 KiB blocks through R_ReadConnection()
   - wrapper detection: .rda magic line, .rds stream header
     ("X\n", "B\n", "A\n"), lazy-load .rdx/.rdb pair
      |
      v
  walk  (src/zrd_walk.c, R-free)
   - one iterative walk with an explicit stack over the item grammar:
     flags (type in bits 0-7, object bit 8, attribute bit 9, tag bit
     10, levels from bit 12), lengths (with the long-vector escape),
     the reference table for REFSXP, every pseudo-type 238-255
   - XDR, native binary and ASCII through one token reader per format
   - limits checked before any length is trusted (§11)
   - emits events: begin item (type, flags, length, offset), end item
      |
      +--> toc / str / deps / scan  (consumers of events, R-free)
      |
      +--> data builder  (src/zrd_build.c, R)
            - builds atomic vectors, lists, attributes, CHARSXP with
              their encodings; refuses any other type
      |
      +--> extract  (skip to the selected item, then build it, either
            with the data builder or, if the caller allows, by handing
            the item's bytes to R's unserialize())
```

The walk never allocates an R object and never calls R, so the scanner
works on files that R itself would refuse, and a hostile file cannot
reach R's allocator through zurds.

Extraction by name or path still reads the stream up to the item: the
format has no index (except in lazy-load databases), so skipping costs
decompression and walking, not building. For `.rda`, `rds_toc()` records
offsets in the uncompressed stream, and `rds_extract()` uses them to
skip without re-walking when the same file is opened twice in a session
(cached by path, size and modification time).

## 5. Public R API (complete v0.1.0 surface)

```r
rds_info(x)                 # header: format, version, writer, compression
rds_toc(x)                  # .rda: one row per object
rds_str(x, max_level = 2)   # structure summary, no objects built
rds_extract(x, what)        # what: a name (.rda) or a path c("fit", "coefficients")
rds_scan(x)                 # findings, one row per risky item
rds_deps(x)                 # namespaces, ALTREP classes, S4 packages
rds_read_data(x, strict = TRUE)   # data-only reader
rds_is_safe(x)              # TRUE when rds_scan() finds nothing

# lazy-load databases
rds_lazyload_toc(path)      # path without extension, as lazyLoad() takes
rds_lazyload_extract(path, name)

zurds_info()
```

`x` is a path, a raw vector (a `serialize()` result) or a connection.
Every function detects the kind (§1) unless `kind =` is given. The `.rda`
magic lines are `RDX3\n`, `RDX2\n`, `RDA3\n` (ASCII) and `RDB3\n`
(native binary).

### Results

`rds_toc()` returns a data frame: `name`, `type` (R's `typeof()` name),
`class` (first class, or `NA`), `length`, `dim` (character, `"100 x 5"`),
`size` (bytes in the uncompressed stream), `offset`, `risky` (logical,
from the scanner's rules).

`rds_scan()` returns a data frame: `path` (where in the object, as
`fit$terms` or `[[3]]`), `finding` (`"closure"`, `"promise"`,
`"environment"`, `"bytecode"`, `"external_pointer"`, `"weak_reference"`,
`"namespace"`, `"package_environment"`, `"altrep"`, `"s4"`,
`"persistent_reference"`), `detail` (the namespace name, the ALTREP class
and package, the S4 class), and `severity` (`"executes"` for promises,
closures and bytecode; `"reaches_out"` for environments, namespaces and
external pointers; `"needs_package"` for ALTREP and S4).

**Settled in Stage 6.** Base R's own ALTREP classes (compact sequences,
deferred strings, wrappers; package `"base"`) are not findings: they need
no package, and the data builder materialises them; any other ALTREP class
is. A reference (`REFSXP`) to an environment, external pointer, weak,
namespace, package or persistent reference read earlier is a finding of
that kind at the reference's own path, so every path that reaches it is
listed. Language objects, symbols and primitives are not findings: they
run nothing as a file loads. An S4 object is found by `OBJSXP` or by the
S4 bit on a vector.

`rds_read_data()` with `strict = TRUE` refuses any finding of severity
`"executes"` or `"reaches_out"` with `zurds_unsafe_content`; with
`strict = FALSE` it replaces them by `NULL` and warns once per kind.
ALTREP items are materialised through their serialized state when the
state is an ordinary vector (compact sequences, wrappers); otherwise they
are refused.

## 6. The stream, as zurds reads it

- Header: two bytes for the format, then integers for the version, the
  writing R version and the minimum reading version; version 3 adds the
  native encoding as a length and characters.
- Items: an integer of flags, then by type: lengths and payloads for
  vectors (`-1` then two integers for long vectors), `-1` for
  `NA_character_`, pairlist cells as attributes, tag and car with the
  cdr as the next item, environments as enclosure, frame, hash table and
  attributes, closures as environment, formals and body.
- References: `REFSXP` carries an index into the table of previously read
  environments and symbols (packed into the flags when small).
- Bytecode: walked by its own grammar (`BCREPDEF`, `BCREPREF`,
  `ATTRLANGSXP`, `ATTRLISTSXP`) so the walk can skip it; never built.
- ALTREP (`ALTREP_SXP`): class information (class name, package, base
  type) then the serialized state; the scanner reports the class and
  package.

Every rule is cross-referenced to the function in `serialize.c` it
mirrors, so a change in R is found by diffing that file at each R
release (§14).

## 7. Lazy-load databases

`.rdx` is itself a compressed serialized list: `variables` (name to
offset and length in `.rdb`), `references` (environments), and
`compressed` (the codec). Each `.rdb` entry is either zlib-compressed
(`compressed = 1`) or prefixed by a four-byte length and a type byte
(`'0'` none, `'1'` zlib, `'2'` bzip2, `'Z'` xz), as `R_decompress2()`
and `R_decompress3()` read them. zurds reads the index with its own
walker, lists the variables, and extracts one through the data builder.
It does not evaluate the promise that `lazyLoadDBfetch()` would evaluate:
a top-level promise in an `.rdb` entry is reported, not forced.

## 8. Safety model

What `rds_read_data()` guarantees, and how it is checked:

1. It never calls `unserialize()`, `eval()` or any function from the file.
2. It builds only `NULL`, logical, integer, double, complex, character,
   raw and list vectors, with attributes whose values are themselves of
   those types. A `class` attribute is kept as a character vector; no
   method of that class is called while building.
3. It refuses (or, non-strict, drops) every other type.
4. All lengths and depths are checked against limits before allocation.

What it does **not** guarantee: that printing or using the result is
safe if the user's own code later dispatches on a class from the file
to a method they have loaded. That is the user's code, not the file's.

`rds_is_safe()` and `rds_scan()` are advisory for `readRDS()`: a clean
scan means the file contains no type that can execute when loaded, but
R's `readRDS()` will still create objects of the file's classes.

## 9. Encodings and strings

`CHARSXP` levels carry the encoding (UTF-8, Latin-1, bytes, native
ASCII). The data builder creates strings with `Rf_mkCharLenCE()` and the
declared encoding; strings marked native are converted from the
writer's native encoding when the header (version 3) names it and it
differs from the session's, and are flagged otherwise. Invalid UTF-8 in a
string marked UTF-8 is an error in strict mode.

## 10. Errors

Every condition inherits `zurds_error`: `zurds_invalid_argument`,
`zurds_format_error` (bad magic, unknown format character, unsupported
version), `zurds_parse_error` (with `offset` and the item path; see
below),
`zurds_unsafe_content` (with the findings), `zurds_not_found` (a name or
path not in the file), `zurds_encoding_error`, `zurds_limit_error` with
`zurds_depth_limit`, `zurds_size_limit` and `zurds_item_limit`,
`zurds_io_error`, `zurds_decompression_error`.

**The item path** (settled in Stage 2): `[[i]]` for vector element `i`,
`$name` for a pairlist value whose tag is `name` (an `.rda` object, an
attribute), `@attr` for an attribute pairlist, and `@tag`, `@cdr`, `@name`
for a cell's tag, a pairlist's end and a symbol's name. List elements are
named by position, never by `names`: a list's `names` attribute follows its
elements in the stream, so the walk has not read it yet. The `offset` of a
parse or limit error is where the item at fault starts in the decompressed
stream; for a truncated stream it is where the stream ends.

## 11. Limits and hostile input

- `max_depth` (default 512): nesting of items; the walk is iterative, so
  this bounds the stack array, not the C stack.
- `max_items` (1e8) and `max_size` (the decompressed stream size, default
  16 GiB for inspection, 4 GiB for building): a decompression bomb stops
  at the limit, not at memory exhaustion.
- Length headers are checked against `max_size` before use; a vector
  claiming 2^52 elements fails before allocation.
- `REFSXP` indices outside the table are parse errors; the table's growth
  is bounded by `max_items`.
- Interrupts are checked every 65,536 items and between input blocks.

## 12. Memory model

The walk keeps its stack and the reference table in C memory owned by a
finalised external pointer, so an interrupt during a long scan frees
them. The data builder `PROTECT`s each container as it is filled and
releases it on completion, in the pattern `zucbor` uses. Input blocks are
reused; the decompressed stream is never held whole unless the source is
a raw vector.

## 13. Build, portability and CRAN

- C99 under the family's lint flags; no vendored code; `LinkingTo:
  zufast`; `Suggests: testthat, withr, knitr, rmarkdown`.
- `src/Makevars`: hidden visibility; `R_init_zurds` the only export.
- Uses `R_ext/Connections.h` (`R_ReadConnection()`, `R_GetConnection()`).
  It is a documented API with a version check (`R_CONNECTIONS_VERSION`),
  used by several CRAN packages; Stage 0 confirms CRAN's checks accept
  it, and keeps a raw-vector fallback (read with `readBin()` in R) if not.
- Stage 0 measures nothing vendored; it settles the connection question
  and the prefix check.
- **Settled 2026-10-10 (Stage 1, pull request #18):** `R_ext/Connections.h`
  passes `R CMD check --as-cran` with no NOTE on every r-actions leg:
  ubuntu release and oldrel-1, macOS, Windows, and the r-devel containers
  `clang23`, `ubuntu-clang` and `ubuntu-gcc16` that stand in for CRAN's
  `r-devel-linux-x86_64-debian-clang` and `-debian-gcc`; and under ASan,
  UBSan, valgrind, rchk and LTO. The `readBin()` fallback is not needed.
  R 4.2.2 (Debian bookworm, on the weekly i386 leg) still lists
  `R_GetConnection` and `R_ReadConnection` as non-API and notes them;
  R 4.4 and later do not. CRAN checks only current releases, so this
  stays a NOTE on that leg, not a reason for the fallback.
- R's codecs fail quietly on truncated input (Stage 1): through `gzfile()`,
  gzip reads short, bzip2 and zstd read nothing, and xz warns; and
  `memDecompress()` with zlib doubles its buffer without bound on
  truncated gzip. zurds therefore never uses `memDecompress()` on input,
  checks the gzip trailer and the xz footer itself, and treats empty
  output and codec warnings as `zurds_decompression_error`.

## 14. Testing

- **Fixtures written by R itself.** `tools/make-fixtures.R`, run on a
  matrix of R versions (oldrel, release, devel), writes every type and
  every combination of format (XDR, binary, ASCII), version (2, 3) and
  compression, plus `.rda` files with many objects and a lazy-load
  database built by `tools:::makeLazyLoadDB()`. A manifest records the
  writer version and SHA-256.
- **The oracle is R**: for every data-only fixture, `rds_read_data()`
  equals `readRDS()` (`identical()`), and `rds_extract()` equals the
  element of the loaded object.
- **The scanner**: one fixture per finding kind, each found at the right
  path; a CVE-2024-27322-style file (a promise at the top level and one
  nested in a list) reported as `"executes"`, and refused by
  `rds_read_data()`.
- **R drift**: CI diffs `serialize.c` at each new R release against the
  commit recorded in the design and fails until the change is reviewed.
- **Fuzzing** with `zufuzz` on the R-free walk, under ASan and UBSan,
  seeded from every fixture; the canary must crash first.
- **Hostile inputs**, each a permanent regression: a long vector of
  2^52 elements with no payload; `REFSXP` index beyond the table; nesting
  10^6 deep; a gzip bomb of 10 GiB of zeros; an `.rda` whose pairlist
  never ends; a truncated ASCII stream; an unknown pseudo-type.
- Tests self-sufficient, under `shuffle = TRUE`, CRAN suite under 15 s.

## 15. Performance targets

Measured by `tools/run-benchmarks`, recorded in the design:

- `rds_toc()` on a 2 GB `.rda` of ten data frames: bounded by
  decompression time, with memory under 64 MB.
- `rds_extract()` of one 100 MB object from that file: no more than
  `load()`'s time for the whole file, with peak memory near the object's
  size rather than the file's.
- `rds_read_data()` on data frames: within 1.5× of `readRDS()`.

## 16. Decisions

| # | Question | Decision |
| --- | --- | --- |
| D1 | Name | `zurds`, R prefix `rds_` |
| D2 | Decoder | project code mirroring `serialize.c`; no vendored library |
| D3 | Compression | R's connections, not a codec library |
| D4 | Building | data-only; `unserialize()` only on opt-in |
| D5 | Lazy-load promises | reported, never forced |
| D6 | Untrusted files | `rds_read_data(strict = TRUE)` |
| D7 | Dependencies | `LinkingTo: zufast` |
| D8 | Info function | `zurds_info()`, the package name (R1) |

Reasons where they are not in the section cited:

- D2: `librdata` covers data frames and vectors only and has no event
  walk to skip or scan; the full grammar is a few hundred lines in
  `serialize.c` and must be followed exactly anyway.
- D3: reading through the running R's own codecs means zurds never
  disagrees with `readRDS()` about which files are readable.

## 17. Open questions

1. **Opt-in R deserialization in `rds_extract()`.** For a closure or a
   model object, hand the item's bytes to `unserialize()` after a clean
   scan of that item only? Recommended: yes, behind `use_r = TRUE`, with
   the scan's findings attached to the result.
2. **Data frames with list columns** of mixed types: build as lists (as
   proposed) or refuse in strict mode? Recommended: build.
3. **`.RData` from very old R** (format version 2, `RDX2`): support fully
   in 0.1.0? Recommended: yes; R writes version 2 when asked. **Settled
   2026-10-10 (Stage 5): yes.** Every format of version 2 is walked, and
   `rds_toc()` agrees with `load()` on the version 2 fixtures.
4. **Diff and hash** in 0.1.0 rather than later? Recommended: later; the
   walk makes them small once the event stream is stable.

## 18. Roadmap

| Stage | Title | Size |
| --- | --- | --- |
| 0 | Identity; connection reading; prefix check; fixtures matrix | S |
| 1 | The walk: XDR, every type, references, limits | L |
| 2 | Binary and ASCII formats; `rds_info()`, `rds_str()` | M |
| 3 | `.rda` table of contents and extraction | M |
| 4 | The scanner and `rds_deps()` | M |
| 5 | The data-only builder, encodings, `rds_read_data()` | M |
| 6 | Lazy-load databases | M |
| 7 | Fuzzing, hostile inputs, drift check, benchmarks | M |
| 8 | Documentation and release 0.1.0 | S |

## 19. Acceptance criteria for v0.1.0

1. Builds everywhere with `zufast` from CRAN; `R CMD check --as-cran`
   clean on every CI leg; `R_init_zurds` the only export.
2. Every fixture of the version × format × compression matrix is
   walked; every data-only fixture round-trips `identical()` to
   `readRDS()`.
3. Every finding kind is detected at its path; the CVE-2024-27322-style
   fixture is refused by `rds_read_data()`.
4. No R object is allocated by the walk; hostile inputs fail with
   classed errors and bounded memory; the fuzz gate has been seen to
   fail on its canary.
5. The drift check is wired to R's release cadence.

## 20. What this RFC does not decide

- Whether zurds should write files. Stripping risky content into a
  data-only copy is useful; it needs an encoder and is left for later.
- Whether to propose the scanner to R Core as a `readRDS(safe = TRUE)`
  option. A working package is the better argument; decide after 0.1.0.

Review prepared with assistance from generative AI.
