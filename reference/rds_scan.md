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
strings and wrappers) need no package and are read by
[`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md);
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
rds_scan(system.file("extdata", "model.rds", package = "zurds"))
#>                                                                  path
#> 1                                            [[2]][[11]]$.Environment
#> 2       [[2]][[11]]$.Environment@hashtab[[11]]$fit[[11]]$.Environment
#> 3 [[2]][[11]]$.Environment@hashtab[[11]]$fit[[12]]$terms$.Environment
#> 4                                      [[2]][[12]]$terms$.Environment
#>       finding detail    severity
#> 1 environment   <NA> reaches_out
#> 2 environment   <NA> reaches_out
#> 3 environment   <NA> reaches_out
#> 4 environment   <NA> reaches_out
rds_is_safe(system.file("extdata", "iris.rds", package = "zurds"))
#> [1] TRUE

# A promise that would run system() when used:
promise <- system.file("extdata", "promise.rds", package = "zurds")
rds_scan(promise)
#>    path finding detail severity
#> 1 [[2]] promise   <NA> executes
rds_is_safe(promise)
#> [1] FALSE
```
