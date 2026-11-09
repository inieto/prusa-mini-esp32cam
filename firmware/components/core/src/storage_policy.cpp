#include "core/storage_policy.hpp"

#include <algorithm>
#include <cstdio>

namespace core {
namespace {

constexpr size_t kMaxPath = 120;

std::string date_string(const CivilTime& t) {
  char buf[16];  // room for any uint16/uint8 values: GCC checks the worst case
  std::snprintf(buf, sizeof buf, "%04u-%02u-%02u", t.year, t.month, t.day);
  return buf;
}

// Keeps printable ASCII letters/digits and a few separators so job names make valid FAT names.
std::string folder_safe(std::string_view name) {
  std::string out;
  for (char c : name) {
    if (out.size() >= 40) break;
    const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_';
    if (keep) out += c;
    else if (!out.empty() && out.back() != '_') out += '_';
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out;
}

}  // namespace

std::optional<std::string> sanitize_relative_path(std::string_view path) {
  if (path.size() > kMaxPath) return std::nullopt;
  std::string out;
  size_t i = 0;
  while (i < path.size()) {
    const size_t slash = path.find('/', i);
    const std::string_view part =
        path.substr(i, slash == std::string_view::npos ? std::string_view::npos : slash - i);
    i = slash == std::string_view::npos ? path.size() : slash + 1;
    if (part.empty() || part == ".") continue;  // "a//b", "./a", leading "/"
    if (part == "..") return std::nullopt;
    for (char c : part) {
      const auto u = static_cast<unsigned char>(c);
      if (u < 0x20 || u == 0x7f || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
          c == '<' || c == '>' || c == '|') {
        return std::nullopt;
      }
    }
    if (!out.empty()) out += '/';
    out += part;
  }
  return out;
}

std::string_view to_string(TimelapseMode mode) {
  switch (mode) {
    case TimelapseMode::Off: return "off";
    case TimelapseMode::Always: return "always";
    case TimelapseMode::WhilePrinting: return "printing";
  }
  return "off";
}

std::optional<TimelapseMode> timelapse_mode_from(std::string_view name) {
  for (TimelapseMode m : {TimelapseMode::Off, TimelapseMode::Always, TimelapseMode::WhilePrinting}) {
    if (to_string(m) == name) return m;
  }
  return std::nullopt;
}

bool valid(const StorageSettings& s) {
  return static_cast<int>(s.timelapse) <= 2 && s.timelapse_interval_s >= 10 &&
         s.timelapse_interval_s <= 3600;
}

std::optional<TimelapseTarget> TimelapsePolicy::on_frame(Millis now, const PrinterJob& printer,
                                                         std::optional<CivilTime> utc) {
  if (settings_.timelapse == TimelapseMode::Off || !utc) return std::nullopt;
  if (last_saved_ && now - *last_saved_ < Millis{settings_.timelapse_interval_s * 1000LL}) {
    return std::nullopt;
  }

  std::string folder;
  if (settings_.timelapse == TimelapseMode::Always) {
    folder = "timelapse/" + date_string(*utc);
  } else {
    if (printer.phase != PrinterPhase::Printing) return std::nullopt;
    if (printer.job_id != job_id_ || job_folder_.empty()) {
      // One folder per print, named when its first frame arrives.
      job_id_ = printer.job_id;
      const std::string name = folder_safe(printer.file);
      char hhmm[8];
      std::snprintf(hhmm, sizeof hhmm, "%02u%02u", utc->hour, utc->minute);
      job_folder_ = "timelapse/" + date_string(*utc) + "_" + hhmm + (name.empty() ? "" : "_" + name);
    }
    folder = job_folder_;
  }

  char file[16];
  std::snprintf(file, sizeof file, "%02u%02u%02u.jpg", utc->hour, utc->minute, utc->second);
  last_saved_ = now;
  return TimelapseTarget{std::move(folder), file};
}

std::vector<std::string> logs_to_delete(std::vector<std::string> names, size_t keep) {
  std::erase_if(names, [](const std::string& n) { return !n.ends_with(".log"); });
  if (names.size() <= keep) return {};
  std::sort(names.begin(), names.end());  // ISO dates sort chronologically; "undated" last
  std::erase(names, std::string("undated.log"));
  if (names.size() <= keep) return {};
  names.resize(names.size() - keep);
  return names;
}

std::optional<std::string> url_decode(std::string_view in) {
  const auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    char c = in[i];
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      if (i + 2 >= in.size()) return std::nullopt;
      const int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
      if (hi < 0 || lo < 0) return std::nullopt;
      c = static_cast<char>(hi * 16 + lo);
      i += 2;
    }
    if (c == '\0') return std::nullopt;
    out += c;
  }
  return out;
}

std::string log_file_name(std::optional<CivilTime> utc) {
  return utc ? date_string(*utc) + ".log" : "undated.log";
}

}  // namespace core
