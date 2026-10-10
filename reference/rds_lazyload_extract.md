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
db <- file.path(system.file("R", package = "stats"), "stats")
toc <- rds_lazyload_toc(db)
data_only <- toc$name[!toc$risky & toc$type %in% c("character", "list")]
if (length(data_only)) str(rds_lazyload_extract(db, data_only[1]))
#>  chr [1:2] "y" "n"
```
