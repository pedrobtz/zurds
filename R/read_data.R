# rds_read_data(): the data-only reader (design sections 5, 8 and 9).

# This session's native encoding, as R names it in a version 3 header.
zrd_session_encoding <- function() {
  cs <- l10n_info()[["codeset"]]
  if (is.null(cs) || !nzchar(cs)) {
    if (isTRUE(l10n_info()[["UTF-8"]])) "UTF-8" else ""
  } else {
    cs
  }
}

#' Read a data-only R serialization file safely
#'
#' Reads a file written by `saveRDS()`, `save()` or `serialize()` with a
#' reader written in C that builds only `NULL`, logical, integer, double,
#' complex, character, raw and list vectors, with attributes of those
#' types. It never calls [unserialize()], [eval()] or anything from the
#' file, and it refuses everything that could run code or reach outside
#' the file. For a data-only file the result is [identical()] to what
#' [readRDS()] or [load()] returns.
#'
#' Classes are kept as attributes; no method of a class is called while
#' the object is built. Using the result may dispatch to methods you have
#' loaded: that is your code, not the file's.
#'
#' Base R's compact sequences (`1:10`), deferred strings and wrappers are
#' built as the ordinary vectors they stand for. Strings keep their
#' declared encoding; strings written in another session's native encoding
#' (named by a version 3 header) are translated to UTF-8.
#'
#' @inheritParams rds_info
#' @param strict When `TRUE`, the default, anything that cannot be built
#'   (closures, promises, environments, language objects, S4 objects, ...)
#'   is an error of class `zurds_unsafe_content`, and so is a string
#'   marked UTF-8 that is not valid UTF-8 (`zurds_encoding_error`). When
#'   `FALSE`, each such item is replaced by `NULL`, with one warning per
#'   kind.
#' @return The object. For an `.rda` file, a named list of its objects, in
#'   the order `save()` wrote them.
#' @section Conditions: A `zurds_unsafe_content` error carries `findings`,
#'   as [rds_scan()] reports them, and `refused`, a data frame of the
#'   items that could not be built (`path`, `type`).
#' @family reading
#' @export
#' @examples
#' f <- tempfile(fileext = ".rds")
#' saveRDS(head(mtcars), f)
#' identical(rds_read_data(f), readRDS(f))
#'
#' saveRDS(list(x = 1:3, f = function() "hello"), f)
#' try(rds_read_data(f))
#' rds_read_data(f, strict = FALSE)
rds_read_data <- function(x, strict = TRUE, kind = NULL) {
  call <- sys.call()
  if (!is.logical(strict) || length(strict) != 1L || is.na(strict)) {
    zrd_invalid_argument("strict", "`strict` must be TRUE or FALSE", call = call)
  }
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  if (identical(input$kind, "lazyload")) {
    zrd_invalid_argument(
      "x", "a lazy-load database is read entry by entry; see rds_lazyload_extract()",
      call = call
    )
  }
  defaults <- zrd_default_limits()
  limits <- zrd_limits(defaults$max_depth, defaults$max_items,
                       defaults$max_size_build, call)
  res <- zrd_build_call(input, limits, strict, call)
  zrd_build_result(res, input, limits, strict, call)
}

# Runs the builder over the whole stream, or (`target` >= 0) only over the
# item that begins at that offset. An error R raises while setting an
# attribute (a dim that does not match, say) is the file's fault: a parse
# error.
zrd_build_call <- function(input, limits, strict, call, target = -1) {
  tryCatch(
    zrd_source_call(
      .Call(zrd_build_walk, input$source, zrd_stream_skip(input), limits,
            identical(input$kind, "rda"), strict, zrd_session_encoding(),
            as.double(target)),
      call
    ),
    zurds_error = function(e) stop(e),
    error = function(e) {
      zrd_abort("zurds_parse_error",
                paste0("cannot build the object: ", conditionMessage(e)),
                offset = NA_real_, path = NA_character_, call = call)
    }
  )
}

zrd_build_result <- function(res, input, limits, strict, call) {
  if (isTRUE(res$encoding_error)) {
    zrd_abort(
      "zurds_encoding_error",
      paste0("a string marked UTF-8 is not valid UTF-8 at byte ",
             format(res$encoding_offset, scientific = FALSE),
             if (nzchar(res$encoding_path)) paste0(" (", res$encoding_path, ")") else ""),
      offset = res$encoding_offset, path = res$encoding_path, call = call
    )
  }
  if (res$status != "ZRD_OK") zrd_walk_fail(res, input, limits, call)
  findings <- zrd_findings_frame(res$findings)
  type <- unname(c(zrd_typeof, `255` = "reference")[as.character(res$refused_type)])
  type[is.na(type)] <- paste0("type ", res$refused_type[is.na(type)])
  refused <- data.frame(path = res$refused_path, type = type, stringsAsFactors = FALSE)
  unsafe <- findings[findings$severity %in% c("executes", "reaches_out"), , drop = FALSE]
  if (nrow(refused) == 0L && nrow(unsafe) == 0L) return(res$value)
  if (strict) {
    what <- unique(c(unsafe$finding, refused$type))
    zrd_abort(
      "zurds_unsafe_content",
      paste0("the file holds what the data-only reader refuses: ",
             paste(what, collapse = ", "),
             ". Use rds_scan() to see where, or strict = FALSE to read it with ",
             "those items replaced by NULL."),
      findings = findings, refused = refused, call = call
    )
  }
  for (k in unique(refused$type)) {
    warning(structure(
      class = c("zurds_dropped", "warning", "condition"),
      list(message = paste0("replaced ", sum(refused$type == k), " ", k,
                            " item(s) by NULL"),
           call = call, type = k)
    ))
  }
  res$value
}
