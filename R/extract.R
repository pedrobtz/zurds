# rds_extract(): one object, list element or data frame column, without
# building the rest (design sections 2, 4 and 5).

#' Extract one object or element from an R serialization file
#'
#' Builds only the item asked for: one object of an `.rda`, or one element
#' of a list or one column of a data frame, at any depth. The rest of the
#' file is walked over, not built. The item is built by the data-only
#' reader of [rds_read_data()], so it can hold only data.
#'
#' The format has no index, so each step of `what` reads the file up to
#' the item; for an `.rda`, the objects' offsets from [rds_toc()] are
#' reused when the same file is read again.
#'
#' @inheritParams rds_info
#' @param what Where the item is. For an `.rda`, the object's name, then
#'   optionally a path inside it; for anything else, a path inside the
#'   object. A path is a character vector of names, such as
#'   `c("fit", "coefficients")`, or a list mixing names and positions, such
#'   as `list("models", 3)`.
#' @param use_r When `FALSE`, the default, the item is built by the
#'   data-only reader and anything that can run code or reach outside the
#'   file is refused. When `TRUE`, the item, and only the item, is handed to
#'   R's [unserialize()], so it may hold closures, environments or anything
#'   else R can read; its scan by [rds_scan()]'s rules is attached to the
#'   result as the attribute `"zurds_findings"` (unless the result is
#'   `NULL` or a symbol, which cannot carry one). Only use this on items
#'   you trust: loading them can run code. An item that refers to an
#'   environment written before it, outside the item, cannot be read alone
#'   and is refused.
#' @return The item.
#' @section Conditions: A name or position not in the file is an error of
#'   class `zurds_not_found`; content the data-only reader refuses is
#'   `zurds_unsafe_content`, as for [rds_read_data()].
#' @family reading
#' @export
#' @examples
#' f <- system.file("extdata", "objects.rda", package = "zurds")
#' rds_extract(f, "cars")
#' rds_extract(f, c("cars", "mpg"))
#'
#' m <- system.file("extdata", "model.rds", package = "zurds")
#' rds_extract(m, list("fit", "coefficients"))
#'
#' # The model as a whole holds a formula and its environment: refused by the
#' # data-only reader, read by R itself when you trust the file.
#' try(rds_extract(m, "fit"))
#' fit <- rds_extract(m, "fit", use_r = TRUE)
#' attr(fit, "zurds_findings")
rds_extract <- function(x, what, use_r = FALSE, kind = NULL) {
  call <- sys.call()
  if (!is.logical(use_r) || length(use_r) != 1L || is.na(use_r)) {
    zrd_invalid_argument("use_r", "`use_r` must be TRUE or FALSE", call = call)
  }
  steps <- zrd_check_path(what, call)
  if (inherits(x, "connection")) {
    zrd_invalid_argument(
      "x", "rds_extract() reads the input more than once: give a path or a raw vector",
      call = call
    )
  }
  first <- zrd_open(x, kind, call)
  kind <- first$kind
  skip <- zrd_stream_skip(first)
  zrd_close(first)
  if (identical(kind, "lazyload")) {
    zrd_invalid_argument("x", "for a lazy-load database, use rds_lazyload_extract()",
                         call = call)
  }
  defaults <- zrd_default_limits()
  limits <- zrd_limits(defaults$max_depth, defaults$max_items,
                       defaults$max_size_build, call)
  # Where the item begins in the stream: -1 is the root.
  target <- -1
  label <- character(0)
  if (identical(kind, "rda")) {
    name <- steps[[1]]
    if (!is.character(name)) {
      zrd_invalid_argument("what", "for an .rda, `what` starts with an object's name",
                           call = call)
    }
    toc <- zrd_cached_toc(x, call)
    i <- match(name, toc$name)
    if (is.na(i)) {
      zrd_abort("zurds_not_found", paste0("no object `", name, "` in the file"),
                what = name, call = call)
    }
    target <- toc$offset[i] - skip
    label <- paste0("$", name)
    steps <- steps[-1]
  }
  for (step in steps) {
    target <- zrd_child_offset(x, kind, limits, target, step, label, call)
    label <- c(label, if (is.character(step)) paste0("$", step) else paste0("[[", step, "]]"))
  }
  if (use_r) return(zrd_extract_with_r(x, kind, limits, target, call))
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  res <- zrd_build_call(input, limits, TRUE, call, target = target)
  zrd_build_result(res, input, limits, TRUE, call)
}

zrd_check_path <- function(what, call) {
  bad <- function() {
    zrd_invalid_argument(
      "what",
      "`what` must be a character vector of names, or a list of names and positions",
      call = call
    )
  }
  steps <- if (is.character(what)) as.list(what) else if (is.list(what)) what else bad()
  if (!length(steps)) bad()
  for (s in steps) {
    ok <- length(s) == 1L && !is.na(s) &&
      (is.character(s) || (is.numeric(s) && s >= 1 && s == trunc(s)))
    if (!ok) bad()
  }
  steps
}

# The table of contents of an .rda, from the cache when the file has not
# changed (rds_toc() fills it).
zrd_cached_toc <- function(x, call) {
  if (is.character(x)) {
    info <- file.info(path.expand(x), extra_cols = FALSE)
    key <- paste(normalizePath(path.expand(x), mustWork = FALSE), info$size,
                 format(as.numeric(info$mtime), digits = 15), sep = "\r")
    if (exists(key, envir = zrd_toc_cache, inherits = FALSE)) {
      return(get(key, envir = zrd_toc_cache))
    }
  }
  rds_toc(x)
}

# The offset of child `step` of the item at `target`.
zrd_child_offset <- function(x, kind, limits, target, step, label, call) {
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  res <- zrd_source_call(
    .Call(zrd_children_walk, input$source, zrd_stream_skip(input), limits, as.double(target)),
    call
  )
  if (res$status != "ZRD_OK") zrd_walk_fail(res, input, limits, call)
  where <- if (length(label)) paste(label, collapse = "") else "the object"
  if (!res$type %in% c(19L, 20L)) {
    zrd_abort("zurds_not_found",
              paste0(where, " is not a list, so it has no element ", format(step)),
              what = step, call = call)
  }
  i <- if (is.character(step)) match(step, res$names) else as.integer(step)
  if (is.na(i) || i > length(res$offsets)) {
    zrd_abort("zurds_not_found", paste0(where, " has no element ", format(step)),
              what = step, call = call)
  }
  res$offsets[[i]]
}

# use_r = TRUE: the item's bytes, made standalone, to unserialize().
zrd_extract_with_r <- function(x, kind, limits, target, call) {
  input <- zrd_open(x, kind, call)
  res <- tryCatch(
    zrd_source_call(
      .Call(zrd_extract_patch, input$source, zrd_stream_skip(input), limits,
            as.double(target)),
      call
    ),
    finally = zrd_close(input)
  )
  if (res$status != "ZRD_OK") zrd_walk_fail(res, input, limits, call)
  if (isTRUE(res$outside)) {
    zrd_abort(
      "zurds_unsafe_content",
      paste0("the item refers to an environment or other object defined ",
             "outside it (at ", res$outside_path, "), which cannot be read alone"),
      findings = zrd_findings_frame(res$findings), refused = NULL, call = call
    )
  }
  bytes <- zrd_read_stream_bytes(x, kind, call, res$item_end)
  head <- bytes[seq_len(res$header_end)]
  body <- zrd_apply_patches(bytes, res)
  value <- unserialize(c(head, body))
  findings <- zrd_findings_frame(res$findings)
  # NULL and symbols cannot carry attributes.
  if (!is.null(value) && !is.symbol(value)) attr(value, "zurds_findings") <- findings
  value
}

# The first `n` bytes of the decompressed stream (after an .rda's magic).
zrd_read_stream_bytes <- function(x, kind, call, n) {
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  zrd_source_call(
    .Call(zrd_source_bytes, input$source, zrd_stream_skip(input), as.double(n)),
    call
  )
}

# The item's bytes with each reference patch applied.
zrd_apply_patches <- function(bytes, res) {
  out <- list()
  pos <- res$item_begin
  for (k in seq_along(res$patch_offset)) {
    off <- res$patch_offset[k]
    out[[length(out) + 1L]] <- bytes[seq.int(pos + 1, length.out = off - pos)]
    out[[length(out) + 1L]] <- res$patch_bytes[[k]]
    pos <- off + res$patch_length[k]
  }
  out[[length(out) + 1L]] <- bytes[seq.int(pos + 1, length.out = res$item_end - pos)]
  unlist(out)
}
