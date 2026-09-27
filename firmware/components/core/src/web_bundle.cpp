#include "core/web_bundle.hpp"

namespace core {

Result<WebBundle> WebBundle::parse(std::span<const uint8_t> data) {
  size_t pos = 0;
  auto take = [&](size_t n) -> std::optional<std::span<const uint8_t>> {
    if (pos + n > data.size()) return std::nullopt;
    auto s = data.subspan(pos, n);
    pos += n;
    return s;
  };
  auto text = [](std::span<const uint8_t> s) {
    return std::string_view(reinterpret_cast<const char*>(s.data()), s.size());
  };

  const auto magic = take(4);
  const auto version = take(1);
  const auto etag = take(16);
  const auto count = take(1);
  if (!magic || text(*magic) != "PCWB" || !version || (*version)[0] != 1 || !etag || !count) {
    return fail(Errc::InvalidArgument);
  }
  if ((*count)[0] > kMaxAssets) return fail(Errc::OutOfRange);

  WebBundle bundle;
  bundle.etag_ = text(*etag);
  for (size_t i = 0; i < (*count)[0]; ++i) {
    const auto path_len = take(1);
    if (!path_len) return fail(Errc::InvalidArgument);
    const auto path = take((*path_len)[0]);
    const auto type_len = path ? take(1) : std::nullopt;
    const auto type = type_len ? take((*type_len)[0]) : std::nullopt;
    const auto size = type ? take(4) : std::nullopt;
    if (!size) return fail(Errc::InvalidArgument);
    const uint32_t n = (*size)[0] | ((*size)[1] << 8) | ((*size)[2] << 16) | ((*size)[3] << 24);
    const auto body = take(n);
    if (!body) return fail(Errc::InvalidArgument);
    bundle.assets_[bundle.count_++] = {text(*path), text(*type), *body};
  }
  if (pos != data.size()) return fail(Errc::InvalidArgument);
  return bundle;
}

std::optional<WebAsset> WebBundle::find(std::string_view path) const {
  for (size_t i = 0; i < count_; ++i) {
    if (assets_[i].path == path) return assets_[i];
  }
  return std::nullopt;
}

}  // namespace core
