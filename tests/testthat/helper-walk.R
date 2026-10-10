# The number of items serialize() writes for a data object, counted from
# the R object by the rules of serialize.c's WriteItem: one per vector,
# string, pairlist cell and end-of-pairlist marker, two for a symbol's
# first appearance (SYMSXP and its name), one for each later appearance
# (REFSXP). An independent oracle for the walk's item count.
expected_items <- function(x) {
  seen <- new.env(parent = emptyenv())
  count <- 0
  sym <- function(name) {
    count <<- count + if (exists(name, envir = seen, inherits = FALSE)) 1 else 2
    assign(name, TRUE, envir = seen)
  }
  pairlist_items <- function(values, tags) {
    for (i in seq_along(values)) {
      count <<- count + 1
      if (!is.null(tags) && nzchar(tags[[i]])) sym(tags[[i]])
      item(values[[i]])
    }
    count <<- count + 1 # NILVALUE_SXP ends the pairlist
  }
  attrs <- function(x) {
    a <- attributes(x)
    if (length(a)) pairlist_items(a, names(a))
  }
  item <- function(x) {
    if (is.null(x)) {
      count <<- count + 1
    } else if (is.symbol(x)) {
      sym(as.character(x))
    } else if (is.pairlist(x)) {
      pairlist_items(as.list(x), names(x))
    } else if (is.character(x)) {
      count <<- count + 1 + length(x)
      attrs(x)
    } else if (is.list(x) || is.expression(x)) {
      count <<- count + 1
      for (el in x) item(el)
      attrs(x)
    } else if (is.atomic(x)) {
      count <<- count + 1
      attrs(x)
    } else {
      stop("expected_items() counts data objects only, not ", typeof(x))
    }
  }
  item(x)
  count
}

# The fixture objects made of data items only: walked since Stage 2.
data_fixture_objects <- c(
  "null", "logical", "integer", "double", "complex", "character", "bytes",
  "raw", "list", "named_list", "data_frame", "factor", "matrix", "pairlist",
  "symbol"
)

# A hand-built XDR stream: the version 3 header, then `...` as big-endian
# integers (numbers) or bytes (raw vectors), in order.
xdr_stream <- function(...) {
  int <- function(x) writeBin(as.integer(x), raw(), endian = "big")
  parts <- lapply(list(...), function(p) if (is.raw(p)) p else int(p))
  c(charToRaw("X\n"), int(c(3, 263681, 197888, 5)), charToRaw("UTF-8"),
    unlist(parts))
}

# Flags for a type with the attribute bit set or not.
xdr_flags <- function(type, attr = FALSE) type + if (attr) 512 else 0
