# libdvdcss C++20 Modernization Plan

**Constraint:** Public C API (`src/dvdcss/dvdcss.h`, `src/dvdcss/dvdcpxm.h`) stays `extern "C"` and ABI-stable throughout. All changes are internal.

## Phase 0 — Build & Baseline

- [x] Bump `cpp_std=c++20` in `meson.build` (keep `c_std=c17` for public header validation)
- [ ] Verify clean build under GCC and Clang with `-std=c++20`
- [x] Verify MinGW-w64 cross-compile still passes syntax checks
- [ ] Add CI matrix covering GCC ≥ 10, Clang ≥ 13 for C++20 support

## Phase 1 — Low-Risk Internal Modernization

### Attributes & annotations

- [x] Add `[[nodiscard]]` to all internal functions returning error codes (`dvdcss_test`, `dvdcss_title`, `dvdcss_disckey`, `GetBusKey`, `GetASF`, ioctl helpers)
- [x] Add `[[likely]]`/`[[unlikely]]` on hot-path branches in CSS/CPXM crypto loops
- [x] Add `noexcept` to functions that never throw (error.cpp formatters, lookup-table helpers, byte-swap utilities)

### Type safety & casts

- [x] Replace remaining C-style casts with `static_cast`/`reinterpret_cast` across all `.cpp` files
- [x] Replace `memcpy`-based type punning with `std::bit_cast` where applicable (endian conversions in `bswap.h`, `cpxm.cpp`)

### Byte-swap layer (`src/bswap.h`)

- [x] Replace `B2N_*` macros with `constexpr` inline functions using `std::endian` and compiler builtins (or `std::byteswap` if targeting C++23 fallback)
- [x] Replace `READ64_BE` macro in `src/cpxm.h` with a `constexpr` function returning `uint64_t`

### Enum modernization

- [x] Convert `enum dvdcss_method` to `enum class` in `src/libdvdcss.h`
- [x] Audit and convert any other bare enums to scoped enums

### Constants

- [x] Convert `#define` constants that aren't part of the public C API to `constexpr` (`DVD_KEY_SIZE`, `CACHE_FILENAME_LENGTH_STRING`, internal buffer sizes in `ioctl.h`)
- [x] Convert `static const` lookup tables in `src/csstables.h` to `constexpr std::array`

## Phase 2 — Data Structures & RAII

### `dvdcss_s` struct (`src/libdvdcss.h`)

- [x] Replace `char *psz_device` (strdup/free) with `std::string`
- [x] Replace `char psz_cachefile[PATH_MAX]` + `char *psz_block` with `std::filesystem::path`
- [x] Replace `const char *psz_error` with `std::string` or `std::string_view` to a static table
- [x] Replace raw function pointers `pf_seek`/`pf_read`/`pf_readv` with a device-strategy abstraction (virtual base class or `std::function`)
- [x] Replace Win32 `char *p_readv_buffer` / `int i_readv_buf_size` with `std::vector<uint8_t>`

### Title key cache (`src/css.h` / `src/css.cpp`)

- [x] Replace `dvd_title` intrusive linked list with `std::vector<dvd_title>`
- [x] Replace `dvd_key` raw `uint8_t[5]` typedef with `std::array<uint8_t, 5>`
- [x] Replace `dvd_key` members in `struct css` with `std::array<uint8_t, DVD_KEY_SIZE>`

### CPXM cache (`src/libdvdcpxm.cpp`)

- [x] Replace global `cpxm_cache` linked list with `std::list<CpxmCacheEntry>` or `std::vector`
- [x] Replace `malloc`/`calloc`/`free` of `p_cpxm` state with `std::unique_ptr<cpxm_s>`
- [x] Replace MKB raw buffer `malloc` with `std::vector<uint8_t>`

### Device layer (`src/device.cpp`)

- [x] Wrap file descriptor lifetime in an RAII `ScopedFd` class (handles `close()`/`CloseHandle()`)
- [x] Replace hardcoded default device path arrays with `constexpr std::array<std::string_view, N>`

### Library open/close (`src/libdvdcss.cpp`)

- [x] Replace `malloc(sizeof(*dvdcss))` with `std::make_unique<dvdcss_s>()`
- [x] Move cleanup logic from `dvdcss_close()` into a `dvdcss_s` destructor
- [x] Replace `exists_or_mkdir()` loop with `std::filesystem::create_directories()`

## Phase 3 — Modern APIs & Idioms

### `std::span` for buffer parameters

- [x] Change internal buffer-passing functions (ioctl helpers, `dvdcss_unscramble`, CSS/CPXM routines) to accept `std::span<uint8_t>` or `std::span<const uint8_t>`
- [x] Replace iovec-style scatter I/O with `std::span<std::span<uint8_t>>` internally (keep raw iovec at the C API boundary)

### `std::format` / `std::print` for string building

- [x] Replace `snprintf`-based cache path construction in `src/libdvdcss.cpp` with `std::format`
- [x] Replace `fprintf`/`vfprintf` in `src/error.cpp` with `std::format` + write (or `std::print` if C++23 available)

### Constexpr crypto tables (`src/cpxm.cpp` / `src/libdvdcpxm.cpp`)

- [x] Make `sbox`, `perm_variant`, device-key tables `constexpr std::array`
- [x] Convert runtime `c2_init()` S-box expansion into a `consteval` / `constexpr` generator function
- [x] Validate crypto output at compile time with `static_assert` on known test vectors

### Ioctl platform dispatch (`src/ioctl.cpp`)

- [x] Replace `INIT_RDC` / `INIT_USCSI` / `INIT_DVDIOCTL` / `INIT_CPT` / `INIT_SSC` macro families with `constexpr` builder functions or platform-specific factory helpers
- [x] Replace `memset(&struct, 0, sizeof)` patterns with value-initialization `{}`

### Error handling internals

- [x] Use `std::optional<T>` for internal functions that currently return sentinel values (e.g., cache lookups, key retrieval)
- [x] Use `std::expected<T, ErrorCode>` (C++23, or a polyfill) for functions with rich error information if warranted

### Namespace & linkage

- [x] Wrap all internal symbols in a `dvdcss` namespace (or `dvdcss::detail`)
- [x] Remove mixed-language `extern "C"` seams from internal headers (`css.h`, `device.h`) since no C TUs remain
- [x] Remove the `print_error_cpp`/`print_debug_cpp` macro remap in `src/libdvdcss.h` — use a single C++ declaration

## Phase 4 — Cleanup & Validation

- [x] Remove `c` from `project()` languages in `meson.build` (keep only for public header install-test if desired)
- [x] Remove C-only compatibility shims (`MALLOC_CAST`, etc.) if any survive
- [x] Audit all `#include` directives: replace C headers (`<string.h>`, `<stdlib.h>`, `<stdio.h>`) with C++ equivalents (`<cstring>`, `<cstdlib>`, `<cstdio>`) in implementation files
- [ ] Run AddressSanitizer + UBSan on test/example binaries to validate no regressions
- [x] Build and verify a standalone C consumer against the installed public headers (the `extern "C"` ABI contract)
- [x] Cross-compile check with MinGW-w64 (`x86_64-w64-mingw32-g++`)
- [ ] Profile CSS key-cracking hot path to verify no performance regression from container changes
