/**
 * \file libdvdcpxm.cpp
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

#include "config.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "bswap.h"
#include "cpxm.h"
#include "device.h"
#include "dvdcss/dvdcpxm.h"
#include "ioctl.h"
#include "libdvdcpxm.h"
#include "libdvdcss.h"

#include <array>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <vector>

using std::memcpy;

namespace dvdcss {

struct cpxm_cache_entry {
  cpxm_s cpxm;
  dev_t st_dev;
};

static std::list<cpxm_cache_entry> g_cpxm_cache;

[[nodiscard]] static std::optional<cpxm_s> find_cached_cpxm(dev_t device_id) {
  for (const auto &cache_entry : g_cpxm_cache) {
    if (cache_entry.st_dev == device_id) {
      return cache_entry.cpxm;
    }
  }

  return std::nullopt;
}

[[nodiscard]] static bool is_sync_code(std::span<const uint8_t> word) noexcept {
  return word.size() >= 4 && word[0] == 0x00 && word[1] == 0x00 &&
         word[2] == 0x01 && word[3] == 0xBA;
}

/* these values are used by libdvdcpxm to process the Media Key Block */
/* They are present inside DVD-Audio players and are used in conjunction with
 * keys taken from the disc in order to generate the final media_key used by the
 * C2 Cypher */
static constexpr std::array<uint8_t, 256> sbox = {
    0x3a, 0xd0, 0x9a, 0xb6, 0xf5, 0xc1, 0x16, 0xb7, 0x58, 0xf6, 0xed, 0xe6,
    0xd9, 0x8c, 0x57, 0xfc, 0xfd, 0x4b, 0x9b, 0x47, 0x0e, 0x8e, 0xff, 0xf3,
    0xbb, 0xba, 0x0a, 0x80, 0x15, 0xd7, 0x2b, 0x36, 0x6a, 0x43, 0x5a, 0x89,
    0xb4, 0x5d, 0x71, 0x19, 0x8f, 0xa0, 0x88, 0xb8, 0xe8, 0x8a, 0xc3, 0xae,
    0x7c, 0x4e, 0x3d, 0xb5, 0x96, 0xcc, 0x21, 0x00, 0x1a, 0x6b, 0x12, 0xdb,
    0x1f, 0xe4, 0x11, 0x9d, 0xd3, 0x93, 0x68, 0xb0, 0x7f, 0x3b, 0x52, 0xb9,
    0x94, 0xdd, 0xa5, 0x1b, 0x46, 0x60, 0x31, 0xec, 0xc9, 0xf8, 0xe9, 0x5e,
    0x13, 0x98, 0xbf, 0x27, 0x56, 0x08, 0x91, 0xe3, 0x6f, 0x20, 0x40, 0xb2,
    0x2c, 0xce, 0x02, 0x10, 0xe0, 0x18, 0xd5, 0x6c, 0xde, 0xcd, 0x87, 0x79,
    0xaf, 0xa9, 0x26, 0x50, 0xf2, 0x33, 0x92, 0x6e, 0xc0, 0x3f, 0x39, 0x41,
    0xaa, 0x5b, 0x7d, 0x24, 0x03, 0xd6, 0x2f, 0xeb, 0x0b, 0x99, 0x86, 0x4c,
    0x51, 0x45, 0x8d, 0x2e, 0xef, 0x07, 0x7b, 0xe2, 0x4d, 0x7a, 0xfe, 0x25,
    0x5c, 0x29, 0xa2, 0xa8, 0xb1, 0xf0, 0xb3, 0xc4, 0x30, 0x7e, 0x63, 0x38,
    0xcb, 0xf4, 0x4f, 0xd1, 0xdf, 0x44, 0x32, 0xdc, 0x17, 0x5f, 0x66, 0x2a,
    0x81, 0x9e, 0x77, 0x4a, 0x65, 0x67, 0x34, 0xfa, 0x54, 0x1e, 0x14, 0xbe,
    0x04, 0xf1, 0xa7, 0x9c, 0x8b, 0x37, 0xee, 0x85, 0xab, 0x22, 0x0f, 0x69,
    0xc5, 0xd4, 0x05, 0x84, 0xa4, 0x73, 0x42, 0xa1, 0x64, 0xe1, 0x70, 0x83,
    0x90, 0xc2, 0x48, 0x0d, 0x61, 0x1c, 0xc6, 0x72, 0xfb, 0x76, 0x74, 0xe7,
    0x01, 0xd8, 0xc8, 0xd2, 0x75, 0xa3, 0xcf, 0x28, 0x82, 0x1d, 0x49, 0x35,
    0xc7, 0xbd, 0xca, 0xa6, 0xac, 0x0c, 0x62, 0xad, 0xf9, 0x3c, 0xea, 0x2d,
    0x59, 0xda, 0x3e, 0x97, 0x6d, 0x09, 0xf7, 0x55, 0xe5, 0x23, 0x53, 0x9f,
    0x06, 0xbc, 0x95, 0x78,
};

static constexpr std::array<device_key_t, 32> cppm_device_keys = {{
    {0x00, 0x4821, 0x6d05086b755c81}, {0x01, 0x091c, 0x97ace18dd26973},
    {0x02, 0x012a, 0xfefc0a25a38d42}, {0x03, 0x469b, 0x0780491970db2c},
    {0x04, 0x0f9b, 0x0bedd116d43484}, {0x05, 0x59b2, 0x566936bcebe294},
    {0x06, 0x5fc8, 0xdc610f649b1fc0}, {0x07, 0x11de, 0x6ee01d3872c2d9},
    {0x08, 0x52b6, 0xd0132c376e439b}, {0x09, 0x135f, 0x800faa66206922},
    {0x0a, 0x3806, 0x9d1aa1460885c2}, {0x0b, 0x2da2, 0x9833f21818ba33},
    {0x0c, 0x113f, 0xd50aa7d022045a}, {0x0d, 0x11ec, 0x88abee7bb83a32},
    {0x0e, 0x071b, 0x9b45eea4e7d140}, {0x0f, 0x5c55, 0x5a49f860cca5cf},

    {0x00, 0x0375, 0x1a12793404c279}, {0x01, 0x4307, 0x61418b44cea550},
    {0x02, 0x1f70, 0x52bde5b73adcda}, {0x03, 0x1bbc, 0x70a031ae493159},
    {0x04, 0x1f9d, 0x0a570636aedb61}, {0x05, 0x4e7b, 0xc313563e7883e9},
    {0x06, 0x07c4, 0x32c55f7bc42d45}, {0x07, 0x4216, 0x4f854df6c1d721},
    {0x08, 0x11c5, 0xc0e3f0f3df33cc}, {0x09, 0x0486, 0xbfca7754db5de6},
    {0x0a, 0x2f82, 0xa964fc061af87c}, {0x0b, 0x236a, 0xb96d68856c45d5},
    {0x0c, 0x5beb, 0xd2ca3cbb7d13cc}, {0x0d, 0x3db6, 0x58cf827ff3c540},
    {0x0e, 0x4b22, 0xbb4037442a869c}, {0x0f, 0x59b5, 0x3a83e0ddf37a6e},
}};

static constexpr std::array<device_key_t, 16> cprm_device_keys = {{
    {0x00, 0x0809, 0xd50fe4150d32d2},
    {0x01, 0x0719, 0x3131c69e825462},
    {0x02, 0x0408, 0x7c2e6878b3a494},
    {0x03, 0x040a, 0x3c9f93ec5848a2},
    {0x04, 0x03bc, 0x614f4bda9876a5},
    {0x05, 0x0812, 0x2e901a9227fc47},
    {0x06, 0x090d, 0xeebf4957d53d62},
    {0x07, 0x0322, 0x6314ec2ca6b32b},
    {0x08, 0x0035, 0x14f6d08c096483},
    {0x09, 0x07c5, 0x8f7eff1d689a81},
    {0x0a, 0x069a, 0xff4b538492c611},
    {0x0b, 0x0bd8, 0x909300c14c1467},
    {0x0c, 0x01d1, 0xba5826ef832e2b},
    {0x0d, 0x0583, 0xa92e636998767d},
    {0x0e, 0x02e8, 0x313f0a51478df8},
    {0x0f, 0x08fc, 0xd28ce525a2be4b},
}};

[[nodiscard]] static std::vector<uint8_t>
copy_cppm_mkb(const uint8_t *input_mkb) {
  if (input_mkb == nullptr) {
    return {};
  }

  std::size_t offset = 16;
  while (true) {
    const uint8_t record_type = input_mkb[offset];
    uint32_t record_length =
        load_unaligned_value<uint32_t>(&input_mkb[offset]) & 0xffffff00u;
    B2N_32(record_length);

    if (record_length < 4) {
      return {};
    }

    offset += record_length;
    if (record_type == 0x02) {
      break;
    }
  }

  return std::vector<uint8_t>(input_mkb, input_mkb + offset);
}

static constexpr uint8_t rol8(uint8_t code, int n) noexcept {
  return static_cast<uint8_t>((code << n) | (code >> (8 - n)));
}

static constexpr std::array<uint32_t, 256> build_sbox_f() noexcept {
  std::array<uint32_t, 256> values = {};

  for (size_t i = 0; i < values.size(); ++i) {
    unsigned c0 = sbox[i];
    const unsigned c1 = rol8(static_cast<uint8_t>(c0 ^ 0x65), 1);
    const unsigned c2 = rol8(static_cast<uint8_t>(c0 ^ 0x2b), 5);
    const unsigned c3 = rol8(static_cast<uint8_t>(c0 ^ 0xc9), 2);
    c0 ^= static_cast<unsigned>(i);
    values[i] = (c3 << 24) + (c2 << 16) + (c1 << 8) + c0;
  }

  return values;
}

static constexpr auto sbox_f = build_sbox_f();

/* Functions Used by the C2 Cypher */
using c2_round_keys = std::array<uint32_t, 10>;

static constexpr uint32_t rol32(uint32_t code, int n) noexcept {
  return (code << n) | (code >> (32 - n));
}

static constexpr uint32_t F(uint32_t code, uint32_t key) noexcept {
  uint32_t work;

  work = code + key;
  work ^= sbox_f[work & 0xff];
  work ^= rol32(work, 9) ^ rol32(work, 22);
  return work;
}

static constexpr c2_round_keys build_c2_round_keys(uint64_t key) noexcept {
  c2_round_keys sk = {};
  uint32_t ktmpa, ktmpb, ktmpc, ktmpd;

  ktmpa = static_cast<uint32_t>((key >> 32) & 0x00ffffffu);
  ktmpb = static_cast<uint32_t>(key & 0xffffffffu);

  for (int round = 0; round < static_cast<int>(sk.size()); ++round) {
    ktmpa &= 0x00ffffff;
    sk[round] =
        ktmpb + (static_cast<uint32_t>(sbox[(ktmpa & 0xff) ^ round]) << 4);
    ktmpc = (ktmpb >> (32 - 17));
    ktmpd = (ktmpa >> (24 - 17));
    ktmpa = (ktmpa << 17) | ktmpc;
    ktmpb = (ktmpb << 17) | ktmpd;
  }

  return sk;
}

static constexpr uint64_t c2_encrypt_block(
    uint64_t code, const c2_round_keys &round_keys) noexcept {
  uint32_t L = static_cast<uint32_t>((code >> 32) & 0xffffffffu);
  uint32_t R = static_cast<uint32_t>(code & 0xffffffffu);
  uint32_t t;

  for (std::size_t round = 0; round < round_keys.size(); ++round) {
    L += F(R, round_keys[round]);
    t = L;
    L = R;
    R = t;
  }

  t = L;
  L = R;
  R = t;
  return (static_cast<uint64_t>(L) << 32) | R;
}

constexpr uint64_t c2_enc(uint64_t code, uint64_t key) noexcept {
  return c2_encrypt_block(code, build_c2_round_keys(key));
}

static constexpr uint64_t c2_decrypt_block(
    uint64_t code, const c2_round_keys &round_keys) noexcept {
  uint32_t L = static_cast<uint32_t>((code >> 32) & 0xffffffffu);
  uint32_t R = static_cast<uint32_t>(code & 0xffffffffu);
  uint32_t t;

  for (int round = static_cast<int>(round_keys.size()) - 1; round >= 0;
       --round) {
    L -= F(R, round_keys[round]);
    t = L;
    L = R;
    R = t;
  }

  t = L;
  L = R;
  R = t;
  return (static_cast<uint64_t>(L) << 32) | R;
}

constexpr uint64_t c2_dec(uint64_t code, uint64_t key) noexcept {
  return c2_decrypt_block(code, build_c2_round_keys(key));
}

constexpr uint64_t c2_g(uint64_t code, uint64_t key) noexcept {
  return c2_enc(code, key) ^ code;
}

static constexpr bool matches_c2_vector(uint64_t code, uint64_t key,
                                        uint64_t encrypted,
                                        uint64_t g_value) noexcept {
  return c2_enc(code, key) == encrypted && c2_dec(encrypted, key) == code &&
         c2_g(code, key) == g_value;
}

/* Keep known-good pre-constexpr outputs pinned at compile time. */
static_assert(matches_c2_vector(0x0011223344556677ULL, 0x006d05086b755c81ULL,
                                0xd6d6a1cbd23b8e7eULL,
                                0xd6c783f8966ee809ULL));
static_assert(matches_c2_vector(0x8899aabbccddeeffULL, 0x00d50fe4150d32d2ULL,
                                0xead20c952dd5780aULL,
                                0x624ba62ee10896f5ULL));
static_assert(matches_c2_vector(0x0123456789abcdefULL, 0x0001020304050607ULL,
                                0x4b62abac869d01e8ULL,
                                0x4a41eecb0f36cc07ULL));
static_assert(matches_c2_vector(0xffffffffffffffffULL, 0x0000000000000000ULL,
                                0x677345a1666887f8ULL,
                                0x988cba5e99977807ULL));

void c2_ecbc(std::span<uint8_t> buffer, uint64_t key) {
  uint32_t L, R, t;
  uint64_t inout, inkey;
  int key_round, i;

  inkey = key;
  key_round = 10;

  for (i = 0; i < static_cast<int>(buffer.size()); i += 8) {
    inout = read64_be(buffer.data() + i);
    L = static_cast<uint32_t>((inout >> 32) & 0xffffffffu);
    R = static_cast<uint32_t>(inout & 0xffffffffu);
    const auto sk = build_c2_round_keys(inkey);

    for (int round = 0; round < 10; round++) {
      L += F(R, sk[round % key_round]);

      if (round == 4) [[unlikely]] {
        inkey = key ^ ((static_cast<uint64_t>(R & 0x00ffffffu) << 32) | L);
      }
      t = L;
      L = R;
      R = t;
    }

    t = L;
    L = R;
    R = t;
    inout = (static_cast<uint64_t>(L) << 32) | R;
    B2N_64(inout);
    memcpy(buffer.data() + i, &inout, sizeof(inout));
    key_round = 2;
  }
}

void c2_dcbc(std::span<uint8_t> buffer, uint64_t key) {
  uint32_t L, R, t;
  uint64_t inout, inkey;
  int key_round, i;
  uint8_t *buf = buffer.data();

  inkey = key;
  key_round = 10;

  for (i = 0; i < static_cast<int>(buffer.size()); i += 8) {
    inout = read64_be(buf);

    L = static_cast<uint32_t>((inout >> 32) & 0xffffffffu);
    R = static_cast<uint32_t>(inout & 0xffffffffu);
    const auto sk = build_c2_round_keys(inkey);

    for (int round = 9; round >= 0; round--) {
      L -= F(R, sk[round % key_round]);
      t = L;
      L = R;
      R = t;

      if (round == 5) [[unlikely]] {
        inkey = key ^ ((static_cast<uint64_t>(R & 0x00ffffffu) << 32) | L);
      }
    }

    t = L;
    L = R;
    R = t;
    inout = (static_cast<uint64_t>(L) << 32) | R;
    B2N_64(inout);
    memcpy(buf, &inout, sizeof(uint64_t));

    buf += 8;
    key_round = 2;
  }
}

/* for CPPM, libdvdread is responsible for retrieving the Media Key Block */
std::vector<uint8_t> cprm_get_mkb(dvdcss_t dvdcss) {
  uint8_t mkb_pack[CPRM_MKB_PACK_SIZE];
  int mkb_packs, i;
  mkb_packs = 16;

  if (ioctl_ReadCPRMMKBPack(dvdcss->i_fd, &dvdcss->css.i_agid, 0,
                            std::span{mkb_pack}, &mkb_packs))
    return {};

  std::vector<uint8_t> mkb(mkb_packs * CPRM_MKB_PACK_SIZE - 16);

  memcpy(mkb.data(), &mkb_pack[16], CPRM_MKB_PACK_SIZE - 16);

  for (i = 1; i < mkb_packs; i++) {
    if (ioctl_ReadCPRMMKBPack(
            dvdcss->i_fd, &dvdcss->css.i_agid, i,
            std::span<uint8_t>{mkb.data() + i * CPRM_MKB_PACK_SIZE - 16,
                               static_cast<size_t>(CPRM_MKB_PACK_SIZE)},
            &mkb_packs)) {
      return {};
    }
  }

  return mkb;
}

static inline uint64_t combine_column_row(uint8_t column, uint16_t row) {
  return (static_cast<uint64_t>(column) << 32) | static_cast<uint64_t>(row);
}

/* This function retrieves the main key used to decryption; this key is derived
 * from applying the C2 cypher to the MKB and the DVD-Audio player device keys,
 * as well as a unique album_id and media_id */
int process_mkb(std::span<const uint8_t> p_mkb, const device_key_t *p_dev_keys,
                size_t nr_dev_keys, uint64_t *p_media_key) {
  int mkb_pos, length, i, i_dev_key, no_more_keys, no_more_records;
  uint8_t record_type, column;
  uint64_t buffer, media_key, verification_data;

  /* Init everything */
  i_dev_key = no_more_keys = 0;
  buffer = media_key = verification_data = 0;

  while (!no_more_keys) {
    /* skip the file identifier and the length */
    mkb_pos = 16;
    no_more_records = 0;
    while (!no_more_records) {
      record_type = p_mkb[mkb_pos];
      uint32_t record_length =
          load_unaligned_value<uint32_t>(&p_mkb[mkb_pos]) & 0xffffff00u;
      B2N_32(record_length);
      length = static_cast<int>(record_length);

      if (length >= 12) {
        buffer = load_unaligned_value<uint64_t>(&p_mkb[mkb_pos + 4]);
      } else {
        if (length < 4)
          length = 4;
      }

      switch (record_type) {
      case 0x82: /* Conditionally calculate media key record */
        B2N_64(buffer);
        buffer = c2_dec(buffer, media_key);

        if ((buffer & 0xffffffff00000000) != 0xdeadbeef00000000)
          break;

        B2N_64(buffer);
        /* intentional fallthrough */
      case 0x01: /* Calculate media key record */
        column = std::bit_cast<std::array<uint8_t, sizeof(buffer)>>(buffer)[4];
        /*
        if (column >= 16 || ((uint8_t*)&buffer)[5] != 0 ||
        ((uint8_t*)&buffer)[6] != 0 || ((uint8_t*)&buffer)[7] != 1) break;
        */
        /* Get appropriate device key for column */
        no_more_keys = 1;
        for (i = i_dev_key; i < static_cast<int>(nr_dev_keys); i++) {
          if (p_dev_keys[i].col == column) {
            no_more_keys = 0;
            i_dev_key = i;
            break;
          }
        }
        if (no_more_keys)
          break;
        if (12 + p_dev_keys[i_dev_key].row * 8 + 8 > length)
          break;
        buffer = load_unaligned_value<uint64_t>(
            &p_mkb[mkb_pos + 12 + p_dev_keys[i_dev_key].row * 8]);
        B2N_64(buffer);

        if (record_type == 0x82)
          buffer = c2_dec(buffer, media_key);

        media_key =
            (c2_dec(buffer, p_dev_keys[i_dev_key].key) & 0x00ffffffffffffff) ^
            combine_column_row(column, p_dev_keys[i_dev_key].row);
        buffer = c2_dec(verification_data, media_key);

        if ((buffer & 0xffffffff00000000) == 0xdeadbeef00000000) {
          *p_media_key = media_key;
          return 0;
        }

        break;
      case 0x02: /* End of media key record */
        no_more_records = 1;
        break;
      case 0x81: /* Verify media key record */
        B2N_64(buffer);
        verification_data = buffer;
        break;
      default:
        break;
      }
      mkb_pos += length;
    }
    i_dev_key++;
  }
  return -1;
}

/* Function should be called on a dvdcss var to set cppm struct which needs
 * to persist in order to decrypt the media */
LIBDVDCSS_EXPORT int dvdcpxm_init(dvdcss_t dvdcss, uint8_t *p_input) {
  /* In the case that p_mkb is received as null, then either you were unable
   * to read the mkb or the encryption type is cprm */
  /* if no file mkb file is passed, check cache */
  if (!p_input) {
    struct stat file_stat;
    fstat(dvdcss->i_fd, &file_stat);
    if (const auto cached_cpxm = find_cached_cpxm(file_stat.st_dev)) {
      dvdcss->cpxm = std::make_unique<cpxm_s>(*cached_cpxm);
      dvdcss->cpxm_was_cached = 0;
      return dvdcss->media_type;
    }
    return -1;
  }

  dvdcss->cpxm = std::make_unique<cpxm_s>();
  auto &cpxm = *dvdcss->cpxm;

  int ret = -1;

  std::vector<uint8_t> cppm_mkb;
  std::vector<uint8_t> cprm_mkb;

  switch (dvdcss->media_type) {
  case COPYRIGHT_PROTECTION_NONE:
    ret = 0;
    break;
  case COPYRIGHT_PROTECTION_CPPM:
    /* the input is the media key block */
    cppm_mkb = copy_cppm_mkb(p_input);
    if (cppm_set_id_album(dvdcss) == 0) {
      if (!cppm_mkb.empty()) {
        ret = process_mkb(cppm_mkb, cppm_device_keys.data(),
                          cppm_device_keys.size(), &dvdcss->cpxm->media_key);
        if (ret)
          break;
      }
    }
    break;
  case COPYRIGHT_PROTECTION_CPRM:
    if (cprm_set_id_media(dvdcss) == 0) {
      cprm_mkb = cprm_get_mkb(dvdcss);
      if (!cprm_mkb.empty()) {
        ret = process_mkb(cprm_mkb, cprm_device_keys.data(),
                          cprm_device_keys.size(), &dvdcss->cpxm->media_key);
        if (ret)
          break;
      }

      /* get the media unique key */
      uint64_t k_mu = c2_g(cpxm.media_key, cpxm.id_media) & 0x00ffffffffffffff;

      /* decrypt the encrypted title key */
      uint64_t k_te;
      k_te = read64_be(p_input);
      uint64_t k_t = c2_dec(k_mu, k_te) & 0x00ffffffffffffff;

      /* store decrypted title key for vr decryption */
      cpxm.vr_k_t = k_t;
    }
    break;
  }

  /* store in cache */
  struct stat stat;
  fstat(dvdcss->i_fd, &stat);
  g_cpxm_cache.push_back({cpxm, stat.st_dev});
  dvdcss->cpxm_was_cached = 1;
  return dvdcss->media_type;
}

/* Ensures that the block is encrypted */
int mpeg2_check_pes_scrambling_control(
    std::span<const uint8_t> p_block) noexcept {
  int pes_scrambling_control;

  pes_scrambling_control = 0;
  if (is_sync_code(p_block)) {
    pes_scrambling_control = (p_block[20] & 0x30) >> 4;
  }
  return pes_scrambling_control;
}

void mpeg2_reset_pes_scrambling_control(std::span<uint8_t> p_block) noexcept {
  if (is_sync_code(p_block)) {
    p_block[20] &= 0xCD; // reset pes_scrambling_control and copyright flags;
  }
}

void mpeg2_reset_cci(std::span<uint8_t> p_block) noexcept {
  uint8_t *p_mlp_pcm, *p_curr;
  int pes_sid;
  int pes_len;

  p_curr = p_block.data();
  if (is_sync_code(p_block)) {
    p_curr += 14 + (p_curr[13] & 0x07);

    while (p_curr < p_block.data() + p_block.size()) {
      pes_len = (p_curr[4] << 8) + p_curr[5];

      if (p_curr[0] == 0x00 && p_curr[1] == 0x00 && p_curr[2] == 0x01) {
        pes_sid = p_curr[3];
        if (pes_sid == 0xbd) // private stream 1
        {
          p_mlp_pcm = p_curr + 9 + p_curr[8];
          switch (p_mlp_pcm[0]) // stream id
          {
          case 0xa0: // PCM stream id
            if (p_mlp_pcm[3] > 8)
              p_mlp_pcm[12] = CCI_BYTE; // reset CCI
            break;
          case 0xa1: // MLP stream di
            if (p_mlp_pcm[3] > 4)
              p_mlp_pcm[8] = CCI_BYTE; // reset CCI
            break;
          }
        }
        p_curr += 6 + pes_len;
      } else
        break;
    }
  }
}

/* Inside this header are fields relating to the type of packet and DVD-Audio
 * specifications. */
/* There are also a set of keys at different addresses that are used by CPPM to
 * decrypt the block */
/* only the last 1920 bytes contain protected content, the first 180 bytes are
 * left untouched. */
int cppm_decrypt_block(std::span<uint8_t> p_buffer, int flags,
                       uint64_t id_album, uint64_t media_key) noexcept {
  uint64_t d_kc_i, k_au, k_i, k_c;
  int encrypted;

  encrypted = 0;
  if (mpeg2_check_pes_scrambling_control(p_buffer)) {
    k_au = c2_g(id_album, media_key) & 0x00ffffffffffffff;

    d_kc_i = read64_be(&p_buffer[24]);
    k_i = c2_g(d_kc_i, k_au) & 0x00ffffffffffffff;

    d_kc_i = read64_be(&p_buffer[32]);
    k_i = c2_g(d_kc_i, k_i) & 0x00ffffffffffffff;

    d_kc_i = read64_be(&p_buffer[40]);
    k_i = c2_g(d_kc_i, k_i) & 0x00ffffffffffffff;

    d_kc_i = read64_be(&p_buffer[48]);
    k_i = c2_g(d_kc_i, k_i) & 0x00ffffffffffffff;

    d_kc_i = read64_be(&p_buffer[84]);
    k_c = c2_g(d_kc_i, k_i) & 0x00ffffffffffffff;

    c2_dcbc(p_buffer.subspan(DVDCPXM_BLOCK_SIZE - DVDCPXM_ENCRYPTED_SIZE,
                             DVDCPXM_ENCRYPTED_SIZE),
            k_c);
    mpeg2_reset_pes_scrambling_control(p_buffer);
    encrypted = 1;
  }

  if ((flags & DVDCPXM_PRESERVE_CCI) != DVDCPXM_PRESERVE_CCI)
    mpeg2_reset_cci(p_buffer);

  return encrypted;
}

/*
 * check if MPEG Video headers exist
 * Returns:
 * 1 = Decryption successful
 * 0 = Decryption failed
 */
int is_valid_mpeg_payload(std::span<const uint8_t> buffer) noexcept {
  for (size_t i = 0; i < DVDCPXM_BLOCK_SIZE - 4; i++) {
    /* Look for the Start Code Prefix (00 00 01) */
    if (buffer[i] == 0x00 && buffer[i + 1] == 0x00 && buffer[i + 2] == 0x01) {
      uint8_t code = buffer[i + 3];
      if (code == 0xB3)
        return 1; // Sequence Header
      if (code == 0xB8)
        return 1; // GOP Header
      if (code == 0x00)
        return 1; // Picture Header
    }
  }
  return 0;
}

int cprm_decrypt_block(std::span<uint8_t> p_buffer, int flags, uint64_t vr_k_t,
                       uint64_t apstb) noexcept {
  uint64_t d_tkc, k_i, k_c;
  int encrypted;

  encrypted = 0;
  if (mpeg2_check_pes_scrambling_control(p_buffer)) {
    /* Add the CPRM_CI byte (or APSTB bits) to the title key to get K_i.
     * We treat this as a full byte (0-255) to support both DVD-VR and
     * DVD-Video. */
    k_i = vr_k_t + (apstb);

    /* read the Title Key Conversion Data */
    d_tkc = read64_be(&p_buffer[84]);
    k_c = c2_g(k_i, d_tkc) & 0x00ffffffffffffff;

    c2_dcbc(p_buffer.subspan(DVDCPXM_BLOCK_SIZE - DVDCPXM_ENCRYPTED_SIZE,
                             DVDCPXM_ENCRYPTED_SIZE),
            k_c);
    mpeg2_reset_pes_scrambling_control(p_buffer);
    /* check if decryption failed */
    if (is_valid_mpeg_payload(p_buffer))
      encrypted = 1;
    else
      encrypted = -1;
  }

  if ((flags & DVDCPXM_PRESERVE_CCI) != DVDCPXM_PRESERVE_CCI)
    mpeg2_reset_cci(p_buffer);

  return encrypted;
}

int dvdcpxm_decrypt(p_cpxm cpxm, int media_type, std::span<uint8_t> p_buffer,
                    int flags) noexcept {
  switch (media_type) {
  case COPYRIGHT_PROTECTION_CPPM:
    return cppm_decrypt_block(p_buffer, flags, cpxm->id_album, cpxm->media_key);
  case COPYRIGHT_PROTECTION_CPRM: {
    /* return early if there is no encryption to avoid allocating 2kb */
    if (!mpeg2_check_pes_scrambling_control(p_buffer))
      return cprm_decrypt_block(p_buffer, flags, cpxm->vr_k_t, cpxm->apstb);

    /* we are not sure if apstb is correct, so we operate on a copied buffer
     * first */
    uint8_t temp_buffer[DVDCPXM_BLOCK_SIZE];

    /* For DVD-VR we only need to check the first 4 possible values since apstb
     * is 2 bits for DVD-Video with CPRM we need to check more, since CPRM_CI is
     * more bits */
    uint64_t nr_possible_values = 256;

    /* assume the retained value is initially correct */
    memcpy(temp_buffer, p_buffer.data(), DVDCPXM_BLOCK_SIZE);
    int result = cprm_decrypt_block(std::span{temp_buffer}, flags, cpxm->vr_k_t,
                                    cpxm->apstb);

    if (result == 1) [[likely]] {
      memcpy(p_buffer.data(), temp_buffer, DVDCPXM_BLOCK_SIZE);
      return result;
    }

    /* our old value must not have been correct, we must guess */
    for (uint64_t guess = 0; guess < nr_possible_values; guess++) {

      /* skip if we already checked this above */
      if (guess == cpxm->apstb) [[unlikely]]
        continue;

      /* try our value */
      memcpy(temp_buffer, p_buffer.data(), DVDCPXM_BLOCK_SIZE);
      result = cprm_decrypt_block(std::span{temp_buffer}, flags, cpxm->vr_k_t,
                                  guess);
      if (result == 1) [[unlikely]] {
        memcpy(p_buffer.data(), temp_buffer, DVDCPXM_BLOCK_SIZE);
        cpxm->apstb = guess;
        return result;
      }
    }
  }
  }

  return 0;
}

/* this function is used internally */
int dvdcpxm_close_internal(dvdcss_t dvdcss) noexcept {
  dvdcss->cpxm.reset();

  if (g_cpxm_cache.empty())
    return 0;

  /* check ID */
  struct stat stat;
  if (fstat(dvdcss->i_fd, &stat) < 0)
    return -1;

  /* remove only if cached */
  if (dvdcss->cpxm_was_cached) {
    for (auto it = g_cpxm_cache.begin(); it != g_cpxm_cache.end(); ++it) {
      if (it->st_dev == stat.st_dev) {
        g_cpxm_cache.erase(it);
        break;
      }
    }
  }
  return 0;
}

} // namespace dvdcss

/* CPXM exported prototype definitions */
/* these methods should behave similarily but use dvdcpxm_decrypt instead of
 * unscramble, and remove any unnecessary code */

using namespace dvdcss;

/* aliased dvdcpxm_close to not break ABI */
int dvdcpxm_close(dvdcss_t dvdcss) { return dvdcss_close(dvdcss); }

int dvdcpxm_read(dvdcss_t dvdcss, void *p_buffer, int i_blocks, int i_flags) {
  uint8_t *_p_buffer = static_cast<uint8_t *>(p_buffer);
  int i_ret, i_index;

  i_ret = dvdcss->device_strategy.read(dvdcss, _p_buffer, i_blocks);

  if (i_ret <= 0 || !(i_flags & DVDCSS_READ_DECRYPT)) {
    return i_ret;
  }

  /* Decrypt the blocks we managed to read */
  for (i_index = i_ret; i_index; i_index--) {
    dvdcpxm_decrypt(
        dvdcss->cpxm.get(), dvdcss->media_type,
        std::span<uint8_t>{_p_buffer, static_cast<size_t>(DVDCSS_BLOCK_SIZE)},
        DVDCPXM_RESET_CCI);
    _p_buffer = _p_buffer + DVDCSS_BLOCK_SIZE;
  }

  return i_ret;
}

int dvdcpxm_seek(dvdcss_t dvdcss, int i_blocks, int i_flags) {
  static_cast<void>(i_flags);
  return dvdcss_seek(dvdcss, i_blocks, DVDCSS_NOFLAGS);
}

int dvdcpxm_readv(dvdcss_t dvdcss, void *p_iovec, int i_blocks, int i_flags) {
  struct iovec *_p_iovec = static_cast<struct iovec *>(p_iovec);
  auto buffers = make_scatter_buffers(_p_iovec, i_blocks);
  int i_ret;

  i_ret = dvdcss->device_strategy.readv(dvdcss, buffers);

  if (i_ret <= 0 || !(i_flags & DVDCSS_READ_DECRYPT)) {
    return i_ret;
  }

  int blocks_remaining = i_ret;

  /* Decrypt the blocks we managed to read */
  for (auto buffer : buffers) {
    if (buffer.size() & 0x7ff) {
      return -1;
    }

    for (size_t offset = 0; offset < buffer.size() && blocks_remaining > 0;
         offset += DVDCSS_BLOCK_SIZE, --blocks_remaining) {
      auto sector = buffer.subspan(offset, DVDCSS_BLOCK_SIZE);
      /* reseting CCI handled by decrypt */
      dvdcpxm_decrypt(dvdcss->cpxm.get(), dvdcss->media_type, sector,
                      DVDCPXM_RESET_CCI);
    }
  }

  return i_ret;
}
