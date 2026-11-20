/*****************************************************************************
 * device.cpp: DVD device access
 *****************************************************************************
 * Copyright (C) 1998-2006 VideoLAN
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include "config.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HAVE_ERRNO_H
#include <cerrno>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef DARWIN_DVD_IOCTL
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/storage/IODVDMedia.h>
#include <IOKit/storage/IOMedia.h>
#include <paths.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include "dvdcss/dvdcss.h"

#include "common.h"
#include "css.h"
#include "device.h"
#include "ioctl.h"
#include "libdvdcss.h"

#ifdef _WIN32
static inline HANDLE dvdcss_to_handle(dvdcss_fd_t fd) {
  return reinterpret_cast<HANDLE>(fd);
}

static int dvdcss_close_handle(dvdcss_fd_t fd) noexcept {
  return CloseHandle(dvdcss_to_handle(fd)) ? 0 : -1;
}
#endif

static int dvdcss_close_fd(dvdcss_fd_t fd) noexcept { return close(fd); }

#include <algorithm>
#include <array>
#include <new>
#include <vector>

using std::getenv;
using std::memcpy;
using std::snprintf;
using std::strerror;
using std::strlen;

namespace dvdcss {

ScopedFd::~ScopedFd() noexcept { static_cast<void>(close()); }

ScopedFd::ScopedFd(ScopedFd &&other) noexcept
    : fd_(other.fd_), kind_(other.kind_) {
  other.fd_ = invalid;
  other.kind_ = Kind::none;
}

ScopedFd &ScopedFd::operator=(ScopedFd &&other) noexcept {
  if (this != &other) {
    reset();
    fd_ = other.fd_;
    kind_ = other.kind_;
    other.fd_ = invalid;
    other.kind_ = Kind::none;
  }

  return *this;
}

void ScopedFd::reset() noexcept { static_cast<void>(close()); }

void ScopedFd::reset(dvdcss_fd_t fd, Kind kind) noexcept {
  if (fd_ == fd && kind_ == kind) {
    return;
  }

  static_cast<void>(close());
  fd_ = fd;
  kind_ = (fd == invalid) ? Kind::none : kind;
}

int ScopedFd::close() noexcept {
  if (!is_valid()) {
    kind_ = Kind::none;
    return 0;
  }

  int result = 0;

#ifdef _WIN32
  if (kind_ == Kind::win32_handle) {
    result = dvdcss_close_handle(fd_);
  } else
#endif
  {
    result = dvdcss_close_fd(fd_);
  }

  fd_ = invalid;
  kind_ = Kind::none;
  return result;
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

/*****************************************************************************
 * Device reading prototypes
 *****************************************************************************/
static int libc_open(dvdcss_t, const char *);
static int libc_seek(dvdcss_t, int);
static int libc_read(dvdcss_t, void *, int);
static int libc_readv(dvdcss_t, ScatterBuffers);

static int stream_seek(dvdcss_t, int);
static int stream_read(dvdcss_t, void *, int);
static int stream_readv(dvdcss_t, ScatterBuffers);

#ifdef _WIN32
static int win2k_open(dvdcss_t, const char *);
static int win2k_seek(dvdcss_t, int);
static int win2k_read(dvdcss_t, void *, int);
static int win2k_readv(dvdcss_t, ScatterBuffers);
#endif

int dvdcss_use_ioctls(dvdcss_t dvdcss) {
  if (dvdcss->uses_stream)
    return 0;

#if defined(_WIN32)
  if (dvdcss->b_file) {
    return 0;
  }

  /* FIXME: implement this for Windows */
  return 1;
#else
  struct stat fileinfo;
  int ret;

  ret = fstat(dvdcss->i_fd, &fileinfo);
  if (ret < 0) {
    return 1; /* What to do?  Be conservative and try to use the ioctls */
  }

  /* Complete this list and check that we test for the right things
   * (I've assumed for all OSs that 'r', (raw) device, are char devices
   *  and those that don't contain/use an 'r' in the name are block devices)
   *
   * Linux needs a block device and Darwin needs a character device.
   */

  /* Check if this is a block/char device */
  if (S_ISBLK(fileinfo.st_mode) || S_ISCHR(fileinfo.st_mode)) {
    return 1;
  } else {
    return 0;
  }
#endif
}

void dvdcss_check_device(dvdcss_t dvdcss) {
#if defined(_WIN32)
  DWORD drives;
  int i;
#elif defined(DARWIN_DVD_IOCTL)
  io_object_t next_media;
  io_iterator_t media_iterator;
  CFMutableDictionaryRef classes_to_match;
#else
  static constexpr std::array<const char *, 3> kDefaultDevices = {
      "/dev/dvd", "/dev/cdrom", "/dev/hdc"};
  int i, i_fd;
#endif

  /* If the device name is non-NULL or stream is set, return. */
  if (!dvdcss->psz_device.empty() || dvdcss->uses_stream) {
    return;
  }

#if defined(_WIN32)
  drives = GetLogicalDrives();

  for (i = 0; drives; i++) {
    char psz_device[] = "A:\\";
    DWORD cur = 1 << i;
    UINT i_ret;

    if ((drives & cur) == 0) {
      continue;
    }
    drives &= ~cur;

    psz_device[0] = static_cast<char>('A' + i);
    i_ret = GetDriveType(psz_device);
    if (i_ret != DRIVE_CDROM) {
      continue;
    }

    /* Remove trailing backslash */
    psz_device[2] = '\0';

    /* FIXME: we want to differentiate between CD and DVD drives
     * using DeviceIoControl() */
    print_debug(dvdcss, "defaulting to drive `%s'", psz_device);
    dvdcss->psz_device = psz_device;
    return;
  }
#elif defined(DARWIN_DVD_IOCTL)

  classes_to_match = IOServiceMatching(kIODVDMediaClass);
  if (classes_to_match == NULL) {
    return;
  }

  CFDictionarySetValue(classes_to_match, CFSTR(kIOMediaEjectableKey),
                       kCFBooleanTrue);

  const auto kern_result = IOServiceGetMatchingServices(
      MACH_PORT_NULL, classes_to_match, &media_iterator);
  if (kern_result != KERN_SUCCESS) {
    return;
  }

  next_media = IOIteratorNext(media_iterator);
  for (;;) {
    char psz_buf[0x32];
    size_t i_pathlen;
    CFTypeRef psz_path;

    next_media = IOIteratorNext(media_iterator);
    if (next_media == 0) {
      break;
    }

    psz_path = IORegistryEntryCreateCFProperty(next_media, CFSTR(kIOBSDNameKey),
                                               kCFAllocatorDefault, 0);
    if (psz_path == NULL) {
      IOObjectRelease(next_media);
      continue;
    }

    snprintf(psz_buf, sizeof(psz_buf), "%s%c", _PATH_DEV, 'r');
    i_pathlen = strlen(psz_buf);

    if (CFGetTypeID(psz_path) == CFStringGetTypeID() &&
        CFStringGetCString(static_cast<CFStringRef>(psz_path),
                           psz_buf + i_pathlen, sizeof(psz_buf) - i_pathlen,
                           kCFStringEncodingASCII)) {
      print_debug(dvdcss, "defaulting to drive `%s'", psz_buf);
      CFRelease(psz_path);
      IOObjectRelease(next_media);
      IOObjectRelease(media_iterator);
      dvdcss->psz_device = psz_buf;
      return;
    }

    CFRelease(psz_path);

    IOObjectRelease(next_media);
  }

  IOObjectRelease(media_iterator);
#else
  for (i = 0; i < static_cast<int>(kDefaultDevices.size()); i++) {
    i_fd = open(kDefaultDevices[i], 0);
    if (i_fd != -1) {
      print_debug(dvdcss, "defaulting to drive `%s'", kDefaultDevices[i]);
      close(i_fd);
      dvdcss->psz_device = kDefaultDevices[i];
      return;
    }
  }
#endif

  print_error(dvdcss, "could not find a suitable default drive");
}

int dvdcss_open_device(dvdcss_t dvdcss) {
  const char *psz_device = getenv("DVDCSS_RAW_DEVICE");
  if (!psz_device) {
    psz_device = dvdcss->psz_device.c_str();
  }
  print_debug(dvdcss, "opening target `%s'", psz_device);

#if defined(_WIN32)
  /* Initialize readv temporary buffer */
  dvdcss->p_readv_buffer.clear();
#endif

  /* if callback functions are initialized */
  if (dvdcss->uses_stream) {
    print_debug(dvdcss, "using stream API for access");
    dvdcss->device_strategy.seek = stream_seek;
    dvdcss->device_strategy.read = stream_read;
    dvdcss->device_strategy.readv = stream_readv;
    return 0;
  }

#if defined(_WIN32)
  dvdcss->b_file = 1;
  /* If device is "X:" or "X:\", we are not actually opening a file. */
  if (psz_device[0] && psz_device[1] == ':' &&
      (!psz_device[2] || (psz_device[2] == '\\' && !psz_device[3])))
    dvdcss->b_file = 0;

  if (!dvdcss->b_file) {
    print_debug(dvdcss, "using Win2K API for access");
    dvdcss->device_strategy.seek = win2k_seek;
    dvdcss->device_strategy.read = win2k_read;
    dvdcss->device_strategy.readv = win2k_readv;
    return win2k_open(dvdcss, psz_device);
  } else
#endif
  {
    print_debug(dvdcss, "using libc API for access");
    dvdcss->device_strategy.seek = libc_seek;
    dvdcss->device_strategy.read = libc_read;
    dvdcss->device_strategy.readv = libc_readv;
    return libc_open(dvdcss, psz_device);
  }
}

int dvdcss_close_device(dvdcss_t dvdcss) {
  if (dvdcss->uses_stream) {
    return 0;
  }

#if defined(_WIN32)
  /* Free readv temporary buffer */
  dvdcss->p_readv_buffer.clear();
  dvdcss->p_readv_buffer.shrink_to_fit();

#endif

  int i_ret = dvdcss->i_fd.close();
  if (i_ret < 0) {
    print_error(dvdcss, "Failed to close fd, data loss possible.");
    return i_ret;
  }

  return 0;
}

/* Following functions are local */

/*****************************************************************************
 * Open commands.
 *****************************************************************************/
static int libc_open(dvdcss_t dvdcss, const char *psz_device) {
  dvdcss_fd_t opened_fd = ScopedFd::invalid;

#ifdef _WIN32
  int wlen;
  wlen = MultiByteToWideChar(CP_UTF8, 0, psz_device, -1, NULL, 0);
  if (wlen > 0) {
    std::vector<wchar_t> wpath(static_cast<size_t>(wlen));
    if (MultiByteToWideChar(CP_UTF8, 0, psz_device, -1, wpath.data(), wlen)) {
      opened_fd = _wopen(wpath.data(), O_BINARY);
    }
  }
#else
  opened_fd = open(psz_device, O_BINARY);
#endif

  if (opened_fd == ScopedFd::invalid) {
    print_error(dvdcss, "failed to open device %s (%s)", psz_device,
                strerror(errno));
    return -1;
  }

  dvdcss->i_fd.reset(opened_fd);

  return 0;
}

#if defined(_WIN32)
static int win2k_open(dvdcss_t dvdcss, const char *psz_device) {
  HANDLE h_fd;
  WCHAR psz_dvd[7] = L"\\\\.\\\0:";
  psz_dvd[4] = psz_device[0];

  /* To work around an M$ bug in IOCTL_DVD_READ_STRUCTURE, we need read
   * _and_ write access to the device (so we can make SCSI Pass Through
   * Requests). Unfortunately this is only allowed if you have
   * administrator privileges so we allow for a fallback method with
   * only read access to the device (in this case ioctl_ReadCopyright()
   * won't send back the right result).
   * (See Microsoft Q241374: Read and Write Access Required for SCSI
   * Pass Through Requests) */
#if _WIN32_WINNT < 0x0602 /* _WIN32_WINNT_WIN8 */
  h_fd = CreateFileW(psz_dvd, GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                     FILE_FLAG_RANDOM_ACCESS, NULL);

  if (h_fd == INVALID_HANDLE_VALUE)
    h_fd = CreateFileW(psz_dvd, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
#else
  CREATEFILE2_EXTENDED_PARAMETERS params;
  ZeroMemory(&params, sizeof(params));
  params.dwSize = sizeof(params);
  params.dwFileFlags = FILE_FLAG_RANDOM_ACCESS;
  h_fd =
      CreateFile2(psz_dvd, GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, &params);

  if (h_fd == INVALID_HANDLE_VALUE)
    h_fd = CreateFile2(psz_dvd, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                       &params);
#endif
  if (h_fd == INVALID_HANDLE_VALUE) {
    print_error(dvdcss, "failed to open device %s", psz_device);
    return -1;
  }

  dvdcss->i_fd.reset(static_cast<dvdcss_fd_t>(reinterpret_cast<intptr_t>(h_fd)),
                     ScopedFd::Kind::win32_handle);
  dvdcss->i_pos = 0;

  return 0;
}
#endif /* defined( _WIN32 ) */

/*****************************************************************************
 * Seek commands.
 *****************************************************************************/
static int libc_seek(dvdcss_t dvdcss, int i_blocks) {
  off_t i_seek;

  if (dvdcss->i_pos == i_blocks) {
    /* We are already in position */
    return i_blocks;
  }

  i_seek = static_cast<off_t>(i_blocks) * static_cast<off_t>(DVDCSS_BLOCK_SIZE);
  i_seek = lseek(dvdcss->i_fd, i_seek, SEEK_SET);

  if (i_seek < 0) {
    print_error(dvdcss, "seek error");
    dvdcss->i_pos = -1;
    return -1;
  }

  dvdcss->i_pos = static_cast<int>(i_seek / DVDCSS_BLOCK_SIZE);

  return dvdcss->i_pos;
}

static int stream_seek(dvdcss_t dvdcss, int i_blocks) {
  const uint64_t i_seek = static_cast<uint64_t>(i_blocks) * DVDCSS_BLOCK_SIZE;

  if (!dvdcss->stream_cb->pf_seek)
    return -1;

  if (dvdcss->i_pos == i_blocks) {
    /* We are already in position */
    return i_blocks;
  }

  if (dvdcss->stream_cb->pf_seek(dvdcss->p_stream, i_seek) != 0) {
    print_error(dvdcss, "seek error");
    dvdcss->i_pos = -1;
    return -1;
  }

  dvdcss->i_pos = i_blocks;

  return dvdcss->i_pos;
}

#if defined(_WIN32)
static int win2k_seek(dvdcss_t dvdcss, int i_blocks) {
  LARGE_INTEGER li_seek;

  if (dvdcss->i_pos == i_blocks) {
    /* We are already in position */
    return i_blocks;
  }

  li_seek.QuadPart = static_cast<LONGLONG>(i_blocks) * DVDCSS_BLOCK_SIZE;

  li_seek.LowPart =
      SetFilePointer(dvdcss_to_handle(dvdcss->i_fd), li_seek.LowPart,
                     &li_seek.HighPart, FILE_BEGIN);
  if ((li_seek.LowPart == INVALID_SET_FILE_POINTER) &&
      GetLastError() != NO_ERROR) {
    dvdcss->i_pos = -1;
    return -1;
  }

  dvdcss->i_pos = static_cast<int>(li_seek.QuadPart / DVDCSS_BLOCK_SIZE);

  return dvdcss->i_pos;
}
#endif /* defined( _WIN32 ) */

/*****************************************************************************
 * Read commands.
 *****************************************************************************/
static int libc_read(dvdcss_t dvdcss, void *p_buffer, int i_blocks) {
  const std::size_t requested_bytes =
      static_cast<std::size_t>(i_blocks) * DVDCSS_BLOCK_SIZE;
#ifdef _WIN32
  const ssize_t bytes_read =
      read(dvdcss->i_fd, p_buffer, static_cast<unsigned int>(requested_bytes));
#else
  const ssize_t bytes_read = read(dvdcss->i_fd, p_buffer, requested_bytes);
#endif

  if (bytes_read < 0) {
    print_error(dvdcss, "read error");
    dvdcss->i_pos = -1;
    return -1;
  }

  const int blocks_read = static_cast<int>(bytes_read / DVDCSS_BLOCK_SIZE);

  /* Handle partial reads */
  if (static_cast<std::size_t>(bytes_read) != requested_bytes) {
    int i_seek, i_set_pos;

    i_set_pos = dvdcss->i_pos + blocks_read;
    dvdcss->i_pos = -1;
    i_seek = libc_seek(dvdcss, i_set_pos);
    if (i_seek < 0) {
      return i_seek;
    }

    /* We have to return now so that i_pos isn't clobbered */
    return blocks_read;
  }

  dvdcss->i_pos += blocks_read;
  return blocks_read;
}

static int stream_read(dvdcss_t dvdcss, void *p_buffer, int i_blocks) {
  const int original_position = dvdcss->i_pos;
  const int requested_bytes = i_blocks * DVDCSS_BLOCK_SIZE;

  if (!dvdcss->stream_cb->pf_read)
    return -1;

  const int bytes_read =
      dvdcss->stream_cb->pf_read(dvdcss->p_stream, p_buffer, requested_bytes);

  if (bytes_read < 0) {
    print_error(dvdcss, "read error");
    dvdcss->i_pos = -1;
    return bytes_read;
  }
  if (bytes_read > requested_bytes) {
    print_error(dvdcss, "stream returned more data than requested");
    dvdcss->i_pos = -1;
    return -1;
  }

  const int blocks_read = bytes_read / DVDCSS_BLOCK_SIZE;

  /* Handle partial reads */
  if (bytes_read != requested_bytes) {
    int i_seek;

    dvdcss->i_pos = -1;
    i_seek = stream_seek(dvdcss, original_position + blocks_read);
    if (i_seek < 0) {
      return i_seek;
    }

    /* We have to return now so that i_pos isn't clobbered */
    return blocks_read;
  }

  dvdcss->i_pos += blocks_read;
  return blocks_read;
}

#if defined(_WIN32)
static int win2k_read(dvdcss_t dvdcss, void *p_buffer, int i_blocks) {
  const int original_position = dvdcss->i_pos;
  DWORD i_bytes;

  if (!ReadFile(dvdcss_to_handle(dvdcss->i_fd), p_buffer,
                i_blocks * DVDCSS_BLOCK_SIZE, &i_bytes, NULL)) {
    dvdcss->i_pos = -1;
    return -1;
  }

  const int blocks_read = static_cast<int>(i_bytes / DVDCSS_BLOCK_SIZE);
  if ((i_bytes % DVDCSS_BLOCK_SIZE) != 0) {
    dvdcss->i_pos = -1;
    if (win2k_seek(dvdcss, original_position + blocks_read) < 0) {
      return -1;
    }
  } else {
    dvdcss->i_pos += blocks_read;
  }
  return blocks_read;
}
#endif /* defined( _WIN32 ) */

/*****************************************************************************
 * Readv commands.
 *****************************************************************************/
static int libc_readv(dvdcss_t dvdcss, ScatterBuffers buffers) {
#if defined(_WIN32)
  const int original_position = dvdcss->i_pos;
  int i_total_bytes = 0;
  int i_bytes;

  for (auto buffer : buffers.entries()) {
    const int i_len = static_cast<int>(buffer.size());

    if (i_len <= 0) {
      continue;
    }

    i_bytes = read(dvdcss->i_fd, buffer.data(), i_len);

    if (i_bytes < 0) {
      /* One of the reads failed, too bad.
       * We won't even bother returning the reads that went OK,
       * and as in the POSIX spec the file position is left
       * unspecified after a failure */
      dvdcss->i_pos = -1;
      return -1;
    }

    i_total_bytes += i_bytes;

    if (i_bytes != i_len) {
      /* We reached the end of the file or a signal interrupted
       * the read. Return a partial read. */
      int i_seek;
      const int i_total_blocks = i_total_bytes / DVDCSS_BLOCK_SIZE;

      dvdcss->i_pos = -1;
      i_seek = libc_seek(dvdcss, original_position + i_total_blocks);
      if (i_seek < 0) {
        return i_seek;
      }

      /* We have to return now so that i_pos isn't clobbered */
      return i_total_blocks;
    }
  }

  const int i_total_blocks = i_total_bytes / DVDCSS_BLOCK_SIZE;

  dvdcss->i_pos += i_total_blocks;
  return i_total_blocks;
#else
  if (buffers.empty()) {
    return 0;
  }

  const int original_position = dvdcss->i_pos;
  const ssize_t bytes_read =
      readv(dvdcss->i_fd, buffers.raw_data(), static_cast<int>(buffers.size()));

  if (bytes_read < 0) {
    dvdcss->i_pos = -1;
    return -1;
  }

  const int blocks_read = static_cast<int>(bytes_read / DVDCSS_BLOCK_SIZE);
  if ((bytes_read % DVDCSS_BLOCK_SIZE) != 0) {
    dvdcss->i_pos = -1;
    if (libc_seek(dvdcss, original_position + blocks_read) < 0) {
      return -1;
    }
  } else {
    dvdcss->i_pos += blocks_read;
  }
  return blocks_read;
#endif
}

/*****************************************************************************
 * stream_readv: vectored read
 *****************************************************************************/
static int stream_readv(dvdcss_t dvdcss, ScatterBuffers buffers) {
  if (!dvdcss->stream_cb->pf_readv)
    return -1;

  const auto requested_size = buffers.byte_size();
  if (!requested_size ||
      *requested_size >
          static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    dvdcss->i_pos = -1;
    return -1;
  }

  const int original_position = dvdcss->i_pos;
  const int i_read = dvdcss->stream_cb->pf_readv(
      dvdcss->p_stream, buffers.raw_data(), static_cast<int>(buffers.size()));

  if (i_read < 0 || static_cast<std::size_t>(i_read) > *requested_size) {
    dvdcss->i_pos = -1;
    return -1;
  }

  const int blocks_read = i_read / DVDCSS_BLOCK_SIZE;
  if ((i_read % DVDCSS_BLOCK_SIZE) != 0) {
    dvdcss->i_pos = -1;
    if (stream_seek(dvdcss, original_position + blocks_read) < 0) {
      return -1;
    }
  } else {
    dvdcss->i_pos += blocks_read;
  }
  return blocks_read;
}

#if defined(_WIN32)
/*****************************************************************************
 * win2k_readv: vectored read using ReadFile for Win2K
 *****************************************************************************/
static int win2k_readv(dvdcss_t dvdcss, ScatterBuffers buffers) {
  const int original_position = dvdcss->i_pos;
  int i_blocks_read;
  int i_total_bytes = 0;
  DWORD i_bytes;
  size_t requested_size = 0;

  for (auto buffer : buffers.entries()) {
    requested_size += buffer.size();
    i_total_bytes += static_cast<int>(buffer.size());
  }

  /* Check the size of the readv temp buffer, just in case we need to
   * realloc something bigger */
  if (dvdcss->p_readv_buffer.size() < requested_size) {
    try {
      dvdcss->p_readv_buffer.resize(requested_size);
    } catch (const std::bad_alloc &) {
      print_error(dvdcss, "scatter input (readv) failed");
      dvdcss->i_pos = -1;
      return -1;
    }
  }

  if (i_total_bytes <= 0)
    return 0;

  if (!ReadFile(dvdcss_to_handle(dvdcss->i_fd), dvdcss->p_readv_buffer.data(),
                i_total_bytes, &i_bytes, NULL)) {
    /* The read failed... too bad.
     * As in the POSIX spec the file position is left
     * unspecified after a failure */
    dvdcss->i_pos = -1;
    return -1;
  }
  i_blocks_read = i_bytes / DVDCSS_BLOCK_SIZE;
  if ((i_bytes % DVDCSS_BLOCK_SIZE) != 0) {
    dvdcss->i_pos = -1;
    if (win2k_seek(dvdcss, original_position + i_blocks_read) < 0) {
      return -1;
    }
  }

  /* We just have to copy the content of the temp buffer into the iovecs */
  std::size_t bytes_remaining =
      static_cast<std::size_t>(i_blocks_read) * DVDCSS_BLOCK_SIZE;
  std::size_t source_offset = 0;
  for (auto buffer : buffers.entries()) {
    if (bytes_remaining == 0) {
      break;
    }
    if (buffer.empty()) {
      continue;
    }
    const std::size_t bytes_to_copy = std::min(buffer.size(), bytes_remaining);
    memcpy(buffer.data(), dvdcss->p_readv_buffer.data() + source_offset,
           bytes_to_copy);
    source_offset += bytes_to_copy;
    bytes_remaining -= bytes_to_copy;
  }

  if ((i_bytes % DVDCSS_BLOCK_SIZE) == 0) {
    dvdcss->i_pos += i_blocks_read;
  }
  return i_blocks_read;
}
#endif /* defined( _WIN32 ) */

} // namespace dvdcss
