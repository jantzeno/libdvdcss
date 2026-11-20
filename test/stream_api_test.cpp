#include <dvdcss/dvdcpxm.h>
#include <dvdcss/dvdcss.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct FakeStream {
  std::vector<std::uint8_t> bytes;
  std::uint64_t position = 0;
  int read_limit = -1;
  int readv_limit = -1;
  bool overreport_read = false;
};

struct TestIovec {
  void *base;
  std::size_t length;
};

FakeStream *null_opaque_stream = nullptr;

FakeStream &resolve_stream(void *opaque) {
  return opaque != nullptr ? *static_cast<FakeStream *>(opaque)
                           : *null_opaque_stream;
}

int seek_callback(void *opaque, std::uint64_t position) {
  auto &stream = resolve_stream(opaque);
  if (position > stream.bytes.size()) {
    return -1;
  }
  stream.position = position;
  return 0;
}

int read_callback(void *opaque, void *buffer, int byte_count) {
  auto &stream = resolve_stream(opaque);
  if (byte_count < 0 || (byte_count > 0 && buffer == nullptr)) {
    return -1;
  }

  const auto available =
      stream.bytes.size() - static_cast<std::size_t>(stream.position);
  if (stream.overreport_read) {
    return byte_count + 1;
  }

  const auto requested = static_cast<std::size_t>(
      stream.read_limit >= 0 ? std::min(byte_count, stream.read_limit)
                             : byte_count);
  const auto transferred = std::min(available, requested);
  std::memcpy(buffer, stream.bytes.data() + stream.position, transferred);
  stream.position += transferred;
  return static_cast<int>(transferred);
}

int readv_callback(void *opaque, const void *raw_iovecs, int count) {
  if (count < 0 || (count > 0 && raw_iovecs == nullptr)) {
    return -1;
  }

  auto &stream = resolve_stream(opaque);
  const auto *iovecs = static_cast<const TestIovec *>(raw_iovecs);
  int total = 0;
  int remaining = stream.readv_limit;
  for (int index = 0; index < count; ++index) {
    int requested = static_cast<int>(iovecs[index].length);
    if (remaining >= 0) {
      requested = std::min(requested, remaining);
    }

    const int saved_limit = stream.read_limit;
    stream.read_limit = -1;
    const int transferred =
        read_callback(opaque, iovecs[index].base, requested);
    stream.read_limit = saved_limit;
    if (transferred < 0) {
      return -1;
    }
    total += transferred;
    if (remaining >= 0) {
      remaining -= transferred;
    }
    if (transferred != static_cast<int>(iovecs[index].length) ||
        remaining == 0) {
      break;
    }
  }
  return total;
}

} // namespace

int main() {
  FakeStream stream;
  stream.bytes.resize(24 * DVDCSS_BLOCK_SIZE);
  for (std::size_t index = 0; index < stream.bytes.size(); ++index) {
    stream.bytes[index] = static_cast<std::uint8_t>(index & 0xff);
  }

  dvdcss_stream_cb callbacks{
      .pf_seek = seek_callback,
      .pf_read = read_callback,
      .pf_readv = readv_callback,
  };

  null_opaque_stream = &stream;
  dvdcss_t handle = dvdcss_open_stream(nullptr, &callbacks);
  if (handle == nullptr) {
    return 1;
  }

  if (dvdcss_seek(handle, 2, DVDCSS_NOFLAGS) != 2) {
    return 2;
  }

  std::array<std::uint8_t, DVDCSS_BLOCK_SIZE> sector{};
  callbacks.pf_read = [](void *, void *, int) { return -1; };
  if (dvdcss_read(handle, sector.data(), 1, DVDCSS_NOFLAGS) != -1) {
    return 22;
  }
  callbacks.pf_read = nullptr;
  if (dvdcss_read(handle, sector.data(), 1, DVDCSS_NOFLAGS) != -1) {
    return 23;
  }
  callbacks.pf_read = read_callback;
  if (dvdcss_seek(handle, 2, DVDCSS_NOFLAGS) != 2) {
    return 24;
  }
  if (dvdcss_read(handle, sector.data(), 1, DVDCSS_NOFLAGS) != 1) {
    return 3;
  }
  if (!std::equal(sector.begin(), sector.end(),
                  stream.bytes.begin() + 2 * DVDCSS_BLOCK_SIZE)) {
    return 4;
  }

  if (dvdcss_seek(handle, 4, DVDCSS_NOFLAGS) != 4) {
    return 5;
  }

  std::array<std::uint8_t, DVDCSS_BLOCK_SIZE> first{};
  std::array<std::uint8_t, DVDCSS_BLOCK_SIZE> second{};
  std::array<TestIovec, 2> iovecs{{
      {first.data(), first.size()},
      {second.data(), second.size()},
  }};

  if (dvdcss_readv(handle, iovecs.data(), static_cast<int>(iovecs.size()),
                   DVDCSS_NOFLAGS) != 2) {
    return 6;
  }
  if (!std::equal(first.begin(), first.end(),
                  stream.bytes.begin() + 4 * DVDCSS_BLOCK_SIZE) ||
      !std::equal(second.begin(), second.end(),
                  stream.bytes.begin() + 5 * DVDCSS_BLOCK_SIZE)) {
    return 7;
  }

  if (dvdcss_read(handle, nullptr, 1, DVDCSS_NOFLAGS) != -1 ||
      dvdcss_readv(handle, nullptr, 1, DVDCSS_NOFLAGS) != -1 ||
      dvdcss_seek(handle, -1, DVDCSS_NOFLAGS) != -1) {
    return 8;
  }

  TestIovec invalid_iovec{nullptr, 1};
  if (dvdcss_readv(handle, &invalid_iovec, 1, DVDCSS_NOFLAGS) != -1) {
    return 9;
  }

  const auto position_before_invalid_read = stream.position;
  std::array<TestIovec, 2> unaligned_iovecs{{
      {first.data(), first.size() / 2},
      {second.data(), second.size() / 2},
  }};
  const auto unaligned_count = static_cast<int>(unaligned_iovecs.size());
  if (dvdcss_readv(handle, unaligned_iovecs.data(), unaligned_count,
                   DVDCSS_NOFLAGS) != -1 ||
      dvdcpxm_readv(handle, unaligned_iovecs.data(), unaligned_count,
                    DVDCSS_NOFLAGS) != -1 ||
      stream.position != position_before_invalid_read) {
    return 18;
  }

  if (dvdcss_seek(handle, 8, DVDCSS_NOFLAGS) != 8) {
    return 10;
  }
  stream.read_limit = DVDCSS_BLOCK_SIZE + 7;
  std::array<std::uint8_t, 2 * DVDCSS_BLOCK_SIZE> partial{};
  if (dvdcss_read(handle, partial.data(), 2, DVDCSS_NOFLAGS) != 1 ||
      stream.position != 9 * DVDCSS_BLOCK_SIZE) {
    return 11;
  }
  stream.read_limit = -1;

  if (dvdcss_seek(handle, 10, DVDCSS_NOFLAGS) != 10) {
    return 12;
  }
  stream.readv_limit = DVDCSS_BLOCK_SIZE + 13;
  if (dvdcss_readv(handle, iovecs.data(), static_cast<int>(iovecs.size()),
                   DVDCSS_NOFLAGS) != 1 ||
      stream.position != 11 * DVDCSS_BLOCK_SIZE) {
    return 13;
  }
  stream.readv_limit = -1;

  stream.overreport_read = true;
  if (dvdcss_read(handle, sector.data(), 1, DVDCSS_NOFLAGS) != -1) {
    return 14;
  }
  stream.overreport_read = false;

  if (dvdcpxm_seek(handle, 12, DVDCSS_NOFLAGS) != 12) {
    return 19;
  }
  const auto uninitialized_position = stream.position;
  const auto raw_begin = stream.bytes.begin() + uninitialized_position;
  const auto iovec_count = static_cast<int>(iovecs.size());
  if (dvdcpxm_read(handle, sector.data(), 1, DVDCSS_READ_DECRYPT) != 1 ||
      dvdcpxm_readv(handle, iovecs.data(), iovec_count, DVDCSS_READ_DECRYPT) != 2 ||
      stream.position != uninitialized_position + 3 * DVDCSS_BLOCK_SIZE ||
      !std::equal(sector.begin(), sector.end(), raw_begin) ||
      !std::equal(first.begin(), first.end(),
                  raw_begin + DVDCSS_BLOCK_SIZE) ||
      !std::equal(second.begin(), second.end(),
                  raw_begin + 2 * DVDCSS_BLOCK_SIZE)) {
    return 20;
  }

  std::uint8_t unprotected_media_marker = 0;
  if (dvdcpxm_init(handle, &unprotected_media_marker) != DVDCPXM_MEDIA_UNPR ||
      dvdcpxm_init(handle, &unprotected_media_marker) != DVDCPXM_MEDIA_UNPR) {
    return 15;
  }

  if (dvdcpxm_seek(handle, 12, DVDCSS_NOFLAGS) != 12 ||
      dvdcpxm_read(handle, sector.data(), 1, DVDCSS_READ_DECRYPT) != 1) {
    return 16;
  }

  if (dvdcpxm_close(handle) != 0) {
    return 17;
  }

  return 0;
}
