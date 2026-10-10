# Table of contents of a lazy-load database

Lists the objects of a lazy-load database, such as an installed
package's `R/<pkg>.rdb` and `.rdx`, or a knitr or targets cache, without
loading any of them. Unlike
[`lazyLoad()`](https://rdrr.io/r/base/lazyload.html), nothing is
evaluated: an entry that is a promise is reported as one, never forced.

## Usage

``` r
rds_lazyload_toc(path)
```

## Arguments

- path:

  The database's path without extension, as
  [`lazyLoad()`](https://rdrr.io/r/base/lazyload.html) takes it, or the
  path of its `.rdb` or `.rdx` file.

## Value

A data frame with one row per object, with the columns of
[`rds_toc()`](https://pedrobtz.github.io/zurds/reference/rds_toc.md):
`name`, `type`, `class`, `length`, `dim`, `size` (the bytes of its
decompressed stream), `offset` (where its entry starts in the `.rdb`)
and `risky`.

## See also

Other lazy-load:
[`rds_lazyload_extract()`](https://pedrobtz.github.io/zurds/reference/rds_lazyload_extract.md)

## Examples

``` r
db <- file.path(system.file("R", package = "stats"), "stats")
head(rds_lazyload_toc(db))
#>                   name        type class length  dim  size offset risky
#> 1                .Diag     closure  <NA>      1 <NA>  4836      0  TRUE
#> 2             .MFclass     closure  <NA>      1 <NA>  3503   1121  TRUE
#> 3       .NewtonRaphson     closure  <NA>      1 <NA> 15297   1903  TRUE
#> 4      .__NAMESPACE__. environment  <NA>      1 <NA>    58   6273  TRUE
#> 5 .__S3MethodsTable__. environment  <NA>      1 <NA>    58   6476  TRUE
#> 6          .__global__   character  <NA>      2 <NA>    58   6537 FALSE
```
