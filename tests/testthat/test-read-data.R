test_that("every data-only fixture round-trips identical() to readRDS()", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rds" & m$object %in% buildable_fixture_objects, ]
  expect_gt(nrow(m), 0)
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    expect_identical(rds_read_data(path), readRDS(path), label = m$file[i])
  }
})

test_that("every .rda fixture reads as the named list of what load() makes", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rda", ]
  for (f in m$file) {
    path <- file.path(dir, f)
    env <- new.env()
    nms <- load(path, envir = env)
    expect_identical(rds_read_data(path), mget(nms, envir = env), label = f)
  }
})

test_that("objects round-trip in every format and version", {
  objs <- list(
    mtcars, iris, 1:10, 10:1, seq(1, 2, by = 0.25), c(a = 1, b = NA),
    list(1, "a", NULL, list(TRUE)), factor(c("x", "y")), matrix(1:6, 2),
    letters, as.character(1:5), as.character(c(1.5, 2)), as.raw(c(0, 255)),
    complex(real = 1, imaginary = NA), c(NA_real_, NaN, Inf, -Inf, -0),
    c(NA, TRUE), Sys.Date(), as.POSIXct("2026-01-01", tz = "UTC"),
    data.frame(a = I(list(1, "b", 2L))), array(1:24, 2:4), sort(c(3, 1, 2)),
    NULL, structure(1:3, foo = "bar"), character(0), list(),
    c("café", NA, ""), .Machine$integer.max, 2^53 + 2
  )
  for (args in list(list(), list(ascii = TRUE), list(ascii = NA),
                    list(xdr = FALSE), list(version = 2))) {
    for (i in seq_along(objs)) {
      b <- do.call(serialize, c(list(objs[[i]], NULL), args))
      expect_identical(rds_read_data(b), unserialize(b),
                       label = paste(i, deparse(args)))
    }
  }
})

test_that("list columns of mixed types are built as lists", {
  df <- data.frame(id = 1:3)
  df$val <- list(1L, "two", list(3))
  b <- serialize(df, NULL)
  expect_identical(rds_read_data(b), df)
})

test_that("strings keep their encoding marks", {
  x <- c("café", "plain")
  latin <- iconv(x, "UTF-8", "latin1")
  bytes <- x
  Encoding(bytes) <- "bytes"
  for (s in list(x, latin, bytes)) {
    got <- rds_read_data(serialize(s, NULL))
    expect_identical(got, s)
    expect_identical(Encoding(got), Encoding(s))
  }
})

test_that("native strings from another encoding are translated", {
  # A version 3 stream from a latin1 session: a native string "café".
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  b <- c(charToRaw("X\n"), int(3, 263681, 197888, 10), charToRaw("ISO-8859-1"),
         int(16, 1, 9, 4), charToRaw("caf"), as.raw(0xe9))
  got <- rds_read_data(b)
  expect_identical(got, "café")
  expect_identical(Encoding(got), "UTF-8")
})

test_that("invalid UTF-8 is an error in strict mode only", {
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  # A string marked UTF-8 (levels 8, so flags 9 + 8 * 4096) holding 0xff.
  b <- c(charToRaw("X\n"), int(3, 263681, 197888, 5), charToRaw("UTF-8"),
         int(16, 1, 9 + 8 * 4096, 2), as.raw(c(0x61, 0xff)))
  cnd <- expect_error(rds_read_data(b), class = "zurds_encoding_error")
  expect_identical(cnd$path, "[[1]]")
  got <- rds_read_data(b, strict = FALSE)
  expect_identical(charToRaw(got), as.raw(c(0x61, 0xff)))
})

test_that("the CVE-2024-27322-style fixtures are refused", {
  dir <- fixture_dir()
  for (f in c("cve_top.rds", "cve_nested.rds")) {
    cnd <- expect_error(rds_read_data(file.path(dir, f)), class = "zurds_unsafe_content")
    expect_identical(cnd$findings$finding, "promise")
    expect_identical(cnd$findings$severity, "executes")
    expect_identical(cnd$refused$type, "promise")
  }
  expect_warning(got <- rds_read_data(file.path(dir, "cve_nested.rds"), strict = FALSE),
                 class = "zurds_dropped")
  expect_identical(got, list(1, NULL))
})

test_that("rds_read_data() never calls R's deserializer", {
  # The C side cannot: tools/check-symbols fails on any reference to
  # R_Unserialize or Rf_eval. Here, the R side: each of these stops if
  # called while the data is read. (lazyLoad() and lazyLoadDBfetch() are
  # left alone: R loads packages through them.)
  dir <- fixture_dir()
  paths <- file.path(dir, c("data_frame.rds", "multi-xdr-v3-gzip.rda", "cve_top.rds"))
  traced <- c("unserialize", "readRDS", "load")
  for (fn in traced) {
    suppressMessages(trace(fn, quote(stop("zurds called the deserializer")),
                           print = FALSE, where = baseenv()))
  }
  withr::defer(for (fn in traced) suppressMessages(untrace(fn, where = baseenv())))
  expect_s3_class(rds_read_data(paths[1]), "data.frame")
  expect_type(rds_read_data(paths[2]), "list")
  expect_error(rds_read_data(paths[3]), class = "zurds_unsafe_content")
  expect_error(unserialize(serialize(1, NULL)), "zurds called the deserializer")
})

test_that("everything but data is refused, and dropped when not strict", {
  dir <- fixture_dir()
  refused <- c("closure", "bytecode", "environment", "language", "formula",
               "builtin", "special", "external_pointer", "s4", "symbol",
               "pairlist", "namespace_closure", "package_env", "promise")
  for (o in refused) {
    path <- file.path(dir, paste0(o, ".rds"))
    expect_error(rds_read_data(path), class = "zurds_unsafe_content", label = o)
    expect_warning(got <- rds_read_data(path, strict = FALSE), class = "zurds_dropped")
    expect_null(got)
  }
})

test_that("non-strict reading warns once per kind", {
  x <- list(function() 1, function() 2, quote(a + b), 1)
  ws <- list()
  got <- withCallingHandlers(
    rds_read_data(serialize(x, NULL), strict = FALSE),
    zurds_dropped = function(w) {
      ws[[length(ws) + 1L]] <<- w
      invokeRestart("muffleWarning")
    }
  )
  expect_identical(got, list(NULL, NULL, NULL, 1))
  expect_identical(vapply(ws, function(w) w$type, ""), c("closure", "language"))
})

test_that("a malformed attribute is a parse error", {
  # c(1, 2) with dim = 3L: R refuses to set it.
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  dbl <- function(...) writeBin(c(...), raw(), endian = "big")
  b <- c(charToRaw("X\n"), int(3, 263681, 197888, 5), charToRaw("UTF-8"),
         int(14 + 512, 2), dbl(1, 2),
         int(1026, 1, 262153, 3), charToRaw("dim"), int(13, 1, 3), int(254))
  expect_error(rds_read_data(b), class = "zurds_parse_error")
})

test_that("building stops at its lower size limit", {
  # A vector claiming 2^30 doubles (8 GiB): over the 4 GiB building limit,
  # under the 16 GiB inspection limit.
  x <- xdr_stream(14, -1, 0, 2^30)
  expect_error(rds_read_data(x), class = "zurds_size_limit")
  expect_error(zrd_walk_info(x), class = "zurds_parse_error")
})

test_that("strict is checked", {
  expect_error(rds_read_data(serialize(1, NULL), strict = NA), class = "zurds_invalid_argument")
})
