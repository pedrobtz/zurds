test_that("every data-only XDR fixture walks to its end", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rds" & m$format == "xdr" & m$object %in% data_fixture_objects, ]
  expect_gt(nrow(m), 0)
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    res <- zrd_walk_info(path)
    obj <- readRDS(path)
    label <- m$file[i]
    expect_identical(res$items, expected_items(obj), label = label)
    expect_identical(res$items_ended, res$items, label = label)
    expect_identical(res$bytes, as.numeric(length(fixture_decompress(path))), label = label)
    expect_identical(res$version, m$version[i], label = label)
  }
})

test_that("every XDR .rda fixture walks to its end", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rda" & m$format == "xdr", ]
  expect_gt(nrow(m), 0)
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    env <- new.env()
    load(path, envir = env)
    names <- strsplit(m$object[i], " ")[[1]]
    # save() writes a pairlist tagged with the names, in the order given.
    expected <- expected_items(as.pairlist(mget(names, envir = env)))
    res <- zrd_walk_info(path)
    expect_identical(res$items, expected, label = m$file[i])
    expect_identical(res$bytes + 5, as.numeric(length(fixture_decompress(path))),
                     label = m$file[i])
  }
})

test_that("the header is read", {
  res <- zrd_walk_info(serialize(1L, NULL, version = 3))
  v <- unclass(getRversion())[[1]]
  expect_identical(res$format, "X")
  expect_identical(res$version, 3L)
  expect_true(nzchar(res$native_encoding))
  expect_identical(res$writer_version, as.integer(sum(v * c(65536, 256, 1))))
  res2 <- zrd_walk_info(serialize(1L, NULL, version = 2))
  expect_identical(res2$version, 2L)
  expect_identical(res2$native_encoding, "")
})

test_that("long pairlists do not deepen the walk", {
  x <- as.pairlist(as.list(seq_len(5000)))
  res <- zrd_walk_info(serialize(x, NULL), max_depth = 4)
  expect_identical(res$items, expected_items(x))
  expect_lte(res$max_depth, 1L)
})

test_that("repeated symbols are read as references", {
  x <- list(list(a = 1, b = 2), list(a = 3, b = 4), list(a = 5))
  expect_identical(zrd_walk_info(serialize(x, NULL))$items, expected_items(x))
})

test_that("long vectors cross many input blocks", {
  x <- list(double(300000), c(letters, NA), raw(70000))
  f <- withr::local_tempfile(fileext = ".rds")
  saveRDS(x, f)
  res <- zrd_walk_info(f)
  expect_identical(res$items, expected_items(x))
  expect_identical(res$bytes, as.numeric(length(serialize(x, NULL))))
})
