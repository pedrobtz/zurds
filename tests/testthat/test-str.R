test_that("rds_str() summarises without building", {
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(list(
    id = c(1L, 2L, 3L), fit = list(coef = c(a = 1, b = 2), m = matrix(1:4, 2)),
    when = as.Date("2026-01-01"),
    df = data.frame(x = c(1L, 2L, 3L), f = factor(c("a", "b", "a"))),
    fn = function(x) x, e = globalenv(), s = quote(sym), u = NULL,
    seq = 1:10
  ), f)
  expect_snapshot(rds_str(f))
  expect_snapshot(rds_str(f, max_level = 1))
  expect_snapshot(rds_str(f, max_level = 0))
})

test_that("rds_str() lists the objects of an .rda", {
  expect_snapshot(rds_str(test_path("fixtures", "multi-xdr-v3-gzip.rda")))
})

test_that("rds_str() names every kind of item", {
  objs <- c("null", "logical", "integer", "double", "complex", "character",
            "raw", "factor", "matrix", "pairlist", "symbol", "language",
            "closure", "bytecode", "environment", "formula", "s4",
            "external_pointer", "compact_seq", "deferred_string", "builtin",
            "special", "namespace_closure", "package_env", "promise")
  out <- lapply(objs, function(o) {
    capture.output(rds_str(test_path("fixtures", paste0(o, ".rds"))))
  })
  expect_snapshot(stats::setNames(out, objs))
})

test_that("rds_str() returns its lines invisibly", {
  f <- serialize(list(a = 1), NULL)
  expect_invisible(lines <- withVisible(capture.output(rds_str(f)))$value)
  expect_type(capture.output(x <- rds_str(f)), "character")
  expect_identical(x, c("List of 1", " $ a: num [1:1]"))
})

test_that("rds_str() checks max_level", {
  x <- serialize(1, NULL)
  expect_error(rds_str(x, max_level = -1), class = "zurds_invalid_argument")
  expect_error(rds_str(x, max_level = "a"), class = "zurds_invalid_argument")
})

test_that("rds_str() works on files R refuses to load", {
  # A promise at the top level: readRDS() refuses it since R 4.4.0.
  x <- xdr_stream(5, 254, 14, 1, writeBin(1, raw(), endian = "big"), 254)
  expect_output(rds_str(x), "promise")
})
