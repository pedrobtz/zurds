test_that("the compiled library is loaded", {
  expect_true("zurds" %in% names(getLoadedDLLs()))
})

test_that("native symbols resolve only through the registration table", {
  # R_useDynamicSymbols(dll, FALSE) in src/init.c: an entry point missing
  # from the .Call table must not be reachable by name.
  dll <- getLoadedDLLs()[["zurds"]]
  expect_false(dll[["dynamicLookup"]])
})
