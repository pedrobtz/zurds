# Read a data-only R serialization file safely

Reads a file written by
[`saveRDS()`](https://rdrr.io/r/base/readRDS.html),
[`save()`](https://rdrr.io/r/base/save.html) or
[`serialize()`](https://rdrr.io/r/base/serialize.html) with a reader
written in C that builds only `NULL`, logical, integer, double, complex,
character, raw and list vectors, with attributes of those types. It
never calls [`unserialize()`](https://rdrr.io/r/base/serialize.html),
[`eval()`](https://rdrr.io/r/base/eval.html) or anything from the file,
and it refuses everything that could run code or reach outside the file.
For a data-only file the result is
[`identical()`](https://rdrr.io/r/base/identical.html) to what
[`readRDS()`](https://rdrr.io/r/base/readRDS.html) or
[`load()`](https://rdrr.io/r/base/load.html) returns.

## Usage

``` r
rds_read_data(x, strict = TRUE, kind = NULL)
```

## Arguments

- x:

  A path, a raw vector (a
  [`serialize()`](https://rdrr.io/r/base/serialize.html) result, or a
  file's bytes) or a connection open for reading in binary mode. For a
  lazy-load database, the path without extension, as
  [`lazyLoad()`](https://rdrr.io/r/base/lazyload.html) takes it, or the
  path of its `.rdb` or `.rdx` file.

- strict:

  When `TRUE`, the default, anything that cannot be built (closures,
  promises, environments, language objects, S4 objects, ...) is an error
  of class `zurds_unsafe_content`, and so is a string marked UTF-8 that
  is not valid UTF-8 (`zurds_encoding_error`). When `FALSE`, each such
  item is replaced by `NULL`, with one warning per kind.

- kind:

  `NULL` to detect the kind of file, or one of `"rds"`, `"rda"` and
  `"lazyload"` to insist on it.

## Value

The object. For an `.rda` file, a named list of its objects, in the
order [`save()`](https://rdrr.io/r/base/save.html) wrote them.

## Details

Classes are kept as attributes; no method of a class is called while the
object is built. Using the result may dispatch to methods you have
loaded: that is your code, not the file's.

Base R's compact sequences (`1:10`), deferred strings and wrappers are
built as the ordinary vectors they stand for. Strings keep their
declared encoding; strings written in another session's native encoding
(named by a version 3 header) are translated to UTF-8.

## Conditions

A `zurds_unsafe_content` error carries `findings`, as
[`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
reports them, and `refused`, a data frame of the items that could not be
built (`path`, `type`).

## Examples

``` r
f <- tempfile(fileext = ".rds")
saveRDS(head(mtcars), f)
identical(rds_read_data(f), readRDS(f))
#> [1] TRUE

saveRDS(list(x = 1:3, f = function() "hello"), f)
try(rds_read_data(f))
#> Error in rds_read_data(f) : 
#>   the file holds what the data-only reader refuses: closure, environment, namespace, bytecode, promise. Use rds_scan() to see where, or strict = FALSE to read it with those items replaced by NULL.
rds_read_data(f, strict = FALSE)
#> Warning: replaced 1 closure item(s) by NULL
#> $x
#> [1] 1 2 3
#> 
#> $f
#> NULL
#> 
```
