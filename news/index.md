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
