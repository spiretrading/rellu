# gesso
A cross-platform application for designing and prototyping user interfaces,
written in C++.

Windows is the initial target platform; Linux and macOS support will follow.

## Building
Run `build.bat` on Windows or `build.sh` on POSIX systems, optionally passing a
configuration (`Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`), `clean`,
or `reset`. Dependencies are downloaded into a `Dependencies` folder on first
build; use `-DD=<path>` to share a dependencies folder across builds.
