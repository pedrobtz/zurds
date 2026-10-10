test_that("every object of every .rda fixture extracts as load() makes it", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rda", ]
  for (f in m$file) {
    path <- file.path(dir, f)
    env <- new.env()
    for (name in load(path, envir = env)) {
      expect_identical(rds_extract(path, name), get(name, envir = env),
                       label = paste(f, name))
    }
  }
})

test_that("every element of list and data frame fixtures extracts as readRDS()", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rds" & m$object %in% c("list", "named_list", "data_frame"), ]
  expect_gt(nrow(m), 0)
  for (f in m$file) {
    path <- file.path(dir, f)
    obj <- readRDS(path)
    for (i in seq_along(obj)) {
      expect_identical(rds_extract(path, list(i)), obj[[i]], label = paste(f, i))
      if (!is.null(names(obj))) {
        expect_identical(rds_extract(path, names(obj)[i]), obj[[i]], label = paste(f, i))
      }
    }
  }
})

test_that("paths go any number of levels down", {
  x <- list(a = list(b = list(c = 1:3, d = "x")), e = list(10, list(20, 30)))
  b <- serialize(x, NULL)
  expect_identical(rds_extract(b, c("a", "b", "c")), 1:3)
  expect_identical(rds_extract(b, list("e", 2, 1)), 20)
  expect_identical(rds_extract(b, "a"), x$a)
  f <- withr::local_tempfile(fileext = ".rda")
  save(x, file = f)
  expect_identical(rds_extract(f, list("x", "e", 2)), x$e[[2]])
})

test_that("extraction reuses rds_toc()'s offsets for an .rda", {
  f <- withr::local_tempfile(fileext = ".rda")
  a <- 1
  b <- letters
  save(a, b, file = f)
  rds_toc(f)
  key <- Filter(function(k) startsWith(k, normalizePath(f)), ls(zrd_toc_cache))
  expect_length(key, 1L)
  # A planted cache entry is used: the object at its offset comes back.
  toc <- get(key, envir = zrd_toc_cache)
  toc$offset[1] <- toc$offset[2]
  assign(key, toc, envir = zrd_toc_cache)
  expect_identical(rds_extract(f, "a"), letters)
})

test_that("names and positions not in the file are zurds_not_found", {
  f <- withr::local_tempfile(fileext = ".rda")
  a <- list(x = 1)
  save(a, file = f)
  expect_error(rds_extract(f, "b"), class = "zurds_not_found")
  expect_error(rds_extract(f, c("a", "y")), class = "zurds_not_found")
  expect_error(rds_extract(f, list("a", 2)), class = "zurds_not_found")
  expect_error(rds_extract(f, c("a", "x", "z")), class = "zurds_not_found")
})

test_that("an item with an executes finding is refused without use_r", {
  x <- list(data = 1:3, fn = function(y) y + 1)
  b <- serialize(x, NULL)
  cnd <- expect_error(rds_extract(b, "fn"), class = "zurds_unsafe_content")
  expect_identical(cnd$findings$finding[1], "closure")
  # Its sibling is data, and reads.
  expect_identical(rds_extract(b, "data"), 1:3)
  dir <- fixture_dir()
  expect_error(rds_extract(file.path(dir, "cve_nested.rds"), list(2)),
               class = "zurds_unsafe_content")
  expect_identical(rds_extract(file.path(dir, "cve_nested.rds"), list(1)), 1)
})

test_that("use_r = TRUE hands only the item to unserialize()", {
  fit <- lm(mpg ~ wt, data = mtcars)
  # Without source references, and in the global environment: the model's
  # formula already holds this test's environment, and a srcfile
  # environment would be shared too; either would be refused as outside
  # the item (see the next test).
  fn <- removeSource(function(y) y * 2)
  environment(fn) <- globalenv()
  x <- list(model = fit, fn = fn, sym = quote(wt))
  for (args in list(list(), list(ascii = TRUE), list(xdr = FALSE))) {
    b <- do.call(serialize, c(list(x, NULL), args))
    got <- rds_extract(b, "fn", use_r = TRUE)
    expect_identical(got(3), 6)
    findings <- attr(got, "zurds_findings")
    expect_identical(findings$finding[1], "closure")
    m <- rds_extract(b, "model", use_r = TRUE)
    expect_equal(coef(m), coef(fit), ignore_attr = TRUE)
    expect_identical(rds_extract(b, "sym", use_r = TRUE), quote(wt))
  }
})

test_that("use_r = TRUE refuses items that refer to environments outside them", {
  e <- new.env()
  x <- list(e, list(e))
  cnd <- expect_error(rds_extract(serialize(x, NULL), list(2), use_r = TRUE),
                      class = "zurds_unsafe_content")
  expect_match(conditionMessage(cnd), "outside it")
})

test_that("arguments are checked", {
  b <- serialize(list(a = 1), NULL)
  expect_error(rds_extract(b, character(0)), class = "zurds_invalid_argument")
  expect_error(rds_extract(b, list(0)), class = "zurds_invalid_argument")
  expect_error(rds_extract(b, NA_character_), class = "zurds_invalid_argument")
  expect_error(rds_extract(b, "a", use_r = NA), class = "zurds_invalid_argument")
  f <- withr::local_tempfile(fileext = ".rda")
  a <- 1
  save(a, file = f)
  expect_error(rds_extract(f, list(1)), class = "zurds_invalid_argument")
  con <- rawConnection(b)
  withr::defer(close(con))
  expect_error(rds_extract(con, "a"), class = "zurds_invalid_argument")
})

test_that("extracting from what is not a list is zurds_not_found", {
  expect_error(rds_extract(serialize(1:3, NULL), list(1)), class = "zurds_not_found")
})
