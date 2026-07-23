/*
 * Copyright (C) 2000, 2001 Billy Biggs <vektor@dumbterm.net>,
 *                          Håkan Hjort <d95hjort@dtek.chalmers.se>
 *
 * This file was taken from the libdvdread library
 *
 * libdvdread is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * libdvdread is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libdvdread; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LIBDVDREAD_BSWAP_H
#define LIBDVDREAD_BSWAP_H

#include <bit>
#include <concepts>
#include <cstdint>
#include <type_traits>

template <std::unsigned_integral T>
  requires(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)
constexpr T b2n_to_native(T value) noexcept {

  if constexpr (std::endian::native == std::endian::big) {
    return value;
  } else {
    return std::byteswap(value);
  }
}

constexpr inline void B2N_16(std::uint16_t &value) noexcept {
  value = b2n_to_native(value);
}

constexpr inline void B2N_32(std::uint32_t &value) noexcept {
  value = b2n_to_native(value);
}

constexpr inline void B2N_64(std::uint64_t &value) noexcept {
  value = b2n_to_native(value);
}

#endif /* LIBDVDREAD_BSWAP_H */
