# The condition hierarchy of design section 10. The class is the contract:
# callers branch on it and on the fields below, never on the message.

#' Conditions raised by zurds
#'
#' Every error zurds raises carries a condition class, so it can be caught
#' by kind rather than by matching the message, which may change. Every
#' class below inherits from `zurds_error`.
#'
#' \describe{
#'   \item{`zurds_invalid_argument`}{An argument was unusable. Carries
#'     `arg`, the argument at fault.}
#'   \item{`zurds_format_error`}{The input is not a file R wrote: bad
#'     magic, an unknown format character or an unsupported version.}
#'   \item{`zurds_parse_error`}{The stream is malformed. Carries `offset`,
#'     the byte offset in the decompressed stream, and `path`, where in the
#'     object the walk was.}
#'   \item{`zurds_unsafe_content`}{The file holds something the data-only
#'     reader refuses. Carries `findings`, as `rds_scan()` would report
#'     them.}
#'   \item{`zurds_not_found`}{A name or path is not in the file.}
#'   \item{`zurds_encoding_error`}{A string is not valid in its declared
#'     encoding.}
#'   \item{`zurds_limit_error`}{A limit was reached. The subclasses
#'     `zurds_depth_limit`, `zurds_size_limit` and `zurds_item_limit` name
#'     which one. Carries `limit`, the argument's name, and `limit_value`.}
#'   \item{`zurds_io_error`}{Reading the input failed.}
#'   \item{`zurds_decompression_error`}{The input could not be
#'     decompressed, for example because it is truncated.}
#' }
#'
#' @name zurds-conditions
#' @examples
#' f <- system.file("extdata", "promise.rds", package = "zurds")
#' cnd <- tryCatch(rds_read_data(f), zurds_unsafe_content = function(e) e)
#' class(cnd)
#' cnd$findings
#'
#' tryCatch(rds_info(charToRaw("not R")), zurds_format_error = conditionMessage)
NULL

# Each class and the classes it inherits from below zurds_error.
zrd_condition_classes <- list(
  zurds_invalid_argument = "zurds_invalid_argument",
  zurds_format_error = "zurds_format_error",
  zurds_parse_error = "zurds_parse_error",
  zurds_unsafe_content = "zurds_unsafe_content",
  zurds_not_found = "zurds_not_found",
  zurds_encoding_error = "zurds_encoding_error",
  zurds_limit_error = "zurds_limit_error",
  zurds_depth_limit = c("zurds_depth_limit", "zurds_limit_error"),
  zurds_size_limit = c("zurds_size_limit", "zurds_limit_error"),
  zurds_item_limit = c("zurds_item_limit", "zurds_limit_error"),
  zurds_io_error = "zurds_io_error",
  zurds_decompression_error = "zurds_decompression_error"
)

# Builds a zurds condition of `class` (a name in zrd_condition_classes)
# with fields `...`.
zrd_condition <- function(class, message, ..., call = NULL) {
  chain <- zrd_condition_classes[[class]]
  if (is.null(chain)) {
    stop("internal error: unknown condition class ", class, call. = FALSE)
  }
  structure(
    class = c(chain, "zurds_error", "error", "condition"),
    list(message = message, call = call, ...)
  )
}

zrd_abort <- function(class, message, ..., call = NULL) {
  stop(zrd_condition(class, message, ..., call = call))
}

zrd_invalid_argument <- function(arg, message, call = NULL) {
  zrd_abort("zurds_invalid_argument", message, arg = arg, call = call)
}
