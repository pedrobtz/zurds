# rds_scan(), rds_is_safe() and rds_deps(): what a file holds that can run
# code, reach outside it, or needs a package (design sections 5 and 8).

zrd_finding_names <- c(
  "closure", "promise", "environment", "bytecode", "external_pointer",
  "weak_reference", "namespace", "package_environment", "altrep", "s4",
  "persistent_reference"
)
zrd_severity_names <- c("executes", "reaches_out", "needs_package")

zrd_scan_raw <- function(x, kind, call) {
  input <- zrd_open(x, kind, call)
  on.exit(zrd_close(input))
  if (identical(input$kind, "lazyload")) {
    zrd_invalid_argument(
      "x", "a lazy-load database is scanned entry by entry; see rds_lazyload_toc()",
      call = call
    )
  }
  limits <- zrd_limits(zrd_default_limits()$max_depth, zrd_default_limits()$max_items,
                       zrd_default_limits()$max_size, call)
  res <- zrd_source_call(
    .Call(zrd_scan_walk, input$source, zrd_stream_skip(input), limits),
    call
  )
  if (res$status != "ZRD_OK") zrd_walk_fail(res, input, limits, call)
  res$findings
}

zrd_findings_frame <- function(f) {
  finding <- zrd_finding_names[f$kind + 1L]
  d1 <- f$detail1
  d2 <- f$detail2
  detail <- ifelse(
    finding %in% c("altrep", "s4") & !is.na(d2),
    paste0(d1, " (", d2, ")"),
    d1
  )
  detail[finding == "package_environment" & !is.na(d1)] <-
    d1[finding == "package_environment" & !is.na(d1)]
  data.frame(
    path = f$path, finding = finding, detail = detail,
    severity = zrd_severity_names[f$severity + 1L],
    stringsAsFactors = FALSE
  )
}

#' Scan an R serialization file for code and references
#'
#' Reports everything in a file that can run code when the file is loaded
#' or used, that reaches outside the file, or that needs a package to
#' load, without loading it. Nothing in the file is evaluated, and a file
#' R itself would refuse can still be scanned.
#'
#' Base R's own ALTREP classes (compact sequences such as `1:10`, deferred
#' strings and wrappers) need no package and are read by
#' `rds_read_data()`; they are not findings.
#'
#' The scan is advisory for [readRDS()]: a clean scan means the file
#' holds no type that can run code when it is loaded, but `readRDS()`
#' still creates objects of the file's classes, and using them can call
#' methods you have loaded.
#'
#' @inheritParams rds_info
#' @return For `rds_scan()`, a data frame with one row per finding, in
#'   stream order:
#'   * `path`: where in the object, such as `[[2]]$fit` (see
#'     [zurds-conditions] for the notation); `""` for the object itself,
#'     and `$name` for an object of an `.rda`.
#'   * `finding`: `"closure"`, `"promise"`, `"bytecode"`,
#'     `"environment"`, `"external_pointer"`, `"weak_reference"`,
#'     `"namespace"`, `"package_environment"`, `"persistent_reference"`,
#'     `"altrep"` or `"s4"`.
#'   * `detail`: the namespace or package environment, the ALTREP class
#'     and its package, or the S4 class and its package; `NA` otherwise.
#'   * `severity`: `"executes"` (closures, promises, bytecode),
#'     `"reaches_out"` (environments, namespaces, package environments,
#'     external pointers, weak and persistent references) or
#'     `"needs_package"` (ALTREP and S4).
#'
#'   For `rds_is_safe()`, `TRUE` when `rds_scan()` finds nothing.
#' @family safety
#' @export
#' @examples
#' rds_scan(system.file("extdata", "model.rds", package = "zurds"))
#' rds_is_safe(system.file("extdata", "iris.rds", package = "zurds"))
#'
#' # A promise that would run system() when used:
#' promise <- system.file("extdata", "promise.rds", package = "zurds")
#' rds_scan(promise)
#' rds_is_safe(promise)
rds_scan <- function(x, kind = NULL) {
  call <- sys.call()
  zrd_findings_frame(zrd_scan_raw(x, kind, call))
}

#' @rdname rds_scan
#' @export
rds_is_safe <- function(x, kind = NULL) {
  call <- sys.call()
  length(zrd_scan_raw(x, kind, call)$kind) == 0L
}

#' Packages a file needs
#'
#' Lists what loading a file would need from outside it: the namespaces
#' and package environments it refers to, the packages of its ALTREP
#' classes, and the packages of its S4 classes. The file is not loaded.
#'
#' @inheritParams rds_info
#' @return A data frame with one row per distinct need:
#'   * `package`: the package's name.
#'   * `reason`: `"namespace"`, `"package_environment"`, `"altrep"` or
#'     `"s4"`.
#'   * `detail`: the ALTREP or S4 class, or `NA`.
#' @family safety
#' @export
#' @examples
#' rds_deps(system.file("extdata", "model.rds", package = "zurds"))
#'
#' # An object from a namespace needs that package:
#' rds_deps(serialize(stats::sd, NULL))
rds_deps <- function(x, kind = NULL) {
  call <- sys.call()
  f <- zrd_scan_raw(x, kind, call)
  finding <- zrd_finding_names[f$kind + 1L]
  pkg <- rep(NA_character_, length(finding))
  detail <- rep(NA_character_, length(finding))
  ns <- finding == "namespace"
  pkg[ns] <- f$detail1[ns]
  pe <- finding == "package_environment"
  pkg[pe] <- sub("^package:", "", f$detail1[pe])
  cl <- finding %in% c("altrep", "s4")
  pkg[cl] <- f$detail2[cl]
  detail[cl] <- f$detail1[cl]
  keep <- !is.na(pkg) & finding %in% c("namespace", "package_environment", "altrep", "s4")
  out <- data.frame(package = pkg[keep], reason = finding[keep], detail = detail[keep],
                    stringsAsFactors = FALSE)
  out <- out[!duplicated(out), , drop = FALSE]
  rownames(out) <- NULL
  out
}
