/* libdvdcss.cpp: DVD reading library.
 *
 * Authors: Stéphane Borel <stef@via.ecp.fr>
 *          Sam Hocevar <sam@zoy.org>
 *          Håkan Hjort <d95hjort@dtek.chalmers.se>
 *
 * Copyright (C) 1998-2008 VideoLAN
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

/**
 * \mainpage libdvdcss developer documentation
 *
 * \section intro Introduction
 *
 * \e libdvdcss is a simple library designed for accessing DVDs like a block
 * device without having to bother about the decryption. The important features
 * are:
 * \li portability: Currently supported platforms are GNU/Linux, FreeBSD,
 *     NetBSD, OpenBSD, Haiku, Mac OS X, Solaris, QNX, OS/2, and Windows
 *     2000 or later.
 * \li adaptability: Unlike most similar projects, libdvdcss does not require
 *     the region of your drive to be set and will try its best to read from
 *     the disc even in the case of a region mismatch.
 * \li simplicity: A DVD player can be built around the \e libdvdcss API using
 *     no more than 4 or 5 library calls.
 *
 * \e libdvdcss is free software, released under the GNU General Public License.
 * This ensures that \e libdvdcss remains free and used only with free
 * software.
 *
 * \section api The libdvdcss API
 *
 * The complete \e libdvdcss programming interface is documented in the
 * dvdcss.h file.
 *
 * \section env Environment variables
 *
 * Some environment variables can be used to change the behavior of
 * \e libdvdcss without having to modify the program which uses it. These
 * variables are:
 *
 * \li \b DVDCSS_VERBOSE: Sets the verbosity level.
 *     - \c 0 outputs no messages at all.
 *     - \c 1 outputs error messages to stderr.
 *     - \c 2 outputs error messages and debug messages to stderr.
 *
 * \li \b DVDCSS_METHOD: Sets the authentication and decryption method
 *     that \e libdvdcss will use to read scrambled discs. Can be one
 *     of \c title, \c key or \c disc.
 *     - \c key is the default method. \e libdvdcss will use a set of
 *       calculated player keys to try and get the disc key. This can fail
 *       if the drive does not recognize any of the player keys.
 *     - \c disc is a fallback method when \c key has failed. Instead of
 *       using player keys, \e libdvdcss will crack the disc key using
 *       a brute force algorithm. This process is CPU intensive and requires
 *       64 MB of memory to store temporary data.
 *     - \c title is the fallback when all other methods have failed. It does
 *       not rely on a key exchange with the DVD drive, but rather uses a
 *       crypto attack to guess the title key. In rare cases this may fail
 *       because there is not enough encrypted data on the disc to perform
 *       a statistical attack, but on the other hand it is the only way to
 *       decrypt a DVD stored on a hard disc, or a DVD with the wrong region
 *       on an RPC2 drive.
 *
 * \li \b DVDCSS_RAW_DEVICE: Specify the raw device to use. Exact usage will
 *     depend on your operating system, the Linux utility to set up raw devices
 *     is \c raw(8) for instance. Please note that on most operating systems,
 *     using a raw device requires highly aligned buffers: Linux requires a
 *     2048 bytes alignment (which is the size of a DVD sector).
 *
 * \li \b DVDCSS_CACHE: Specify a directory in which to cache title key
 *     values. This will speed up descrambling of DVDs which are in the
 *     cache. The DVDCSS_CACHE directory is created if it does not exist,
 *     and a subdirectory is created named after the DVD's title or
 *     manufacturing date. If DVDCSS_CACHE is not set or is empty, \e libdvdcss
 *     will use the default value which is "${HOME}/.dvdcss/" under Unix and
 *     "C:\Documents and Settings\$USER\Application Data\dvdcss\" under Win32.
 *     The special value "off" disables caching.
 */

/*
 * Preamble
 */
#include "config.h"
#include "libdvdcpxm.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <span>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

#include "dvdcss/dvdcss.h"

#include "common.h"
#include "css.h"
#include "device.h"
#include "ioctl.h"
#include "libdvdcss.h"

using std::atoi;
using std::format;
using std::getenv;
using std::memcpy;
using std::strcmp;
using std::strlen;
using std::strncmp;

#if defined(HAVE_BROKEN_MKDIR) || defined(_WIN32)
#include <direct.h>
#define mkdir(a, b) _mkdir(a)
#endif

inline constexpr char kCacheTagName[] = "CACHEDIR.TAG";
inline constexpr int kStringKeySize = DVD_KEY_SIZE * 2;
inline constexpr int kInterestingSector = 16;
inline constexpr int kDiscTitleOffset = 40;
inline constexpr int kDiscTitleLength = 32;
inline constexpr int kManufacturingDateOffset = 813;
inline constexpr int kManufacturingDateLength = 16;

static std::string format_hex_bytes(std::span<const uint8_t> bytes) {
  std::string result;
  result.reserve(bytes.size() * 2);

  for (const uint8_t byte : bytes) {
    std::format_to(std::back_inserter(result), "{:02x}", byte);
  }

  return result;
}

static int create_directories_if_needed(const std::filesystem::path &path) {
  std::error_code error;

  if (path.empty()) {
    return -1;
  }

  if (std::filesystem::exists(path, error)) {
    return error ? -1 : 0;
  }

  std::filesystem::create_directories(path, error);
  return error ? -1 : 0;
}

static dvdcss_t dvdcss_open_common(const char *psz_target, void *p_stream,
                                   dvdcss_stream_cb *p_stream_cb);

dvdcss_s::~dvdcss_s() noexcept { static_cast<void>(cleanup()); }

int dvdcss_s::cleanup() noexcept {
  if (cleanup_done) {
    return cleanup_result;
  }

  cleanup_done = true;

  if (cpxm || cpxm_was_cached) {
    static_cast<void>(dvdcpxm_close_internal(this));
  }

  cleanup_result = dvdcss_close_device(this);
  return cleanup_result;
}

static void set_verbosity(dvdcss_t dvdcss) {
  const char *psz_verbose = getenv("DVDCSS_VERBOSE");

  dvdcss->b_debug = 0;
  dvdcss->b_errors = 0;

  if (psz_verbose != NULL) {
    int i = atoi(psz_verbose);

    if (i >= 2)
      dvdcss->b_debug = 1;
    if (i >= 1)
      dvdcss->b_errors = 1;
  }
}

static int set_access_method(dvdcss_t dvdcss) {
  const char *psz_method = getenv("DVDCSS_METHOD");

  if (!psz_method)
    return 0;

  if (!strncmp(psz_method, "key", 4)) {
    dvdcss->i_method = dvdcss_method::key;
  } else if (!strncmp(psz_method, "disc", 5)) {
    dvdcss->i_method = dvdcss_method::disc;
  } else if (!strncmp(psz_method, "title", 5)) {
    dvdcss->i_method = dvdcss_method::title;
  } else {
    print_error(dvdcss,
                "unknown decryption method %s, please choose "
                "from 'title', 'key' or 'disc'",
                psz_method);
    return -1;
  }
  return 0;
}

static int set_cache_directory(dvdcss_t dvdcss) {
  char *psz_cache = getenv("DVDCSS_CACHE");
  std::filesystem::path cache_directory;

  if (psz_cache && !strcmp(psz_cache, "off")) {
    return -1;
  }

  if (psz_cache == NULL || psz_cache[0] == '\0') {
#ifdef _WIN32
    char psz_home[PATH_MAX];

    /* Cache our keys in
     * C:\Documents and Settings\$USER\Application Data\dvdcss\ */
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL,
                         SHGFP_TYPE_CURRENT, psz_home) == S_OK) {
      cache_directory = std::filesystem::path(psz_home) / "dvdcss";
    }
#else
#ifdef __ANDROID__
    /* $HOME is not writable on __ANDROID__ so we have to create a custom
     * directory in userland */
    char *psz_home = "/sdcard/Android/data/org.videolan.dvdcss";

    int i_ret = create_directories_if_needed(psz_home);
    if (i_ret < 0 && errno != EEXIST) {
      print_error(dvdcss, "failed creating home directory");
      psz_home = NULL;
    }
#else
    char *psz_home = NULL;
#ifdef HAVE_PWD_H
    struct passwd *p_pwd;

    /* Try looking in password file for home dir. */
    p_pwd = getpwuid(getuid());
    if (p_pwd && p_pwd->pw_dir && p_pwd->pw_dir[0]) {
      psz_home = p_pwd->pw_dir;
    }
#endif /* HAVE_PWD_H */

#endif /* __ANDROID__ */

    if (psz_home == NULL) {
      psz_home = getenv("HOME");
    }

    /* Cache our keys in ${HOME}/.dvdcss/ */
    if (psz_home && psz_home[0]) {
#ifdef __OS2__
      if (*psz_home == '/' || *psz_home == '\\') {
        const char *psz_unixroot = getenv("UNIXROOT");

        if (psz_unixroot && psz_unixroot[0] && psz_unixroot[1] == ':' &&
            psz_unixroot[2] == '\0') {
          cache_directory = psz_unixroot;
        }
      }
#endif /* __OS2__ */

      if (cache_directory.empty()) {
        cache_directory = std::filesystem::path(psz_home) / ".dvdcss";
      } else {
        cache_directory += psz_home;
        cache_directory /= ".dvdcss";
      }
    }
#endif /* ! defined( _WIN32 ) */
  } else {
    cache_directory = psz_cache;
  }

  /* Check that there is enough space for the cache directory path and the
   * block filename. The +1s are path separators. */
  const std::string cache_directory_string = cache_directory.string();
  if (!cache_directory.empty() &&
      cache_directory_string.size() + 1 + kDiscTitleLength + 1 +
              kManufacturingDateLength + 1 + kStringKeySize + 1 +
              sizeof(kCacheTagName) >
          PATH_MAX) {
    print_error(dvdcss, "cache directory name is too long");
    return -1;
  }

  dvdcss->psz_cachefile = std::move(cache_directory);
  return 0;
}

static int init_cache_dir(dvdcss_t dvdcss) {
  static const char psz_tag[] =
      "Signature: 8a477f597d28d172789f06886806bc55\r\n"
      "# This file is a cache directory tag created by libdvdcss.\r\n"
      "# For information about cache directory tags, see:\r\n"
      "#   http://www.brynosaurus.com/cachedir/\r\n";
  int i_fd, i_ret;

  i_ret = create_directories_if_needed(dvdcss->psz_cachefile);
  if (i_ret < 0 && errno != EEXIST) {
    print_error(dvdcss, "failed creating cache directory '%s'",
                dvdcss->psz_cachefile.string().c_str());
    dvdcss->psz_cachefile.clear();
    return -1;
  }

  const auto tagfile = dvdcss->psz_cachefile / kCacheTagName;
  const auto tagfile_string = tagfile.string();
  i_fd = open(tagfile_string.c_str(), O_RDWR | O_CREAT, 0644);
  if (i_fd >= 0) {
    ssize_t len = strlen(psz_tag);
    if (write(i_fd, psz_tag, len) < len) {
      print_error(dvdcss, "Error writing cache directory tag, continuing..\n");
    }
    close(i_fd);
  }
  return 0;
}

static void create_cache_subdir(dvdcss_t dvdcss) {
  uint8_t p_sector[DVDCSS_BLOCK_SIZE];
  std::string cache_subdir;
  std::string serial_string;
  std::string key_string;
  char *psz_title;
  uint8_t *psz_serial;
  int i, i_ret;

  /* We read sector 0. If it starts with 0x000001ba (BE), we are
   * reading a VOB file, and we should not cache anything. */

  i_ret = dvdcss->device_strategy.seek(dvdcss, 0);
  if (i_ret != 0) {
    goto error;
  }

  i_ret = dvdcss->device_strategy.read(dvdcss, p_sector, 1);
  if (i_ret != 1) {
    goto error;
  }

  if (p_sector[0] == 0x00 && p_sector[1] == 0x00 && p_sector[2] == 0x01 &&
      p_sector[3] == 0xba) {
    goto error;
  }

  /* The data we are looking for is at sector 16 (32768 bytes):
   *  - offset 40: disc title (32 uppercase chars)
   *  - offset 813: manufacturing date + serial no (16 digits) */

  i_ret = dvdcss->device_strategy.seek(dvdcss, kInterestingSector);
  if (i_ret != kInterestingSector) {
    goto error;
  }

  i_ret = dvdcss->device_strategy.read(dvdcss, p_sector, 1);
  if (i_ret != 1) {
    goto error;
  }

  /* Get the disc title */
  psz_title = reinterpret_cast<char *>(p_sector) + kDiscTitleOffset;
  psz_title[kDiscTitleLength] = '\0';

  for (i = 0; i < kDiscTitleLength; i++) {
    if (psz_title[i] <= ' ') {
      psz_title[i] = '\0';
      break;
    } else if (psz_title[i] == '/' || psz_title[i] == '\\') {
      psz_title[i] = '-';
    }
  }

  /* Get the date + serial */
  psz_serial = p_sector + kManufacturingDateOffset;
  psz_serial[kManufacturingDateLength] = '\0';
  serial_string = reinterpret_cast<char *>(psz_serial);

  /* Check that all characters are digits, otherwise convert. */
  for (i = 0; i < kManufacturingDateLength; i++) {
    if (psz_serial[i] < '0' || psz_serial[i] > '9') {
      serial_string = format_hex_bytes(
          std::span<const uint8_t>{psz_serial, kManufacturingDateLength / 2});
      break;
    }
  }

  /* Get disk key, since some discs have the same title, manufacturing
   * date and serial number, but different keys. */
  if (dvdcss->b_scrambled) {
    key_string = format_hex_bytes(std::span{dvdcss->css.p_disc_key});
  }

  /* We have a disc name or ID, we can create the cache subdirectory. */
  cache_subdir = format("{}-{}-{}", psz_title, serial_string, key_string);
  dvdcss->psz_cachefile /= cache_subdir;

  i_ret = create_directories_if_needed(dvdcss->psz_cachefile);
  if (i_ret < 0 && errno != EEXIST) {
    print_error(dvdcss, "failed creating cache subdirectory");
    goto error;
  }

  print_debug(dvdcss, "Content Scrambling System (CSS) key cache dir: %s",
              dvdcss->psz_cachefile.string().c_str());
  return;

error:
  dvdcss->psz_cachefile.clear();
}

static void init_cache(dvdcss_t dvdcss) {
  /* Set CSS key cache directory. */
  int i_ret = set_cache_directory(dvdcss);
  if (i_ret < 0) {
    return;
  }

  /* If the cache is enabled, initialize the cache directory. */
  i_ret = init_cache_dir(dvdcss);
  if (i_ret < 0) {
    return;
  }

  /* If the cache is enabled, create a DVD-specific subdirectory. */
  create_cache_subdir(dvdcss);
}

/**
 * \brief Open a DVD device or directory and return a dvdcss instance.
 *
 * \param psz_target a string containing the target name, for instance
 *        "/dev/hdc" or "E:"
 * \return a handle to a dvdcss instance or NULL on error.
 *
 * Initialize the \e libdvdcss library, open the requested DVD device or
 * directory, and return a handle to be used for all subsequent \e libdvdcss
 * calls. \e libdvdcss checks whether ioctls can be performed on the disc,
 * and when possible, the disc key is retrieved.
 */
extern "C" dvdcss_t dvdcss_open(const char *psz_target) {
  return dvdcss_open_common(psz_target, NULL, NULL);
}

/**
 * \brief Open a DVD device using dvdcss_stream_cb.
 *
 * \param p_stream a private handle used by p_stream_cb
 * \param p_stream_cb a struct containing seek and read functions
 * \return a handle to a dvdcss instance or NULL on error.
 *
 * \see dvdcss_open()
 */
extern "C" dvdcss_t dvdcss_open_stream(void *p_stream,
                                       dvdcss_stream_cb *p_stream_cb) {
  return dvdcss_open_common(NULL, p_stream, p_stream_cb);
}

static dvdcss_t dvdcss_open_common(const char *psz_target, void *p_stream,
                                   dvdcss_stream_cb *p_stream_cb) {
  int i_ret;

  /* Allocate the library structure. */
  auto dvdcss_state = std::make_unique<dvdcss_s>();
  dvdcss_t dvdcss = dvdcss_state.get();
  if (dvdcss == NULL) {
    return NULL;
  }

  if (psz_target == NULL && (p_stream == NULL || p_stream_cb == NULL)) {
    goto error;
  }

  /* Initialize structure with default values. */
  dvdcss->i_fd.reset();
  dvdcss->i_pos = 0;
  dvdcss->psz_device = psz_target ? psz_target : "";
  dvdcss->psz_error = "no error";
  dvdcss->i_method = dvdcss_method::key;
  dvdcss->psz_cachefile.clear();
  dvdcss->device_strategy = {};

  dvdcss->p_stream = p_stream;
  dvdcss->p_stream_cb = p_stream_cb;

  dvdcss->cpxm = nullptr;
  dvdcss->cpxm_was_cached = 0;

  /* Set library verbosity from DVDCSS_VERBOSE environment variable. */
  set_verbosity(dvdcss);

  /* Set DVD access method from DVDCSS_METHOD environment variable. */
  if (set_access_method(dvdcss) < 0) {
    goto error;
  }

  /* Open device. */
  dvdcss_check_device(dvdcss);
  i_ret = dvdcss_open_device(dvdcss);
  if (i_ret < 0) {
    goto error;
  }

  dvdcss->b_scrambled = 1; /* Assume the worst */
  dvdcss->b_ioctls = dvdcss_use_ioctls(dvdcss);

  if (dvdcss->b_ioctls) {
    i_ret = dvdcss_test(dvdcss);

    if (i_ret == -3) {
      print_debug(dvdcss, "scrambled disc on a region-free RPC-II "
                          "drive: possible failure, but continuing "
                          "anyway");
    } else if (i_ret < 0) {
      /* Disable the CSS ioctls and hope that it works? */
      print_debug(dvdcss, "could not check whether the disc was scrambled");
      dvdcss->b_ioctls = 0;
    } else {
      print_debug(dvdcss, i_ret ? "disc is scrambled" : "disc is unscrambled");
      dvdcss->b_scrambled = i_ret;
    }
  }

  dvdcss->css.p_disc_key.fill(0);
  /* If disc is CSS protected and the ioctls work, authenticate the drive */
  if (dvdcss->b_scrambled && dvdcss->b_ioctls) {
    i_ret = dvdcss_disckey(dvdcss);

    if (i_ret < 0) {
      print_debug(dvdcss, "could not get disc key");
    }
  }

  init_cache(dvdcss);

  /* Seek to the beginning, just for safety. */
  dvdcss->device_strategy.seek(dvdcss, 0);

  return dvdcss_state.release();

error:
  return NULL;
}

/**
 * \brief Return a string containing the last error that occurred in the
 *        given \e libdvdcss instance.
 *
 * \param dvdcss a \e libdvdcss instance
 * \return a NULL-terminated string containing the last error message.
 *
 * Return a string with the last error message produced by \e libdvdcss.
 * Useful to conveniently format error messages in external applications.
 */
extern "C" const char *dvdcss_error(const dvdcss_t dvdcss) {
  return dvdcss->psz_error.c_str();
}

/**
 * \brief Seek in the disc and change the current key if requested.
 *
 * \param dvdcss a \e libdvdcss instance
 * \param i_blocks an absolute block offset to seek to
 * \param i_flags #DVDCSS_NOFLAGS, optionally ORed with one of #DVDCSS_SEEK_KEY
 *        or #DVDCSS_SEEK_MPEG
 * \return the new position in blocks or a negative value in case an error
 *         happened.
 *
 * This function seeks to the requested position, in logical blocks.
 *
 * You typically set \p i_flags to #DVDCSS_NOFLAGS when seeking in a .IFO.
 *
 * If #DVDCSS_SEEK_MPEG is specified in \p i_flags and if \e libdvdcss finds it
 * reasonable to do so (i.e., if the dvdcss method is not "title"), the current
 * title key will be checked and a new one will be calculated if necessary.
 * This flag is typically used when reading data from a .VOB file.
 *
 * If #DVDCSS_SEEK_KEY is specified, the title key will always be checked,
 * even with the "title" method. This flag is typically used when seeking
 * in a new title.
 */
extern "C" int dvdcss_seek(dvdcss_t dvdcss, int i_blocks, int i_flags) {
  /* title cracking method is too slow to be used at each seek */
  if (((i_flags & DVDCSS_SEEK_MPEG) &&
       (dvdcss->i_method != dvdcss_method::title)) ||
      (i_flags & DVDCSS_SEEK_KEY)) {
    /* check the title key */
    if (dvdcss_title(dvdcss, i_blocks)) {
      return -1;
    }
  }

  return dvdcss->device_strategy.seek(dvdcss, i_blocks);
}

/**
 * \brief Read from the disc and decrypt data if requested.
 *
 * \param dvdcss a \e libdvdcss instance
 * \param p_buffer a buffer that will contain the data read from the disc
 * \param i_blocks the amount of blocks to read
 * \param i_flags #DVDCSS_NOFLAGS, optionally ORed with #DVDCSS_READ_DECRYPT
 * \return the amount of blocks read or a negative value in case an
 *         error happened.
 *
 * Read \p i_blocks logical blocks from the DVD.
 *
 * You typically set \p i_flags to #DVDCSS_NOFLAGS when reading data from a
 * .IFO file on the DVD.
 *
 * If #DVDCSS_READ_DECRYPT is specified in \p i_flags, dvdcss_read() will
 * automatically decrypt scrambled sectors. This flag is typically used when
 * reading data from a .VOB file on the DVD. It has no effect on unscrambled
 * discs or unscrambled sectors and can be safely used on those.
 *
 * \warning dvdcss_read() expects to be able to write \p i_blocks *
 *          #DVDCSS_BLOCK_SIZE bytes into \p p_buffer.
 */
extern "C" int dvdcss_read(dvdcss_t dvdcss, void *p_buffer, int i_blocks,
                           int i_flags) {
  uint8_t *_p_buffer = static_cast<uint8_t *>(p_buffer);
  int i_ret, i_index;

  i_ret = dvdcss->device_strategy.read(dvdcss, _p_buffer, i_blocks);

  if (i_ret <= 0 || !dvdcss->b_scrambled || !(i_flags & DVDCSS_READ_DECRYPT)) {
    return i_ret;
  }

  if (dvdcss->css.p_title_key == dvdcss_key{}) {
    /* For what we believe is an unencrypted title,
     * check that there are no encrypted blocks */
    for (i_index = i_ret; i_index; i_index--) {
      if (_p_buffer[0x14] & 0x30) {
        print_error(dvdcss, "no key but found encrypted block");
        /* Only return the initial range of unscrambled blocks? */
        /* or fail completely? return 0; */
        break;
      }
      _p_buffer = _p_buffer + DVDCSS_BLOCK_SIZE;
    }
  } else {
    /* Decrypt the blocks we managed to read */
    for (i_index = i_ret; i_index; i_index--) {
      dvdcss_unscramble(dvdcss->css.p_title_key,
                        std::span<uint8_t>{
                            _p_buffer, static_cast<size_t>(DVDCSS_BLOCK_SIZE)});
      _p_buffer[0x14] &= 0x8f;
      _p_buffer = _p_buffer + DVDCSS_BLOCK_SIZE;
    }
  }

  return i_ret;
}

/**
 * \brief Read data from the disc into multiple buffers and decrypt data if
 *        requested.
 *
 * \param dvdcss a \e libdvdcss instance
 * \param p_iovec a pointer to an array of iovec structures that will contain
 *        the data read from the disc
 * \param i_blocks the amount of blocks to read
 * \param i_flags #DVDCSS_NOFLAGS, optionally ORed with #DVDCSS_READ_DECRYPT
 * \return the amount of blocks read or a negative value in case an
 *         error happened.
 *
 * Read \p i_blocks logical blocks from the DVD and write them
 * to an array of iovec structures.
 *
 * You typically set \p i_flags to #DVDCSS_NOFLAGS when reading data from a
 * .IFO file on the DVD.
 *
 * If #DVDCSS_READ_DECRYPT is specified in \p i_flags, dvdcss_readv() will
 * automatically decrypt scrambled sectors. This flag is typically used when
 * reading data from a .VOB file on the DVD. It has no effect on unscrambled
 * discs or unscrambled sectors and can be safely used on those.
 *
 * \warning dvdcss_readv() expects to be able to write \p i_blocks *
 *          #DVDCSS_BLOCK_SIZE bytes into the buffers pointed by \p p_iovec.
 *          Moreover, all iov_len members of the iovec structures should be
 *          multiples of #DVDCSS_BLOCK_SIZE.
 */
extern "C" int dvdcss_readv(dvdcss_t dvdcss, void *p_iovec, int i_blocks,
                            int i_flags) {
  struct iovec *_p_iovec = static_cast<struct iovec *>(p_iovec);
  auto buffers = make_scatter_buffers(_p_iovec, i_blocks);
  int i_ret;

  i_ret = dvdcss->device_strategy.readv(dvdcss, buffers);

  if (i_ret <= 0 || !dvdcss->b_scrambled || !(i_flags & DVDCSS_READ_DECRYPT)) {
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
      dvdcss_unscramble(dvdcss->css.p_title_key, sector);
      sector[0x14] &= 0x8f;
    }
  }

  return i_ret;
}

/**
 * \brief Clean up library state and structures.
 *
 * \param dvdcss a \e libdvdcss instance
 * \return zero in case of success, a negative value otherwise.
 *
 * Close the DVD device and free all the memory allocated by \e libdvdcss.
 * On return, the #dvdcss_t is invalidated and may not be used again.
 */
extern "C" int dvdcss_close(dvdcss_t dvdcss) {
  const int i_ret = dvdcss->cleanup();
  delete dvdcss;

  return i_ret;
}

/**
 * \brief Detect whether or not a DVD is scrambled
 *
 * \param dvdcss a \e libdvdcss instance.
 * \return 1 if the DVD is scrambled, 0 otherwise.
 */
extern "C" int dvdcss_is_scrambled(dvdcss_t dvdcss) {
  return dvdcss->b_scrambled;
}

/**
 * \brief Return the type of encryption
 *
 * \return 0 if the disc is unencrypted, 1 if cppm or css, 2 if cprm
 */
extern "C" int dvdcss_get_encryption_type(dvdcss_t dvdcss) {
  return dvdcss->media_type;
}
