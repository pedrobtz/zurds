# Structure of an R serialization file

Prints a compact summary of what a file holds, in the style of
[`utils::str()`](https://rdrr.io/r/utils/str.html), from the serialized
stream alone: no object is built, no code in the file runs, and a file R
would refuse to load can still be summarised.

## Usage

``` r
rds_str(x, max_level = 2, kind = NULL)
```

## Arguments

- x:

  A path, a raw vector (a
  [`serialize()`](https://rdrr.io/r/base/serialize.html) result, or a
  file's bytes) or a connection open for reading in binary mode. For a
  lazy-load database, the path without extension, as
  [`lazyLoad()`](https://rdrr.io/r/base/lazyload.html) takes it, or the
  path of its `.rdb` or `.rdx` file.

- max_level:

  How many levels of nested lists to show; 0 shows only the top.

- kind:

  `NULL` to detect the kind of file, or one of `"rds"`, `"rda"` and
  `"lazyload"` to insist on it.

## Value

The lines of the summary, invisibly. They are printed as a side effect.

## Details

Values are not shown, only types, lengths, names and classes. Elements
of a list are listed up to `max_level` levels down, at most 99 per list.
Closures, environments, promises and other items that could run code or
reach outside the file are named, never followed.

## See also

Other inspection:
[`rds_info()`](https://pedrobtz.github.io/zurds/reference/rds_info.md),
[`rds_toc()`](https://pedrobtz.github.io/zurds/reference/rds_toc.md)

## Examples

``` r
f <- tempfile(fileext = ".rds")
saveRDS(list(id = 1:3, fit = list(coef = c(a = 1, b = 2)), when = Sys.Date()), f)
rds_str(f)
#> List of 3
#>  $ id: int <compact_intseq>
#>  $ fit:List of 1
#>   ..$ coef: num [1:2]
#>  $ when: 'Date' num [1:1]

g <- tempfile(fileext = ".rda")
x <- letters
y <- iris
save(x, y, file = g)
rds_str(g, max_level = 1)
#> .rda with 2 objects:
#>  $ x: chr [1:26]
#>  $ y:'data.frame':   150 obs. of  5 variables:
```
