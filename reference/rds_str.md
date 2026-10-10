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
rds_str(system.file("extdata", "model.rds", package = "zurds"))
#> List of 2
#>  $ data:'data.frame':    6 obs. of  2 variables:
#>   ..$ mpg: num [1:6]
#>   ..$ wt: num [1:6]
#>  $ fit:'lm' List of 12
#>   ..$ coefficients: num [1:2]
#>   ..$ residuals: num [1:32]
#>   ..$ effects: num [1:32]
#>   ..$ rank: int [1:1]
#>   ..$ fitted.values: num [1:32]
#>   ..$ assign: int [1:2]
#>   ..$ qr:'qr' List of 5
#>   ..$ df.residual: int [1:1]
#>   ..$ xlevels:List of 0
#>   ..$ call: language
#>   ..$ terms: 'terms', 'formula' language
#>   ..$ model:'data.frame':    32 obs. of  2 variables:

rds_str(system.file("extdata", "objects.rda", package = "zurds"), max_level = 1)
#> .rda with 4 objects:
#>  $ cars:'data.frame':    10 obs. of  11 variables:
#>  $ species: Factor w/ 3 levels "setosa","versicolor","virginica": [1:3]
#>  $ grid: num [1:6] (matrix)
#>  $ summarise: function

# A file R refuses to load can still be summarised:
rds_str(system.file("extdata", "promise.rds", package = "zurds"))
#> List of 2
#>  $ : chr [1:1]
#>  $ : promise
```
