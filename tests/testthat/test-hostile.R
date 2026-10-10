# The hostile inputs of design section 14, each a permanent regression:
# each fails with a classed error, and memory stays bounded.

test_that("a long vector of 2^52 elements with no payload", {
  x <- xdr_stream(14, -1, 2^20, 0)
  expect_error(zrd_walk_info(x), class = "zurds_parse_error")
  expect_error(rds_read_data(x), class = "zurds_parse_error")
  expect_error(rds_scan(x), class = "zurds_parse_error")
  # Within R's upper bound, a length past max_size: before any allocation.
  y <- xdr_stream(24, -1, 65536, 0)
  expect_error(rds_read_data(y), class = "zurds_size_limit")
  expect_error(rds_str(y), class = "zurds_size_limit")
})

test_that("a REFSXP index beyond the table", {
  for (x in list(xdr_stream(7 * 256 + 255), xdr_stream(19, 1, 255, 2^30))) {
    expect_error(rds_scan(x), class = "zurds_parse_error")
    expect_error(rds_read_data(x), class = "zurds_parse_error")
  }
})

test_that("nesting 10^6 deep", {
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  body <- rep(int(19, 1), 1e6)
  x <- c(xdr_stream(), body, int(254))
  for (f in list(zrd_walk_info, rds_scan, rds_read_data)) {
    cnd <- expect_error(f(x), class = "zurds_depth_limit")
    expect_identical(cnd$limit, "max_depth")
  }
  # A pairlist as long is not deep: the walk reuses its frame.
  cells <- rep(int(2, 254), 1e5)
  long <- c(xdr_stream(), cells, int(254))
  expect_identical(zrd_walk_info(long)$max_depth, 1L)
})

test_that("a gzip bomb stops at its limit, not at memory", {
  # 64 MiB of zeros, compressed to a few dozen kilobytes, as one raw
  # vector: inspected under a 16 MiB limit, refused before it is read.
  f <- withr::local_tempfile(fileext = ".rds")
  con <- gzfile(f, "wb")
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  writeBin(c(xdr_stream(), int(24, 64 * 1024^2)), con)
  zeros <- raw(1024^2)
  for (i in 1:64) writeBin(zeros, con)
  close(con)
  expect_lt(file.size(f), 1e6)
  cnd <- expect_error(zrd_walk_info(f, max_size = 16 * 1024^2), class = "zurds_size_limit")
  # Skipped, not built: memory stays far below the vector's size.
  used <- max_vcells(expect_identical(rds_info(f)$kind, "rds"))
  expect_lt(used, 1e6)
  used <- max_vcells(res <- zrd_walk_info(f))
  expect_identical(res$bytes, 23 + 8 + 64 * 1024^2)
  expect_lt(used, 1e6)
})

test_that("an .rda whose pairlist never ends", {
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  cell <- c(int(1026, 1, 262153, 1), charToRaw("x"), int(13, 1, 1))
  x <- c(charToRaw("RDX3\n"), xdr_stream(), rep(cell, 2e4))
  # Cut short: a parse error at its end.
  expect_error(rds_toc(x), class = "zurds_parse_error")
  # Under an item limit, stopped by it.
  expect_error(zrd_walk_info(x, max_items = 1e4), class = "zurds_item_limit")
})

test_that("a truncated ASCII stream", {
  # Cut inside the structure. (A cut in the last number can leave another
  # complete stream: "254" cut to "25" is an S4 object R reads too.)
  b <- serialize(list(a = "text", b = 1:3 + 0.5), NULL, ascii = TRUE)
  for (n in c(3, 10, 40, 80)) {
    expect_error(rds_read_data(b[seq_len(n)]), class = "zurds_parse_error")
    expect_error(rds_scan(b[seq_len(n)]), class = "zurds_parse_error")
  }
})

test_that("an unknown pseudo-type", {
  for (t in c(237, 243, 244, 245, 246)) {
    expect_error(rds_scan(xdr_stream(t)), class = "zurds_parse_error")
    expect_error(rds_read_data(xdr_stream(19, 1, t)), class = "zurds_parse_error")
  }
})
