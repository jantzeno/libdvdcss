/*****************************************************************************
 * libdvdcss.h: private DVD reading library data
 *****************************************************************************
 * Copyright (C) 1998-2001 VideoLAN
 *
 * Authors: Stéphane Borel <stef@via.ecp.fr>
 *          Sam Hocevar <sam@zoy.org>
 *
 * libdvdcss is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * libdvdcss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libdvdcss; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *****************************************************************************/

#ifndef DVDCSS_LIBDVDCSS_H
#define DVDCSS_LIBDVDCSS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <config.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include "common.h"
#include "cpxm.h"
#include "css.h"
#include "device.h"
#include "dvdcss/dvdcss.h"

namespace dvdcss {

/*****************************************************************************
 * libdvdcss method: used like init flags
 *****************************************************************************/
enum class dvdcss_method {
  key,
  disc,
  title,
};

void print_error(dvdcss_t, const char *, ...) noexcept;
void print_debug(const dvdcss_t, const char *, ...) noexcept;

namespace detail {

void set_boundary_error(dvdcss_t, const char *) noexcept;

template <typename Operation>
int invoke_c_api(dvdcss_t context, Operation &&operation) noexcept {
  if (context == nullptr) {
    return -1;
  }

  try {
    return std::forward<Operation>(operation)();
  } catch (const std::bad_alloc &) {
    set_boundary_error(context, "out of memory");
  } catch (...) {
    set_boundary_error(context, "unexpected internal error");
  }

  return -1;
}

} // namespace detail

} // namespace dvdcss

/*****************************************************************************
 * The libdvdcss structure
 *****************************************************************************/
struct dvdcss_s {
  ~dvdcss_s() noexcept;

  int cleanup() noexcept;

  /* File descriptor */
  std::string psz_device;
  dvdcss::ScopedFd i_fd;
  int i_pos;

  /* File handling */
  dvdcss::DeviceStrategy device_strategy;

  /* Decryption stuff */
  dvdcss::dvdcss_method i_method;
  dvdcss::css css;
  int b_ioctls;
  int b_scrambled;
  std::vector<dvdcss::dvd_title> p_titles;

  /* Key cache directory */
  std::filesystem::path psz_cachefile;

  /* Error management */
  std::string psz_error;
  int b_errors;
  int b_debug;

  /* struct to be used only internally in CPXM */
  std::unique_ptr<dvdcss::cpxm_s> cpxm;
  /* CPXM state can be reused by another handle for the same open device. */
  std::optional<std::pair<std::uintmax_t, std::uintmax_t>> cpxm_cache_key;
  bool owns_cpxm_cache_entry = false;

  /* i_copyright read from ioctl_copyright used by cpxm to determine type of
   * encryption */
  /* 0 - None, 1 - CPPM, 2 - CPRM */
  int media_type;

#ifdef _WIN32
  int b_file;
  std::vector<uint8_t> p_readv_buffer;
#endif /* _WIN32 */

  void *p_stream;
  dvdcss_stream_cb stream_cb{};
  bool uses_stream = false;

  int cleanup_result = 0;
  bool cleanup_done = false;
};

#endif /* DVDCSS_LIBDVDCSS_H */
