/**
 * \file cpxm.h
 * \author Maxim V.Anisiutkin <Maxim.Anisiutkin@gmail.com>
 * \author Saifelden Ismail <saifeldenmi@gmail.com>
 *
 * \brief Integration of libdvdcpxm functionality into libdvdcss.
 *
 * This file adapts core logic from libdvdcpxm for use in libdvdcss, allowing
 * improved support for CPPM-protected DVD-Audio discs.
 */

/*
 * Copyright (C) 1999-2025 VideoLAN
 * Copyright (C) Maxim V.Anisiutkin <Maxim.Anisiutkin@gmail.com>
 * Copyright (C) 2025 Saifelden Ismail <saifeldenmi@gmail.com>
 *
 * This file is part of libdvdcss.
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
 */
#ifndef CPXM_H
#define CPXM_H

#include "bswap.h"
#include "dvdcss/dvdcss.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dvdcss {

template <typename T>
[[nodiscard]] inline T load_unaligned_value(const void *src) noexcept {
  static_assert(std::is_trivially_copyable_v<T>);

  std::array<std::byte, sizeof(T)> bytes{};
  const auto *src_bytes = static_cast<const std::byte *>(src);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = src_bytes[i];
  }

  return std::bit_cast<T>(bytes);
}

[[nodiscard]] inline uint64_t read64_be(const void *src) noexcept {
  uint64_t value = load_unaligned_value<uint64_t>(src);
  B2N_64(value);
  return value;
}

typedef struct cpxm {
  uint64_t media_key;
  uint64_t id_album;
  uint64_t id_media;
  uint64_t vr_k_t;
  uint64_t apstb;
} cpxm_s;

/* for persistance */
typedef cpxm_s *p_cpxm;

/* cpxm uses the same css authentification method when using a usb dvd drive */
[[nodiscard]] int cppm_set_id_album(dvdcss_t dvdcss);
[[nodiscard]] int cprm_set_id_media(dvdcss_t dvdcss);

} // namespace dvdcss

#endif // CPXM_H
