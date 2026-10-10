test_that("zufast links and works", {
  expect_true(zurds_info()$zufast_ok)
})

test_that("versions are reported as strings", {
  info <- zurds_info()
  expect_match(info$zufast_version, "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  expect_identical(info$version, as.character(packageVersion("zurds")))
})

test_that("default limits are the design's", {
  lim <- zurds_info()$limits
  expect_identical(lim$max_depth, 512L)
  expect_identical(lim$max_items, 1e8)
  expect_identical(lim$max_size, 16 * 1024^3)
  expect_identical(lim$max_size_build, 4 * 1024^3)
})

test_that("the compressions include every one R always has", {
  comp <- zurds_info()$compressions
  expect_true(all(c("gzip", "bzip2", "xz") %in% comp))
  expect_true(all(comp %in% c("gzip", "bzip2", "xz", "zstd")))
})

test_that("zstd is reported exactly when this R can write it", {
  f <- withr::local_tempfile(fileext = ".rds")
  can <- tryCatch(
    {
      saveRDS(1L, f, compress = "zstd")
      TRUE
    },
    error = function(e) FALSE
  )
  expect_identical("zstd" %in% zurds_info()$compressions, can)
})

test_that("zurds_info() prints", {
  expect_output(print(zurds_info()), "self-test ok", fixed = TRUE)
})

test_that("the zufast self-test fails on bytes that are not its probe", {
  expect_false(.Call(zrd_build_info, as.raw(1:4))$zufast_ok)
  expect_false(.Call(zrd_build_info, as.raw(1:3))$zufast_ok)
  expect_false(.Call(zrd_build_info, 1L)$zufast_ok)
})
