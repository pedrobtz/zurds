test_that("rds_toc() agrees with load() for every .rda fixture", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rda", ]
  expect_gt(nrow(m), 0)
  for (f in m$file) expect_toc_matches(file.path(dir, f))
})

test_that("rds_toc() agrees with load() on every kind of object", {
  f <- withr::local_tempfile(fileext = ".rda")
  local({
    x <- 1:10
    y <- seq(1, 2, by = 0.5)
    z <- as.character(1:4)
    cars <- mtcars
    m <- matrix(0, 3, 4)
    arr <- array(1:24, 2:4)
    fn <- function(x) x
    cmp <- compiler::cmpfun(function(x) x + 1)
    n <- NULL
    fct <- factor(c("a", "b"))
    cl <- quote(f(a, b))
    pl <- pairlist(1, 2, 3)
    sy <- quote(s)
    dt <- Sys.Date()
    tm <- as.POSIXct("2026-01-01", tz = "UTC")
    lst <- list(1, list(2))
    nm <- c(a = 1, b = 2)
    chr <- c("café", NA)
    rw <- as.raw(1:3)
    cx <- 1i
    df_names <- data.frame(a = 1:2, row.names = c("r1", "r2"))
    w <- sort(c(3, 1, 2))
    ex <- expression(a + b, c)
    s4 <- methods::new("dgTMatrix0", x = 1)
    save(list = ls(), file = f)
  }, envir = local({
    e <- new.env()
    methods::setClass("dgTMatrix0", representation(x = "numeric"), where = e)
    e
  }))
  toc <- expect_toc_matches(f)
  expect_identical(toc$type[toc$name == "s4"], "S4")
  expect_identical(toc$dim[toc$name == "arr"], "2 x 3 x 4")
  expect_identical(toc$dim[toc$name == "cars"], "32 x 11")
})

test_that("rds_toc() reports offsets and sizes in the decompressed stream", {
  f <- withr::local_tempfile(fileext = ".rda")
  a <- c(1.5, 2.5)
  b <- "text"
  save(a, b, file = f)
  toc <- rds_toc(f)
  bytes <- fixture_decompress(f)
  # Each object's bytes are exactly a serialized item: its flags start at
  # `offset` (0-based) and it takes `size` bytes. Between the two objects
  # lie the next cell's flags (4 bytes) and its tag: a symbol's flags (4)
  # and its name, a CHARSXP of flags, length and "b" (9).
  expect_identical(toc$offset[1] + toc$size[1], toc$offset[2] - 17)
  expect_identical(bytes[toc$offset[1] + 1:4], as.raw(c(0, 0, 0, 14)))
  expect_lte(max(toc$offset + toc$size), length(bytes))
})

test_that("rds_toc() reads every format, version and compression", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rda", ]
  tocs <- lapply(file.path(dir, m$file), rds_toc)
  keep <- c("name", "type", "class", "length", "dim")
  for (t in tocs[-1]) expect_identical(t[keep], tocs[[1]][keep])
})

test_that("rds_toc() refuses a single-object file with a hint", {
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(1, f)
  cnd <- expect_error(rds_toc(f), class = "zurds_invalid_argument")
  expect_match(conditionMessage(cnd), "rds_str()", fixed = TRUE)
  expect_identical(cnd$arg, "x")
})

test_that("rds_toc() caches offsets by path, size and modification time", {
  f <- withr::local_tempfile(fileext = ".rda")
  a <- 1
  save(a, file = f)
  toc <- rds_toc(f)
  hit <- Filter(function(k) startsWith(k, normalizePath(f)), ls(zrd_toc_cache))
  expect_length(hit, 1L)
  expect_identical(get(hit, envir = zrd_toc_cache), toc)
})

test_that("rds_toc() allocates nothing in proportion to the objects", {
  f <- withr::local_tempfile(fileext = ".rda")
  local({
    a <- rep(1.5, 1e6)
    b <- rep(2.5, 1e6)
    l <- as.list(seq_len(1e5) + 0.5)
    save(a, b, l, file = f)
  })
  rds_toc(f) # loads the code it runs, outside the measurement
  gc(reset = TRUE)
  before <- gc()[2, "max used"]
  toc <- rds_toc(f)
  after <- gc()[2, "max used"]
  expect_identical(toc$length, c(1e6, 1e6, 1e5))
  # Building the objects would take over 2.1 million Vcells (2 million
  # doubles, and 100,000 lists of one); the walk builds none of them.
  expect_lt(after - before, 5e5)
})
