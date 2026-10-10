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
  base <- file.path(fixture_dir(), "lazyload")
  env <- new.env()
  lazyLoad(base, envir = env)
  toc <- rds_lazyload_toc(base)
  for (name in toc$name[!toc$risky]) {
    expect_identical(rds_lazyload_extract(base, name), get(name, envir = env),
                     label = name)
  }
  expect_error(rds_lazyload_extract(base, "closure"), class = "zurds_unsafe_content")
  expect_error(rds_lazyload_extract(base, "nope"), class = "zurds_not_found")
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
  base <- file.path(withr::local_tempdir(), "db")
  flag <- file.path(dirname(base), "forced")
  # An unforced promise whose code would create `flag`: file.create(flag).
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  chars <- function(s) c(int(262153, nchar(s)), charToRaw(s))
  call <- c(int(6, 1), chars("file.create"), int(2, 16, 1), chars(flag), int(254))
  promise <- c(charToRaw("X\n"), int(3, 263681, 197888, 5), charToRaw("UTF-8"),
               int(5 + 1024, 253, 252), call)
  write_lazyload(base, list(p = promise, x = serialize(1:3 + 0L, NULL)))
  toc <- rds_lazyload_toc(base)
  expect_identical(toc$type, c("promise", "integer"))
  expect_identical(toc$risky, c(TRUE, FALSE))
  expect_error(rds_lazyload_extract(base, "p"), class = "zurds_unsafe_content")
  expect_identical(rds_lazyload_extract(base, "x"), c(1L, 2L, 3L))
  expect_false(file.exists(flag))
})

test_that("corrupt entries are decompression errors", {
  base <- file.path(withr::local_tempdir(), "db")
  write_lazyload(base, list(x = serialize(letters, NULL)))
  rdb <- paste0(base, ".rdb")
  b <- readBin(rdb, "raw", file.size(rdb))
  # Truncate the entry's zlib stream, keeping its declared length.
  writeBin(b[seq_len(length(b) - 6)], rdb)
  idx <- readRDS(paste0(base, ".rdx"))
  idx$variables$x[2] <- length(b) - 6
  saveRDS(idx, paste0(base, ".rdx"))
  expect_error(rds_lazyload_extract(base, "x"), class = "zurds_decompression_error")
  # An entry claiming to lie past the end of the .rdb.
  idx$variables$x[2] <- length(b) + 100
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
