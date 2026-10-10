test_that("every XDR fixture walks to its end", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind != "lazyload" & m$format == "xdr", ]
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    res <- zrd_walk_info(path)
    skip <- if (m$kind[i] == "rda") 5 else 0
    expect_identical(res$bytes + skip, as.numeric(length(fixture_decompress(path))),
                     label = m$file[i])
    expect_identical(res$items_ended, res$items, label = m$file[i])
  }
})

test_that("each code fixture holds the item kind it is for", {
  dir <- fixture_dir()
  kinds <- list(
    closure = "CLOSXP", bytecode = "BCODESXP", environment = "ENVSXP",
    env_cycle = c("ENVSXP", "REFSXP"), promise = "PROMSXP",
    language = "LANGSXP", formula = "LANGSXP", builtin = "BUILTINSXP",
    special = "SPECIALSXP", namespace_closure = "NAMESPACESXP",
    package_env = "PACKAGESXP", compact_seq = "ALTREP_SXP",
    deferred_string = "ALTREP_SXP", external_pointer = "EXTPTRSXP",
    s4 = "OBJSXP", persistent = "PERSISTSXP"
  )
  for (obj in names(kinds)) {
    res <- zrd_walk_info(file.path(dir, paste0(obj, ".rds")))
    for (k in kinds[[obj]]) {
      expect_gt(walked_types(res, sxp[[k]]), 0, label = paste(obj, k))
    }
  }
})

test_that("an environment that contains itself is walked once", {
  e <- new.env(parent = emptyenv())
  e$self <- e
  e$x <- list(e, e)
  res <- zrd_walk_info(serialize(e, NULL))
  expect_identical(walked_types(res, sxp[["ENVSXP"]]), 1)
  expect_identical(walked_types(res, sxp[["REFSXP"]]), 3)
})

test_that("byte-compiled closures nested in bytecode constants are walked", {
  f <- compiler::cmpfun(function(x) {
    g <- function(y) y + x
    vapply(1:3, function(i) g(i) * 2, numeric(1))
  })
  x <- serialize(f, NULL)
  res <- zrd_walk_info(x)
  expect_identical(res$bytes, as.numeric(length(x)))
  expect_gte(walked_types(res, sxp[["BCODESXP"]]), 1)
})

test_that("long calls do not deepen the walk", {
  call <- as.call(c(as.name("sum"), as.list(seq_len(2000))))
  f <- compiler::cmpfun(eval(call("function", NULL, call)))
  for (x in list(call, f)) {
    res <- zrd_walk_info(serialize(x, NULL), max_depth = 16)
    expect_identical(res$bytes, as.numeric(length(serialize(x, NULL))))
  }
})

test_that("a DOTSXP, a weak reference and S4 objects are walked", {
  # DOTSXP holding one value, untagged: flags 17, car 1L, end.
  res <- zrd_walk_info(xdr_stream(17, 13, 1, 1, 254))
  expect_identical(walked_types(res, sxp[["DOTSXP"]]), 1)
  # WEAKREFSXP: registered, no payload.
  res <- zrd_walk_info(xdr_stream(23))
  expect_identical(walked_types(res, sxp[["WEAKREFSXP"]]), 1)
  # A list referring back to the weak reference.
  res <- zrd_walk_info(xdr_stream(19, 2, 23, 1 * 256 + 255))
  expect_identical(walked_types(res, sxp[["REFSXP"]]), 1)
})

test_that("bytecode references out of range are parse errors", {
  # A BCODESXP with no reps whose single constant is a BCREPREF.
  x <- xdr_stream(21, 0, 13, 0, 1, 243, 0)
  expect_error(zrd_walk_info(x), class = "zurds_parse_error")
  x <- xdr_stream(21, 1, 13, 0, 1, 244, 5, 6)
  expect_error(zrd_walk_info(x), class = "zurds_parse_error")
})

test_that("class and generic references are parse errors", {
  expect_error(zrd_walk_info(xdr_stream(246)), class = "zurds_parse_error")
  expect_error(zrd_walk_info(xdr_stream(245)), class = "zurds_parse_error")
})

test_that("persistent strings with names are refused, as R refuses them", {
  expect_error(zrd_walk_info(xdr_stream(247, 1, 0)), class = "zurds_parse_error")
})

test_that("errors inside code name their path", {
  f <- function(x) x + 1
  x <- serialize(f, NULL)
  # Corrupt the last item flags (the body's end) into an unknown type.
  n <- length(x)
  x[n] <- as.raw(30)
  cnd <- expect_error(zrd_walk_info(x), class = "zurds_parse_error")
  expect_match(cnd$path, "^@body")
})
