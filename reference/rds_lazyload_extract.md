# Extract one object from a lazy-load database

Reads one object of a lazy-load database with the data-only reader of
[`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md).
A promise, a closure or anything else that could run code is refused,
never evaluated.

## Usage

``` r
rds_lazyload_extract(path, name)
```

## Arguments

- path:

  The database's path without extension, as
  [`lazyLoad()`](https://rdrr.io/r/base/lazyload.html) takes it, or the
  path of its `.rdb` or `.rdx` file.

- name:

  The object's name.

## Value

The object.

## Conditions

A name not in the database is an error of class `zurds_not_found`;
content the data-only reader refuses is `zurds_unsafe_content`.

## See also

Other lazy-load:
[`rds_lazyload_toc()`](https://pedrobtz.github.io/zurds/reference/rds_lazyload_toc.md)

## Examples

``` r
db <- file.path(system.file("extdata", package = "zurds"), "lazy")
rds_lazyload_extract(db, "table")
#>   id name
#> 1  1    a
#> 2  2    b
#> 3  3    c
try(rds_lazyload_extract(db, "helper"))
#> Error in rds_lazyload_extract(db, "helper") : 
#>   the file holds what the data-only reader refuses: closure. Use rds_scan() to see where, or strict = FALSE to read it with those items replaced by NULL.
```
