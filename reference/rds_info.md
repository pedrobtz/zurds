# Header of an R serialization file

Reads the header of a file written by
[`saveRDS()`](https://rdrr.io/r/base/readRDS.html),
[`save()`](https://rdrr.io/r/base/save.html) or
[`serialize()`](https://rdrr.io/r/base/serialize.html), or of a
lazy-load database's index, without reading the object it holds.

## Usage

``` r
rds_info(x, kind = NULL)
```

## Arguments

- x:

  A path, a raw vector (a
  [`serialize()`](https://rdrr.io/r/base/serialize.html) result, or a
  file's bytes) or a connection open for reading in binary mode. For a
  lazy-load database, the path without extension, as
  [`lazyLoad()`](https://rdrr.io/r/base/lazyload.html) takes it, or the
  path of its `.rdb` or `.rdx` file.

- kind:

  `NULL` to detect the kind of file, or one of `"rds"`, `"rda"` and
  `"lazyload"` to insist on it.

## Value

A list of class `rds_info` with elements:

- `kind`: `"rds"` (a single stream, as
  [`saveRDS()`](https://rdrr.io/r/base/readRDS.html) and
  [`serialize()`](https://rdrr.io/r/base/serialize.html) write), `"rda"`
  (as [`save()`](https://rdrr.io/r/base/save.html) writes) or
  `"lazyload"`.

- `format`: `"xdr"`, `"binary"` or `"ascii"`.

- `version`: the serialization format version, 2 or 3.

- `writer_version`: the version of R that wrote the stream, as
  `"4.6.1"`.

- `min_reader_version`: the oldest version of R that can read it.

- `native_encoding`: the writer's native encoding (version 3 only; `NA`
  for version 2).

- `compression`: `"none"`, `"gzip"`, `"bzip2"`, `"xz"` or `"zstd"`; `NA`
  for a connection, whose compression zurds does not see. For a
  lazy-load database, that of its index.

## See also

Other inspection:
[`rds_str()`](https://pedrobtz.github.io/zurds/reference/rds_str.md)

## Examples

``` r
f <- tempfile(fileext = ".rds")
saveRDS(mtcars, f)
rds_info(f)
#> <rds_info>
#> kind:               rds
#> format:             xdr, version 3
#> written by R:       4.6.1
#> readable from R:    3.5.0
#> native encoding:    UTF-8
#> compression:        gzip

rds_info(serialize(1:3, NULL, version = 2))
#> <rds_info>
#> kind:               rds
#> format:             xdr, version 2
#> written by R:       4.6.1
#> readable from R:    2.3.0
#> native encoding:    NA
#> compression:        none
```
