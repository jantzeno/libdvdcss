/**
 * \file libdvdcpxm.h
 * \author Maxim V.Anisiutkin <Maxim.Anisiutkin@gmail.com>
 * \author Saifelden Ismail <saifelden@gmail.com>
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

#ifndef _LIBDVDCPXM_H
#define _LIBDVDCPXM_H

#include "dvdcss/dvdcpxm.h"
#include "dvdcss/dvdcss.h"
#include <stdint.h>

namespace dvdcss {

inline constexpr int COPYRIGHT_PROTECTION_NONE = 0;
inline constexpr int COPYRIGHT_PROTECTION_CPPM = 1;
inline constexpr int COPYRIGHT_PROTECTION_CPRM = 2;

inline constexpr int CPRM_STRUCT_MEDIA_ID = 0x06;
inline constexpr int CPRM_STRUCT_MKB = 0x07;

inline constexpr int CPRM_MEDIA_ID_SIZE = 20;
inline constexpr int CPRM_MKB_PACK_SIZE = 24576;
inline constexpr int CPRM_MKB_SIZE = 16 * CPRM_MKB_PACK_SIZE - 16;

inline constexpr uint8_t CCI_BYTE = 0x00;

typedef struct {
  uint8_t col;
  uint16_t row;
  uint64_t key;
} device_key_t;

typedef struct {
  struct {
    uint8_t type : 4;
    uint8_t reserved : 4;
    uint8_t manufacturer_id[2];
    uint8_t serial_number[5];
  } id_media;

  uint8_t dvd_mac[10];
} cprm_media_id_t;

typedef struct {
  uint8_t mkb_hash[8];
  uint8_t reserved[8];
} cprm_mkb_desc_t;

typedef struct {
  cprm_mkb_desc_t mkb_desc;
  uint8_t mkb[CPRM_MKB_SIZE];
} cprm_mkb_t;

/* used to clear dvdcpxm structures, including cache */
int dvdcpxm_close_internal(dvdcss_t) noexcept;

} // namespace dvdcss

#endif
