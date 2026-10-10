# Table of contents of an .rda file

Lists the objects in a file written by
[`save()`](https://rdrr.io/r/base/save.html), with their types, classes,
lengths, dimensions and where they lie in the decompressed stream,
without building any of them. Nothing in the file is evaluated, and
memory stays small however large the objects are.

## Usage

``` r
rds_toc(x, kind = NULL)
```

## Arguments

- x:

  A path, raw vector or connection holding an `.rda` file.

- kind:

  `NULL` to detect the kind of file, or one of `"rds"`, `"rda"` and
  `"lazyload"` to insist on it.

## Value

A data frame with one row per object, in the order
[`save()`](https://rdrr.io/r/base/save.html) wrote them:

- `name`: the object's name.

- `type`: its type, as [`typeof()`](https://rdrr.io/r/base/typeof.html)
  names it.

- `class`: its first class, or `NA` when it has no class attribute.

- `length`: its length, as
  [`length()`](https://rdrr.io/r/base/length.html) reports it for
  vectors and lists; `NA` where the stream does not record it
  (environments, functions, ALTREP objects).

- `dim`: its dimensions as text, `"3 x 4"`; for a data frame, rows by
  columns; `NA` otherwise.

- `size`: the bytes it takes in the decompressed stream.

- `offset`: where it starts in the decompressed file, in bytes.

- `risky`: whether it holds anything that can run code or reach outside
  the file: a finding of
  [`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
  of severity `"executes"` or `"reaches_out"`.

## See also

Other inspection:
[`rds_info()`](https://pedrobtz.github.io/zurds/reference/rds_info.md),
[`rds_str()`](https://pedrobtz.github.io/zurds/reference/rds_str.md)

## Examples

``` r
rds_toc(system.file("extdata", "objects.rda", package = "zurds"))
#>        name    type      class length     dim size offset risky
#> 1      cars    list data.frame     11 10 x 11 1396     48 FALSE
#> 2   species integer     factor      3    <NA>  133   1467 FALSE
#> 3      grid  double       <NA>      6   2 x 3   95   1620 FALSE
#> 4 summarise closure       <NA>      1    <NA>  132   1740  TRUE
```
