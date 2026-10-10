# zurds 0.0.0.9000

* `zurds_info()` reports the package and `zufast` versions, the default
  limits, and the compressions this R can read.
* `rds_info()` reads the header of an `.rds`, `.rda` or `serialize()`
  stream, or of a lazy-load database's index: kind, format, version, the
  writing and minimum reading R versions, native encoding and compression.
* `rds_str()` prints a `str()`-like summary of a file from its stream
  alone, without building any object or running any code. It reads XDR,
  native binary (either byte order) and ASCII streams.
* `rds_toc()` lists the objects of an `.rda` file, with their types, classes,
  lengths, dimensions, sizes and offsets, without building any of them.
* `rds_scan()` reports everything in a file that can run code, reach outside
  it, or needs a package; `rds_is_safe()` is `TRUE` when it finds nothing;
  `rds_deps()` lists the packages a file needs. `rds_toc()` fills `risky`.
* `rds_read_data()` reads data-only files with a reader written in C that
  builds only vectors, lists and attributes, and never calls `unserialize()`.
  The result is `identical()` to `readRDS()`.
* `rds_extract()` reads one object, list element or data frame column
  without building the rest; `use_r = TRUE` hands only that item to R.
