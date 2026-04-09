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

#include <array>
#include <bit>
#include <config.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename T> constexpr T b2n_fallback_byteswap(T value) noexcept {
  static_assert(std::is_integral_v<T>);

  auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  for (std::size_t i = 0; i < bytes.size() / 2; ++i) {
    const auto opposite = bytes.size() - 1 - i;
    const auto tmp = bytes[i];
    bytes[i] = bytes[opposite];
    bytes[opposite] = tmp;
  }

  return std::bit_cast<T>(bytes);
}

constexpr std::uint16_t b2n_builtin_byteswap(std::uint16_t value) noexcept {
#if defined(__clang__)
#if __has_builtin(__builtin_bswap16)
  return __builtin_bswap16(value);
#else
  return b2n_fallback_byteswap(value);
#endif
#elif defined(__GNUC__)
  return __builtin_bswap16(value);
#else
  return b2n_fallback_byteswap(value);
#endif
}

constexpr std::uint32_t b2n_builtin_byteswap(std::uint32_t value) noexcept {
#if defined(__clang__)
#if __has_builtin(__builtin_bswap32)
  return __builtin_bswap32(value);
#else
  return b2n_fallback_byteswap(value);
#endif
#elif defined(__GNUC__)
  return __builtin_bswap32(value);
#else
  return b2n_fallback_byteswap(value);
#endif
}

constexpr std::uint64_t b2n_builtin_byteswap(std::uint64_t value) noexcept {
#if defined(__clang__)
#if __has_builtin(__builtin_bswap64)
  return __builtin_bswap64(value);
#else
  return b2n_fallback_byteswap(value);
#endif
#elif defined(__GNUC__)
  return __builtin_bswap64(value);
#else
  return b2n_fallback_byteswap(value);
#endif
}

template <typename T> constexpr T b2n_to_native(T value) noexcept {
  static_assert(std::is_same_v<T, std::uint16_t> ||
                std::is_same_v<T, std::uint32_t> ||
                std::is_same_v<T, std::uint64_t>);

  if constexpr (std::endian::native == std::endian::big) {
    return value;
  } else {
    return b2n_builtin_byteswap(value);
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
