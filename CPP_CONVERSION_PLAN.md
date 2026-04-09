# libdvdcss C-to-C++ Conversion Plan

## Scope

This plan covers an incremental, file-by-file migration of the tracked source tree from C to C++ while preserving the installed C ABI in `src/dvdcss/*.h`. The objective is to end with a C++ implementation, a stable C-facing API, and a build that can tolerate mixed `.c` and `.cpp` translation units during the transition.

This plan does not include generated files under `builddir/`; those should be regenerated as part of each migration step.

## Constraints And Non-Negotiables

- [ ] Keep the public ABI C-compatible for the full migration. `src/dvdcss/dvdcss.h` and `src/dvdcss/dvdcpxm.h` already expose `extern "C"` guards and should remain installable from C projects.
- [ ] Convert incrementally. Do not rename every file in one change; mixed C/C++ builds will make regressions easier to isolate.
- [ ] Preserve platform support. `ioctl.c`, `device.c`, and the Windows/OS-specific blocks in headers are the highest portability risk.
- [ ] Avoid changing on-disk cache formats, ioctl behavior, or decryption logic while doing mechanical language conversion.
- [ ] Prefer a C-like C++ style first. Introduce RAII and stronger typing only after the code compiles cleanly as C++.

## Global Preparation

### Phase 0: Build System And Policy

- [x] Update `meson.build` to declare both `c` and `cpp` project languages during the transition.
- [x] Add a project-wide C++ standard, preferably `cpp_std=c++17` or newer, without changing the existing `c_std=c17` until all remaining `.c` files are gone.
- [x] Ensure common compiler arguments are applied per language instead of assuming C-only compilation.
- [x] Keep symbol visibility and install rules unchanged.
- [ ] Add a CI/build matrix that exercises both GCC and Clang in mixed-language mode if available.

Recommended first Meson target state:

- [x] Top-level project languages: `['c', 'cpp']`
- [x] Standards: keep `c17`, add `c++17`
- [x] Mixed object list: allow `.c` and `.cpp` in the same `library()` target

### Phase 1: Cross-Cutting Compatibility Fixes

Apply these before renaming any implementation files:

- [x] Audit all internal headers for C++ compatibility.
- [x] Replace C constructs that are invalid in C++:
   - [x] implicit `void *` conversions
   - [x] designated initializers that are not portable across the chosen C++ standard
   - [x] identifiers that collide with C++ keywords or stricter type rules
   - [x] macro patterns that rely on C-only behavior
- [x] Ensure every internal header is self-contained under C++ compilation.
- [x] Keep exported declarations inside `extern "C"` only where the symbol is part of the public ABI.
- [x] Decide whether internal functions remain C linkage or move to normal C++ linkage. The simplest path is to keep only the public API in `extern "C"`.

Audit note:

- [x] Current Linux/GCC C++17 verification passed for `src/common.h`, `src/bswap.h`, `src/css.h`, `src/device.h`, `src/ioctl.h`, `src/cpxm.h`, `src/libdvdcpxm.h`, and `src/libdvdcss.h`, both individually and in aggregate.
- [x] Source-level C++ cleanup pass replaced implicit `void *` conversions in public/internal read paths, added explicit allocation casts in not-yet-converted `.c` files, hardened byte-swap macros into statement-safe assignment forms, and added explicit `dlsym()` function-pointer casts for the Solaris path.
- [x] Linkage policy is now explicit: installed headers keep `extern "C"` only around exported ABI functions, while private logging helpers use normal C++ linkage for C++ callers plus a narrow C-linkage bridge so the remaining C translation units still link during the mixed-language phase.
- [x] Remaining dormant-platform review items: Win32-specific type remapping in `src/common.h`, and Win32 ioctl structure definitions in `src/ioctl.h` that rely on non-portable layout patterns.
- [x] Win32 validation passed under a stubbed C++17 Windows-header environment for the `_MSC_VER` and `__MINGW32__` branches in `src/common.h`, and for the `_WIN32` branch in `src/ioctl.h`.
- [x] Win32 validation also passed with the real `x86_64-w64-mingw32-g++` frontend for `src/common.h` and `src/ioctl.h`.

## File-By-File Order

The order below is optimized for dependency control and risk containment, not filename order.

### 1. Build And Installed Header Layer

#### `meson.build`

Purpose:
Make mixed C/C++ compilation possible before any file renames.

Work:

- [x] Add `cpp` as a project language.
- [x] Add a `cpp_std` default option.
- [x] Split language-specific arguments where needed.

Exit criteria:

- [x] The project configures successfully with both C and C++ compilers enabled.
- [x] A no-op build still succeeds before source conversion starts.

#### `src/meson.build`

Purpose:
Allow source-by-source migration from `.c` to `.cpp`.

Work:

- [x] Rename entries in `dvdcss_src` one file at a time as files are converted.
- [x] Keep the library target name, install settings, and pkg-config generation unchanged.

Exit criteria:

- [x] The source list accurately reflects a mixed `.c` and `.cpp` set throughout the migration.

#### `test/meson.build`

Purpose:
Keep example/test programs building during the mixed-language period.

Work:

- [x] Update source filenames when test files are renamed.
- [x] Verify any C++-only linker requirements are picked up automatically by Meson.

Exit criteria:

- [x] Both test executables still link after each relevant conversion.

#### `src/dvdcss/dvdcss.h`

Purpose:
Preserve the public C API while making the header safe for C++ consumers.

Work:

- [x] Keep `extern "C"` guards exactly around the exported C API.
- [x] Verify callback signatures remain C-compatible.
- [x] Check macro exports and visibility attributes under C++ compilers.

Exit criteria:

- [x] The header compiles as both C and C++.
- [x] Existing C clients need no source changes.

Validation note:

- [x] `src/dvdcss/dvdcss.h` now keeps `extern "C"` only around exported function declarations; the opaque handle typedef, callback typedef, and flag macros remain outside the linkage block.
- [x] Native C and C++ consumer snippets compiled successfully against the generated/uninstalled header set, including `dvdcss_stream_cb` callback assignment and `dvdcss_open_stream()` usage.
- [x] Win32-style import/export macro forms in `LIBDVDCSS_EXPORT` compiled successfully under `x86_64-w64-mingw32-g++` with both `LIBDVDCSS_IMPORTS` and `LIBDVDCSS_EXPORTS` defined.

#### `src/dvdcss/dvdcpxm.h`

Purpose:
Do the same ABI-preservation pass for the CPXM public API.

Work:

- [x] Confirm exported declarations remain C-compatible.
- [x] Validate integer types and include order under C++.

Exit criteria:

- [x] The header compiles cleanly in both languages.

Validation note:

- [x] `src/dvdcss/dvdcpxm.h` keeps only the exported CPXM API functions inside `extern "C"`, while public constants and fixed-width integer usage remain outside the linkage block.
- [x] Native C and C++ consumer snippets compiled successfully when including only `src/dvdcss/dvdcpxm.h`, confirming that `uint8_t`, `dvdcss_t`, and `LIBDVDCSS_EXPORT` arrive through the public include chain without extra include ordering requirements.
- [x] C++ include-order checks passed with both `dvdcpxm.h` before `dvdcss.h` and `dvdcss.h` before `dvdcpxm.h`, and Win32-target syntax checks passed under `x86_64-w64-mingw32-g++` with both `LIBDVDCSS_IMPORTS` and `LIBDVDCSS_EXPORTS` defined.

#### `src/dvdcss/version.h.in`

Purpose:
Low risk, but verify generation still works unchanged when the project becomes mixed-language.

Work:

- [x] No substantive API changes expected.
- [x] Only touch if C++ compilation reveals macro or include-order issues.

Validation note:

- [x] No template changes were required: `src/dvdcss/version.h.in` still expands to a language-agnostic macro header with numeric version components, a string literal, and a pure macro `DVDCSS_VERSION_CODE()` helper.
- [x] The generated `builddir/src/dvdcss/version.h` compiled successfully as a standalone C and C++ header, and its computed `DVDCSS_VERSION` value matched the expanded major/minor/micro macros.
- [x] Include-order checks passed when `dvdcss/version.h` and `dvdcss/dvdcss.h` were included in either order, and MinGW-w64 C++ syntax checks also passed for the generated header.

### 2. Internal Header Layer

Convert headers before implementations that include them.

#### `src/common.h`

Why first:
It defines platform-dependent type and function remaps that many implementation files inherit.

Work:

- [x] Verify the Windows `off_t`, `ssize_t`, and function remapping macros behave under C++.
- [x] Remove any C-style assumptions around typedef redefinition if they fail in C++.

Risk:
High on Windows, low elsewhere.

#### `src/bswap.h`

Why early:
Byte-swap helpers are likely to be included by CPXM code and may contain macro tricks that need stricter typing.

Work:

- [x] Ensure macros or inline helpers are valid in C++.
- [x] Prefer `static inline` or `constexpr` only if that does not change ABI or behavior.

Validation note:

- [x] `src/bswap.h` now uses statement-safe assignment macros for `B2N_16`, `B2N_32`, and `B2N_64`, which compile cleanly as C++ while preserving the existing in-place update semantics at call sites.
- [x] Standalone C++ validation passed for both the active little-endian path and the no-op `WORDS_BIGENDIAN` path, and the `READ64_BE` helper in `src/cpxm.h` continued to compile cleanly through the CPXM include chain.
- [x] No conversion to `static inline` or `constexpr` was needed: keeping macros avoids changing the current platform-selection logic and lvalue-style mutation behavior while the codebase is still mixed C/C++.

#### `src/css.h`

Why early:
Defines shared internal structures used by the core library.

Work:

- [x] Keep POD layout stable.
- [x] Make typedefs and forward declarations C++-clean.

Validation note:

- [x] `dvd_key`, `dvd_title`, and `css` remain plain data declarations with stable field order and no C++-only members; C++ validation confirmed the structs still satisfy standard-layout/trivial expectations for the current implementation style.
- [x] The `dvdcss_unscramble()` prototype in `src/css.h` now matches the implementation by using the `dvd_key` typedef for the key parameter, removing the lingering array-bound/signature mismatch warning without changing call-site behavior.
- [x] Mixed-language rebuild validation passed after the prototype cleanup, and no additional typedef or forward-declaration changes were required for C++ compatibility.

#### `src/device.h`

Why early:
Declares `struct iovec` fallback logic and device entry points.

Work:

- [x] Validate the fallback `iovec` definition under C++.
- [x] Ensure include ordering and `size_t` visibility stay correct.

Risk:
Medium because platform headers vary.

Validation note:

- [x] `src/device.h` now includes `<stddef.h>` before the fallback `struct iovec` branch so `size_t` is provided explicitly instead of relying on platform header side effects.
- [x] Standalone C++ validation passed on the active `sys/uio.h` path, and a forced fallback build under `x86_64-w64-mingw32-g++` also accepted the local `struct iovec` declaration after undefining `HAVE_SYS_UIO_H`.
- [x] No layout or signature changes were required for the device entry points; the change is limited to making the fallback type definition self-contained under mixed-language builds.

#### `src/ioctl.h`

Why early:
Contains the densest macro and platform-API surface in the repo.

Work:

- [x] Audit packed structs, bitfields, zero-length arrays, and Windows typedefs for C++ compiler acceptance.
- [x] Replace C-only allocation or cast assumptions where necessary.
- [x] Keep binary layouts unchanged.

Risk:
Very high across Windows, BSD, Solaris, QNX, and OS/2 code paths.

Validation note:

- [x] `src/ioctl.h` now includes `<stdlib.h>` for the `__QNXNTO__` branch so the `INIT_CPT` allocation macro no longer relies on an external C declaration of `malloc`.
- [x] A forced QNX-path C++ syntax check confirmed that `INIT_CPT` expands cleanly with typed allocation and cleanup in C++.
- [x] Win32-target C++ validation confirmed that the earlier `DVD_COPY_PROTECT_PARAMETERS` and `DVD_COPY_PROTECT_KEY` layout cleanup preserved key binary invariants, including `offsetof(DVD_COPY_PROTECT_KEY, KeyData) == 24`, `DVD_COPY_PROTECT_KEY_HEADER_SIZE == 24`, and the expected compact sizes for `DVD_RPC_KEY` and `DVD_ASF`.

#### `src/cpxm.h`

Why early:
Shares CPXM state and macros with both the public and private implementation layers.

Work:

- [x] Make the `READ64_BE` macro safe under C++.
- [x] Ensure `p_cpxm` and related typedefs stay plain-data friendly.

Validation note:

- [x] `READ64_BE` compiled cleanly in C++ for both direct `uint64_t` destinations and struct-member lvalues, using byte-array and byte-pointer sources without relying on aliasing-unsafe casts.
- [x] The `WORDS_BIGENDIAN` path also compiled cleanly through `cpxm.h`, confirming that the macro remains valid regardless of which `B2N_64` branch is active.
- [x] `cpxm_s` remains a trivial standard-layout struct with five contiguous `uint64_t` fields, and `p_cpxm` remains a plain pointer typedef suitable for the current C-style state management.

#### `src/libdvdcpxm.h`

Why early:
Defines CPXM internal types and constants used by newer code.

Work:

- [x] Validate bitfields, nested structs, and fixed-width integer use under C++.
- [x] Confirm the public include chain remains valid when this header is compiled from a `.cpp` file.

Validation note:

- [x] `device_key_t`, `cprm_media_id_t`, `cprm_mkb_desc_t`, and `cprm_mkb_t` compiled cleanly as trivial standard-layout types in C++, with the nested `id_media` bitfields remaining assignable and the fixed-width integer fields keeping their expected layout.
- [x] Layout checks confirmed the current binary assumptions used by the CPRM paths, including `sizeof(cprm_media_id_t) == 18`, `offsetof(cprm_media_id_t, dvd_mac) == 8`, `sizeof(cprm_mkb_desc_t) == 16`, and `offsetof(cprm_mkb_t, mkb) == 16`.
- [x] Standalone C++ include-chain checks passed when including only `src/libdvdcpxm.h`, and the header also compiled cleanly when `src/dvdcss/dvdcpxm.h` was included first; MinGW-w64 C++ accepted the same header and layout assertions unchanged.

#### `src/libdvdcss.h`

Why late in header phase:
It aggregates nearly every internal dependency.

Work:

- [x] Keep `struct dvdcss_s` layout stable while the codebase is mixed-language.
- [x] Decide whether function pointers stay raw C-style or receive explicit casts/wrappers in C++ implementation files.
- [x] Avoid introducing constructors, references, or non-POD members until all C callers are isolated behind the public API.

Risk:
High because this struct is central to the whole library.

Validation note:

- [x] `dvdcss_s` compiled cleanly as a trivial standard-layout type in C++, and focused offset checks confirmed that the core state ordering remains intact across the file-access, CSS, CPXM, and stream-callback portions of the struct.
- [x] The internal `pf_seek`, `pf_read`, and `pf_readv` members keep their existing raw C-style function-pointer types; C++ validation confirmed that same-signature helper functions still assign directly, so wrapper layers are not required yet during the mixed-language phase.
- [x] C++ consumers of `src/libdvdcss.h` continue to see the intended private logging policy through the `print_error` and `print_debug` macro remap, and the header compiled cleanly both after `src/dvdcss/dvdcss.h` and under a MinGW-w64 C++ check of the Win32-tail layout path.

### 3. Lowest-Risk Implementation Files

Start with files that are isolated or mostly algorithmic. Rename each file from `.c` to `.cpp`, fix compile errors, rebuild, and run tests before moving on.

#### `src/error.c` -> `src/error.cpp`

Why first:
Usually self-contained and a good probe for variadic function compatibility under C++.

Focus:

- [x] Variadic formatting and const-correctness.
- [x] Any implicit string-literal or pointer conversions.

#### `src/cpxm.c` -> `src/cpxm.cpp`

Why next:
Algorithm-heavy code with fewer OS-entry-point dependencies than device access.

Focus:

- [x] Cast cleanup.
- [x] Fixed-width integer arithmetic.
- [x] Macro-heavy byte-order helpers.

Validation note:

- [x] The implementation now lives in `src/cpxm.cpp`; the source compiled as C++17 without algorithm changes beyond the filename move, confirming that the earlier cast cleanup and `READ64_BE` hardening were already sufficient for this unit.
- [x] The CPXM entry points keep a narrow C-linkage seam during the mixed-language phase so the remaining C implementation files still bind to `cppm_set_id_album()` and `cprm_set_id_media()`, while `cpxm.cpp` explicitly links to the still-C CSS and ioctl helpers it depends on.
- [x] Meson was reconfigured for both the main and examples-enabled build directories, `meson compile -C builddir` and `meson compile -C builddir-tests-mixed` both succeeded, and a tiny external-style C consumer still compiled cleanly against the public `dvdcss.h` and `dvdcpxm.h` headers.

#### `src/libdvdcpxm.c` -> `src/libdvdcpxm.cpp`

Why next:
Keeps the CPXM subsystem coherent before touching the main DVD CSS path.

Focus:

- [ ] Interaction with `p_cpxm` state.
- [ ] Allocation and cleanup patterns that may want RAII later.

### 4. Core CSS Logic

#### `src/css.c` -> `src/css.cpp`

Why here:
Central algorithmic code, but less OS-specific than device and ioctl paths.

Focus:

- [ ] Table lookups and integer conversions.
- [ ] Any `void *` casts and legacy macros.
- [ ] Preservation of exact decryption behavior.

#### `src/csstables.h`

Why with `css.cpp`:
It likely exists only to support CSS lookup-table logic.

Focus:

- [ ] Ensure constant table declarations remain usable from C++.
- [ ] Prefer `static const` or `constexpr` only if object layout and linkage remain compatible with the current usage.

### 5. Device And I/O Layer

#### `src/device.c` -> `src/device.cpp`

Why after CSS logic:
This file bridges library state to platform I/O callbacks and file descriptors.

Focus:

- [ ] Function-pointer assignments in `dvdcss_s`.
- [ ] Raw buffer allocation and ownership.
- [ ] Windows-specific `readv` emulation details.

Risk:
High because it sits between core logic and all operating-system access.

#### `src/ioctl.c` -> `src/ioctl.cpp`

Why near the end:
This is the most platform-fragile implementation file and should be converted only after header cleanup and lower-risk files are stable.

Focus:

- [ ] Platform-specific ioctl request structs.
- [ ] Manual buffer casting.
- [ ] Compiler acceptance of system-header interactions across supported OSes.

Risk:
Highest single-file risk in the repository.

### 6. Top-Level Library Orchestration

#### `src/libdvdcss.c` -> `src/libdvdcss.cpp`

Why near last:
It owns process environment parsing, cache-path logic, device opening, and the public API entry points.

Focus:

- [ ] Public entry points must preserve C linkage and signatures.
- [ ] Memory allocation, ownership, and cleanup should stay behaviorally identical before any RAII refactor.
- [ ] Environment variable and filesystem logic must remain portable.

Recommended rule for this file:
Do not redesign internals during the conversion rename. First make it compile as C++ with minimal changes, then consider cleanup in a later pass.

### 7. Tests And Example Programs

#### `test/csstest.c` -> `test/csstest.cpp`

Why now:
It depends only on the public API and should validate that C-callable headers still work when the consumer is C++.

Focus:

- [ ] Replace legacy C-style casts if needed.
- [ ] Keep behavior identical.

#### `test/dvd_region.c` -> `test/dvd_region.cpp`

Why last:
It directly includes `ioctl.c`, which makes it the most awkward test-side migration target.

Focus:

- [ ] Decide whether to keep source inclusion of `ioctl.c` or refactor the needed helpers into a reusable internal unit first.
- [ ] Verify C++ compilation does not create duplicate-definition or linkage surprises.

Risk:
High because it couples directly to internal implementation details.

## Suggested Commit Sequence

Use one focused change per step.

- [x] Mixed-language Meson enablement.
- [x] Public-header C++ compatibility pass.
- [x] Internal-header C++ compatibility pass.
- [x] `error.c` conversion.
- [x] `cpxm.c` conversion.
- [ ] `libdvdcpxm.c` conversion.
- [ ] `css.c` plus `csstables.h` cleanup.
- [ ] `device.c` conversion.
- [ ] `ioctl.c` conversion.
- [ ] `libdvdcss.c` conversion.
- [ ] `csstest.c` conversion.
- [ ] `dvd_region.c` conversion and possible test refactor.
- [ ] Final cleanup: remove leftover C-only build settings and switch the project fully to C++ if no `.c` sources remain.

## Definition Of Done Per File

Each file conversion is complete only when all of the following are true:

- [ ] The file has been renamed to `.cpp` where applicable.
- [ ] The library configures and builds successfully.
- [ ] No new compiler warnings of consequence are introduced for the converted file.
- [ ] Relevant tests or example programs still build.
- [ ] Public installed headers remain consumable from a C compiler.

## Validation Strategy

For every step in the sequence:

- [ ] Reconfigure Meson after filename changes.
- [ ] Rebuild from a clean build directory at least for milestone steps.
- [ ] Run the example/test binaries that are enabled in the current build.
- [ ] Build a tiny external C consumer against the installed or uninstalled headers to verify ABI and header compatibility.

Recommended milestone validations:

- [ ] After public-header pass.
- [ ] After internal-header pass.
- [ ] After each of the high-risk files: `device`, `ioctl`, and `libdvdcss`.
- [ ] After the final test conversion.

## Known Hotspots To Watch

- [ ] `src/ioctl.h` and `src/ioctl.c`: system APIs, packed data, bitfields, and platform-specific macros.
- [ ] `src/libdvdcss.h`: central state struct shared across almost every module.
- [ ] `src/common.h`: Windows compatibility typedefs and macro remapping.
- [ ] `test/dvd_region.c`: includes an implementation file directly.
- [ ] Any allocation site that currently relies on implicit `malloc` to typed-pointer conversion.

## Follow-Up Cleanup After Full Conversion

Once every implementation file is in C++ and stable:

- [ ] Decide whether to keep the internal codebase mostly C-like or introduce selective C++ cleanup.
- [ ] If cleanup is desired, do it in a second phase:
   - [ ] replace raw allocations with RAII where low-risk
   - [ ] reduce macro usage in favor of typed helpers
   - [ ] narrow linkage of internal helpers
   - [ ] improve const-correctness
- [ ] Keep the public headers C-first even if internals become more idiomatic C++.

## Recommended First Execution Slice

If starting immediately, the safest first slice is:

- [x] Update `meson.build` and `src/meson.build` for mixed-language support.
- [x] Make `src/common.h`, `src/device.h`, `src/ioctl.h`, `src/css.h`, `src/cpxm.h`, `src/libdvdcpxm.h`, and `src/libdvdcss.h` compile as C++ headers.
- [x] Convert `src/error.c` to `src/error.cpp` and verify the build.

That slice will reveal most of the structural C++ issues without forcing a high-risk port of the device and ioctl layers too early.