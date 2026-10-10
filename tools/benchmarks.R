# One benchmark of tools/run-benchmarks, in its own R process so its peak
# memory is its own. Usage: Rscript tools/benchmarks.R STEP FILE
args <- commandArgs(trailingOnly = TRUE)
step <- args[[1]]
file <- args[[2]]
suppressPackageStartupMessages(library(zurds))
t <- switch(step,
  # R with zurds loaded, doing nothing: the floor under every peak.
  idle = system.time(NULL),
  # The baseline for the table of contents: decompressing the file.
  decompress = system.time({
    con <- gzfile(file, "rb")
    while (length(readBin(con, "raw", 2^24))) NULL
    close(con)
  }),
  toc = system.time(toc <- rds_toc(file)),
  load = system.time(load(file, envir = new.env())),
  extract = system.time(x <- rds_extract(file, "df05")),
  readrds = system.time(x <- readRDS(file)),
  read_data = system.time(x <- rds_read_data(file)),
  stop("unknown step ", step)
)
cat(sprintf("%s %.2f\n", step, t[["elapsed"]]))
