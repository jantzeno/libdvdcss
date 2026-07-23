# libdvdcss C++23

This repository is a C++23 implementation of libdvdcss that preserves the
established C API and shared-library ABI used by applications such as
HandBrake. It supports CSS for DVD-Video and CPXM (CPPM/CPRM) for DVD-Audio
and recordable media.

This is an experimental fork and is not affiliated with VideoLAN.

## Supported targets

| Operating system | Architectures | Toolchains |
| --- | --- | --- |
| Linux | x86-64, AArch64 | GCC, Clang |
| Windows | x86-64, AArch64 | MSVC, MinGW-w64 |
| macOS | x86-64, Apple silicon | Apple Clang |

Meson checks the target and the required C++23 standard-library facilities at
configure time. Other operating systems and 32-bit targets are intentionally
rejected.

The installed headers remain C11-compatible. Only the implementation requires
C++23, and only the documented C symbols are exported from the shared library.

## Build and test

Requirements:

- Meson 1.3 or newer
- Ninja
- a C11 compiler
- a C++23 compiler and standard library with `std::byteswap`,
  `std::expected`, `std::filesystem`, `std::format`, and `std::span`

```sh
meson setup build --buildtype=release
meson compile -C build
meson test -C build --print-errorlogs
```

Install with:

```sh
meson install -C build
```

`pkg-config --cflags --libs libdvdcss` supplies the installed consumer flags.
See [INSTALL](INSTALL) for cross-compilation and sanitizer builds.

Useful Meson options:

- `-Denable_tests=true|false` controls automated tests and defaults to `true`.
- `-Denable_examples=true|false` builds `csstest` and, on Linux,
  `dvd_region`; it defaults to `false`.
- `-Denable_docs=true|false` builds the Doxygen API documentation.

The automated suite uses in-memory stream fixtures and does not require a DVD
drive. A physical-disc smoke test remains appropriate before distributing a
new platform package.

## API compatibility

The public headers are:

- `<dvdcss/dvdcss.h>` for CSS access and decryption
- `<dvdcss/dvdcpxm.h>` for CPXM access and decryption

The ABI retains the `libdvdcss.so.2` compatibility version and exports exactly
the documented `dvdcss_*` and `dvdcpxm_*` entry points. C++ exceptions are
contained at every C boundary and are reported through normal return values
and `dvdcss_error()`.

## Runtime configuration

`DVDCSS_METHOD={key|disc|title}` selects the CSS key-recovery method:

- `key` uses built-in player keys and is the default.
- `disc` cracks the disc key before decrypting title keys.
- `title` derives title keys from encrypted sectors.

`DVDCSS_VERBOSE={0|1|2}` controls logging. `0` is silent, `1` reports errors,
and `2` also enables debug output.

`DVDCSS_CACHE=off` disables the key cache. Otherwise, the value names the
cache directory. If unset, the default is `.dvdcss` beneath the user's home
directory on Unix-like systems and `dvdcss` beneath AppData on Windows.

`DVDCSS_RAW_DEVICE` overrides the raw device used for authentication while
reading through another target.

## License

libdvdcss is distributed under the GNU General Public License, version 2 or
later. See [COPYING](COPYING).
