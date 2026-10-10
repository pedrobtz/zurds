zrd_format_names <- c(X = "xdr", B = "binary", A = "ascii")

# R_Version(v, p, s) is 65536 v + 256 p + s (serialize.c, DecodeVersion).
zrd_decode_version <- function(v) {
  if (is.na(v) || v < 0) return(NA_character_)
  paste(v %/% 65536, (v %% 65536) %/% 256, v %% 256, sep = ".")
}

#' Header of an R serialization file
#'
#' Reads the header of a file written by `saveRDS()`, `save()` or
#' `serialize()`, or of a lazy-load database's index, without reading the
#' object it holds.
#'
#' @param x A path, a raw vector (a `serialize()` result, or a file's
#'   bytes) or a connection open for reading in binary mode. For a
#'   lazy-load database, the path without extension, as `lazyLoad()` takes
#'   it, or the path of its `.rdb` or `.rdx` file.
#' @param kind `NULL` to detect the kind of file, or one of `"rds"`,
#'   `"rda"` and `"lazyload"` to insist on it.
#' @return A list of class `rds_info` with elements:
#'   * `kind`: `"rds"` (a single stream, as `saveRDS()` and `serialize()`
#'     write), `"rda"` (as `save()` writes) or `"lazyload"`.
#'   * `format`: `"xdr"`, `"binary"` or `"ascii"`.
#'   * `version`: the serialization format version, 2 or 3.
#'   * `writer_version`: the version of R that wrote the stream, as
#'     `"4.6.1"`.
#'   * `min_reader_version`: the oldest version of R that can read it.
#'   * `native_encoding`: the writer's native encoding (version 3 only;
#'     `NA` for version 2).
#'   * `compression`: `"none"`, `"gzip"`, `"bzip2"`, `"xz"` or `"zstd"`;
#'     `NA` for a connection, whose compression zurds does not see. For a
#'     lazy-load database, that of its index.
#' @family inspection
#' @export
#' @examples
#' f <- tempfile(fileext = ".rds")
#' saveRDS(mtcars, f)
#' rds_info(f)
#'
#' rds_info(serialize(1:3, NULL, version = 2))
rds_info <- function(x, kind = NULL) {
  call <- sys.call()
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  if (identical(input$kind, "lazyload")) {
    # The index is itself an .rds file.
    idx <- zrd_open(paste0(input$path, ".rdx"), "rds", call)
    on.exit(zrd_close(idx), add = TRUE)
    head <- zrd_read_head(idx, call)
  } else {
    head <- zrd_read_head(input, call)
  }
  structure(
    list(
      kind = input$kind,
      format = unname(zrd_format_names[head$format]),
      version = head$version,
      writer_version = zrd_decode_version(head$writer_version),
      min_reader_version = zrd_decode_version(head$min_reader_version),
      native_encoding = if (head$version >= 3L) head$native_encoding else NA_character_,
      compression = input$compression
    ),
    class = "rds_info"
  )
}

zrd_read_head <- function(input, call) {
  res <- zrd_source_call(
    .Call(zrd_walk_head, input$source, zrd_stream_skip(input)),
    call
  )
  if (res$status != "ZRD_OK") {
    zrd_walk_fail(res, input, zrd_default_limits(), call)
  }
  res
}

#' @export
format.rds_info <- function(x, ...) {
  c(
    "<rds_info>",
    paste0("kind:               ", x$kind),
    paste0("format:             ", x$format, ", version ", x$version),
    paste0("written by R:       ", x$writer_version),
    paste0("readable from R:    ", x$min_reader_version),
    paste0("native encoding:    ", x$native_encoding),
    paste0("compression:        ", x$compression)
  )
}

#' @export
print.rds_info <- function(x, ...) {
  writeLines(format(x, ...))
  invisible(x)
}
