#!/usr/bin/env Rscript
# Writes the fixtures every stage tests against, with R itself as the
# writer (design section 14), and a manifest of what each file is.
#
# Usage: Rscript tools/make-fixtures.R [--full] [outdir]
#
# Without --full it writes the small set checked in under
# tests/testthat/fixtures/: every object once (XDR, version 3, gzip), one
# object across the whole format x version x compression matrix, and the
# .rda and lazy-load files. With --full it writes every object across the
# whole matrix, which CI does on oldrel, release and devel. The manifest
# records the writing R version and each file's SHA-256 (R >= 4.5).

args <- commandArgs(trailingOnly = TRUE)
full <- "--full" %in% args
args <- setdiff(args, "--full")
out <- if (length(args)) args[[1]] else file.path("tests", "testthat", "fixtures")
dir.create(out, recursive = TRUE, showWarnings = FALSE)
unlink(list.files(out, full.names = TRUE), recursive = TRUE)

# zstd is written only where this R was built with it.
compressions <- c("none", "gzip", "bzip2", "xz",
  if (!inherits(try(memCompress(as.raw(1L), "zstd"), silent = TRUE), "try-error")) "zstd")
formats <- c("xdr", "binary", "ascii")
versions <- c(2L, 3L)

# Objects written to every file. Kept small: the matrix multiplies them.
fixture_objects <- function() {
  e <- new.env(parent = emptyenv())
  assign("a", 1L, envir = e)
  list(
    null = NULL,
    logical = c(TRUE, FALSE, NA),
    integer = c(1L, -2L, NA, .Machine$integer.max),
    double = c(1.5, -0, NA, NaN, Inf, -Inf, 1e-300),
    complex = c(1 + 2i, NA, complex(real = Inf, imaginary = -1)),
    character = c("a", NA, "", "café", iconv("naïve", "UTF-8", "latin1")),
    bytes = `Encoding<-`("\xff\xfe", "bytes"),
    raw = as.raw(c(0, 1, 255)),
    list = list(1L, "b", list(TRUE, NULL)),
    named_list = list(x = 1, y = list(z = "deep")),
    # c() rather than 1:3, which would stay a compact (ALTREP) sequence.
    data_frame = data.frame(n = c(1L, 2L, 3L), s = c("x", "y", NA), stringsAsFactors = FALSE),
    factor = factor(c("lo", "hi", "lo"), levels = c("lo", "hi")),
    matrix = matrix(1:6, 2, dimnames = list(c("r1", "r2"), NULL)),
    compact_seq = 1:10,
    pairlist = pairlist(a = 1, b = "two"),
    symbol = quote(sym),
    language = quote(f(x, y = 2)),
    closure = local(function(x) x + 1, envir = globalenv()),
    bytecode = compiler::cmpfun(function(x) x * 2),
    environment = e,
    formula = y ~ x,
    external_pointer = methods::new("externalptr"),
    env_cycle = local({
      e <- new.env(parent = emptyenv())
      e$self <- e
      lockEnvironment(e)
      e
    }),
    promise = local({
      e <- new.env(parent = emptyenv())
      delayedAssign("p", stop("never forced"), assign.env = e)
      e
    }),
    builtin = sum,
    special = `if`,
    namespace_closure = stats::sd,
    package_env = as.environment("package:stats"),
    deferred_string = as.character(1:5),
    s4 = local({
      where <- new.env()
      methods::setClass("ZurdsFixture", representation(x = "numeric"), where = where)
      methods::new("ZurdsFixture", x = 1)
    })
  )
}

# The bytes of one stream in the given format and version.
stream_bytes <- function(obj, format, version) {
  switch(format,
    xdr = serialize(obj, NULL, version = version),
    binary = serialize(obj, NULL, xdr = FALSE, version = version),
    ascii = serialize(obj, NULL, ascii = TRUE, version = version)
  )
}

write_compressed <- function(bytes, path, compression) {
  con <- switch(compression,
    none = file(path, "wb"),
    gzip = gzfile(path, "wb"),
    bzip2 = bzfile(path, "wb"),
    xz = xzfile(path, "wb"),
    zstd = zstdfile(path, "wb")
  )
  on.exit(close(con))
  writeBin(bytes, con)
}

# An .rda: the magic line, then a serialized pairlist of name = value, as
# save() writes. Written by hand only for native binary (RDB3), which save()
# has no argument for; the others go through save() itself.
write_rda <- function(objs, path, format, version, compression) {
  if (format == "binary") {
    magic <- charToRaw(if (version == 2L) "RDB2\n" else "RDB3\n")
    bytes <- c(magic, serialize(as.pairlist(objs), NULL, xdr = FALSE, version = version))
    write_compressed(bytes, path, compression)
  } else {
    env <- list2env(objs)
    save(list = names(objs), file = path, envir = env, ascii = format == "ascii",
         version = version, compress = if (compression == "none") FALSE else compression)
  }
}

ext <- c(none = "", gzip = ".gz", bzip2 = ".bz2", xz = ".xz", zstd = ".zst")
rows <- list()
add <- function(file, kind, object, format, version, compression) {
  rows[[length(rows) + 1L]] <<- data.frame(
    file = file, kind = kind, object = object, format = format,
    version = version, compression = compression, stringsAsFactors = FALSE
  )
}

objs <- fixture_objects()
matrix_objects <- if (full) names(objs) else "named_list"

# Every object once in the default layout, as saveRDS() writes it.
for (nm in names(objs)) {
  f <- paste0(nm, ".rds")
  saveRDS(objs[[nm]], file.path(out, f))
  add(f, "rds", nm, "xdr", 3L, "gzip")
}

# A persistent reference: an environment written through a refhook, as
# saveRDS(refhook =) writes it. In the default layout only.
local({
  f <- "persistent.rds"
  saveRDS(new.env(), file.path(out, f), refhook = function(x) "zurds-ref")
  add(f, "rds", "persistent", "xdr", 3L, "gzip")
})

# The matrix.
for (nm in matrix_objects) for (fmt in formats) for (v in versions) for (comp in compressions) {
  f <- sprintf("%s-%s-v%d-%s.rds", nm, fmt, v, comp)
  write_compressed(stream_bytes(objs[[nm]], fmt, v), file.path(out, f), comp)
  add(f, "rds", nm, fmt, v, comp)
}

# .rda files: several objects, every format, both versions, gzip; and the
# default layout across every compression.
rda_objs <- objs[c("integer", "character", "data_frame", "named_list")]
for (fmt in formats) for (v in versions) {
  f <- sprintf("multi-%s-v%d-gzip.rda", fmt, v)
  write_rda(rda_objs, file.path(out, f), fmt, v, "gzip")
  add(f, "rda", paste(names(rda_objs), collapse = " "), fmt, v, "gzip")
}
for (comp in compressions) {
  f <- sprintf("multi-xdr-v3-%s.rda", comp)
  write_rda(rda_objs, file.path(out, f), "xdr", 3L, comp)
  add(f, "rda", paste(names(rda_objs), collapse = " "), "xdr", 3L, comp)
}

# A lazy-load database, as R CMD INSTALL builds a package's.
ll_env <- list2env(objs[c("integer", "data_frame", "closure")])
tools:::makeLazyLoadDB(ll_env, file.path(out, "lazyload"))
for (f in c("lazyload.rdb", "lazyload.rdx")) {
  add(f, "lazyload", paste(ls(ll_env), collapse = " "), "xdr", 3L, "zlib")
}

manifest <- do.call(rbind, rows)
manifest$writer <- as.character(getRversion())
sha <- if (exists("sha256sum", asNamespace("tools"))) tools::sha256sum else function(f) NA_character_
manifest$sha256 <- unname(sha(file.path(out, manifest$file)))
write.csv(manifest, file.path(out, "manifest.csv"), row.names = FALSE)
message(nrow(manifest), " fixtures written to ", out)
