test_that("every fixture walks to the same events as its XDR twin", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind != "lazyload", ]
  twin <- sub("-(binary|ascii)-", "-xdr-", m$file)
  m <- m[m$format != "xdr" & twin %in% m$file, ]
  expect_gt(nrow(m), 0)
  for (i in seq_len(nrow(m))) {
    a <- event_shape(file.path(dir, m$file[i]))
    b <- event_shape(file.path(dir, sub("-(binary|ascii)-", "-xdr-", m$file[i])))
    expect_identical(a, b, label = m$file[i])
  }
})

test_that("every fixture in every format walks to its end", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind != "lazyload", ]
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    res <- zrd_walk_info(path)
    skip <- if (m$kind[i] == "rda") 5 else 0
    expect_identical(res$bytes + skip, as.numeric(length(fixture_decompress(path))),
                     label = m$file[i])
  }
})

test_that("ASCII strings with escapes are decoded", {
  x <- c("a\nb", "tab\there", "quote\"s", "back\\slash", "\001\177", "café")
  b <- serialize(x, NULL, ascii = TRUE)
  ev <- zrd_trace(b, max_depth = 8L)$events
  expect_identical(zrd_walk_info(b)$bytes, as.numeric(length(b)))
  # Names in a pairlist go through the same decoder.
  p <- serialize(pairlist(`a b\n` = 1), NULL, ascii = TRUE)
  ev <- zrd_trace(p)$events
  expect_identical(ev$name[ev$role == "car"][1], "a b\n")
})

test_that("ASCII numbers, NA and specials are walked", {
  x <- list(c(1.5, NA, NaN, Inf, -Inf, 1e-300), c(1L, NA), c(TRUE, NA),
            complex(real = NA, imaginary = 1), as.raw(c(0, 255)))
  for (hex in c(TRUE, NA)) {
    b <- serialize(x, NULL, ascii = hex)
    expect_identical(zrd_walk_info(b)$bytes, as.numeric(length(b)))
  }
})

test_that("a truncated ASCII stream fails with zurds_parse_error", {
  b <- serialize(list(a = 1:3 + 0L, b = "text"), NULL, ascii = TRUE)
  for (n in c(5, 20, length(b) %/% 2, length(b) - 3)) {
    expect_error(zrd_walk_info(b[seq_len(n)]), class = "zurds_parse_error")
  }
})

test_that("a malformed ASCII integer is a parse error", {
  b <- charToRaw("A\n3\n263681\n197888\n5\nUTF-8\n13\nxyz\n")
  expect_error(zrd_walk_info(b), class = "zurds_parse_error")
  long <- c(charToRaw("A\n3\n263681\n197888\n5\nUTF-8\n13\n"), charToRaw(strrep("1", 200)))
  expect_error(zrd_walk_info(long), class = "zurds_parse_error")
})

test_that("binary streams are read in either byte order", {
  le <- serialize(list(1L, "a"), NULL, xdr = FALSE)
  expect_identical(zrd_walk_info(le)$bytes, as.numeric(length(le)))
  # Big-endian, as a big-endian machine writes it: the XDR stream with a
  # binary header is exactly that.
  be <- serialize(list(1L, "a"), NULL)
  be[1] <- charToRaw("B")
  expect_identical(zrd_walk_info(be)$bytes, as.numeric(length(be)))
  expect_identical(event_shape(be), event_shape(serialize(list(1L, "a"), NULL)))
})
