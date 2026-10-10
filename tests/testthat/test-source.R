test_that("every fixture is opened and its kind and compression detected", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  for (i in seq_len(nrow(m))) {
    p <- zrd_probe(file.path(dir, m$file[i]))
    expect_identical(p$kind, m$kind[i], label = m$file[i])
    expect_identical(p$format, m$format[i], label = m$file[i])
    expect_identical(p$compression, m$compression[i], label = m$file[i])
  }
})

test_that("every stream fixture reads to its full decompressed length", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind != "lazyload", ]
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    expected <- length(fixture_decompress(path))
    expect_identical(zrd_probe(path)$length, as.numeric(expected), label = m$file[i])
  }
})

test_that("every fixture is opened through a raw vector too", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind != "lazyload", ]
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    bytes <- readBin(path, "raw", file.size(path))
    p <- zrd_probe(bytes)
    expect_identical(p$kind, m$kind[i], label = m$file[i])
    expect_identical(p$format, m$format[i], label = m$file[i])
    expect_identical(p$compression, m$compression[i], label = m$file[i])
    expect_identical(p$length, as.numeric(length(fixture_decompress(path))),
                     label = m$file[i])
  }
})

test_that("a serialize() result is an rds stream", {
  p <- zrd_probe(serialize(list(1, "a"), NULL))
  expect_identical(p$kind, "rds")
  expect_identical(p$format, "xdr")
  expect_identical(p$compression, "none")
  expect_identical(p$length, as.numeric(length(serialize(list(1, "a"), NULL))))
})

test_that("a connection is read as given", {
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(1:3, f)
  con <- gzfile(f, "rb")
  withr::defer(close(con))
  p <- zrd_probe(con)
  expect_identical(p$kind, "rds")
  expect_identical(p$length, as.numeric(length(serialize(1:3, NULL))))
})

test_that("a lazy-load database is found from its base, .rdb or .rdx path", {
  base <- file.path(fixture_dir(), "lazyload")
  for (p in c(base, paste0(base, ".rdb"), paste0(base, ".rdx"))) {
    expect_identical(zrd_probe(p)$kind, "lazyload")
  }
})

test_that("a truncated file fails with zurds_decompression_error", {
  for (comp in c("gzip", "bzip2", "xz")) {
    f <- withr::local_tempfile(fileext = ".rds")
    saveRDS(as.list(1:5000), f, compress = comp)
    b <- readBin(f, "raw", file.size(f))
    writeBin(b[seq_len(length(b) %/% 2)], f)
    expect_error(zrd_probe(f), class = "zurds_decompression_error", label = comp)
    expect_error(zrd_probe(b[seq_len(length(b) %/% 2)]),
                 class = "zurds_decompression_error", label = comp)
  }
})

test_that("a truncated zstd file fails with zurds_decompression_error", {
  skip_if_not("zstd" %in% zurds_info()$compressions)
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(as.list(1:5000), f, compress = "zstd")
  b <- readBin(f, "raw", file.size(f))
  writeBin(b[seq_len(length(b) %/% 2)], f)
  expect_error(zrd_probe(f), class = "zurds_decompression_error")
})

test_that("unknown magic fails with zurds_format_error", {
  f <- withr::local_tempfile()
  writeLines("not an R file", f)
  expect_error(zrd_probe(f), class = "zurds_format_error")
  expect_error(zrd_probe(charToRaw("hello")), class = "zurds_format_error")
  expect_error(zrd_probe(raw()), class = "zurds_format_error")
})

test_that("a missing file fails with zurds_io_error", {
  f <- withr::local_tempfile()
  expect_error(zrd_probe(f), class = "zurds_io_error")
})

test_that("kind = overrides detection and is checked against the input", {
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(1, f)
  expect_identical(zrd_probe(f, kind = "rds")$kind, "rds")
  expect_error(zrd_probe(f, kind = "rda"), class = "zurds_format_error")
  expect_error(zrd_probe(f, kind = "lazyload"), class = "zurds_io_error")
  expect_error(zrd_probe(f, kind = "nope"), class = "zurds_invalid_argument")
})

test_that("x must be a path, a raw vector or a connection", {
  expect_error(zrd_probe(1), class = "zurds_invalid_argument")
  expect_error(zrd_probe(c("a", "b")), class = "zurds_invalid_argument")
  expect_error(zrd_probe(NA_character_), class = "zurds_invalid_argument")
})

test_that("a connection opened in text mode is refused", {
  f <- withr::local_tempfile()
  writeLines("x", f)
  con <- file(f, "r")
  withr::defer(close(con))
  expect_error(zrd_probe(con), class = "zurds_invalid_argument")
})

test_that("reads cross block boundaries", {
  x <- as.double(seq_len(100000))
  p <- zrd_probe(serialize(x, NULL))
  expect_identical(p$length, as.numeric(length(serialize(x, NULL))))
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(x, f)
  expect_identical(zrd_probe(f)$length, p$length)
})
