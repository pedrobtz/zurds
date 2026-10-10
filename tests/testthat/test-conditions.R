test_that("every condition inherits zurds_error", {
  for (class in names(zrd_condition_classes)) {
    cnd <- zrd_condition(class, "m")
    expect_s3_class(cnd, c(class, "zurds_error", "error", "condition"))
  }
})

test_that("the limit subclasses inherit zurds_limit_error", {
  for (class in c("zurds_depth_limit", "zurds_size_limit", "zurds_item_limit")) {
    expect_error(
      zrd_abort(class, "m", limit = "x", limit_value = 1),
      class = "zurds_limit_error"
    )
  }
})

test_that("design section 10's classes are all defined", {
  expect_setequal(
    names(zrd_condition_classes),
    c(
      "zurds_invalid_argument", "zurds_format_error", "zurds_parse_error",
      "zurds_unsafe_content", "zurds_not_found", "zurds_encoding_error",
      "zurds_limit_error", "zurds_depth_limit", "zurds_size_limit",
      "zurds_item_limit", "zurds_io_error", "zurds_decompression_error"
    )
  )
})

test_that("fields are carried on the condition", {
  cnd <- tryCatch(
    zrd_abort("zurds_parse_error", "bad", offset = 12, path = "[[1]]"),
    zurds_parse_error = identity
  )
  expect_identical(cnd$offset, 12)
  expect_identical(cnd$path, "[[1]]")
  expect_identical(conditionMessage(cnd), "bad")
})

test_that("invalid arguments name the argument", {
  cnd <- tryCatch(
    zrd_invalid_argument("x", "`x` must be a path"),
    zurds_invalid_argument = identity
  )
  expect_identical(cnd$arg, "x")
})

test_that("an unknown class is an internal error", {
  expect_error(zrd_condition("zurds_nope", "m"), "internal error")
})
