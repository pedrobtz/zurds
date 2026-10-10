# Opening an input (design sections 1, 4 and 5): a path, a raw vector or a
# connection becomes a source of decompressed stream bytes, with its kind
# and compression detected. Every user-facing function starts here.

zrd_kinds <- c("rds", "rda", "lazyload")

# Leading bytes of each compressed container R writes.
zrd_compression_magic <- list(
  gzip = as.raw(c(0x1f, 0x8b)),
  bzip2 = charToRaw("BZh"),
  xz = as.raw(c(0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00)),
  zstd = as.raw(c(0x28, 0xb5, 0x2f, 0xfd))
)

zrd_detect_compression <- function(head) {
  for (nm in names(zrd_compression_magic)) {
    m <- zrd_compression_magic[[nm]]
    if (length(head) >= length(m) && identical(head[seq_along(m)], m)) {
      return(nm)
    }
  }
  "none"
}

# The .rda magic lines (saveload.c) and the stream headers (serialize.c).
zrd_rda_magic <- c(
  "RDX3\n" = "xdr", "RDX2\n" = "xdr", "RDA3\n" = "ascii", "RDA2\n" = "ascii",
  "RDB3\n" = "binary", "RDB2\n" = "binary"
)
zrd_stream_formats <- c("X\n" = "xdr", "B\n" = "binary", "A\n" = "ascii")

# Kind and format from the first bytes of the decompressed stream, or NULL.
zrd_detect_wrapper <- function(head) {
  if (length(head) >= 5L) {
    magic <- rawToChar(head[1:5])
    if (magic %in% names(zrd_rda_magic)) {
      return(list(kind = "rda", format = unname(zrd_rda_magic[magic]),
                  rda_version = as.integer(substr(magic, 4, 4))))
    }
  }
  if (length(head) >= 2L && head[2L] == as.raw(0x0a)) {
    fmt <- rawToChar(head[1:2])
    if (fmt %in% names(zrd_stream_formats)) {
      return(list(kind = "rds", format = unname(zrd_stream_formats[fmt]),
                  rda_version = NA_integer_))
    }
  }
  NULL
}

# The lazy-load base of `path` ("pkg" for "pkg", "pkg.rdb" or "pkg.rdx"),
# or NULL when there is no .rdb / .rdx pair.
zrd_lazyload_base <- function(path) {
  base <- sub("[.]rd[bx]$", "", path)
  if (file.exists(paste0(base, ".rdx")) && file.exists(paste0(base, ".rdb"))) {
    base
  } else {
    NULL
  }
}

zrd_check_kind <- function(kind, call) {
  if (is.null(kind)) return(NULL)
  if (!is.character(kind) || length(kind) != 1L || !kind %in% zrd_kinds) {
    zrd_invalid_argument(
      "kind",
      paste0("`kind` must be one of ", paste0('"', zrd_kinds, '"', collapse = ", "),
             ", or NULL"),
      call = call
    )
  }
  kind
}

# Opens `x` and returns a zurds_input: a list with the source (an external
# pointer, NULL for a lazy-load database until Stage 9), the kind, format,
# compression and, for a path, the path. The caller closes it with
# zrd_close().
zrd_open <- function(x, kind = NULL, call = parent.frame()) {
  kind <- zrd_check_kind(kind, call)
  input <- if (is.raw(x)) {
    zrd_open_raw(x, call)
  } else if (inherits(x, "connection")) {
    zrd_open_connection(x, call)
  } else if (is.character(x) && length(x) == 1L && !is.na(x)) {
    zrd_open_path(x, kind, call)
  } else {
    zrd_invalid_argument(
      "x", "`x` must be a path, a raw vector or a connection",
      call = call
    )
  }
  if (identical(input$kind, "lazyload")) {
    if (!is.null(kind) && kind != "lazyload") zrd_kind_mismatch(kind, input, call)
    return(input)
  }
  head <- tryCatch(
    zrd_source_call(.Call(zrd_source_peek, input$source, 5L), call),
    error = function(e) {
      zrd_close(input)
      stop(e)
    }
  )
  zrd_check_empty(input, head, call)
  wrapper <- zrd_detect_wrapper(head)
  if (is.null(wrapper)) {
    zrd_close(input)
    zrd_abort(
      "zurds_format_error",
      "not an R serialization stream: unknown magic bytes",
      call = call
    )
  }
  input[names(wrapper)] <- wrapper
  if (!is.null(kind) && kind != input$kind) zrd_kind_mismatch(kind, input, call)
  input
}

zrd_kind_mismatch <- function(kind, input, call) {
  zrd_close(input)
  zrd_abort(
    "zurds_format_error",
    paste0("`kind = \"", kind, "\"` but the input is ", input$kind),
    call = call
  )
}

# A compressed input that decompresses to nothing is broken: bzip2 and zstd
# streams cut short read as empty through gzfile().
zrd_check_empty <- function(input, head, call) {
  if (length(head) > 0L) return(invisible())
  zrd_close(input)
  if (input$compression %in% c("none", NA)) {
    zrd_abort("zurds_format_error", "the input is empty", call = call)
  }
  zrd_abort(
    "zurds_decompression_error",
    paste0("the ", input$compression, " data decompresses to nothing; ",
           "it is truncated or corrupt"),
    call = call
  )
}

# A compressed raw vector is written to a temporary file and read like a
# path, through gzfile(): memDecompress() keeps doubling its buffer on a
# truncated gzip stream when R uses zlib, until memory runs out.
zrd_open_raw <- function(x, call) {
  compression <- zrd_detect_compression(x[seq_len(min(6L, length(x)))])
  if (compression != "none") {
    tmp <- tempfile("zurds-", fileext = ".bin")
    writeBin(x, tmp)
    input <- tryCatch(
      zrd_open_path(tmp, "rds", call),
      error = function(e) {
        unlink(tmp)
        stop(e)
      }
    )
    input$path <- NA_character_
    input$tempfile <- tmp
    return(input)
  }
  list(source = .Call(zrd_source_new, x), kind = NA_character_,
       compression = compression, path = NA_character_, gzip_size = NA_real_,
       connection = NULL, owns_connection = FALSE)
}

zrd_open_connection <- function(con, call) {
  if (!isOpen(con)) {
    open(con, "rb")
    owns <- TRUE
  } else {
    if (summary(con)[["text"]] != "binary" || summary(con)[["can read"]] != "yes") {
      zrd_invalid_argument(
        "x", "a connection must be open for reading in binary mode",
        call = call
      )
    }
    owns <- FALSE
  }
  list(source = .Call(zrd_source_new, con), kind = NA_character_,
       compression = NA_character_, path = NA_character_, gzip_size = NA_real_,
       connection = con, owns_connection = owns)
}

zrd_open_path <- function(path, kind, call) {
  path <- path.expand(path)
  if (is.null(kind) || kind == "lazyload") {
    base <- zrd_lazyload_base(path)
    if (!is.null(base)) {
      return(list(source = NULL, kind = "lazyload", format = "xdr",
                  rda_version = NA_integer_,
                  compression = zrd_file_compression(paste0(base, ".rdx"), call),
                  path = base, gzip_size = NA_real_, connection = NULL,
                  owns_connection = FALSE))
    }
    if (identical(kind, "lazyload")) {
      zrd_abort(
        "zurds_io_error",
        paste0("no lazy-load database at '", path, "': need ",
               "both a .rdb and a .rdx file"),
        call = call
      )
    }
  }
  if (!file.exists(path) || dir.exists(path)) {
    zrd_abort("zurds_io_error", paste0("cannot open '", path, "': no such file"),
              path = path, call = call)
  }
  compression <- zrd_file_compression(path, call)
  zrd_check_xz_footer(compression, zrd_file_tail(path, 2L), call)
  gzip_size <- if (compression == "gzip") zrd_raw_isize(zrd_file_tail(path, 4L)) else NA_real_
  con <- tryCatch(
    gzfile(path, "rb"),
    error = function(e) {
      zrd_abort("zurds_io_error",
                paste0("cannot open '", path, "': ", conditionMessage(e)),
                path = path, call = call)
    }
  )
  list(source = .Call(zrd_source_new, con), kind = NA_character_,
       compression = compression, path = path, gzip_size = gzip_size,
       connection = con, owns_connection = TRUE)
}

zrd_file_compression <- function(path, call) {
  head <- tryCatch(
    readBin(path, "raw", 6L),
    error = function(e) {
      zrd_abort("zurds_io_error",
                paste0("cannot read '", path, "': ", conditionMessage(e)),
                path = path, call = call)
    }
  )
  zrd_detect_compression(head)
}

# The last `n` bytes of a file.
zrd_file_tail <- function(path, n) {
  size <- file.size(path)
  if (is.na(size) || size < n) return(raw())
  con <- file(path, "rb")
  on.exit(close(con))
  seek(con, size - n)
  readBin(con, "raw", n)
}

# ISIZE, the gzip trailer's uncompressed length modulo 2^32 (RFC 1952),
# from the last four bytes.
zrd_raw_isize <- function(tail) {
  if (length(tail) != 4L) return(NA_real_)
  sum(as.numeric(tail) * 256^(0:3))
}

zrd_check_gzip_size <- function(n, isize, call) {
  if (!is.na(isize) && n %% 2^32 != isize) {
    zrd_abort(
      "zurds_decompression_error",
      paste0("the gzip data is truncated: it decompresses to fewer bytes ",
             "than its trailer records"),
      call = call
    )
  }
}

# An xz stream ends with the footer magic "YZ"; one cut short does not, and
# R's decoders return what they decoded without an error.
zrd_check_xz_footer <- function(compression, tail, call) {
  if (compression == "xz" && !identical(tail, charToRaw("YZ"))) {
    zrd_abort(
      "zurds_decompression_error",
      "the xz data is truncated: it has no stream footer",
      call = call
    )
  }
}

# Evaluates a source routine's .Call, turning a codec's warning into an
# error: R's connections report corrupt compressed data by warning.
zrd_source_call <- function(expr, call) {
  withCallingHandlers(
    expr,
    warning = function(w) {
      zrd_abort(
        "zurds_decompression_error",
        paste0("cannot decompress the input: ", conditionMessage(w)),
        call = call
      )
    }
  )
}

zrd_close <- function(input) {
  if (isTRUE(input$owns_connection)) {
    try(close(input$connection), silent = TRUE)
  }
  if (!is.null(input$tempfile)) unlink(input$tempfile)
  invisible()
}

# Reads the rest of the input and returns the stream's length, raising
# zurds_decompression_error when the codec reports a problem or a gzip
# stream is shorter than its trailer says.
zrd_drain <- function(input, call = parent.frame()) {
  n <- zrd_source_call(.Call(zrd_source_drain, input$source), call)
  zrd_check_gzip_size(n, input$gzip_size, call)
  n
}

# Internal: what Stage 1 can tell about an input. Used by the tests; the
# user-facing rds_info() arrives in Stage 4.
zrd_probe <- function(x, kind = NULL) {
  input <- zrd_open(x, kind)
  on.exit(zrd_close(input))
  length <- if (is.null(input$source)) NA_real_ else zrd_drain(input)
  list(kind = input$kind, format = input$format, compression = input$compression,
       length = length)
}
