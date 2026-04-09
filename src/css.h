/*****************************************************************************
 * css.h: Structures for DVD authentication and unscrambling
 *****************************************************************************
 * Copyright (C) 1999-2001 VideoLAN
 *
 * Author: Stéphane Borel <stef@via.ecp.fr>
 *
 * based on:
 *  - css-auth by Derek Fawcus <derek@spider.com>
 *  - DVD CSS ioctls example program by Andrew T. Veliath <andrewtv@usa.net>
 *  - DeCSSPlus by Ethan Hawke
 *  - The Divide and conquer attack by Frank A. Stevenson <frank@funcom.com>
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

#ifndef DVDCSS_CSS_H
#define DVDCSS_CSS_H

#ifdef __cplusplus
#include <array>
#endif

#include <stdint.h>

#include "dvdcss/dvdcss.h"

#define DVD_KEY_SIZE 5

#ifdef __cplusplus
using dvdcss_key = std::array<uint8_t, DVD_KEY_SIZE>;
#else
typedef uint8_t dvdcss_key[DVD_KEY_SIZE];
#endif

typedef struct dvd_title {
  int i_startlb;
  dvdcss_key p_key;
} dvd_title;

typedef struct css {
  int i_agid;             /* Current Authentication Grant ID. */
  dvdcss_key p_bus_key;   /* Current session key. */
  dvdcss_key p_disc_key;  /* This DVD disc's key. */
  dvdcss_key p_title_key; /* Current title key. */
} css;

/*****************************************************************************
 * Prototypes in css.c
 *****************************************************************************/
#ifdef __cplusplus
extern "C" {
[[nodiscard]] int dvdcss_test(dvdcss_t);
[[nodiscard]] int dvdcss_title(dvdcss_t, int);
[[nodiscard]] int dvdcss_disckey(dvdcss_t);
int dvdcss_unscramble(const dvdcss_key &, uint8_t *);

/* exported for USB authentification in CPXM */
[[nodiscard]] int GetBusKey(dvdcss_t);
[[nodiscard]] int GetASF(dvdcss_t);
}
#endif

#endif /* DVDCSS_CSS_H */
