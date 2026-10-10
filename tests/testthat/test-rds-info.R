test_that("rds_info() agrees with the manifest for every fixture", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  files <- ifelse(m$kind == "lazyload", sub("[.]rd[bx]$", "", m$file), m$file)
  for (i in seq_len(nrow(m))) {
    info <- rds_info(file.path(dir, files[i]))
    label <- m$file[i]
    expect_s3_class(info, "rds_info")
    expect_identical(info$kind, m$kind[i], label = label)
    if (m$kind[i] != "lazyload") {
      expect_identical(info$format, m$format[i], label = label)
      expect_identical(info$version, m$version[i], label = label)
      expect_identical(info$compression, m$compression[i], label = label)
      expect_identical(info$writer_version, m$writer[i], label = label)
    }
  }
})

test_that("rds_info() reads raw vectors and connections", {
  info <- rds_info(serialize(1, NULL, version = 2, ascii = TRUE))
  expect_identical(info$format, "ascii")
  expect_identical(info$version, 2L)
  expect_identical(info$native_encoding, NA_character_)
  expect_identical(info$min_reader_version, "2.3.0")
  info <- rds_info(serialize(1, NULL, xdr = FALSE))
  expect_identical(info$format, "binary")
  expect_true(nzchar(info$native_encoding))
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(1, f, compress = "bzip2")
  con <- file(f, "rb")
  withr::defer(close(con))
  expect_identical(rds_info(f)$compression, "bzip2")
})

test_that("a lazy-load database reports its index", {
  base <- file.path(fixture_dir(), "lazyload")
  info <- rds_info(base)
  expect_identical(info$kind, "lazyload")
  expect_identical(info$format, "xdr")
})

test_that("rds_info() prints", {
  expect_snapshot(print(rds_info(test_path("fixtures", "multi-ascii-v2-gzip.rda"))),
                  transform = function(x) sub("[0-9]+[.][0-9]+[.][0-9]+$", "<version>", x))
})

test_that("rds_info() refuses what is not a serialization file", {
  expect_error(rds_info(charToRaw("hello")), class = "zurds_format_error")
  expect_error(rds_info(1), class = "zurds_invalid_argument")
  bad <- serialize(1, NULL)
  bad[6] <- as.raw(9)
  expect_error(rds_info(bad), class = "zurds_format_error")
})
