# Extract one object or element from an R serialization file

Builds only the item asked for: one object of an `.rda`, or one element
of a list or one column of a data frame, at any depth. The rest of the
file is walked over, not built. The item is built by the data-only
reader of
[`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md),
so it can hold only data.

## Usage

``` r
rds_extract(x, what, use_r = FALSE, kind = NULL)
```

## Arguments

- x:

  A path, a raw vector (a
  [`serialize()`](https://rdrr.io/r/base/serialize.html) result, or a
  file's bytes) or a connection open for reading in binary mode. For a
  lazy-load database, the path without extension, as
  [`lazyLoad()`](https://rdrr.io/r/base/lazyload.html) takes it, or the
  path of its `.rdb` or `.rdx` file.

- what:

  Where the item is. For an `.rda`, the object's name, then optionally a
  path inside it; for anything else, a path inside the object. A path is
  a character vector of names, such as `c("fit", "coefficients")`, or a
  list mixing names and positions, such as `list("models", 3)`.

- use_r:

  When `FALSE`, the default, the item is built by the data-only reader
  and anything that can run code or reach outside the file is refused.
  When `TRUE`, the item, and only the item, is handed to R's
  [`unserialize()`](https://rdrr.io/r/base/serialize.html), so it may
  hold closures, environments or anything else R can read; its scan by
  [`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)'s
  rules is attached to the result as the attribute `"zurds_findings"`
  (unless the result is `NULL` or a symbol, which cannot carry one).
  Only use this on items you trust: loading them can run code. An item
  that refers to an environment written before it, outside the item,
  cannot be read alone and is refused.

- kind:

  `NULL` to detect the kind of file, or one of `"rds"`, `"rda"` and
  `"lazyload"` to insist on it.

## Value

The item.

## Details

The format has no index, so each step of `what` reads the file up to the
item; for an `.rda`, the objects' offsets from
[`rds_toc()`](https://pedrobtz.github.io/zurds/reference/rds_toc.md) are
reused when the same file is read again.

## Conditions

A name or position not in the file is an error of class
`zurds_not_found`; content the data-only reader refuses is
`zurds_unsafe_content`, as for
[`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md).

## See also

Other reading:
[`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md)

## Examples

``` r
f <- tempfile(fileext = ".rda")
cars <- mtcars
fit <- list(coefficients = c(a = 1, b = 2), call = quote(lm(y ~ x)))
save(cars, fit, file = f)
head(rds_extract(f, "cars"))
#>                    mpg cyl disp  hp drat    wt  qsec vs am gear carb
#> Mazda RX4         21.0   6  160 110 3.90 2.620 16.46  0  1    4    4
#> Mazda RX4 Wag     21.0   6  160 110 3.90 2.875 17.02  0  1    4    4
#> Datsun 710        22.8   4  108  93 3.85 2.320 18.61  1  1    4    1
#> Hornet 4 Drive    21.4   6  258 110 3.08 3.215 19.44  1  0    3    1
#> Hornet Sportabout 18.7   8  360 175 3.15 3.440 17.02  0  0    3    2
#> Valiant           18.1   6  225 105 2.76 3.460 20.22  1  0    3    1
rds_extract(f, c("cars", "mpg"))
#>  [1] 21.0 21.0 22.8 21.4 18.7 18.1 14.3 24.4 22.8 19.2 17.8 16.4 17.3 15.2 10.4
#> [16] 10.4 14.7 32.4 30.4 33.9 21.5 15.5 15.2 13.3 19.2 27.3 26.0 30.4 15.8 19.7
#> [31] 15.0 21.4
rds_extract(f, c("fit", "coefficients"))
#> a b 
#> 1 2 

g <- tempfile(fileext = ".rds")
saveRDS(list(a = 1, b = list(10, 20)), g)
rds_extract(g, list("b", 2))
#> [1] 20
```
