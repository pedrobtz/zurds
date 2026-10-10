# What rds_toc() should report for an object, computed from the object.
toc_expected <- function(obj) {
  cls <- attr(obj, "class", exact = TRUE)
  d <- if (is.data.frame(obj)) c(nrow(obj), ncol(obj)) else dim(obj)
  list(
    type = typeof(obj),
    class = if (is.null(cls)) NA_character_ else cls[[1]],
    length = if (is.environment(obj)) NA_real_ else as.numeric(length(obj)),
    dim = if (is.null(d)) NA_character_ else paste(d, collapse = " x ")
  )
}

expect_toc_matches <- function(path) {
  toc <- rds_toc(path)
  env <- new.env()
  load(path, envir = env)
  expect_identical(sort(toc$name), sort(ls(env, all.names = TRUE)), label = path)
  for (i in seq_len(nrow(toc))) {
    want <- toc_expected(get(toc$name[i], envir = env))
    got <- as.list(toc[i, c("type", "class", "length", "dim")])
    expect_identical(got, want, label = paste(path, toc$name[i]))
  }
  invisible(toc)
}
