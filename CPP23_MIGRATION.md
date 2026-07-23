# C++23 migration record

The C++23 migration is complete. This document records the compatibility
contract and the validation required for future changes.

## Compatibility contract

- The installed API remains C-compatible and is compiled in the test suite by
  a C11 compiler.
- The shared-library ABI remains `libdvdcss.so.2`/the corresponding platform
  library, with these 14 exports:
  `dvdcss_open`, `dvdcss_open_stream`, `dvdcss_close`, `dvdcss_seek`,
  `dvdcss_read`, `dvdcss_readv`, `dvdcss_error`, `dvdcss_is_scrambled`,
  `dvdcss_get_encryption_type`, `dvdcpxm_init`, `dvdcpxm_close`,
  `dvdcpxm_seek`, `dvdcpxm_read`, and `dvdcpxm_readv`.
- C++ implementation symbols have hidden visibility. Exceptions never cross
  the C ABI boundary.
- Cache file names, environment variables, callback signatures, block size,
  flags, pkg-config name, and shared-library compatibility version are
  unchanged.

## Implementation state

- All library translation units are C++ and require C++23.
- Ownership of handles, CPXM state, title keys, paths, and temporary buffers is
  represented with RAII containers.
- Buffer-oriented internals use `std::span`; scatter I/O uses a non-owning,
  allocation-free range over the caller's iovec entries.
- Rich internal failures use `std::expected`; endian conversion uses
  `std::endian` and `std::byteswap`.
- Crypto lookup tables and C2 known-answer vectors are evaluated at compile
  time.
- CPXM cache access is synchronized and keyed by both device and inode.
- Stream callback tables are copied at open time. Invalid callbacks, counts,
  layouts, over-reported reads, and non-sector-aligned partial reads fail
  deterministically without corrupting logical position.

## Supported matrix

- Linux: x86-64 and AArch64, GCC or Clang
- Windows: x86-64 and AArch64, MSVC or MinGW-w64
- macOS: x86-64 and Apple silicon, Apple Clang

Meson rejects other systems and CPU families, and obsolete ioctl/device
backends have been removed from the maintained source.

## Required validation

Before release:

1. Build and run tests with GCC and Clang at warning level 3.
2. Run the AddressSanitizer and UndefinedBehaviorSanitizer configuration.
3. Cross-build MinGW-w64 and run the fixture tests under Wine when available.
4. Confirm the exported-symbol test reports exactly the 14-symbol C ABI.
5. Build with examples enabled.
6. Smoke-test representative CSS and CPXM physical media on each packaged
   operating system; automated tests intentionally require no optical drive.
