/*****************************************************************************
 * ioctl.cpp: DVD ioctl replacement function
 *****************************************************************************
 * Copyright (C) 1999-2001 VideoLAN
 *
 * Authors: Markus Kuespert <ltlBeBoy@beosmail.com>
 *          Sam Hocevar <sam@zoy.org>
 *          Jon Lech Johansen <jon-vl@nanocrew.net>
 *          Håkan Hjort <d95hjort@dtek.chalmers.se>
 *          Eugenio Jarosiewicz <ej0@cise.ufl.edu>
 *          David Siebörger <drs-videolan@rucus.ru.ac.za>
 *          Alex Strelnikov <lelik@os2.ru>
 *          Gildas Bazin <gbazin@netcourrier.com>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include "config.h"

#include <array>
#include <cstring>
#include <span>
#include <sys/types.h>

#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>
#else
#include <netinet/in.h>
#include <sys/ioctl.h>
#endif

#ifdef DVD_STRUCT_IN_LINUX_CDROM_H
#include <linux/cdrom.h>
#endif
#ifdef HAVE_SCSI_SG_H
#include <scsi/sg.h>
#endif
#ifdef DARWIN_DVD_IOCTL
#include <IOKit/storage/IODVDMediaBSDClient.h>
#endif

#include "common.h"
#include "css.h"
#include "dvdcss/dvdcss.h"
#include "ioctl.h"
#include "libdvdcpxm.h"

using std::memcpy;
using std::memset;

#define DVD_CHALLENGE_SIZE 10

namespace dvdcss {

#if defined(_WIN32)
static inline HANDLE dvdcss_to_handle(dvdcss_fd_t fd) {
  return reinterpret_cast<HANDLE>(fd);
}
#endif

#if defined(_WIN32)
static void WinInitSPTD(SCSI_PASS_THROUGH_DIRECT *, int);
#endif

#if defined(DARWIN_DVD_IOCTL)
template <typename DkDvdType, typename BufferType>
struct DarwinDvdIoctlRequest {
  DkDvdType command{};
  BufferType buffer{};

  template <typename FormatType>
  explicit DarwinDvdIoctlRequest(FormatType format) {
    command.format = format;
    command.buffer = &buffer;
    command.bufferLength = sizeof(buffer);
  }

  DarwinDvdIoctlRequest(const DarwinDvdIoctlRequest &) = delete;
  DarwinDvdIoctlRequest &operator=(const DarwinDvdIoctlRequest &) = delete;
  DarwinDvdIoctlRequest(DarwinDvdIoctlRequest &&) = delete;
  DarwinDvdIoctlRequest &operator=(DarwinDvdIoctlRequest &&) = delete;
};
#endif

/*****************************************************************************
 * ioctl_ReadCopyright: check whether the disc is encrypted or not
 *****************************************************************************/
int ioctl_ReadCopyright(dvdcss_fd_t i_fd, int i_layer, int *pi_copyright) {
  int i_ret;

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_struct dvd = {0};

  dvd.type = DVD_STRUCT_COPYRIGHT;
  dvd.copyright.layer_num = i_layer;

  i_ret = ioctl(i_fd, DVD_READ_STRUCT, &dvd);

  *pi_copyright = dvd.copyright.cpst;

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_read_structure_t, DVDCopyrightInfo> request{
      kDVDStructureFormatCopyrightInfo};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.layer = i_layer;

  i_ret = ioctl(i_fd, DKIOCDVDREADSTRUCTURE, &dvd);

  *pi_copyright = dvdbs.copyrightProtectionSystemType;

#elif defined(_WIN32)
  DWORD tmp;
  SCSI_PASS_THROUGH_DIRECT sptd = {};
  uint8_t p_buffer[8];
  sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
  sptd.DataBuffer = p_buffer;
  sptd.DataTransferLength = sizeof(p_buffer);
  WinInitSPTD(&sptd, GPCMD_READ_DVD_STRUCTURE);

  /*  When using IOCTL_DVD_READ_STRUCTURE and
      DVD_COPYRIGHT_DESCRIPTOR, CopyrightProtectionType
      seems to be always 6 ???
      To work around this MS bug we try to send a raw SCSI command
      instead (if we've got enough privileges to do so). */

  sptd.Cdb[6] = i_layer;
  sptd.Cdb[7] = DVD_STRUCT_COPYRIGHT;

  i_ret =
      DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_SCSI_PASS_THROUGH_DIRECT,
                      &sptd, sizeof(SCSI_PASS_THROUGH_DIRECT), &sptd,
                      sizeof(SCSI_PASS_THROUGH_DIRECT), &tmp, NULL)
          ? 0
          : -1;

  if (i_ret == 0) {
    *pi_copyright = p_buffer[4];
  }

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReadDiscKey: get the disc key
 *****************************************************************************/
int ioctl_ReadDiscKey(dvdcss_fd_t i_fd, const int *pi_agid,
                      std::span<uint8_t> p_key) {
  int i_ret;

  if (p_key.size() < DVD_DISCKEY_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_struct dvd = {0};

  dvd.type = DVD_STRUCT_DISCKEY;
  dvd.disckey.agid = *pi_agid;

  i_ret = ioctl(i_fd, DVD_READ_STRUCT, &dvd);

  if (i_ret < 0) {
    return i_ret;
  }

  memcpy(p_key.data(), dvd.disckey.value, DVD_DISCKEY_SIZE);

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_read_structure_t, DVDDiscKeyInfo> request{
      kDVDStructureFormatDiscKeyInfo};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.grantID = *pi_agid;

  i_ret = ioctl(i_fd, DKIOCDVDREADSTRUCTURE, &dvd);

  memcpy(p_key.data(), dvdbs.discKeyStructures, DVD_DISCKEY_SIZE);

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_DISK_KEY_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);

  key->KeyLength = DVD_DISK_KEY_LENGTH;
  key->SessionId = *pi_agid;
  key->KeyType = DVD_KEY_TYPE::DvdDiskKey;
  key->KeyFlags = 0;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_READ_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

  if (i_ret < 0) {
    return i_ret;
  }

  memcpy(p_key.data(), key->KeyData, DVD_DISCKEY_SIZE);

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReadTitleKey: get the title key
 *****************************************************************************/
int ioctl_ReadTitleKey(dvdcss_fd_t i_fd, const int *pi_agid, int i_pos,
                       std::span<uint8_t> p_key) {
  int i_ret;

  if (p_key.size() < DVD_KEY_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_LU_SEND_TITLE_KEY;
  auth_info.lstk.agid = *pi_agid;
  auth_info.lstk.lba = i_pos;

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

  memcpy(p_key.data(), auth_info.lstk.title_key, DVD_KEY_SIZE);

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_report_key_t, DVDTitleKeyInfo> request{
      kDVDKeyFormatTitleKey};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.address = i_pos;
  dvd.grantID = *pi_agid;
  dvd.keyClass = kDVDKeyClassCSS_CPPM_CPRM;

  i_ret = ioctl(i_fd, DKIOCDVDREPORTKEY, &dvd);

  memcpy(p_key.data(), dvdbs.titleKeyValue, DVD_KEY_SIZE);

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_TITLE_KEY_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);

  key->KeyLength = DVD_TITLE_KEY_LENGTH;
  key->SessionId = *pi_agid;
  key->KeyType = DVD_KEY_TYPE::DvdTitleKey;
  key->KeyFlags = 0;
  key->Parameters.TitleOffset.QuadPart =
      static_cast<LONGLONG>(i_pos) * DVDCSS_BLOCK_SIZE;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_READ_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

  memcpy(p_key.data(), key->KeyData, DVD_KEY_SIZE);

#else
#error "DVD ioctls are unavailable on this system"

#endif

  return i_ret;
}

/*****************************************************************************
 * ioctl_ReportAgid: get AGID from the drive
 *****************************************************************************/
int ioctl_ReportAgid(dvdcss_fd_t i_fd, int *pi_agid) {
  int i_ret;

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_LU_SEND_AGID;
  auth_info.lsa.agid = *pi_agid;

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

  *pi_agid = auth_info.lsa.agid;

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_report_key_t, DVDAuthenticationGrantIDInfo>
      request{kDVDKeyFormatAGID_CSS};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.grantID = *pi_agid;
  dvd.keyClass = kDVDKeyClassCSS_CPPM_CPRM;

  i_ret = ioctl(i_fd, DKIOCDVDREPORTKEY, &dvd);

  *pi_agid = dvdbs.grantID;

#elif defined(_WIN32)
  DWORD tmp = 0;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_START_SESSION, &tmp,
                          4, pi_agid, sizeof(*pi_agid), &tmp, NULL)
              ? 0
              : -1;

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReportChallenge: get challenge from the drive
 *****************************************************************************/
int ioctl_ReportChallenge(dvdcss_fd_t i_fd, const int *pi_agid,
                          std::span<uint8_t> p_challenge) {
  int i_ret;

  if (p_challenge.size() < DVD_CHALLENGE_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_LU_SEND_CHALLENGE;
  auth_info.lsc.agid = *pi_agid;

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

  memcpy(p_challenge.data(), auth_info.lsc.chal, DVD_CHALLENGE_SIZE);

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_report_key_t, DVDChallengeKeyInfo> request{
      kDVDKeyFormatChallengeKey};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.grantID = *pi_agid;

  i_ret = ioctl(i_fd, DKIOCDVDREPORTKEY, &dvd);

  memcpy(p_challenge.data(), dvdbs.challengeKeyValue, DVD_CHALLENGE_SIZE);

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_CHALLENGE_KEY_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);

  key->KeyLength = DVD_CHALLENGE_KEY_LENGTH;
  key->SessionId = *pi_agid;
  key->KeyType = DVD_KEY_TYPE::DvdChallengeKey;
  key->KeyFlags = 0;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_READ_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

  if (i_ret < 0) {
    return i_ret;
  }

  memcpy(p_challenge.data(), key->KeyData, DVD_CHALLENGE_SIZE);

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReportASF: get ASF from the drive
 *****************************************************************************/
int ioctl_ReportASF(dvdcss_fd_t i_fd, int *pi_asf) {
  int i_ret;

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_LU_SEND_ASF;
  auth_info.lsasf.asf = *pi_asf;

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

  *pi_asf = auth_info.lsasf.asf;

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_report_key_t, DVDAuthenticationSuccessFlagInfo>
      request{kDVDKeyFormatASF};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  i_ret = ioctl(i_fd, DKIOCDVDREPORTKEY, &dvd);

  *pi_asf = dvdbs.successFlag;

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_ASF_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);
  PDVD_ASF keyData;

  key->KeyLength = DVD_ASF_LENGTH;
  key->KeyType = DVD_KEY_TYPE::DvdAsf;
  key->KeyFlags = 0;

  keyData = reinterpret_cast<PDVD_ASF>(key->KeyData);
  keyData->SuccessFlag = *pi_asf;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_READ_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

  if (i_ret < 0) {
    return i_ret;
  }

  keyData = reinterpret_cast<PDVD_ASF>(key->KeyData);
  *pi_asf = keyData->SuccessFlag;

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReportKey1: get the first key from the drive
 *****************************************************************************/
int ioctl_ReportKey1(dvdcss_fd_t i_fd, const int *pi_agid,
                     std::span<uint8_t> p_key) {
  int i_ret;

  if (p_key.size() < DVD_KEY_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_LU_SEND_KEY1;
  auth_info.lsk.agid = *pi_agid;

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

  memcpy(p_key.data(), auth_info.lsk.key, DVD_KEY_SIZE);

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_report_key_t, DVDKey1Info> request{
      kDVDKeyFormatKey1};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.grantID = *pi_agid;

  i_ret = ioctl(i_fd, DKIOCDVDREPORTKEY, &dvd);

  memcpy(p_key.data(), dvdbs.key1Value, DVD_KEY_SIZE);

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_BUS_KEY_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);

  key->KeyLength = DVD_BUS_KEY_LENGTH;
  key->SessionId = *pi_agid;
  key->KeyType = DVD_KEY_TYPE::DvdBusKey1;
  key->KeyFlags = 0;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_READ_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

  memcpy(p_key.data(), key->KeyData, DVD_KEY_SIZE);

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_InvalidateAgid: invalidate the current AGID
 *****************************************************************************/
int ioctl_InvalidateAgid(dvdcss_fd_t i_fd, int *pi_agid) {
  int i_ret;

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVDCSS_INVALIDATE_AGID;
  auth_info.lsa.agid = *pi_agid;

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_send_key_t, DVDAuthenticationGrantIDInfo>
      request{kDVDKeyFormatAGID_Invalidate};
  auto &dvd = request.command;

  dvd.grantID = *pi_agid;

  i_ret = ioctl(i_fd, DKIOCDVDSENDKEY, &dvd);

#elif defined(_WIN32)
  DWORD tmp;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_END_SESSION,
                          pi_agid, sizeof(*pi_agid), NULL, 0, &tmp, NULL)
              ? 0
              : -1;

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReadCPRMMediaID: Reads the unique album identified used for CPRM
 * decryption
 *****************************************************************************/
int ioctl_ReadCPRMMediaId(dvdcss_fd_t i_fd, int *p_agid,
                          std::span<uint8_t> p_data_buffer) {
  int i_ret;

  if (p_data_buffer.size() < CPRM_MEDIA_ID_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT) && defined(HAVE_SCSI_SG_H)
  struct sg_io_hdr io_hdr = {};
  uint8_t sense[32] = {0};
  uint8_t cdb[12] = {0};
  std::array<uint8_t, CPRM_MEDIA_ID_SIZE + 4> data_buf = {};

  cdb[0] = GPCMD_READ_DVD_STRUCTURE;
  cdb[7] = CPRM_STRUCT_MEDIA_ID;
  cdb[8] = static_cast<uint8_t>((CPRM_MEDIA_ID_SIZE + 4) >> 8);
  cdb[9] = static_cast<uint8_t>((CPRM_MEDIA_ID_SIZE + 4) & 0xff);
  cdb[10] = (*p_agid << 6);

  io_hdr.interface_id = 'S';
  io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
  io_hdr.cmd_len = sizeof(cdb);
  io_hdr.mx_sb_len = sizeof(sense);
  io_hdr.dxfer_len = CPRM_MEDIA_ID_SIZE + 4;
  io_hdr.dxferp = data_buf.data();
  io_hdr.cmdp = cdb;
  io_hdr.sbp = sense;
  io_hdr.timeout = 5000;

  i_ret = ioctl(i_fd, SG_IO, &io_hdr);
  if (i_ret < 0 || io_hdr.status) {
    i_ret = -1;
  } else {
    memcpy(p_data_buffer.data(), data_buf.data() + 4, CPRM_MEDIA_ID_SIZE);
    i_ret = 0;
  }

#elif defined(_WIN32)
  DWORD tmp;
  SCSI_PASS_THROUGH_DIRECT sptd = {};
  sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
  std::array<uint8_t, CPRM_MEDIA_ID_SIZE + 4> raw_buffer = {};
  sptd.DataBuffer = raw_buffer.data();
  sptd.DataTransferLength = CPRM_MEDIA_ID_SIZE + 4;

  WinInitSPTD(&sptd, GPCMD_READ_DVD_STRUCTURE);

  sptd.Cdb[7] = CPRM_STRUCT_MEDIA_ID;
  sptd.Cdb[10] = *p_agid << 6;

  i_ret =
      DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_SCSI_PASS_THROUGH_DIRECT,
                      &sptd, sizeof(sptd), &sptd, sizeof(sptd), &tmp, NULL)
          ? 0
          : -1;

  if (i_ret == 0)
    memcpy(p_data_buffer.data(), raw_buffer.data() + 4, CPRM_MEDIA_ID_SIZE);

#elif defined(DARWIN_DVD_IOCTL)
  int h_dvd;
  dk_dvd_read_structure_t dvd;
  uint8_t dvdbs[CPRM_MEDIA_ID_SIZE + 4];
  dvd.format = CPRM_STRUCT_MEDIA_ID;
  dvd.buffer = &dvdbs;
  dvd.bufferLength = sizeof(dvdbs);

  dvd.grantID = *p_agid;

  i_ret = ioctl(h_dvd, DKIOCDVDREADSTRUCTURE, &dvd);
  if (i_ret == 0)
    memcpy(p_data_buffer.data(), dvd.buffer, CPRM_MEDIA_ID_SIZE);

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReadCPRMMKBPack: Reads Media Key Block pack from the disk for cprm
 *****************************************************************************/
int ioctl_ReadCPRMMKBPack(dvdcss_fd_t i_fd, int *p_agid, int mkb_pack,
                          std::span<uint8_t> p_mkb_pack, int *p_total_packs) {
  int i_ret;

  if (p_mkb_pack.size() < CPRM_MKB_PACK_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT) && defined(HAVE_SCSI_SG_H)
  std::array<uint8_t, CPRM_MKB_PACK_SIZE + 4> sptd_buf = {};
  uint8_t cdb[12] = {0};
  uint8_t sense[32] = {0};

  cdb[0] = GPCMD_READ_DVD_STRUCTURE;
  cdb[2] = static_cast<uint8_t>((mkb_pack >> 24) & 0xFF);
  cdb[3] = static_cast<uint8_t>((mkb_pack >> 16) & 0xFF);
  cdb[4] = static_cast<uint8_t>((mkb_pack >> 8) & 0xFF);
  cdb[5] = static_cast<uint8_t>(mkb_pack & 0xFF);
  cdb[7] = CPRM_STRUCT_MKB;
  cdb[8] = static_cast<uint8_t>((CPRM_MKB_PACK_SIZE + 4) >> 8);
  cdb[9] = static_cast<uint8_t>((CPRM_MKB_PACK_SIZE + 4) & 0xFF);
  cdb[10] = *p_agid << 6;

  struct sg_io_hdr io_hdr = {};

  io_hdr.interface_id = 'S';
  io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
  io_hdr.cmd_len = sizeof(cdb);
  io_hdr.mx_sb_len = sizeof(sense);
  io_hdr.dxfer_len = CPRM_MKB_PACK_SIZE + 4;
  io_hdr.dxferp = sptd_buf.data();
  io_hdr.cmdp = cdb;
  io_hdr.sbp = sense;
  io_hdr.timeout = 5000;

  i_ret = ioctl(i_fd, SG_IO, &io_hdr);
  if (i_ret < 0 || io_hdr.status) {
    i_ret = -1;
  } else {
    *p_total_packs = sptd_buf[3];
    memcpy(p_mkb_pack.data(), sptd_buf.data() + 4, CPRM_MKB_PACK_SIZE);
  }

#elif defined(DARWIN_DVD_IOCTL)
  int h_dvd;
  dk_dvd_read_structure_t dvd;
  uint8_t dvdbs[CPRM_MKB_PACK_SIZE];
  dvd.format = CPRM_STRUCT_MKB;
  dvd.buffer = &dvdbs;
  dvd.bufferLength = sizeof(dvdbs);

  dvd.grantID = *p_agid;

  i_ret = ioctl(h_dvd, DKIOCDVDREADSTRUCTURE, &dvd);
  if (i_ret == 0)
    memcpy(p_mkb_pack.data(), dvd.buffer, CPRM_MKB_PACK_SIZE);

#elif defined(_WIN32)
  DWORD tmp;
  SCSI_PASS_THROUGH_DIRECT sptd = {};
  uint8_t p_buffer[CPRM_MKB_PACK_SIZE + 4];
  sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
  sptd.DataBuffer = p_buffer;
  sptd.DataTransferLength = CPRM_MKB_PACK_SIZE + 4;

  WinInitSPTD(&sptd, GPCMD_READ_DVD_STRUCTURE);

  sptd.Cdb[2] = static_cast<uint8_t>((mkb_pack >> 24) & 0xFF);
  sptd.Cdb[3] = static_cast<uint8_t>((mkb_pack >> 16) & 0xFF);
  sptd.Cdb[4] = static_cast<uint8_t>((mkb_pack >> 8) & 0xFF);
  sptd.Cdb[5] = static_cast<uint8_t>(mkb_pack & 0xFF);
  sptd.Cdb[7] = CPRM_STRUCT_MKB;
  sptd.Cdb[10] = *p_agid << 6;

  i_ret =
      DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_SCSI_PASS_THROUGH_DIRECT,
                      &sptd, sizeof(sptd), &sptd, sizeof(sptd), &tmp, NULL)
          ? 0
          : -1;

  if (i_ret == 0) {
    *p_total_packs = p_buffer[3];
    memcpy(p_mkb_pack.data(), p_buffer + 4, CPRM_MKB_PACK_SIZE);
  }
  return i_ret;

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_SendChallenge: send challenge to the drive
 *****************************************************************************/
int ioctl_SendChallenge(dvdcss_fd_t i_fd, const int *pi_agid,
                        std::span<const uint8_t> p_challenge) {
  int i_ret;

  if (p_challenge.size() < DVD_CHALLENGE_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_HOST_SEND_CHALLENGE;
  auth_info.hsc.agid = *pi_agid;

  memcpy(auth_info.hsc.chal, p_challenge.data(), DVD_CHALLENGE_SIZE);

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_send_key_t, DVDChallengeKeyInfo> request{
      kDVDKeyFormatChallengeKey};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.grantID = *pi_agid;
  dvd.keyClass = kDVDKeyClassCSS_CPPM_CPRM;

  dvdbs.dataLength[1] = 0xe;
  memcpy(dvdbs.challengeKeyValue, p_challenge.data(), DVD_CHALLENGE_SIZE);

  i_ret = ioctl(i_fd, DKIOCDVDSENDKEY, &dvd);

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_CHALLENGE_KEY_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);

  key->KeyLength = DVD_CHALLENGE_KEY_LENGTH;
  key->SessionId = *pi_agid;
  key->KeyType = DVD_KEY_TYPE::DvdChallengeKey;
  key->KeyFlags = 0;

  memcpy(key->KeyData, p_challenge.data(), DVD_CHALLENGE_SIZE);

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_SEND_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_SendKey2: send the second key to the drive
 *****************************************************************************/
int ioctl_SendKey2(dvdcss_fd_t i_fd, const int *pi_agid,
                   std::span<const uint8_t> p_key) {
  int i_ret;

  if (p_key.size() < DVD_KEY_SIZE) {
    return -1;
  }

#if defined(HAVE_LINUX_DVD_STRUCT)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_HOST_SEND_KEY2;
  auth_info.hsk.agid = *pi_agid;

  memcpy(auth_info.hsk.key, p_key.data(), DVD_KEY_SIZE);

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_send_key_t, DVDKey2Info> request{
      kDVDKeyFormatKey2};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.grantID = *pi_agid;
  dvd.keyClass = kDVDKeyClassCSS_CPPM_CPRM;

  dvdbs.dataLength[1] = 0xa;
  memcpy(dvdbs.key2Value, p_key.data(), DVD_KEY_SIZE);

  i_ret = ioctl(i_fd, DKIOCDVDSENDKEY, &dvd);

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_BUS_KEY_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);

  key->KeyLength = DVD_BUS_KEY_LENGTH;
  key->SessionId = *pi_agid;
  key->KeyType = DVD_KEY_TYPE::DvdBusKey2;
  key->KeyFlags = 0;

  memcpy(key->KeyData, p_key.data(), DVD_KEY_SIZE);

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_SEND_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

/*****************************************************************************
 * ioctl_ReportRPC: get RPC (Regional Playback Control) status for the drive
 *****************************************************************************/
int ioctl_ReportRPC(dvdcss_fd_t i_fd, int *p_type, int *p_mask, int *p_scheme) {
  int i_ret;

#if defined(HAVE_LINUX_DVD_STRUCT) && defined(DVD_LU_SEND_RPC_STATE)
  dvd_authinfo auth_info = {0};

  auth_info.type = DVD_LU_SEND_RPC_STATE;

  i_ret = ioctl(i_fd, DVD_AUTH, &auth_info);

  *p_type = auth_info.lrpcs.type;
  *p_mask = auth_info.lrpcs.region_mask;
  *p_scheme = auth_info.lrpcs.rpc_scheme;

#elif defined(HAVE_LINUX_DVD_STRUCT)
  /* FIXME: OpenBSD doesn't know this */
  i_ret = -1;

#elif defined(DARWIN_DVD_IOCTL)
  DarwinDvdIoctlRequest<dk_dvd_report_key_t, DVDRegionPlaybackControlInfo>
      request{kDVDKeyFormatRegionState};
  auto &dvd = request.command;
  auto &dvdbs = request.buffer;

  dvd.keyClass = kDVDKeyClassCSS_CPPM_CPRM;

  i_ret = ioctl(i_fd, DKIOCDVDREPORTKEY, &dvd);

  *p_type = dvdbs.typeCode;
  *p_mask = dvdbs.driveRegion;
  *p_scheme = dvdbs.rpcScheme;

#elif defined(_WIN32)
  DWORD tmp;
  uint8_t buffer[DVD_RPC_KEY_LENGTH] = {0};
  auto *key = reinterpret_cast<PDVD_COPY_PROTECT_KEY>(&buffer);
  PDVD_RPC_KEY keyData;

  key->KeyLength = DVD_RPC_KEY_LENGTH;
  key->KeyType = DVD_KEY_TYPE::DvdGetRpcKey;
  key->KeyFlags = 0;

  i_ret = DeviceIoControl(dvdcss_to_handle(i_fd), IOCTL_DVD_READ_KEY, key,
                          key->KeyLength, key, key->KeyLength, &tmp, NULL)
              ? 0
              : -1;

  if (i_ret < 0) {
    return i_ret;
  }

  keyData = reinterpret_cast<PDVD_RPC_KEY>(key->KeyData);
  *p_type = keyData->TypeCode;
  *p_mask = keyData->RegionMask;
  *p_scheme = keyData->RpcScheme;

#else
#error "DVD ioctls are unavailable on this system"

#endif
  return i_ret;
}

#if defined(_WIN32)
/*****************************************************************************
 * WinInitSPTD: initialize a sptd structure
 *****************************************************************************
 * This function initializes a SCSI pass through command structure for future
 * use, either a read command or a write command.
 *****************************************************************************/
static void WinInitSPTD(SCSI_PASS_THROUGH_DIRECT *p_sptd, int i_type) {
  memset(p_sptd->DataBuffer, 0, p_sptd->DataTransferLength);

  switch (i_type) {
  case GPCMD_SEND_KEY:
    p_sptd->DataIn = SCSI_IOCTL_DATA_OUT;
    break;

  case GPCMD_READ_DVD_STRUCTURE:
  case GPCMD_REPORT_KEY:
    p_sptd->DataIn = SCSI_IOCTL_DATA_IN;
    break;
  }

  p_sptd->Cdb[0] = i_type;
  p_sptd->Cdb[8] = static_cast<uint8_t>(p_sptd->DataTransferLength >> 8) & 0xff;
  p_sptd->Cdb[9] = static_cast<uint8_t>(p_sptd->DataTransferLength) & 0xff;
  p_sptd->CdbLength = 12;

  p_sptd->TimeOutValue = 2;
}
#endif /* defined( _WIN32 ) */

} // namespace dvdcss
