#include "core/exif.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace core {
namespace {

// Bounds-checked sequential writer; any overflow poisons the whole write.
class ByteWriter {
 public:
  explicit ByteWriter(std::span<uint8_t> out) : out_(out) {}

  void u8(uint8_t v) {
    if (pos_ < out_.size()) out_[pos_] = v;
    else overflow_ = true;
    ++pos_;
  }
  void be16(uint16_t v) { u8(v >> 8); u8(v & 0xFF); }
  void le16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
  void le32(uint32_t v) { le16(v & 0xFFFF); le16(v >> 16); }
  void bytes(std::span<const uint8_t> data) {
    if (pos_ + data.size() > out_.size()) {
      overflow_ = true;
    } else {
      std::memcpy(out_.data() + pos_, data.data(), data.size());
    }
    pos_ += data.size();
  }
  void patch_be16(size_t at, uint16_t v) {
    if (at + 1 < out_.size()) {
      out_[at] = v >> 8;
      out_[at + 1] = v & 0xFF;
    }
  }
  size_t pos() const { return pos_; }
  bool overflow() const { return overflow_; }

 private:
  std::span<uint8_t> out_;
  size_t pos_ = 0;
  bool overflow_ = false;
};

constexpr uint16_t kTypeAscii = 2;
constexpr uint16_t kTypeShort = 3;

struct Entry {
  uint16_t tag;
  uint16_t type;
  std::string_view text;  // ASCII entries (written with a trailing NUL)
  uint16_t value = 0;     // SHORT entries
};

std::span<const uint8_t> as_bytes(std::string_view s) {
  return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

}  // namespace

uint16_t exif_orientation(Rotation rotation) {
  switch (rotation) {
    case Rotation::Deg0: return 1;
    case Rotation::Deg90: return 6;
    case Rotation::Deg180: return 3;
    case Rotation::Deg270: return 8;
  }
  return 1;
}

Result<size_t> write_jpeg_with_exif(std::span<const uint8_t> jpeg, const ExifInfo& info,
                                    std::span<uint8_t> out) {
  if (jpeg.size() < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
    return fail(Errc::InvalidArgument);
  }

  // Skip existing APP0 (JFIF) / APP1 (EXIF) segments right after SOI.
  size_t body = 2;
  while (body + 4 <= jpeg.size() && jpeg[body] == 0xFF &&
         (jpeg[body + 1] == 0xE0 || jpeg[body + 1] == 0xE1)) {
    const size_t segment = (jpeg[body + 2] << 8) | jpeg[body + 3];
    if (segment < 2 || body + 2 + segment > jpeg.size()) return fail(Errc::InvalidArgument);
    body += 2 + segment;
  }

  // EXIF DateTime is exactly "YYYY:MM:DD HH:MM:SS"; out-of-range values are dropped.
  char datetime[32] = {};
  std::optional<CivilTime> taken_at = info.taken_at;
  if (taken_at) {
    const CivilTime& t = *taken_at;
    const bool valid = t.year <= 9999 && t.month >= 1 && t.month <= 12 && t.day >= 1 &&
                       t.day <= 31 && t.hour <= 23 && t.minute <= 59 && t.second <= 60;
    if (valid) {
      std::snprintf(datetime, sizeof datetime, "%04u:%02u:%02u %02u:%02u:%02u", t.year, t.month,
                    t.day, t.hour, t.minute, t.second);
    } else {
      taken_at.reset();
    }
  }

  std::array<Entry, 5> entries{};
  size_t count = 0;
  entries[count++] = {0x010F, kTypeAscii, info.make};
  entries[count++] = {0x0110, kTypeAscii, info.model};
  entries[count++] = {0x0112, kTypeShort, {}, exif_orientation(info.rotation)};
  entries[count++] = {0x0131, kTypeAscii, info.software};
  if (taken_at) entries[count++] = {0x0132, kTypeAscii, std::string_view(datetime, 19)};

  ByteWriter w(out);
  w.be16(0xFFD8);
  w.be16(0xFFE1);
  const size_t length_at = w.pos();
  w.be16(0);  // APP1 length, patched below
  w.bytes(as_bytes(std::string_view("Exif\0\0", 6)));

  const size_t tiff = w.pos();
  w.u8('I');
  w.u8('I');
  w.le16(42);
  w.le32(8);  // IFD0 right after the TIFF header

  // Offsets inside the TIFF block: header (8) + IFD (2 + 12n + 4) then string data.
  uint32_t data_offset = 8 + 2 + 12 * count + 4;
  w.le16(static_cast<uint16_t>(count));
  for (size_t i = 0; i < count; ++i) {
    const Entry& e = entries[i];
    w.le16(e.tag);
    w.le16(e.type);
    if (e.type == kTypeShort) {
      w.le32(1);
      w.le16(e.value);
      w.le16(0);
      continue;
    }
    const uint32_t n = static_cast<uint32_t>(e.text.size()) + 1;  // with NUL
    w.le32(n);
    if (n <= 4) {
      std::array<uint8_t, 4> inline_value{};
      std::memcpy(inline_value.data(), e.text.data(), e.text.size());
      w.bytes(inline_value);
    } else {
      w.le32(data_offset);
      data_offset += n + (n & 1);  // keep word alignment
    }
  }
  w.le32(0);  // no IFD1

  for (size_t i = 0; i < count; ++i) {
    const Entry& e = entries[i];
    const size_t n = e.text.size() + 1;
    if (e.type != kTypeAscii || n <= 4) continue;
    w.bytes(as_bytes(e.text));
    w.u8(0);
    if (n & 1) w.u8(0);
  }

  const size_t app1_length = w.pos() - length_at;
  if (app1_length > 0xFFFF) return fail(Errc::OutOfRange);
  w.patch_be16(length_at, static_cast<uint16_t>(app1_length));
  (void)tiff;

  w.bytes(jpeg.subspan(body));
  if (w.overflow()) return fail(Errc::NoMemory, static_cast<int32_t>(w.pos()));
  return w.pos();
}

}  // namespace core
