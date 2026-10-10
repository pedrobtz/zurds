# Default limits, design section 11. Every entry point takes its defaults
# from here, so zurds_info() reports what the walk enforces.
zrd_default_limits <- function() {
  list(
    max_depth = 512L,
    max_items = 1e8,
    max_size = 16 * 1024^3,
    max_size_build = 4 * 1024^3
  )
}

# The compressions this R reads through gzfile(). zstd is optional: R
# accepts it from 4.5.0 and only where it was built with libzstd.
zrd_compressions <- function() {
  zstd <- tryCatch(
    {
      memCompress(as.raw(1L), type = "zstd")
      TRUE
    },
    error = function(e) FALSE
  )
  c("gzip", "bzip2", "xz", if (zstd) "zstd")
}

#' Build information
#'
#' Reports the version of zurds, the version of the 'zufast' headers it was
#' compiled against, the default limits, and the compressions this R can
#' read.
#'
#' @return A list of class `zurds_info` with elements:
#'   * `version`: the zurds version, a string.
#'   * `zufast_version`: the 'zufast' header version compiled in. 'zufast'
#'     is header-only, so this describes the code inside this package, not
#'     a library loaded at run time.
#'   * `zufast_ok`: `TRUE` if a self-test through 'zufast' passed.
#'   * `limits`: the default `max_depth`, `max_items`, `max_size` (bytes,
#'     for inspection) and `max_size_build` (bytes, for building objects).
#'   * `compressions`: the compressions this R can read, from `"gzip"`,
#'     `"bzip2"`, `"xz"` and `"zstd"`.
#' @export
#' @examples
#' zurds_info()
zurds_info <- function() {
  info <- .Call(zrd_build_info)
  structure(
    list(
      version = unname(getNamespaceVersion("zurds")),
      zufast_version = info$zufast_version,
      zufast_ok = info$zufast_ok,
      limits = zrd_default_limits(),
      compressions = zrd_compressions()
    ),
    class = "zurds_info"
  )
}

#' @export
format.zurds_info <- function(x, ...) {
  lim <- x$limits
  num <- function(v) format(v, scientific = FALSE, big.mark = ",")
  c(
    "<zurds_info>",
    paste0("zurds:          ", x$version),
    paste0("zufast:         ", x$zufast_version,
           if (isTRUE(x$zufast_ok)) " (self-test ok)" else " (self-test FAILED)"),
    paste0("max_depth:      ", lim$max_depth),
    paste0("max_items:      ", num(lim$max_items)),
    paste0("max_size:       ", num(lim$max_size), " bytes"),
    paste0("max_size_build: ", num(lim$max_size_build), " bytes"),
    paste0("compressions:   ", paste(x$compressions, collapse = ", "))
  )
}

#' @export
print.zurds_info <- function(x, ...) {
  writeLines(format(x, ...))
  invisible(x)
}
