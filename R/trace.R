# The walk's events as a table, and as a tree of items built from them
# (src/zrd_trace.c). rds_str() and the tests are built on these.

# Roles, as zrd_role in src/zrd_walk.h numbers them.
zrd_roles <- c(
  "root", "element", "attr", "tag", "car", "cdr", "cell", "printname",
  "enclos", "frame", "hashtab", "prot", "exttag", "altrep_info",
  "altrep_state", "altrep_attr", "bc_code", "bc_const", "bc_lang"
)

# Walks `x` and returns its events down to `max_depth`: a list with the
# walk's result and `events`, a data frame with one row per BEGIN (kind 1)
# or END (kind 2) event.
zrd_trace <- function(x, kind = NULL, max_depth = 6L, max_events = 1e6,
                      limits = zrd_default_limits(), call = NULL) {
  lim <- zrd_limits(limits$max_depth, limits$max_items, limits$max_size, call)
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  if (identical(input$kind, "lazyload")) {
    zrd_invalid_argument("x", "a lazy-load database has no single stream to walk",
                         call = call)
  }
  res <- zrd_source_call(
    .Call(zrd_walk_trace, input$source, zrd_stream_skip(input), lim,
          as.integer(max_depth), as.double(max_events)),
    call
  )
  if (res$status != "ZRD_OK") zrd_walk_fail(res, input, lim, call)
  ev <- res$events
  res$events <- data.frame(
    kind = ev$kind, type = ev$type, flags = ev$flags, depth = ev$depth,
    role = zrd_roles[ev$role + 1L], index = ev$index, length = ev$length,
    offset = ev$offset, name = ev$name, data = ev$data,
    data_complete = ev$data_complete, stringsAsFactors = FALSE
  )
  res$kind <- input$kind
  res
}

# The items of an event table as a tree: one row per BEGIN event, with
# `parent` (a row number, 0 at the top), `end` (the offset just past the
# item, NA if its END was not recorded) and the string data of its END.
zrd_event_tree <- function(ev) {
  begin <- which(ev$kind == 1L)
  node <- integer(nrow(ev))
  node[begin] <- seq_along(begin)
  parent <- integer(length(begin))
  end <- rep(NA_real_, length(begin))
  data <- rep(NA_character_, length(begin))
  complete <- rep(TRUE, length(begin))
  stack <- integer(0)
  for (i in seq_len(nrow(ev))) {
    if (ev$kind[i] == 1L) {
      parent[node[i]] <- if (length(stack)) stack[length(stack)] else 0L
      stack <- c(stack, node[i])
    } else if (length(stack)) {
      top <- stack[length(stack)]
      stack <- stack[-length(stack)]
      end[top] <- ev$offset[i]
      data[top] <- ev$data[i]
      complete[top] <- ev$data_complete[i]
    }
  }
  tree <- ev[begin, c("type", "flags", "depth", "role", "index", "length", "offset", "name")]
  tree$parent <- parent
  tree$end <- end
  tree$data <- data
  tree$data_complete <- complete
  rownames(tree) <- NULL
  tree
}
