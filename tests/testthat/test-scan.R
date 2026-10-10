test_that("every finding kind is found at its path with its severity", {
  dir <- fixture_dir()
  scan <- function(o) rds_scan(file.path(dir, paste0(o, ".rds")))
  has <- function(s, path, finding, severity) {
    any(s$path == path & s$finding == finding & s$severity == severity)
  }
  expect_true(has(scan("closure"), "", "closure", "executes"))
  expect_true(has(scan("bytecode"), "@body", "bytecode", "executes"))
  expect_true(has(scan("environment"), "", "environment", "reaches_out"))
  expect_true(has(scan("formula"), "$.Environment", "environment", "reaches_out"))
  # A cycle: the environment, then a reference back to it at $self.
  cyc <- scan("env_cycle")
  expect_true(has(cyc, "", "environment", "reaches_out"))
  expect_true(any(cyc$finding == "environment" & grepl("\\$self$", cyc$path)))
  expect_true(has(scan("promise"), "@hashtab[[26]]$p", "promise", "executes"))
  expect_true(has(scan("external_pointer"), "", "external_pointer", "reaches_out"))
  expect_true(has(scan("namespace_closure"), "@env", "namespace", "reaches_out"))
  expect_true(has(scan("package_env"), "", "package_environment", "reaches_out"))
  expect_true(has(scan("persistent"), "", "persistent_reference", "reaches_out"))
  expect_true(has(scan("s4"), "", "s4", "needs_package"))
  # Weak references and non-base ALTREP classes, built by hand.
  weak <- rds_scan(xdr_stream(19, 1, 23))
  expect_true(has(weak, "[[1]]", "weak_reference", "reaches_out"))
  sym <- function(s) list(1, 262153, nchar(s), charToRaw(s))
  alt <- do.call(xdr_stream, c(
    list(19, 1, 238, 2), sym("my_class"), list(2), sym("mypkg"),
    list(2, 13, 1, 13, 254), list(13, 1, 0), list(254)
  ))
  a <- rds_scan(alt)
  expect_true(has(a, "[[1]]", "altrep", "needs_package"))
  expect_identical(a$detail[a$finding == "altrep"], "my_class (mypkg)")
})

test_that("details name namespaces, packages and classes", {
  dir <- fixture_dir()
  s <- rds_scan(file.path(dir, "namespace_closure.rds"))
  expect_identical(s$detail[s$finding == "namespace"], "stats")
  s <- rds_scan(file.path(dir, "package_env.rds"))
  expect_identical(s$detail, "package:stats")
  s <- rds_scan(file.path(dir, "s4.rds"))
  expect_match(s$detail, "^ZurdsFixture \\(")
  s <- rds_scan(file.path(dir, "persistent.rds"))
  expect_identical(s$detail, "zurds-ref")
})

test_that("the CVE-2024-27322-style fixtures are reported as executing", {
  dir <- fixture_dir()
  top <- rds_scan(file.path(dir, "cve_top.rds"))
  expect_identical(top[c("path", "finding", "severity")],
                   data.frame(path = "", finding = "promise", severity = "executes"))
  nested <- rds_scan(file.path(dir, "cve_nested.rds"))
  expect_identical(nested[c("path", "finding", "severity")],
                   data.frame(path = "[[2]]", finding = "promise", severity = "executes"))
  expect_false(rds_is_safe(file.path(dir, "cve_top.rds")))
})

test_that("rds_is_safe() is TRUE for exactly the clean fixtures", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rds", ]
  for (i in seq_len(nrow(m))) {
    path <- file.path(dir, m$file[i])
    clean <- m$object[i] %in% clean_fixture_objects
    expect_identical(rds_is_safe(path), clean, label = m$file[i])
    if (clean) expect_identical(nrow(rds_scan(path)), 0L, label = m$file[i])
  }
})

test_that("rds_toc() marks risky objects", {
  f <- withr::local_tempfile(fileext = ".rda")
  local({
    data <- mtcars
    fn <- function(x) x
    seq <- 1:10
    env <- new.env()
    obj <- methods::new("externalptr")
    save(data, fn, seq, env, obj, file = f)
  })
  toc <- rds_toc(f)
  expect_identical(toc$risky, c(FALSE, TRUE, FALSE, TRUE, TRUE))
  s <- rds_scan(f)
  expect_true(all(c("$fn", "$env", "$obj") %in% s$path))
})

test_that("rds_deps() lists namespaces, package environments and classes", {
  dir <- fixture_dir()
  d <- rds_deps(file.path(dir, "namespace_closure.rds"))
  expect_identical(d$package, "stats")
  expect_identical(d$reason, "namespace")
  d <- rds_deps(file.path(dir, "package_env.rds"))
  expect_identical(d, data.frame(package = "stats", reason = "package_environment",
                                 detail = NA_character_))
  d <- rds_deps(file.path(dir, "named_list.rds"))
  expect_identical(nrow(d), 0L)
  expect_named(d, c("package", "reason", "detail"))
})

test_that("rds_scan() reads every format", {
  dir <- fixture_dir()
  m <- fixture_manifest(dir)
  m <- m[m$kind == "rds" & m$object == "named_list", ]
  for (f in m$file) expect_identical(nrow(rds_scan(file.path(dir, f))), 0L)
})
