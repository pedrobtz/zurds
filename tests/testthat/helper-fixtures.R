# The fixtures written by tools/make-fixtures.R. ZURDS_FIXTURES points at a
# directory of the full matrix, which CI generates; otherwise the small set
# checked in under fixtures/ is used.
fixture_dir <- function() {
  dir <- Sys.getenv("ZURDS_FIXTURES", "")
  if (nzchar(dir)) dir else test_path("fixtures")
}

# The manifest, minus rows for compressions this R cannot read.
fixture_manifest <- function(dir = fixture_dir()) {
  m <- utils::read.csv(file.path(dir, "manifest.csv"), stringsAsFactors = FALSE)
  m[m$compression %in% c("none", zurds_info()$compressions), ]
}

# The decompressed bytes of a fixture, read by R: the oracle for lengths.
fixture_decompress <- function(path) {
  con <- gzfile(path, "rb")
  on.exit(close(con))
  out <- list()
  repeat {
    b <- readBin(con, "raw", 1e6)
    if (!length(b)) break
    out[[length(out) + 1L]] <- b
  }
  unlist(out)
}
