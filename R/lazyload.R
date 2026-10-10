# Lazy-load databases (design sections 2, 5 and 7): an .rdx index and an
# .rdb file of objects compressed one by one, as R CMD INSTALL, knitr and
# targets write them.

zrd_lazyload_open <- function(path, call) {
  if (!is.character(path) || length(path) != 1L || is.na(path)) {
    zrd_invalid_argument("path", "`path` must be a single path", call = call)
  }
  base <- zrd_lazyload_base(path.expand(path))
  if (is.null(base)) {
    zrd_abort("zurds_io_error",
              paste0("no lazy-load database at '", path, "': need both a .rdb and a .rdx file"),
              path = path, call = call)
  }
  # The index is a data-only .rds; zurds' own reader reads it.
  index <- tryCatch(
    rds_read_data(paste0(base, ".rdx"), kind = "rds"),
    zurds_error = function(e) {
      zrd_abort("zurds_format_error",
                paste0("'", base, ".rdx' is not a lazy-load index: ", conditionMessage(e)),
                call = call)
    }
  )
  ok <- is.list(index) && all(c("variables", "compressed") %in% names(index)) &&
    is.list(index$variables)
  vars <- if (ok) index$variables else list()
  ok <- ok && all(vapply(vars, function(v) is.numeric(v) && length(v) == 2L &&
                                  all(!is.na(v)) && all(v >= 0), TRUE))
  if (!ok || is.null(names(vars)) && length(vars)) {
    zrd_abort("zurds_format_error",
              paste0("'", base, ".rdx' is not a lazy-load index"), call = call)
  }
  list(base = base, variables = vars, compressed = index$compressed)
}

# The serialized stream of one entry: its bytes in the .rdb, decompressed
# as R_decompress1/2/3 (connections.c) lay them out. The length the entry
# declares is checked against the building limit before anything is
# decompressed. zlib, the default, goes through zurds' own bounded decoder
# (src/zrd_inflate.c): R's memDecompress() grows its buffer without bound
# on a truncated zlib stream when R lacks libdeflate. bzip2 and xz go
# through memDecompress(), which fails cleanly on them, and must give the
# declared length.
zrd_lazyload_entry <- function(db, key, name, call) {
  rdb <- paste0(db$base, ".rdb")
  size <- file.size(rdb)
  off <- key[[1]]
  len <- key[[2]]
  if (is.na(size) || off + len > size || len < 4) {
    zrd_abort("zurds_format_error",
              paste0("entry `", name, "` lies outside '", rdb, "'"), call = call)
  }
  con <- file(rdb, "rb")
  on.exit(close(con))
  seek(con, off)
  bytes <- readBin(con, "raw", len)
  declared <- sum(as.numeric(bytes[1:4]) * 256^(3:0))
  if (declared > zrd_default_limits()$max_size_build) {
    zrd_abort("zurds_size_limit",
              paste0("entry `", name, "` claims more bytes than the building limit"),
              limit = "max_size", limit_value = zrd_default_limits()$max_size_build,
              call = call)
  }
  comp <- db$compressed
  if (isTRUE(comp) || identical(as.integer(comp), 1L)) {
    type <- "gzip"
    data <- bytes[-(1:4)]
  } else {
    marker <- rawToChar(bytes[5])
    type <- switch(marker, "0" = "none", "1" = "gzip", "2" = "bzip2", "Z" = "xz", NA)
    if (is.na(type)) {
      zrd_abort("zurds_format_error",
                paste0("entry `", name, "` has an unknown compression type"), call = call)
    }
    data <- bytes[-(1:5)]
  }
  out <- if (type == "none") {
    data
  } else if (type == "gzip") {
    .Call(zrd_inflate_raw, data, declared)
  } else {
    # R writes xz entries as raw LZMA2; zrd_xz_wrap() puts them in the
    # .xz container memDecompress() reads.
    if (type == "xz") data <- .Call(zrd_xz_wrap, data, declared)
    tryCatch(memDecompress(data, type = type), error = function(e) NULL,
             warning = function(w) NULL)
  }
  if (is.character(out)) {
    zrd_abort("zurds_decompression_error",
              paste0("entry `", name, "` cannot be decompressed: ", out), call = call)
  }
  if (is.null(out) || length(out) != declared) {
    zrd_abort("zurds_decompression_error",
              paste0("entry `", name, "` cannot be decompressed: it is truncated or corrupt"),
              call = call)
  }
  out
}

#' Table of contents of a lazy-load database
#'
#' Lists the objects of a lazy-load database, such as an installed
#' package's `R/<pkg>.rdb` and `.rdx`, or a knitr or targets cache, without
#' loading any of them. Unlike [lazyLoad()], nothing is evaluated: an entry
#' that is a promise is reported as one, never forced.
#'
#' @param path The database's path without extension, as [lazyLoad()]
#'   takes it, or the path of its `.rdb` or `.rdx` file.
#' @return A data frame with one row per object, with the columns of
#'   [rds_toc()]: `name`, `type`, `class`, `length`, `dim`, `size` (the
#'   bytes of its decompressed stream), `offset` (where its entry starts in
#'   the `.rdb`) and `risky`.
#' @family lazy-load
#' @export
#' @examples
#' db <- file.path(system.file("extdata", package = "zurds"), "lazy")
#' rds_lazyload_toc(db)
#'
#' # An installed package's own database:
#' stats <- file.path(system.file("R", package = "stats"), "stats")
#' head(rds_lazyload_toc(stats))
rds_lazyload_toc <- function(path) {
  call <- sys.call()
  db <- zrd_lazyload_open(path, call)
  limits <- zrd_limits(zrd_default_limits()$max_depth, zrd_default_limits()$max_items,
                       zrd_default_limits()$max_size_build, call)
  rows <- lapply(names(db$variables), function(name) {
    key <- db$variables[[name]]
    stream <- zrd_lazyload_entry(db, key, name, call)
    input <- zrd_open(stream, "rds", call)
    on.exit(zrd_close(input))
    res <- zrd_source_call(.Call(zrd_toc_walk, input$source, 0L, limits, TRUE), call)
    if (res$status != "ZRD_OK") zrd_walk_fail(res, input, limits, call)
    row <- zrd_toc_frame(res$objects, 0)
    row$name <- name
    row$size <- length(stream)
    row$offset <- key[[1]]
    row
  })
  out <- if (length(rows)) do.call(rbind, rows) else
    zrd_toc_frame(list(name = character(), type = integer(), flags = integer(),
                       length = numeric(), offset = numeric(), end = numeric(),
                       class = character(), dim = list(), nrow = numeric(),
                       altrep = character(), altrep_length = numeric(),
                       cells = numeric(), risky = logical()), 0)
  rownames(out) <- NULL
  out
}

#' Extract one object from a lazy-load database
#'
#' Reads one object of a lazy-load database with the data-only reader of
#' [rds_read_data()]. A promise, a closure or anything else that could run
#' code is refused, never evaluated.
#'
#' @inheritParams rds_lazyload_toc
#' @param name The object's name.
#' @return The object.
#' @section Conditions: A name not in the database is an error of class
#'   `zurds_not_found`; content the data-only reader refuses is
#'   `zurds_unsafe_content`.
#' @family lazy-load
#' @export
#' @examples
#' db <- file.path(system.file("extdata", package = "zurds"), "lazy")
#' rds_lazyload_extract(db, "table")
#' try(rds_lazyload_extract(db, "helper"))
rds_lazyload_extract <- function(path, name) {
  call <- sys.call()
  if (!is.character(name) || length(name) != 1L || is.na(name)) {
    zrd_invalid_argument("name", "`name` must be a single name", call = call)
  }
  db <- zrd_lazyload_open(path, call)
  key <- db$variables[[name]]
  if (is.null(key) || !name %in% names(db$variables)) {
    zrd_abort("zurds_not_found", paste0("no object `", name, "` in the database"),
              what = name, call = call)
  }
  stream <- zrd_lazyload_entry(db, key, name, call)
  # Errors name this call, not the reader underneath.
  tryCatch(
    rds_read_data(stream, kind = "rds"),
    zurds_error = function(e) {
      e$call <- call
      stop(e)
    }
  )
}
