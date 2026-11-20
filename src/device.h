/*****************************************************************************
 * device.h: DVD device access
 *****************************************************************************
 * Copyright (C) 1998-2002 VideoLAN
 *
 * Authors: Stéphane Borel <stef@via.ecp.fr>
 *          Sam Hocevar <sam@zoy.org>
 *          Håkan Hjort <d95hjort@dtek.chalmers.se>
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

#ifndef DVDCSS_DEVICE_H
#define DVDCSS_DEVICE_H

#include "common.h"
#include "config.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <ranges>
#include <span>

/*****************************************************************************
 * iovec structure: vectored data entry
 *****************************************************************************/
#ifndef HAVE_SYS_UIO_H
#include <io.h> /* read() */
struct iovec {
  void *iov_base; /* Pointer to data. */
  size_t iov_len; /* Length of data.  */
};
#else
#include <sys/types.h>
#include <sys/uio.h> /* struct iovec */
#endif

#include "dvdcss/dvdcss.h"

namespace dvdcss {

class ScopedFd {
public:
  enum class Kind {
    none,
    posix,
#ifdef _WIN32
    win32_handle,
#endif
  };

  static constexpr dvdcss_fd_t invalid = static_cast<dvdcss_fd_t>(-1);

  ScopedFd() noexcept = default;
  ~ScopedFd() noexcept;

  ScopedFd(const ScopedFd &) = delete;
  ScopedFd &operator=(const ScopedFd &) = delete;
  ScopedFd(ScopedFd &&other) noexcept;
  ScopedFd &operator=(ScopedFd &&other) noexcept;

  [[nodiscard]] dvdcss_fd_t get() const noexcept { return fd_; }
  [[nodiscard]] bool is_valid() const noexcept { return fd_ != invalid; }
  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  operator dvdcss_fd_t() const noexcept { return fd_; }

  void reset() noexcept;
  void reset(dvdcss_fd_t fd, Kind kind = Kind::posix) noexcept;
  [[nodiscard]] int close() noexcept;

private:
  dvdcss_fd_t fd_{invalid};
  Kind kind_{Kind::none};
};

using DeviceSeek = int (*)(dvdcss_t, int);
using DeviceRead = int (*)(dvdcss_t, void *, int);
using ScatterBuffer = std::span<uint8_t>;

class ScatterBuffers {
public:
  ScatterBuffers() noexcept = default;
  ScatterBuffers(struct iovec *iovecs, std::size_t count) noexcept
      : iovecs_(iovecs, count) {}

  [[nodiscard]] bool empty() const noexcept { return iovecs_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return iovecs_.size(); }
  [[nodiscard]] struct iovec *raw_data() const noexcept { return iovecs_.data(); }

  [[nodiscard]] std::optional<std::size_t> byte_size() const noexcept {
    std::size_t total = 0;
    for (const auto &entry : iovecs_) {
      if ((entry.iov_base == nullptr && entry.iov_len != 0) ||
          entry.iov_len % DVDCSS_BLOCK_SIZE != 0 ||
          entry.iov_len > std::numeric_limits<std::size_t>::max() - total) {
        return std::nullopt;
      }
      total += entry.iov_len;
    }
    return total;
  }

  [[nodiscard]] auto entries() const noexcept {
    return iovecs_ | std::views::transform([](const iovec &entry) noexcept {
      return ScatterBuffer{static_cast<uint8_t *>(entry.iov_base), entry.iov_len};
    });
  }

private:
  std::span<iovec> iovecs_;
};

using DeviceReadv = int (*)(dvdcss_t, ScatterBuffers);

[[nodiscard]] inline ScatterBuffers make_scatter_buffers(struct iovec *iovecs,
                                                         int count) noexcept {
  if (count <= 0) {
    return {};
  }
  return {iovecs, static_cast<std::size_t>(count)};
}

struct DeviceStrategy {
  DeviceSeek seek = nullptr;
  DeviceRead read = nullptr;
  DeviceReadv readv = nullptr;
};

/*****************************************************************************
 * Device reading prototypes
 *****************************************************************************/
int dvdcss_use_ioctls(dvdcss_t);
void dvdcss_check_device(dvdcss_t);
int dvdcss_open_device(dvdcss_t);
int dvdcss_close_device(dvdcss_t);

} // namespace dvdcss

#endif /* DVDCSS_DEVICE_H */
