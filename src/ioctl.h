/*****************************************************************************
 * ioctl.h: DVD ioctl replacement function
 *****************************************************************************
 * Copyright (C) 1999-2001 VideoLAN
 *
 * Authors: Sam Hocevar <sam@zoy.org>
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

#ifndef DVDCSS_IOCTL_H
#define DVDCSS_IOCTL_H

#include <span>
#include <stddef.h>
#include <stdint.h>

#include "common.h"

namespace dvdcss {

[[nodiscard]] int ioctl_ReadCopyright(dvdcss_fd_t, int, int *);
[[nodiscard]] int ioctl_ReadDiscKey(dvdcss_fd_t, const int *,
                                    std::span<uint8_t>);
[[nodiscard]] int ioctl_ReadTitleKey(dvdcss_fd_t, const int *, int,
                                     std::span<uint8_t>);
[[nodiscard]] int ioctl_ReportAgid(dvdcss_fd_t, int *);
[[nodiscard]] int ioctl_ReportChallenge(dvdcss_fd_t, const int *,
                                        std::span<uint8_t>);
[[nodiscard]] int ioctl_ReportKey1(dvdcss_fd_t, const int *,
                                   std::span<uint8_t>);
[[nodiscard]] int ioctl_ReportASF(dvdcss_fd_t, int *);
[[nodiscard]] int ioctl_InvalidateAgid(dvdcss_fd_t, int *);
[[nodiscard]] int ioctl_SendChallenge(dvdcss_fd_t, const int *,
                                      std::span<const uint8_t>);
[[nodiscard]] int ioctl_SendKey2(dvdcss_fd_t, const int *,
                                 std::span<const uint8_t>);
[[nodiscard]] int ioctl_ReportRPC(dvdcss_fd_t, int *, int *, int *);

/* Special Ioctl functions for cpxm decryption */
[[nodiscard]] int ioctl_ReadCPRMMKBPack(dvdcss_fd_t, int *, int,
                                        std::span<uint8_t>, int *);
[[nodiscard]] int ioctl_ReadCPRMMediaId(dvdcss_fd_t, int *, std::span<uint8_t>);

inline constexpr int DVD_DISCKEY_SIZE = 2048;

/*****************************************************************************
 * Additional types, OpenBSD-specific
 *****************************************************************************/
#if defined(HAVE_OPENBSD_DVD_STRUCT)
typedef union dvd_struct dvd_struct;
typedef union dvd_authinfo dvd_authinfo;
#endif

/*****************************************************************************
 * Various DVD I/O tables
 *****************************************************************************/
/* The generic packet command opcodes for CD/DVD Logical Units,
 * From Table 57 of the SFF8090 Ver. 3 (Mt. Fuji) draft standard. */
#define GPCMD_READ_DVD_STRUCTURE 0xad
#define GPCMD_REPORT_KEY 0xa4
#define GPCMD_SEND_KEY 0xa3
#ifndef DVD_STRUCT_IN_SYS_DVDIO_H
/* DVD struct types */
#define DVD_STRUCT_PHYSICAL 0x00
#define DVD_STRUCT_COPYRIGHT 0x01
#define DVD_STRUCT_DISCKEY 0x02
#define DVD_STRUCT_BCA 0x03
#define DVD_STRUCT_MANUFACT 0x04
/* Key formats */
#define DVD_REPORT_AGID 0x00
#define DVD_REPORT_CHALLENGE 0x01
#define DVD_SEND_CHALLENGE 0x01
#define DVD_REPORT_KEY1 0x02
#define DVD_SEND_KEY2 0x03
#define DVD_REPORT_TITLE_KEY 0x04
#define DVD_REPORT_ASF 0x05
#define DVD_SEND_RPC 0x06
#define DVD_REPORT_RPC 0x08
#endif /* ndef DVD_STRUCT_IN_SYS_DVDIO_H */
#define DVDCSS_INVALIDATE_AGID 0x3f

/*****************************************************************************
 * Win32-ioctl-specific
 *****************************************************************************/
#if defined(_WIN32)

#define _WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#define IOCTL_DVD_START_SESSION                                                \
  CTL_CODE(FILE_DEVICE_DVD, 0x0400, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DVD_READ_KEY                                                     \
  CTL_CODE(FILE_DEVICE_DVD, 0x0401, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DVD_SEND_KEY                                                     \
  CTL_CODE(FILE_DEVICE_DVD, 0x0402, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DVD_END_SESSION                                                  \
  CTL_CODE(FILE_DEVICE_DVD, 0x0403, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DVD_GET_REGION                                                   \
  CTL_CODE(FILE_DEVICE_DVD, 0x0405, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DVD_SEND_KEY2                                                    \
  CTL_CODE(FILE_DEVICE_DVD, 0x0406, METHOD_BUFFERED,                           \
           FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DVD_READ_STRUCTURE                                               \
  CTL_CODE(FILE_DEVICE_DVD, 0x0450, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SCSI_PASS_THROUGH_DIRECT                                         \
  CTL_CODE(FILE_DEVICE_CONTROLLER, 0x0405, METHOD_BUFFERED,                    \
           FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define DVD_COPYRIGHT_MASK 0x00000040
#define DVD_NOT_COPYRIGHTED 0x00000000
#define DVD_COPYRIGHTED 0x00000040

#define DVD_SECTOR_PROTECT_MASK 0x00000020
#define DVD_SECTOR_NOT_PROTECTED 0x00000000
#define DVD_SECTOR_PROTECTED 0x00000020

#define SCSI_IOCTL_DATA_OUT 0
#define SCSI_IOCTL_DATA_IN 1

typedef ULONG DVD_SESSION_ID, *PDVD_SESSION_ID;

enum class DVD_STRUCTURE_FORMAT {
  DvdPhysicalDescriptor,
  DvdCopyrightDescriptor,
  DvdDiskKeyDescriptor,
  DvdBCADescriptor,
  DvdManufacturerDescriptor,
  DvdMaxDescriptor,
};

using PDVD_STRUCTURE_FORMAT = DVD_STRUCTURE_FORMAT *;

typedef struct DVD_READ_STRUCTURE {
  LARGE_INTEGER BlockByteOffset;
  DVD_STRUCTURE_FORMAT Format;
  DVD_SESSION_ID SessionId;
  UCHAR LayerNumber;
} DVD_READ_STRUCTURE, *PDVD_READ_STRUCTURE;

typedef struct DVD_COPYRIGHT_DESCRIPTOR {
  UCHAR CopyrightProtectionType;
  UCHAR RegionManagementInformation;
  USHORT Reserved;
} DVD_COPYRIGHT_DESCRIPTOR, *PDVD_COPYRIGHT_DESCRIPTOR;

enum class DVD_KEY_TYPE {
  DvdChallengeKey = 0x01,
  DvdBusKey1,
  DvdBusKey2,
  DvdTitleKey,
  DvdAsf,
  DvdSetRpcKey = 0x6,
  DvdGetRpcKey = 0x8,
  DvdDiskKey = 0x80,
  DvdInvalidateAGID = 0x3f,
};

typedef union DVD_COPY_PROTECT_PARAMETERS {
  struct DVD_COPY_PROTECT_FILE_BLOCK {
    ULONG FileHandle;
    ULONG Reserved; /* used for NT alignment */
  } File;
  LARGE_INTEGER TitleOffset;
} DVD_COPY_PROTECT_PARAMETERS;

typedef struct DVD_COPY_PROTECT_KEY {
  ULONG KeyLength;
  DVD_SESSION_ID SessionId;
  DVD_KEY_TYPE KeyType;
  ULONG KeyFlags;
  DVD_COPY_PROTECT_PARAMETERS Parameters;
  UCHAR KeyData[1];
} DVD_COPY_PROTECT_KEY, *PDVD_COPY_PROTECT_KEY;

inline constexpr ULONG DVD_COPY_PROTECT_KEY_HEADER_SIZE =
    static_cast<ULONG>(offsetof(DVD_COPY_PROTECT_KEY, KeyData));

typedef struct DVD_ASF {
  UCHAR Reserved0[3];
  UCHAR SuccessFlag : 1;
  UCHAR Reserved1 : 7;
} DVD_ASF, *PDVD_ASF;

typedef struct DVD_RPC_KEY {
  UCHAR UserResetsAvailable : 3;
  UCHAR ManufacturerResetsAvailable : 3;
  UCHAR TypeCode : 2;
  UCHAR RegionMask;
  UCHAR RpcScheme;
  UCHAR Reserved2[1];
} DVD_RPC_KEY, *PDVD_RPC_KEY;

inline constexpr ULONG DVD_CHALLENGE_KEY_LENGTH =
    12 + DVD_COPY_PROTECT_KEY_HEADER_SIZE;
inline constexpr ULONG DVD_BUS_KEY_LENGTH =
    8 + DVD_COPY_PROTECT_KEY_HEADER_SIZE;
inline constexpr ULONG DVD_TITLE_KEY_LENGTH =
    8 + DVD_COPY_PROTECT_KEY_HEADER_SIZE;
inline constexpr ULONG DVD_DISK_KEY_LENGTH =
    DVD_DISCKEY_SIZE + DVD_COPY_PROTECT_KEY_HEADER_SIZE;
inline constexpr ULONG DVD_RPC_KEY_LENGTH =
    static_cast<ULONG>(sizeof(DVD_RPC_KEY)) + DVD_COPY_PROTECT_KEY_HEADER_SIZE;
inline constexpr ULONG DVD_ASF_LENGTH =
    static_cast<ULONG>(sizeof(DVD_ASF)) + DVD_COPY_PROTECT_KEY_HEADER_SIZE;

typedef struct SCSI_PASS_THROUGH_DIRECT {
  USHORT Length;
  UCHAR ScsiStatus;
  UCHAR PathId;
  UCHAR TargetId;
  UCHAR Lun;
  UCHAR CdbLength;
  UCHAR SenseInfoLength;
  UCHAR DataIn;
  ULONG DataTransferLength;
  ULONG TimeOutValue;
  PVOID DataBuffer;
  ULONG SenseInfoOffset;
  UCHAR Cdb[16];
} SCSI_PASS_THROUGH_DIRECT, *PSCSI_PASS_THROUGH_DIRECT;

#endif /* defined( _WIN32 ) */

/*****************************************************************************
 * OS/2-ioctl-specific
 *****************************************************************************/
#if defined(__OS2__)

#define CDROMDISK_EXECMD 0x7A

#define EX_DIRECTION_IN 0x01
#define EX_PLAYING_CHK 0x02

#pragma pack(1)

struct OS2_ExecSCSICmd {
  unsigned long id_code;      // 'CD01'
  unsigned short data_length; // length of the Data Packet
  unsigned short cmd_length;  // length of the Command Buffer
  unsigned short flags;       // flags
  unsigned char command[16];  // Command Buffer for SCSI command
};

#pragma pack()

#endif /* defined( __OS2__ ) */

} // namespace dvdcss

#endif /* DVDCSS_IOCTL_H */
