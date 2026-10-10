# Running the walk (design sections 4, 6 and 11) and turning its status
# into a classed condition.

# Checks the limit arguments and returns them as a list for C.
zrd_limits <- function(max_depth, max_items, max_size, call) {
  check <- function(value, name, upper) {
    if (!is.numeric(value) || length(value) != 1L || is.na(value) ||
        value < 1 || value > upper || value != trunc(value)) {
      zrd_invalid_argument(
        name,
        paste0("`", name, "` must be a whole number from 1 to ",
               format(upper, scientific = FALSE, big.mark = ",")),
        call = call
      )
    }
  }
  check(max_depth, "max_depth", 1e6)
  check(max_items, "max_items", 2^52)
  check(max_size, "max_size", 2^62)
  list(max_depth = as.integer(max_depth), max_items = as.double(max_items),
       max_size = as.double(max_size))
}

# The bytes before the stream: an .rda's magic line.
zrd_stream_skip <- function(input) {
  if (identical(input$kind, "rda")) 5L else 0L
}

# Status names from src/zrd_walk.c -> condition class and message stem.
zrd_walk_status <- list(
  ZRD_ERR_TRUNCATED = c("zurds_parse_error", "the stream is truncated"),
  ZRD_ERR_FORMAT = c("zurds_format_error", "invalid stream header"),
  ZRD_ERR_UNSUPPORTED = c("zurds_parse_error", "unsupported item"),
  ZRD_ERR_PARSE = c("zurds_parse_error", "malformed stream"),
  ZRD_ERR_BAD_REF = c("zurds_parse_error", "malformed stream"),
  ZRD_ERR_DEPTH_LIMIT = c("zurds_depth_limit", "limit reached"),
  ZRD_ERR_ITEM_LIMIT = c("zurds_item_limit", "limit reached"),
  ZRD_ERR_SIZE_LIMIT = c("zurds_size_limit", "limit reached"),
  ZRD_ERR_NOMEM = c("zurds_io_error", "out of memory")
)

zrd_limit_names <- c(
  zurds_depth_limit = "max_depth", zurds_item_limit = "max_items",
  zurds_size_limit = "max_size"
)

# Raises the condition for a failed walk.
zrd_walk_fail <- function(res, input, limits, call) {
  # A stream cut short inside a compressed file is a decompression problem
  # if the codec says so; zrd_drain() raises it.
  if (res$status == "ZRD_ERR_TRUNCATED" && !is.null(input$source)) {
    zrd_drain(input, call)
  }
  entry <- zrd_walk_status[[res$status]]
  if (is.null(entry)) entry <- c("zurds_parse_error", "malformed stream")
  class <- entry[[1]]
  where <- paste0(
    " at byte ", format(res$offset, scientific = FALSE),
    if (nzchar(res$path)) paste0(" (", res$path, ")") else ""
  )
  msg <- paste0(entry[[2]], where, ": ", res$detail)
  if (class %in% names(zrd_limit_names)) {
    lim <- zrd_limit_names[[class]]
    zrd_abort(class, msg, limit = lim, limit_value = limits[[lim]],
              offset = res$offset, path = res$path, call = call)
  }
  zrd_abort(class, msg, offset = res$offset, path = res$path, call = call)
}

# Internal: walks the stream in `x` and returns what the walk saw. Used by
# the tests until the user-facing functions arrive.
zrd_walk_info <- function(x, kind = NULL,
                           max_depth = zrd_default_limits()$max_depth,
                           max_items = zrd_default_limits()$max_items,
                           max_size = zrd_default_limits()$max_size,
                           call = NULL) {
  limits <- zrd_limits(max_depth, max_items, max_size, call)
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  if (identical(input$kind, "lazyload")) {
    zrd_invalid_argument("x", "a lazy-load database has no single stream to walk",
                         call = call)
  }
  res <- zrd_source_call(
    .Call(zrd_walk_stats, input$source, zrd_stream_skip(input), limits),
    call
  )
  if (res$status != "ZRD_OK") zrd_walk_fail(res, input, limits, call)
  res
}
