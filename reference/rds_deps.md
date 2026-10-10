# Packages a file needs

Lists what loading a file would need from outside it: the namespaces and
package environments it refers to, the packages of its ALTREP classes,
and the packages of its S4 classes. The file is not loaded.

## Usage

``` r
rds_deps(x, kind = NULL)
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

A data frame with one row per distinct need:

- `package`: the package's name.

- `reason`: `"namespace"`, `"package_environment"`, `"altrep"` or
  `"s4"`.

- `detail`: the ALTREP or S4 class, or `NA`.

## See also

Other safety:
[`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)

## Examples

``` r
rds_deps(system.file("extdata", "model.rds", package = "zurds"))
#> [1] package reason  detail 
#> <0 rows> (or 0-length row.names)

# An object from a namespace needs that package:
rds_deps(serialize(stats::sd, NULL))
#>   package    reason detail
#> 1   stats namespace   <NA>
```
