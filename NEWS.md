# zurds 0.0.0.9000

* `zurds_info()` reports the package and `zufast` versions, the default
  limits, and the compressions this R can read.
* `rds_info()` reads the header of an `.rds`, `.rda` or `serialize()`
  stream, or of a lazy-load database's index: kind, format, version, the
  writing and minimum reading R versions, native encoding and compression.
* `rds_str()` prints a `str()`-like summary of a file from its stream
  alone, without building any object or running any code. It reads XDR,
  native binary (either byte order) and ASCII streams.
