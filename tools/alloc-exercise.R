# The workload alloc-failure.yml sweeps: every allocation zurds makes in C
# (the source, its input block, the walk, its stack, reference table and
# symbol names) is failed in turn, and each must surface as an R error, not
# a crash. With --baseline it does everything but the walks, so the sweep
# starts above the allocations it shares with R's own startup. Both modes
# walk a tiny input first, so the lazy loading of zurds' R code falls under
# the floor and the sweep reaches the C allocations quickly.
#
# The connection is opened before the floor too: a failed allocation inside
# R's own gzfile() crashes R (seen 2026-10-10), which is a finding about R.
baseline <- "--baseline" %in% commandArgs(trailingOnly = TRUE)
library(zurds)
x <- list(a = c(1L, 2L), b = letters, c = list(d = 1, e = list(f = "g")))
f <- tempfile(fileext = ".rds")
saveRDS(x, f, compress = FALSE)
bytes <- serialize(x, NULL)
con <- file(f, "rb")
invisible(zurds:::zrd_walk_info(serialize(1L, NULL)))
if (!baseline) {
  for (input in list(bytes, con)) {
    res <- zurds:::zrd_walk_info(input)
    stopifnot(res$status == "ZRD_OK", res$items == 53)
  }
}
close(con)
unlink(f)
