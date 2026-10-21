# rellu
A cross-platform application for designing and prototyping user interfaces,
written in C++.

Windows is the initial target platform; Linux and macOS support will follow.

## Building
Run `build.bat` on Windows or `build.sh` on POSIX systems, optionally passing a
configuration (`Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`), `clean`,
or `reset`. Dependencies are downloaded into a `Dependencies` folder on first
build; use `-DD=<path>` to share a dependencies folder across builds.

## Testing
Each `*Tests` executable runs automatically as a post-build step; a failing
test fails the build. Performance cases that time large documents, such as
`LayersModel/large_document_performance` in `UiTests` and
`Scale/large_document` in `ModelTests`, are marked with `doctest::skip` and
do not run post-build. Run them on demand from `Tests/<config>` in the build
directory, preferably in Release, with `--no-skip`, e.g.
`UiTests --no-skip -tc=large_document_performance`.
