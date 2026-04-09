/*****************************************************************************
 * error.cpp: error management functions
 *****************************************************************************
 * Copyright (C) 1998-2002 VideoLAN
 *
 * Author: Sam Hocevar <sam@zoy.org>
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

#include "config.h"

#include <cstdarg>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#elif defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif

#include "libdvdcss.h"

static void write_stderr(std::string_view message) noexcept {
  const char *data = message.data();
  size_t remaining = message.size();

  while (remaining > 0) {
#ifdef _WIN32
    const int written =
        _write(_fileno(stderr), data, static_cast<unsigned int>(remaining));
#elif defined(HAVE_UNISTD_H)
    const ssize_t written = write(STDERR_FILENO, data, remaining);
#else
    const size_t written = std::fwrite(data, 1, remaining, stderr);
#endif

    if (written <= 0) {
      break;
    }

    data += written;
    remaining -= static_cast<size_t>(written);
  }
}

static std::string format_printf_message(const char *psz_string,
                                         va_list args) noexcept {
  if (psz_string == nullptr) {
    return {};
  }

  va_list args_copy;
  va_copy(args_copy, args);
  const int formatted_size = std::vsnprintf(nullptr, 0, psz_string, args_copy);
  va_end(args_copy);

  if (formatted_size < 0) {
    return psz_string;
  }

  std::string message(static_cast<size_t>(formatted_size) + 1, '\0');

  va_copy(args_copy, args);
  std::vsnprintf(message.data(), message.size(), psz_string, args_copy);
  va_end(args_copy);

  message.pop_back();
  return message;
}

static void print_message(const char *prefix,
                          std::string_view formatted_message) noexcept {
  try {
    write_stderr(std::format("libdvdcss {}: {}\n", prefix, formatted_message));
  } catch (...) {
    write_stderr("libdvdcss logging failure\n");
  }
}

static void vprint_error(dvdcss_t dvdcss, const char *psz_string,
                         va_list args) noexcept {
  const std::string formatted_message = format_printf_message(psz_string, args);

  if (dvdcss->b_errors) {
    print_message("error", formatted_message);
  }

  dvdcss->psz_error = formatted_message;
}

static void vprint_debug(const dvdcss_t dvdcss, const char *psz_string,
                         va_list args) noexcept {
  const std::string formatted_message = format_printf_message(psz_string, args);

  if (dvdcss->b_debug) {
    print_message("debug", formatted_message);
  }
}

/*****************************************************************************
 * Error messages
 *****************************************************************************/
void print_error(dvdcss_t dvdcss, const char *psz_string, ...) noexcept {
  va_list args;

  va_start(args, psz_string);
  vprint_error(dvdcss, psz_string, args);
  va_end(args);
}

/*****************************************************************************
 * Debug messages
 *****************************************************************************/
void print_debug(const dvdcss_t dvdcss, const char *psz_string, ...) noexcept {
  va_list args;

  va_start(args, psz_string);
  vprint_debug(dvdcss, psz_string, args);
  va_end(args);
}