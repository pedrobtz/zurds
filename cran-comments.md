## This is a first submission

zurds is new; `Version` is 0.1.0. It reads the files R writes with
`saveRDS()`, `save()` and `serialize()`, and lazy-load databases, without
handing them to `unserialize()`: it reports headers, lists the objects in an
`.rda` file, summarises structure, extracts one object, scans for anything
that can run code (the class of file behind CVE-2024-27322), and reads
data-only files with a reader written in C.

## Test environments

* local macOS 15.7 (x86-64), R 4.5.2 (R 4.6.1 on the CI legs)
* GitHub Actions, R CMD check `--as-cran` on every push to the main branch:
  * macOS (arm64), R release
  * Windows (x86-64), R release and R-devel
  * Ubuntu, R release, oldrel-1, and 4.1 (the version `Depends` declares)
  * the r-devel containers matching CRAN's Linux flavours (`clang23`,
    `ubuntu-clang`, `ubuntu-gcc16`), and the `nosuggests` and `nold`
    flavours
  * CRAN's additional checks (`rcnst`, `rlibro`, `vnu`)
* Also on every push: link-time optimisation, `rchk`, `gctorture`,
  AddressSanitizer and UndefinedBehaviorSanitizer, valgrind with
  `--leak-check=full`, and GCC's `-fanalyzer`. Weekly: i386 and musl builds
  that run the test suite, and a sweep that fails each native allocation in
  turn. Nightly: libFuzzer over the C reader.

## R CMD check results

0 errors | 0 warnings | 1 note

* This is a new submission.

On R 4.5 and earlier (oldrel), and nowhere else, there is a second NOTE:
`Found non-API calls to R: 'R_GetConnection', 'R_ReadConnection'`. These
come from `R_ext/Connections.h`, which R 4.6 lists as API; the package
uses them to read the bytes of a `gzfile()` connection, so that it reads
every compression the running R can read without bundling a codec.

## Compiled code

All C code is the package's own; nothing is vendored. `zufast` is used
through `LinkingTo:` only, for header-only parsing primitives. The shared
object exports only `R_init_zurds`. Reading a connection uses
`R_ReadConnection()`, part of R's documented connections API
(`R_ext/Connections.h`).

## Method references

There is no published method to cite. The package follows R's own
serialization format as implemented in `src/main/serialize.c`.
