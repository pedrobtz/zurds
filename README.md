# zurds

<!-- badges: start -->
[![R-CMD-check](https://github.com/pedrobtz/zurds/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/pedrobtz/zurds/actions/workflows/R-CMD-check.yaml)
[![coverage](https://raw.githubusercontent.com/pedrobtz/zurds/main/.github/badges/coverage.svg)](https://github.com/pedrobtz/zurds/actions/workflows/coverage.yaml)
<!-- badges: end -->

zurds reads the files R writes with `saveRDS()`, `save()` and
`serialize()`, and lazy-load databases, without handing them to R's
deserializer. It can tell you what a file holds, pull out one object, check
whether a file is safe to load, and read data-only files with a reader that
cannot run code.

`readRDS()` and `load()` are all or nothing: to learn what a 2 GB `.rda`
contains, or whether a model file from a colleague carries closures and
environments, you have to load all of it, and loading can run code
(CVE-2024-27322). zurds walks the file instead, building nothing it was not
asked for.

## Installation

zurds is not on CRAN yet. Install it, and the `zufast` headers it is built
with, from GitHub:

``` r
# install.packages("pak")
pak::pak("pedrobtz/zurds")
```

## What is in a file

``` r
library(zurds)
f <- system.file("extdata", "objects.rda", package = "zurds")

rds_toc(f)
#>        name    type      class length     dim size offset risky
#> 1      cars    list data.frame     11 10 x 11 1396     48 FALSE
#> 2   species integer     factor      3    <NA>  133   1467 FALSE
#> 3      grid  double       <NA>      6   2 x 3   95   1620 FALSE
#> 4 summarise closure       <NA>      1    <NA>  132   1740  TRUE
```

`rds_str()` summarises any file the way `str()` does, and `rds_info()`
reads its header.

## Is it safe to load?

``` r
rds_scan(system.file("extdata", "promise.rds", package = "zurds"))
#>    path finding detail severity
#> 1 [[2]] promise   <NA> executes
```

A promise, a closure or bytecode can run code; an environment, a namespace
or an external pointer reaches outside the file; ALTREP and S4 classes need
their packages. `rds_is_safe()` is `TRUE` when the scan finds nothing, and
`rds_deps()` lists the packages a file would need.

## Reading data safely

``` r
f <- system.file("extdata", "iris.rds", package = "zurds")
identical(rds_read_data(f), readRDS(f))
#> [1] TRUE
```

`rds_read_data()` builds only vectors, lists and their attributes, and
refuses everything else. `rds_extract()` reads one object, element or
column without building the rest, and `rds_lazyload_toc()` and
`rds_lazyload_extract()` do the same for lazy-load databases.

See `vignette("safe-reading", package = "zurds")` for what the safety
guarantee covers and what it does not.
