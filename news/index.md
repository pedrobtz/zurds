# Changelog

## zurds 0.0.0.9000

- [`zurds_info()`](https://pedrobtz.github.io/zurds/reference/zurds_info.md)
  reports the package and `zufast` versions, the default limits, and the
  compressions this R can read.
- [`rds_info()`](https://pedrobtz.github.io/zurds/reference/rds_info.md)
  reads the header of an `.rds`, `.rda` or
  [`serialize()`](https://rdrr.io/r/base/serialize.html) stream, or of a
  lazy-load database’s index: kind, format, version, the writing and
  minimum reading R versions, native encoding and compression.
- [`rds_str()`](https://pedrobtz.github.io/zurds/reference/rds_str.md)
  prints a [`str()`](https://rdrr.io/r/utils/str.html)-like summary of a
  file from its stream alone, without building any object or running any
  code. It reads XDR, native binary (either byte order) and ASCII
  streams.
- [`rds_toc()`](https://pedrobtz.github.io/zurds/reference/rds_toc.md)
  lists the objects of an `.rda` file, with their types, classes,
  lengths, dimensions, sizes and offsets, without building any of them.
- [`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
  reports everything in a file that can run code, reach outside it, or
  needs a package;
  [`rds_is_safe()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
  is `TRUE` when it finds nothing;
  [`rds_deps()`](https://pedrobtz.github.io/zurds/reference/rds_deps.md)
  lists the packages a file needs.
  [`rds_toc()`](https://pedrobtz.github.io/zurds/reference/rds_toc.md)
  fills `risky`.
- [`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md)
  reads data-only files with a reader written in C that builds only
  vectors, lists and attributes, and never calls
  [`unserialize()`](https://rdrr.io/r/base/serialize.html). The result
  is [`identical()`](https://rdrr.io/r/base/identical.html) to
  [`readRDS()`](https://rdrr.io/r/base/readRDS.html).
