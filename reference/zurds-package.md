# zurds: Inspect and Safely Read R Serialization Files

Reads the files R writes with saveRDS(), save() and serialize(), and
lazy-load databases, without handing them to R's deserializer. It
reports a file's header, lists the objects in an '.rda' file, summarises
the structure of any stream, extracts a single object or list element,
and scans for everything that can run code or reach outside the file,
such as promises, closures and environments. Data-only files can be read
with a reader written in C that builds only vectors, lists and
attributes and never evaluates code from the file.

## See also

Useful links:

- <https://github.com/pedrobtz/zurds>

- <https://pedrobtz.github.io/zurds/>

- Report bugs at <https://github.com/pedrobtz/zurds/issues>

## Author

**Maintainer**: Pedro Baltazar <pedrobtz@gmail.com> \[copyright holder\]

Authors:

- Pedro Baltazar <pedrobtz@gmail.com> \[copyright holder\]
