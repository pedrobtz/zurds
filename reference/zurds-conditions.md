# Conditions raised by zurds

Every error zurds raises carries a condition class, so it can be caught
by kind rather than by matching the message, which may change. Every
class below inherits from `zurds_error`.

## Details

- `zurds_invalid_argument`:

  An argument was unusable. Carries `arg`, the argument at fault.

- `zurds_format_error`:

  The input is not a file R wrote: bad magic, an unknown format
  character or an unsupported version.

- `zurds_parse_error`:

  The stream is malformed. Carries `offset`, the byte offset in the
  decompressed stream, and `path`, where in the object the walk was.

- `zurds_unsafe_content`:

  The file holds something the data-only reader refuses. Carries
  `findings`, as
  [`rds_scan()`](https://pedrobtz.github.io/zurds/reference/rds_scan.md)
  would report them.

- `zurds_not_found`:

  A name or path is not in the file.

- `zurds_encoding_error`:

  A string is not valid in its declared encoding.

- `zurds_limit_error`:

  A limit was reached. The subclasses `zurds_depth_limit`,
  `zurds_size_limit` and `zurds_item_limit` name which one. Carries
  `limit`, the argument's name, and `limit_value`.

- `zurds_io_error`:

  Reading the input failed.

- `zurds_decompression_error`:

  The input could not be decompressed, for example because it is
  truncated.

## Examples

``` r
tryCatch(
  stop(structure(
    class = c("zurds_format_error", "zurds_error", "error", "condition"),
    list(message = "not an R serialization file", call = NULL)
  )),
  zurds_error = function(e) class(e)
)
#> [1] "zurds_format_error" "zurds_error"        "error"             
#> [4] "condition"         
```
