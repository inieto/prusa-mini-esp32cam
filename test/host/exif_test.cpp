#include "core/exif.hpp"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

using core::ExifInfo;
using core::Rotation;

namespace {

// Minimal EXIF reader (IFD0 only) to verify what the writer produced.
struct Parsed {
  std::map<uint16_t, std::string> ascii;
  std::map<uint16_t, uint16_t> shorts;
  size_t body_offset = 0;  // first byte after APP1
};

uint16_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t le32(const uint8_t* p) { return le16(p) | (le16(p + 2) << 16); }

Parsed parse(const std::vector<uint8_t>& jpeg) {
  Parsed out;
  EXPECT_EQ(jpeg[0], 0xFF);
  EXPECT_EQ(jpeg[1], 0xD8);
  EXPECT_EQ(jpeg[2], 0xFF);
  EXPECT_EQ(jpeg[3], 0xE1);
  const size_t len = (jpeg[4] << 8) | jpeg[5];
  EXPECT_EQ(std::string(jpeg.begin() + 6, jpeg.begin() + 12), std::string("Exif\0\0", 6));
  const uint8_t* tiff = jpeg.data() + 12;
  EXPECT_EQ(tiff[0], 'I');
  EXPECT_EQ(le16(tiff + 2), 42);
  const uint8_t* ifd = tiff + le32(tiff + 4);
  const uint16_t count = le16(ifd);
  uint16_t previous = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t* e = ifd + 2 + 12 * i;
    const uint16_t tag = le16(e), type = le16(e + 2);
    const uint32_t n = le32(e + 4);
    EXPECT_GT(tag, previous) << "tags must be sorted";
    previous = tag;
    if (type == 3) {
      out.shorts[tag] = le16(e + 8);
    } else {
      const uint8_t* data = n <= 4 ? e + 8 : tiff + le32(e + 8);
      EXPECT_EQ(data[n - 1], 0) << "ASCII must be NUL-terminated";
      out.ascii[tag] = std::string(reinterpret_cast<const char*>(data), n - 1);
    }
  }
  out.body_offset = 4 + len;
  return out;
}

const std::vector<uint8_t> kBody = {0xFF, 0xDB, 0x00, 0x03, 0x01, 0xFF, 0xD9};

}  // namespace

TEST(Exif, OrientationValues) {
  EXPECT_EQ(core::exif_orientation(Rotation::Deg0), 1);
  EXPECT_EQ(core::exif_orientation(Rotation::Deg90), 6);
  EXPECT_EQ(core::exif_orientation(Rotation::Deg180), 3);
  EXPECT_EQ(core::exif_orientation(Rotation::Deg270), 8);
}

TEST(Exif, ReplacesJfifAndKeepsImageData) {
  std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 'J', 'F'};
  jpeg.insert(jpeg.end(), kBody.begin(), kBody.end());

  std::vector<uint8_t> out(512);
  ExifInfo info{.rotation = Rotation::Deg90,
                .taken_at = core::CivilTime{2026, 9, 27, 13, 5, 9}};
  auto written = core::write_jpeg_with_exif(jpeg, info, out);
  ASSERT_TRUE(written.has_value());
  out.resize(*written);

  Parsed p = parse(out);
  EXPECT_EQ(p.shorts[0x0112], 6);
  EXPECT_EQ(p.ascii[0x010F], "OmniVision");
  EXPECT_EQ(p.ascii[0x0110], "OV2640");
  EXPECT_EQ(p.ascii[0x0132], "2026:09:27 13:05:09");
  EXPECT_EQ(std::vector<uint8_t>(out.begin() + p.body_offset, out.end()), kBody);
}

TEST(Exif, OmitsDateTimeWhenClockNotSynced) {
  std::vector<uint8_t> jpeg = {0xFF, 0xD8};
  jpeg.insert(jpeg.end(), kBody.begin(), kBody.end());
  std::vector<uint8_t> out(512);
  auto written = core::write_jpeg_with_exif(jpeg, {}, out);
  ASSERT_TRUE(written.has_value());
  out.resize(*written);
  Parsed p = parse(out);
  EXPECT_EQ(p.ascii.count(0x0132), 0u);
  EXPECT_EQ(p.shorts[0x0112], 1);
}

TEST(Exif, RejectsNonJpegAndSmallOutput) {
  std::vector<uint8_t> not_jpeg = {0x00, 0x01, 0x02, 0x03};
  std::vector<uint8_t> out(512);
  EXPECT_EQ(core::write_jpeg_with_exif(not_jpeg, {}, out).error().code,
            core::Errc::InvalidArgument);

  std::vector<uint8_t> jpeg = {0xFF, 0xD8};
  jpeg.insert(jpeg.end(), kBody.begin(), kBody.end());
  std::vector<uint8_t> tiny(20);
  EXPECT_EQ(core::write_jpeg_with_exif(jpeg, {}, tiny).error().code, core::Errc::NoMemory);
}

TEST(Exif, RejectsTruncatedSegment) {
  std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x7F, 0xFF, 0x00};
  std::vector<uint8_t> out(512);
  EXPECT_FALSE(core::write_jpeg_with_exif(jpeg, {}, out).has_value());
}

TEST(Exif, DropsOutOfRangeDate) {
  std::vector<uint8_t> jpeg = {0xFF, 0xD8};
  jpeg.insert(jpeg.end(), kBody.begin(), kBody.end());
  std::vector<uint8_t> out(512);
  ExifInfo info{.taken_at = core::CivilTime{20260, 13, 40, 25, 61, 61}};
  auto written = core::write_jpeg_with_exif(jpeg, info, out);
  ASSERT_TRUE(written.has_value());
  out.resize(*written);
  EXPECT_EQ(parse(out).ascii.count(0x0132), 0u);
}
