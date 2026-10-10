# rds_toc(): the objects of an .rda without building any (design section 5).

# typeof() names by SEXPTYPE.
zrd_typeof <- c(
  `0` = "NULL", `1` = "symbol", `2` = "pairlist", `3` = "closure",
  `4` = "environment", `5` = "promise", `6` = "language", `7` = "special",
  `8` = "builtin", `9` = "char", `10` = "logical", `13` = "integer",
  `14` = "double", `15` = "complex", `16` = "character", `17` = "...",
  `19` = "list", `20` = "expression", `21` = "bytecode",
  `22` = "externalptr", `23` = "weakref", `24` = "raw", `25` = "object",
  # Pseudo-types stand for values of these types.
  `254` = "NULL", `253` = "environment", `242` = "environment",
  `241` = "environment", `250` = "environment", `249` = "environment",
  `248` = "environment", `247` = "environment", `252` = "symbol",
  `251` = "symbol"
)

# What base R's ALTREP classes are, by class name.
zrd_altrep_typeof <- c(
  compact_intseq = "integer", compact_realseq = "double",
  deferred_string = "character", wrap_integer = "integer",
  wrap_real = "double", wrap_string = "character", wrap_logical = "logical",
  wrap_complex = "complex", wrap_raw = "raw", wrap_list = "list"
)

# The offsets of the last tables read, by path, size and modification
# time, for rds_extract() (design section 4).
zrd_toc_cache <- new.env(parent = emptyenv())

zrd_cache_key <- function(input) {
  if (is.na(input$path)) return(NULL)
  info <- file.info(input$path, extra_cols = FALSE)
  paste(normalizePath(input$path, mustWork = FALSE), info$size,
        format(as.numeric(info$mtime), digits = 15), sep = "\r")
}

#' Table of contents of an .rda file
#'
#' Lists the objects in a file written by `save()`, with their types,
#' classes, lengths, dimensions and where they lie in the decompressed
#' stream, without building any of them. Nothing in the file is evaluated,
#' and memory stays small however large the objects are.
#'
#' @inheritParams rds_info
#' @param x A path, raw vector or connection holding an `.rda` file.
#' @return A data frame with one row per object, in the order `save()`
#'   wrote them:
#'   * `name`: the object's name.
#'   * `type`: its type, as [typeof()] names it.
#'   * `class`: its first class, or `NA` when it has no class attribute.
#'   * `length`: its length, as [length()] reports it for vectors and
#'     lists; `NA` where the stream does not record it (environments,
#'     functions, ALTREP objects).
#'   * `dim`: its dimensions as text, `"3 x 4"`; for a data frame, rows
#'     by columns; `NA` otherwise.
#'   * `size`: the bytes it takes in the decompressed stream.
#'   * `offset`: where it starts in the decompressed file, in bytes.
#'   * `risky`: whether it holds anything that can run code or reach
#'     outside the file: a finding of [rds_scan()] of severity
#'     `"executes"` or `"reaches_out"`.
#' @family inspection
#' @export
#' @examples
#' f <- tempfile(fileext = ".rda")
#' x <- 1:10
#' cars <- mtcars
#' m <- matrix(0, 3, 4)
#' save(x, cars, m, file = f)
#' rds_toc(f)
rds_toc <- function(x, kind = NULL) {
  call <- sys.call()
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  if (!identical(input$kind, "rda")) {
    zrd_invalid_argument(
      "x",
      paste0("`x` is an .", input$kind, " file, which holds a single object; ",
             "rds_toc() lists the objects of an .rda file. Use rds_str() to ",
             "see its structure."),
      call = call
    )
  }
  limits <- zrd_limits(zrd_default_limits()$max_depth, zrd_default_limits()$max_items,
                       zrd_default_limits()$max_size, call)
  skip <- zrd_stream_skip(input)
  res <- zrd_source_call(.Call(zrd_toc_walk, input$source, skip, limits), call)
  if (res$status != "ZRD_OK") zrd_walk_fail(res, input, limits, call)
  toc <- zrd_toc_frame(res$objects, skip)
  key <- zrd_cache_key(input)
  if (!is.null(key)) assign(key, toc, envir = zrd_toc_cache)
  toc
}

zrd_toc_frame <- function(o, skip) {
  type <- unname(zrd_typeof[as.character(o$type)])
  # ALTREP objects take the type their class stands for.
  alt <- o$type == 238L
  type[alt] <- unname(zrd_altrep_typeof[o$altrep[alt]])
  # S4 objects carry the S4 bit (gp bit 4) in their levels.
  s4 <- o$type == 25L & bitwAnd(bitwShiftR(o$flags, 12L), 16L) != 0L
  type[s4] <- "S4"
  # Vectors record their length; a pairlist, call or `...` has one per
  # cell; an ALTREP object's is in its state; NULL has none; functions,
  # symbols and other single values have length 1. An environment's would
  # need its frame counted, and is left NA.
  length <- ifelse(o$length >= 0, o$length, NA_real_)
  pl <- o$type %in% c(2L, 6L, 17L)
  length[pl] <- o$cells[pl]
  length[alt] <- ifelse(o$altrep_length[alt] >= 0, o$altrep_length[alt], NA_real_)
  length[o$type == 254L] <- 0
  length[o$type %in% c(1L, 3L, 5L, 7L, 8L, 21L, 22L, 23L, 25L)] <- 1
  dim <- vapply(seq_along(o$dim), function(i) {
    d <- o$dim[[i]]
    if (length(d)) return(paste(d, collapse = " x "))
    if (identical(o$class[i], "data.frame") && o$nrow[i] >= 0) {
      return(paste(format(o$nrow[i], scientific = FALSE), "x",
                   format(o$length[i], scientific = FALSE)))
    }
    NA_character_
  }, character(1))
  data.frame(
    name = o$name, type = type, class = o$class, length = length,
    dim = dim, size = o$end - o$offset, offset = o$offset + skip,
    risky = o$risky, stringsAsFactors = FALSE
  )
}
