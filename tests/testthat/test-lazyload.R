test_that("rds_lazyload_toc() lists every variable of the fixture", {
  base <- file.path(fixture_dir(), "lazyload")
  toc <- rds_lazyload_toc(base)
  env <- new.env()
  lazyLoad(base, envir = env)
  expect_setequal(toc$name, ls(env))
  for (i in seq_len(nrow(toc))) {
    want <- toc_expected(get(toc$name[i], envir = env))
    expect_identical(as.list(toc[i, c("type", "class", "length", "dim")]), want,
                     label = toc$name[i])
  }
  expect_identical(toc$risky[toc$name == "closure"], TRUE)
  # The .rdb and .rdx paths name the same database.
  expect_identical(rds_lazyload_toc(paste0(base, ".rdb")), toc)
  expect_identical(rds_lazyload_toc(paste0(base, ".rdx")), toc)
})

test_that("each data-only entry extracts identical() to lazyLoad()'s value", {
  for (db in c("lazyload", "lazyload_c2", "lazyload_c3")) {
    base <- file.path(fixture_dir(), db)
    env <- new.env()
    lazyLoad(base, envir = env)
    toc <- rds_lazyload_toc(base)
    for (name in toc$name[!toc$risky]) {
      expect_identical(rds_lazyload_extract(base, name), get(name, envir = env),
                       label = paste(db, name))
    }
  }
  base <- file.path(fixture_dir(), "lazyload")
  expect_error(rds_lazyload_extract(base, "closure"), class = "zurds_unsafe_content")
  expect_error(rds_lazyload_extract(base, "nope"), class = "zurds_not_found")
})

test_that("xz entries are wrapped for R's decoder, and checked", {
  base <- file.path(fixture_dir(), "lazyload_c3")
  idx <- readRDS(paste0(base, ".rdx"))
  k <- idx$variables$data_frame
  con <- file(paste0(base, ".rdb"), "rb")
  withr::defer(close(con))
  seek(con, k[1])
  b <- readBin(con, "raw", k[2])
  n <- sum(as.numeric(b[1:4]) * 256^(3:0))
  expect_identical(rawToChar(b[5]), "Z")
  raw <- b[-(1:5)]
  full <- memDecompress(.Call(zrd_xz_wrap, raw, n), "xz")
  expect_identical(length(full), as.integer(n))
  # Cut short, the container's index no longer matches: an error, never a
  # silent prefix.
  cut <- .Call(zrd_xz_wrap, raw[seq_len(length(raw) - 5)], n)
  expect_error(memDecompress(cut, "xz"))
})

test_that("installed base packages' databases are listed", {
  for (pkg in c("utils", "stats")) {
    db <- file.path(system.file("R", package = pkg), pkg)
    toc <- rds_lazyload_toc(db)
    expect_gt(nrow(toc), 100)
    expect_true("closure" %in% toc$type)
    expect_false(anyNA(toc$type))
  }
})

test_that("a promise entry is reported and not evaluated", {
  base <- file.path(normalizePath(fixture_dir()), "lazyload_promise")
  withr::local_dir(withr::local_tempdir())
  toc <- rds_lazyload_toc(base)
  expect_identical(toc$type, c("promise", "integer"))
  expect_identical(toc$risky, c(TRUE, FALSE))
  expect_error(rds_lazyload_extract(base, "p"), class = "zurds_unsafe_content")
  expect_identical(rds_lazyload_extract(base, "x"), c(1L, 2L, 3L))
  # Its code would create this file in the working directory.
  expect_false(file.exists("zurds-lazyload-promise-was-forced"))
})

test_that("the zlib decoder agrees with R on every stream", {
  pairs <- readRDS(file.path(fixture_dir(), "zlib.rds"))
  for (name in names(pairs)) {
    p <- pairs[[name]]
    expect_identical(.Call(zrd_inflate_raw, p$zlib, length(p$plain)), p$plain, label = name)
  }
})

test_that("the zlib decoder refuses what is not exactly the declared stream", {
  p <- readRDS(file.path(fixture_dir(), "zlib.rds"))$serialized
  z <- p$zlib
  n <- length(p$plain)
  msg <- function(...) {
    out <- .Call(zrd_inflate_raw, ...)
    expect_type(out, "character")
    out
  }
  # Truncated anywhere, including inside the trailer.
  for (cut in c(1, 2, 10, length(z) %/% 2, length(z) - 3, length(z) - 1)) {
    msg(z[seq_len(cut)], n)
  }
  expect_match(msg(z, n - 1), "more bytes")
  expect_match(msg(z, n + 1), "fewer bytes")
  bad <- z
  bad[length(bad)] <- xor(bad[length(bad)], as.raw(1))
  expect_match(msg(bad, n), "Adler-32")
  expect_match(msg(as.raw(c(0x78, 0x00, 0, 0)), 0), "not a zlib")
  # A distance before the start of the output: a fixed block (01) whose
  # first symbol is a length code.
  msg(as.raw(c(0x78, 0x9c, 0x03, 0x02, 0x00, 0, 0, 0, 1)), 10)
  # Random corruption never hangs or crashes.
  withr::local_seed(1)
  for (i in 1:200) {
    b <- z
    k <- sample(seq_along(b)[-(1:2)], 3)
    b[k] <- as.raw(sample(0:255, 3))
    out <- .Call(zrd_inflate_raw, b, n)
    expect_true(is.character(out) || is.raw(out))
  }
})

test_that("corrupt entries are decompression errors", {
  dir <- withr::local_tempdir()
  base <- file.path(dir, "db")
  src <- file.path(fixture_dir(), "lazyload_promise")
  file.copy(paste0(src, ".rdb"), paste0(base, ".rdb"))
  file.copy(paste0(src, ".rdx"), paste0(base, ".rdx"))
  idx <- readRDS(paste0(base, ".rdx"))
  # Entry `x`, cut short by six bytes, its declared length kept.
  idx$variables$x[2] <- idx$variables$x[2] - 6L
  saveRDS(idx, paste0(base, ".rdx"))
  expect_error(rds_lazyload_extract(base, "x"), class = "zurds_decompression_error")
  # An entry claiming to lie past the end of the .rdb.
  idx$variables$x[2] <- idx$variables$x[2] + 100L
  saveRDS(idx, paste0(base, ".rdx"))
  expect_error(rds_lazyload_toc(base), class = "zurds_format_error")
})

test_that("a missing database or a bad index is reported", {
  dir <- withr::local_tempdir()
  expect_error(rds_lazyload_toc(file.path(dir, "none")), class = "zurds_io_error")
  base <- file.path(dir, "bad")
  saveRDS(list(1, 2), paste0(base, ".rdx"))
  writeBin(raw(0), paste0(base, ".rdb"))
  expect_error(rds_lazyload_toc(base), class = "zurds_format_error")
  expect_error(rds_lazyload_extract(base, NA_character_), class = "zurds_invalid_argument")
})
