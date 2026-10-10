# Scan an R serialization file for code and references

Reports everything in a file that can run code when the file is loaded
or used, that reaches outside the file, or that needs a package to load,
without loading it. Nothing in the file is evaluated, and a file R
itself would refuse can still be scanned.

## Usage

``` r
rds_scan(x, kind = NULL)

rds_is_safe(x, kind = NULL)
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

For `rds_scan()`, a data frame with one row per finding, in stream
order:

- `path`: where in the object, such as `[[2]]$fit` (see
  [zurds-conditions](https://pedrobtz.github.io/zurds/reference/zurds-conditions.md)
  for the notation); `""` for the object itself, and `$name` for an
  object of an `.rda`.

- `finding`: `"closure"`, `"promise"`, `"bytecode"`, `"environment"`,
  `"external_pointer"`, `"weak_reference"`, `"namespace"`,
  `"package_environment"`, `"persistent_reference"`, `"altrep"` or
  `"s4"`.

- `detail`: the namespace or package environment, the ALTREP class and
  its package, or the S4 class and its package; `NA` otherwise.

- `severity`: `"executes"` (closures, promises, bytecode),
  `"reaches_out"` (environments, namespaces, package environments,
  external pointers, weak and persistent references) or
  `"needs_package"` (ALTREP and S4).

For `rds_is_safe()`, `TRUE` when `rds_scan()` finds nothing.

## Details

Base R's own ALTREP classes (compact sequences such as `1:10`, deferred
strings and wrappers) need no package and are read by `rds_read_data()`;
they are not findings.

The scan is advisory for
[`readRDS()`](https://rdrr.io/r/base/readRDS.html): a clean scan means
the file holds no type that can run code when it is loaded, but
[`readRDS()`](https://rdrr.io/r/base/readRDS.html) still creates objects
of the file's classes, and using them can call methods you have loaded.

## See also

Other safety:
[`rds_deps()`](https://pedrobtz.github.io/zurds/reference/rds_deps.md)

## Examples

``` r
f <- tempfile(fileext = ".rds")
saveRDS(list(data = mtcars, model = lm(mpg ~ wt, mtcars)), f)
head(rds_scan(f))
#>                                                         path     finding detail
#> 1                                   [[2]][[11]]$.Environment environment   <NA>
#> 2                            [[2]][[11]]$.Environment@enclos environment   <NA>
#> 3                     [[2]][[11]]$.Environment@enclos@enclos environment   <NA>
#> 4              [[2]][[11]]$.Environment@enclos@enclos@enclos environment   <NA>
#> 5     [[2]][[11]]$.Environment@enclos@hashtab[[14]]$DONTSHOW     closure   <NA>
#> 6 [[2]][[11]]$.Environment@enclos@hashtab[[14]]$DONTSHOW@env environment   <NA>
#>      severity
#> 1 reaches_out
#> 2 reaches_out
#> 3 reaches_out
#> 4 reaches_out
#> 5    executes
#> 6 reaches_out
rds_is_safe(f)
#> [1] FALSE

saveRDS(mtcars, f)
rds_is_safe(f)
#> [1] TRUE
```
