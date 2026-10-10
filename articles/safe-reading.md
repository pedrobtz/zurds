# Reading R files from elsewhere

``` r

library(zurds)
ex <- function(f) system.file("extdata", f, package = "zurds")
```

An `.rds` or `.rda` file is a serialized R object, and R objects can
hold code. [`readRDS()`](https://rdrr.io/r/base/readRDS.html) and
[`load()`](https://rdrr.io/r/base/load.html) rebuild everything in the
file: closures, environments, promises. Since R 4.4.0 a promise at the
top level of a file is refused (CVE-2024-27322), but a promise nested in
a list, a closure or an environment still loads, and runs when used.

zurds reads the file without R’s deserializer. This vignette shows how
to look before you load, and what its data-only reader does and does not
promise.

## Look first

[`rds_info()`](https://pedrobtz.github.io/zurds/reference/rds_info.md)
reads only the header.
[`rds_str()`](https://pedrobtz.github.io/zurds/reference/rds_str.md) and
[`rds_toc()`](https://pedrobtz.github.io/zurds/reference/rds_toc.md)
walk the whole stream but build nothing: a 2 GB `.rda` costs the time to
decompress it, not the memory to hold it.

``` r

rds_info(ex("model.rds"))
#> <rds_info>
#> kind:               rds
#> format:             xdr, version 3
#> written by R:       4.6.1
#> readable from R:    3.5.0
#> native encoding:    UTF-8
#> compression:        gzip
rds_str(ex("model.rds"))
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
rds_toc(ex("objects.rda"))
#>        name    type      class length     dim size offset risky
#> 1      cars    list data.frame     11 10 x 11 1396     48 FALSE
#> 2   species integer     factor      3    <NA>  133   1467 FALSE
#> 3      grid  double       <NA>      6   2 x 3   95   1620 FALSE
#> 4 summarise closure       <NA>      1    <NA>  132   1740  TRUE
```

## Scan

[`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
lists everything that can run code (`"executes"`), reach outside the
file (`"reaches_out"`), or needs a package to load (`"needs_package"`),
with where it is:

``` r

rds_scan(ex("model.rds"))
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
rds_scan(ex("promise.rds"))
#>    path finding detail severity
#> 1 [[2]] promise   <NA> executes
rds_deps(serialize(stats::sd, NULL))
#>   package    reason detail
#> 1   stats namespace   <NA>
```

A linear model carries its formula’s environment: loading it gives you
that environment back. The second file holds a promise whose code would
call [`system()`](https://rdrr.io/r/base/system.html).

## Read data, refuse code

[`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md)
is a reader written in C that builds only `NULL`, logical, integer,
double, complex, character, raw and list vectors, with their attributes.
For a data-only file its result is
[`identical()`](https://rdrr.io/r/base/identical.html) to
[`readRDS()`](https://rdrr.io/r/base/readRDS.html).

``` r

identical(rds_read_data(ex("iris.rds")), readRDS(ex("iris.rds")))
#> [1] TRUE
```

Anything else is an error, with the scan attached:

``` r

cnd <- tryCatch(rds_read_data(ex("promise.rds")),
                zurds_unsafe_content = function(e) e)
cnd$findings
#>    path finding detail severity
#> 1 [[2]] promise   <NA> executes
```

or, with `strict = FALSE`, replaced by `NULL` with a warning:

``` r

rds_read_data(ex("promise.rds"), strict = FALSE)
#> Warning in rds_read_data(ex("promise.rds"), strict = FALSE): replaced 1 promise
#> item(s) by NULL
#> [[1]]
#> [1] "harmless"
#> 
#> [[2]]
#> NULL
```

[`rds_extract()`](https://pedrobtz.github.io/zurds/reference/rds_extract.md)
reads one object, list element or column the same way, and
[`rds_lazyload_extract()`](https://pedrobtz.github.io/zurds/reference/rds_lazyload_extract.md)
one object of a lazy-load database.

## What the guarantee covers

[`rds_read_data()`](https://pedrobtz.github.io/zurds/reference/rds_read_data.md):

1.  never calls
    [`unserialize()`](https://rdrr.io/r/base/serialize.html),
    [`eval()`](https://rdrr.io/r/base/eval.html) or any function from
    the file;
2.  builds only the types above, with attributes of those types; a
    `class` attribute is kept, and no method of that class is called
    while building;
3.  refuses, or with `strict = FALSE` drops, every other type;
4.  checks every length and depth against its limits before allocating.

What it does not cover: once you have the object, your own code may
dispatch on its class to methods you have loaded. A data frame is still
a data frame, and a class name from the file can still select a
[`print()`](https://rdrr.io/r/base/print.html) method in your session.
That is your code, not the file’s.

[`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
and
[`rds_is_safe()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
are advisory for [`readRDS()`](https://rdrr.io/r/base/readRDS.html): a
clean scan means the file holds no type that runs code as it loads, but
[`readRDS()`](https://rdrr.io/r/base/readRDS.html) still creates objects
of the file’s classes. When you trust a file’s code,
`rds_extract(use_r = TRUE)` hands one item, and only that item, to R’s
own reader, with its scan attached.
