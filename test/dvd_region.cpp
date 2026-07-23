/*
 * Inspect or set the RPC region on a Linux DVD drive.
 *
 * Setting a drive region consumes one of the drive's limited region changes.
 * This diagnostic therefore requires an explicit interactive confirmation.
 */

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <getopt.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr std::string_view kDefaultDevice = "/dev/dvd";

class FileDescriptor {
public:
  explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
  ~FileDescriptor() noexcept {
    if (fd_ >= 0) {
      static_cast<void>(close(fd_));
    }
  }

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

private:
  int fd_;
};

[[nodiscard]] int send_rpc(int fd, int region_mask) {
#if defined(DVD_HOST_SEND_RPC_STATE)
  dvd_authinfo authentication{};
  authentication.type = DVD_HOST_SEND_RPC_STATE;
  authentication.hrpcs.pdrc = region_mask;
  return ioctl(fd, DVD_AUTH, &authentication);
#else
  static_cast<void>(fd);
  static_cast<void>(region_mask);
  errno = ENOTSUP;
  return -1;
#endif
}

[[nodiscard]] int read_copyright(int fd, int *copyright) {
  dvd_struct descriptor{};
  descriptor.type = DVD_STRUCT_COPYRIGHT;
  descriptor.copyright.layer_num = 0;

  const int result = ioctl(fd, DVD_READ_STRUCT, &descriptor);
  if (result == 0) {
    *copyright = descriptor.copyright.cpst;
  }
  return result;
}

[[nodiscard]] int report_rpc(int fd, int *type, int *region_mask, int *scheme) {
#if defined(DVD_LU_SEND_RPC_STATE)
  dvd_authinfo authentication{};
  authentication.type = DVD_LU_SEND_RPC_STATE;

  const int result = ioctl(fd, DVD_AUTH, &authentication);
  if (result == 0) {
    *type = authentication.lrpcs.type;
    *region_mask = authentication.lrpcs.region_mask;
    *scheme = authentication.lrpcs.rpc_scheme;
  }
  return result;
#else
  static_cast<void>(fd);
  static_cast<void>(type);
  static_cast<void>(region_mask);
  static_cast<void>(scheme);
  errno = ENOTSUP;
  return -1;
#endif
}

[[nodiscard]] int parse_region(std::string_view text) {
  int region = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), region);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      region < 1 || region > 8) {
    return -1;
  }
  return region;
}

void print_usage(const char *program) {
  std::fprintf(stderr, "Usage: %s [-d device] [-s -r region]\n", program);
}

void print_region(int type, int region_mask, int scheme) {
  std::printf("Drive region type: %d\n", type);
  std::printf("Playable regions:");
  for (int region = 1; region <= 8; ++region) {
    if ((region_mask & (1 << (region - 1))) == 0) {
      std::printf(" %d", region);
    }
  }
  std::printf("\nRPC scheme: %d\n", scheme);
}

} // namespace

int main(int argc, char *argv[]) {
  const char *device = kDefaultDevice.data();
  bool set_region_requested = false;
  int requested_region = 0;

  for (int option; (option = getopt(argc, argv, "d:sr:h")) != -1;) {
    switch (option) {
    case 'd':
      device = optarg;
      break;
    case 's':
      set_region_requested = true;
      break;
    case 'r':
      requested_region = parse_region(optarg);
      if (requested_region < 0) {
        std::fprintf(stderr, "Region must be an integer from 1 through 8.\n");
        return 2;
      }
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 2;
    }
  }

  if (optind != argc || (set_region_requested && requested_region == 0)) {
    print_usage(argv[0]);
    return 2;
  }

  const FileDescriptor fd{open(device, O_RDONLY | O_NONBLOCK)};
  if (!fd) {
    std::fprintf(stderr, "Cannot open %s: %s\n", device, std::strerror(errno));
    return 1;
  }

  int copyright = 0;
  if (read_copyright(fd.get(), &copyright) == 0) {
    std::printf("Copyright protection: %d\n", copyright);
  }

  int type = 0;
  int region_mask = 0;
  int scheme = 0;
  if (report_rpc(fd.get(), &type, &region_mask, &scheme) < 0) {
    std::fprintf(stderr, "Cannot read drive region: %s\n",
                 std::strerror(errno));
    return 1;
  }
  print_region(type, region_mask, scheme);

  if (!set_region_requested) {
    return 0;
  }

  std::printf("Setting the drive region consumes one of its limited changes.\n"
              "Type YES to set region %d: ",
              requested_region);
  char confirmation[4]{};
  if (std::fgets(confirmation, sizeof(confirmation), stdin) == nullptr ||
      std::string_view{confirmation} != "YES") {
    std::printf("Cancelled.\n");
    return 0;
  }

  const int region_mask_to_set = 0xff & ~(1 << (requested_region - 1));
  if (send_rpc(fd.get(), region_mask_to_set) < 0) {
    std::fprintf(stderr, "Cannot set drive region: %s\n", std::strerror(errno));
    return 1;
  }

  return 0;
}
