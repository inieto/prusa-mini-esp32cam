#include "core/log_tail.hpp"

namespace core {
namespace {
constexpr uint32_t kMagic = 0x4C544C31;  // "LTL1"
}  // namespace

uint32_t LogTail::check_of(const Storage& s) {
  return (s.magic ^ 0xA5A5A5A5u) + s.head * 2654435761u + (s.wrapped ? 0x9E3779B9u : 0x7F4A7C15u);
}

std::string LogTail::take_previous(bool trust) {
  std::string out;
  const bool intact = trust && s_.magic == kMagic && s_.head < kCapacity && s_.wrapped <= 1 &&
                      s_.check == check_of(s_);
  if (intact) {
    if (s_.wrapped) out.append(s_.data + s_.head, kCapacity - s_.head);
    out.append(s_.data, s_.head);
    if (s_.wrapped) {  // drop the partial oldest line
      const size_t nl = out.find('\n');
      if (nl != std::string::npos) out.erase(0, nl + 1);
    }
    // A crash can interrupt a write: never hand control or binary bytes to the UI.
    for (char& c : out) {
      const auto u = static_cast<unsigned char>(c);
      if ((u < 0x20 && c != '\n' && c != '\t') || u == 0x7F) c = '?';
    }
  }
  s_.magic = kMagic;
  s_.head = 0;
  s_.wrapped = 0;
  s_.check = check_of(s_);
  return out;
}

void LogTail::append(std::string_view text) {
  if (s_.magic != kMagic) return;  // not initialised by take_previous yet
  for (char c : text) {
    s_.data[s_.head] = c;
    if (++s_.head == kCapacity) {
      s_.head = 0;
      s_.wrapped = 1;
    }
  }
  s_.check = check_of(s_);
}

}  // namespace core
