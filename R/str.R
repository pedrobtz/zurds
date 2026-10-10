# rds_str(): a str()-like summary built from the walk's events, with no
# object built (design sections 2 and 5).

zrd_type_abbr <- c(
  `10` = "logi", `13` = "int", `14` = "num", `15` = "cplx", `16` = "chr",
  `24` = "raw"
)

#' Structure of an R serialization file
#'
#' Prints a compact summary of what a file holds, in the style of
#' [utils::str()], from the serialized stream alone: no object is built,
#' no code in the file runs, and a file R would refuse to load can still
#' be summarised.
#'
#' Values are not shown, only types, lengths, names and classes. Elements
#' of a list are listed up to `max_level` levels down, at most 99 per
#' list. Closures, environments, promises and other items that could run
#' code or reach outside the file are named, never followed.
#'
#' @inheritParams rds_info
#' @param max_level How many levels of nested lists to show; 0 shows only
#'   the top.
#' @return The lines of the summary, invisibly. They are printed as a side
#'   effect.
#' @family inspection
#' @export
#' @examples
#' rds_str(system.file("extdata", "model.rds", package = "zurds"))
#'
#' rds_str(system.file("extdata", "objects.rda", package = "zurds"), max_level = 1)
#'
#' # A file R refuses to load can still be summarised:
#' rds_str(system.file("extdata", "promise.rds", package = "zurds"))
rds_str <- function(x, max_level = 2, kind = NULL) {
  call <- sys.call()
  if (!is.numeric(max_level) || length(max_level) != 1L || is.na(max_level) ||
      max_level < 0 || max_level > 100 || max_level != trunc(max_level)) {
    zrd_invalid_argument("max_level", "`max_level` must be a whole number from 0 to 100",
                         call = call)
  }
  # A level of nesting is an element (depth + 1); its names and class are
  # three more levels down (attributes, value, strings). An .rda adds one.
  tr <- zrd_trace(x, kind, max_depth = max_level + 5L, call = call)
  tree <- zrd_event_tree(tr$events)
  lines <- zrd_str_lines(tree, tr$kind, max_level)
  if (isTRUE(tr$truncated)) lines <- c(lines, " [summary truncated: too many items]")
  writeLines(lines)
  invisible(lines)
}

zrd_str_lines <- function(tree, kind, max_level) {
  kids <- split(seq_len(nrow(tree)), factor(tree$parent, levels = 0:nrow(tree)))
  children <- function(i) kids[[as.character(i)]]
  ctx <- list(tree = tree, children = children, max_level = max_level)
  roots <- children(0L)
  if (identical(kind, "rda")) {
    objs <- zrd_pairlist_values(ctx, roots)
    head <- paste0(".rda with ", length(objs$values), " object",
                   if (length(objs$values) == 1L) "" else "s", ":")
    c(head, zrd_str_children(ctx, objs$values, objs$names, 1L))
  } else {
    zrd_str_node(ctx, roots[[1]], level = 0L, label = "")
  }
}

# The values of a pairlist whose cells are `cells` (the first and its
# continuations, siblings in the tree), with their tags.
zrd_pairlist_values <- function(ctx, cells) {
  tree <- ctx$tree
  values <- integer(0)
  names <- character(0)
  for (cell in cells) {
    if (!tree$role[cell] %in% c("root", "attr", "cell", "element", "car",
                                 "cdr", "tag")) next
    if (!tree$type[cell] %in% c(2L, 6L, 17L)) next
    car <- Filter(function(k) tree$role[k] == "car", ctx$children(cell))
    if (!length(car)) next
    values <- c(values, car[[1]])
    names <- c(names, if (is.na(tree$name[car[[1]]])) "" else tree$name[car[[1]]])
  }
  list(values = values, names = names)
}

# The attributes of item `i`, as a named list of value rows.
zrd_attrs <- function(ctx, i) {
  kids <- ctx$children(i)
  roles <- ctx$tree$role[kids]
  first <- which(roles == "attr")
  if (!length(first)) return(list())
  cells <- kids[first[[1]]:length(kids)]
  cells <- cells[ctx$tree$role[cells] %in% c("attr", "cell")]
  pv <- zrd_pairlist_values(ctx, cells)
  out <- as.list(pv$values)
  names(out) <- pv$names
  out
}

# The strings of a character vector row, NA where not recorded.
zrd_strings <- function(ctx, i) {
  if (is.null(i) || ctx$tree$type[i] != 16L) return(character(0))
  kids <- ctx$children(i)
  kids <- kids[ctx$tree$role[kids] == "element"]
  ctx$tree$data[kids]
}

zrd_quote <- function(s, max = 5L) {
  s <- ifelse(is.na(s), "NA", paste0("\"", s, "\""))
  if (length(s) > max) s <- c(s[seq_len(max)], "...")
  paste(s, collapse = ",")
}

zrd_length_text <- function(n) {
  if (is.na(n) || n < 0) "" else if (n == 0) "(0) " else paste0(" [1:", format(n, scientific = FALSE), "]")
}

# The class of an ALTREP item: the first symbol in its class information
# (serialize.c writes class, package and type), named directly or by
# reference.
zrd_altrep_class <- function(ctx, i) {
  tree <- ctx$tree
  kids <- ctx$children(i)
  info <- kids[tree$role[kids] == "altrep_info"]
  if (!length(info)) return(NA_character_)
  below <- function(r) {
    out <- integer(0)
    for (k in ctx$children(r)) out <- c(out, k, below(k))
    out
  }
  rows <- below(info[[1]])
  rows <- rows[tree$type[rows] %in% c(9L, 255L) & !is.na(tree$data[rows])]
  if (length(rows)) tree$data[rows[[1]]] else NA_character_
}

# What an ALTREP class stands for, for the classes base R serializes.
zrd_altrep_types <- c(
  compact_intseq = "int", compact_realseq = "num", deferred_string = "chr",
  wrap_integer = "int", wrap_real = "num", wrap_string = "chr",
  wrap_logical = "logi", wrap_complex = "cplx", wrap_raw = "raw",
  wrap_list = "List"
)

# One line (and its children's lines) for item `i` at `level`.
zrd_str_node <- function(ctx, i, level, label) {
  tree <- ctx$tree
  type <- tree$type[i]
  n <- tree$length[i]
  attrs <- zrd_attrs(ctx, i)
  cls <- zrd_strings(ctx, attrs$class)
  cls_text <- if (length(cls)) paste0("'", paste(cls, collapse = "', '"), "' ") else ""
  desc <- switch(as.character(type),
    `254` = "NULL",
    `1` = paste0("symbol ", zrd_symbol_name(ctx, i)),
    `2` = "Dotted pair list",
    `3` = paste0(cls_text, "function"),
    `4` = "<environment>",
    `5` = "promise",
    `6` = paste0(cls_text, "language"),
    `7` = , `8` = "primitive",
    `17` = "...",
    `21` = "bytecode",
    `22` = "<externalptr>",
    `23` = "<weakref>",
    `25` = paste0("Formal class ", trimws(cls_text)),
    `238` = {
      ac <- zrd_altrep_class(ctx, i)
      ty <- if (ac %in% names(zrd_altrep_types)) zrd_altrep_types[[ac]] else "ALTREP"
      paste0(ty, " <", ac, ">")
    },
    `241` = "<environment: base>",
    `242` = "<environment: R_EmptyEnv>",
    `250` = "<environment: namespace:base>",
    `253` = "<environment: R_GlobalEnv>",
    `249` = "<environment: namespace>",
    `248` = "<environment: package>",
    `247` = "<persistent reference>",
    `255` = {
      d <- tree$data[i]
      if (is.na(d)) "<reference>" else paste0("symbol ", d)
    },
    NULL
  )
  sub <- character(0)
  if (is.null(desc)) {
    if (type %in% c(19L, 20L)) {
      elems <- ctx$children(i)
      elems <- elems[tree$role[elems] == "element"]
      nm <- zrd_strings(ctx, attrs$names)
      if (length(nm) < length(elems)) nm <- c(nm, rep("", length(elems) - length(nm)))
      nm[is.na(nm)] <- ""
      if ("data.frame" %in% cls) {
        # The rows are the first column's length, when it is known: an
        # ALTREP column's length is in its state.
        nrow <- if (length(elems)) tree$length[elems[[1]]] else 0
        obs <- if (nrow >= 0) paste0(format(nrow, scientific = FALSE), " obs. of  ") else ""
        desc <- paste0("'data.frame':\t", obs, n, " variable", if (n == 1) "" else "s", ":")
      } else {
        desc <- paste0(cls_text, if (type == 20L) "expression" else "List", " of ",
                       format(n, scientific = FALSE))
      }
      if (level < ctx$max_level) sub <- zrd_str_children(ctx, elems, nm, level + 1L)
    } else if (type == 13L && "factor" %in% cls) {
      lev <- zrd_strings(ctx, attrs$levels)
      desc <- paste0("Factor w/ ", length(lev), " level", if (length(lev) == 1) "" else "s",
                     if (length(lev)) paste0(" ", zrd_quote(lev)) else "",
                     ":", zrd_length_text(n))
    } else if (as.character(type) %in% names(zrd_type_abbr)) {
      dims <- if (is.null(attrs$dim)) "" else {
        d <- tree$length[attrs$dim]
        if (d == 2) " (matrix)" else paste0(" (", d, "-d array)")
      }
      desc <- paste0(cls_text, zrd_type_abbr[[as.character(type)]],
                     zrd_length_text(n), dims)
    } else {
      desc <- paste0("<item type ", type, ">")
    }
  }
  # As str() does: no space before a list's description.
  space <- if (grepl("^('[^']*' )?(List|expression|'data.frame'|Dotted)", desc)) "" else " "
  c(paste0(label, space, desc), sub)
}

zrd_symbol_name <- function(ctx, i) {
  kids <- ctx$children(i)
  if (!length(kids)) return("")
  d <- ctx$tree$data[kids[[1]]]
  if (is.na(d)) "" else d
}

zrd_str_children <- function(ctx, rows, names, level, list_len = 99L) {
  out <- character(0)
  indent <- if (level == 1L) " " else paste0(" ", strrep(" ..", level - 1L))
  shown <- seq_len(min(length(rows), list_len))
  for (k in shown) {
    label <- paste0(indent, "$ ", names[[k]], ":")
    out <- c(out, zrd_str_node(ctx, rows[[k]], level, label))
  }
  if (length(rows) > list_len) {
    out <- c(out, paste0(indent, " [list output truncated]"))
  }
  out
}
