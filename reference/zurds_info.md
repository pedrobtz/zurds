# Build information

Reports the version of zurds, the version of the 'zufast' headers it was
compiled against, the default limits, and the compressions this R can
read.

## Usage

``` r
zurds_info()
```

## Value

A list of class `zurds_info` with elements:

- `version`: the zurds version, a string.

- `zufast_version`: the 'zufast' header version compiled in. 'zufast' is
  header-only, so this describes the code inside this package, not a
  library loaded at run time.

- `zufast_ok`: `TRUE` if a self-test through 'zufast' passed.

- `limits`: the default `max_depth`, `max_items`, `max_size` (bytes, for
  inspection) and `max_size_build` (bytes, for building objects).

- `compressions`: the compressions this R can read, from `"gzip"`,
  `"bzip2"`, `"xz"` and `"zstd"`.

## Examples

``` r
zurds_info()
#> <zurds_info>
#> zurds:          0.1.0
#> zufast:         0.1.0 (self-test ok)
#> max_depth:      512
#> max_items:      100,000,000
#> max_size:       17,179,869,184 bytes
#> max_size_build: 4,294,967,296 bytes
#> compressions:   gzip, bzip2, xz, zstd
```
