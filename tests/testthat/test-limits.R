test_that("hand-built streams walk", {
  # list(1L): VECSXP of length 1 holding an INTSXP of length 1.
  res <- zrd_walk_info(xdr_stream(19, 1, 13, 1, 7))
  expect_identical(res$items, 2)
})

test_that("a vector claiming 2^52 elements fails before allocating", {
  # serialize.c's ReadLENGTH refuses an upper part above 65536.
  x <- xdr_stream(14, -1, 2^20, 0)
  expect_error(zrd_walk_info(x), class = "zurds_parse_error")
})

test_that("a vector longer than max_size allows trips zurds_size_limit", {
  # 2^40 doubles, 8 TiB, with no payload.
  x <- xdr_stream(14, -1, 256, 0)
  cnd <- expect_error(zrd_walk_info(x), class = "zurds_size_limit")
  expect_s3_class(cnd, "zurds_limit_error")
  expect_identical(cnd$limit, "max_size")
  expect_identical(cnd$limit_value, zurds_info()$limits$max_size)
  expect_identical(cnd$offset, 23)
  # The same stream under a smaller limit, and a modest vector.
  expect_error(zrd_walk_info(serialize(double(1000), NULL), max_size = 1000),
               class = "zurds_size_limit")
})

test_that("a list claiming more items than max_items trips zurds_item_limit", {
  x <- xdr_stream(19, .Machine$integer.max)
  expect_error(zrd_walk_info(x), class = "zurds_item_limit")
  expect_error(zrd_walk_info(serialize(list(1, 2, 3), NULL), max_items = 3),
               class = "zurds_item_limit")
  expect_identical(zrd_walk_info(serialize(list(1, 2, 3), NULL), max_items = 4)$items, 4)
})

test_that("nesting past max_depth trips zurds_depth_limit", {
  nest <- function(n) {
    x <- 1
    for (i in seq_len(n)) x <- list(x)
    x
  }
  expect_error(zrd_walk_info(serialize(nest(5), NULL), max_depth = 4),
               class = "zurds_depth_limit")
  expect_identical(zrd_walk_info(serialize(nest(4), NULL), max_depth = 4)$max_depth, 4L)
  # Deeper than the default, built by hand: 600 nested lists.
  x <- do.call(xdr_stream, c(as.list(rep(c(19, 1), 600)), list(254)))
  cnd <- expect_error(zrd_walk_info(x), class = "zurds_depth_limit")
  expect_identical(cnd$limit_value, 512L)
})

test_that("a REFSXP beyond the table is a parse error", {
  # Packed index 5, and an unpacked index 1 in an empty table.
  expect_error(zrd_walk_info(xdr_stream(5 * 256 + 255)), class = "zurds_parse_error")
  expect_error(zrd_walk_info(xdr_stream(255, 1)), class = "zurds_parse_error")
})

test_that("an unknown or reserved type is a parse error, never a skip", {
  for (type in c(0, 11, 12, 18, 26, 31, 237, 243, 244, 245, 246)) {
    expect_error(zrd_walk_info(xdr_stream(type)), class = "zurds_parse_error",
                 label = type)
  }
})

test_that("a truncated stream is a parse error with an offset", {
  x <- serialize(list(1, "abc"), NULL)
  for (n in c(10, 25, 30, length(x) - 1)) {
    cnd <- expect_error(zrd_walk_info(x[seq_len(n)]), class = "zurds_parse_error")
    expect_lte(cnd$offset, n)
  }
})

test_that("a truncated gzip file is a decompression error", {
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(as.list(1:5000), f)
  b <- readBin(f, "raw", file.size(f))
  writeBin(b[seq_len(length(b) %/% 2)], f)
  expect_error(zrd_walk_info(f), class = "zurds_decompression_error")
})

test_that("pairlist values are named by their tags", {
  x <- serialize(pairlist(a = 1, b = list(2, 3)), NULL)
  hits <- which(sapply(seq_len(length(x) - 3), function(i)
    identical(x[i:(i + 3)], as.raw(c(0, 0, 0, 14)))))
  x[hits[3] + 3] <- as.raw(30)
  cnd <- expect_error(zrd_walk_info(x), class = "zurds_parse_error")
  expect_identical(cnd$path, "$b[[2]]")
})

test_that("errors name the path of the item at fault", {
  # list(a = 1, b = list(2, <bad>)): the bad item is [[2]][[2]]. Elements
  # are named by position: a list's names follow its elements.
  x <- serialize(list(a = 1, b = list(2, 3)), NULL)
  y <- x
  # Corrupt the flags of the last REALSXP, 3, into an unknown type.
  hits <- which(sapply(seq_len(length(y) - 3), function(i)
    identical(y[i:(i + 3)], as.raw(c(0, 0, 0, 14)))))
  y[hits[3] + 3] <- as.raw(30)
  cnd <- expect_error(zrd_walk_info(y), class = "zurds_parse_error")
  expect_identical(cnd$path, "[[2]][[2]]")
})

test_that("other formats and versions are refused for now", {
  expect_error(zrd_walk_info(serialize(1, NULL, xdr = FALSE)), class = "zurds_parse_error")
  expect_error(zrd_walk_info(serialize(1, NULL, ascii = TRUE)), class = "zurds_parse_error")
  bad <- serialize(1, NULL)
  bad[6] <- as.raw(9)
  expect_error(zrd_walk_info(bad), class = "zurds_format_error")
})

test_that("limit arguments are checked", {
  x <- serialize(1, NULL)
  expect_error(zrd_walk_info(x, max_depth = 0), class = "zurds_invalid_argument")
  expect_error(zrd_walk_info(x, max_items = NA), class = "zurds_invalid_argument")
  expect_error(zrd_walk_info(x, max_size = "a"), class = "zurds_invalid_argument")
  expect_error(zrd_walk_info(x, max_depth = 1.5), class = "zurds_invalid_argument")
})
