// Copyright (c) 2026 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#include "rocjitsu/vm/amdgpu/pci/bar_access_trace.h"

#include "util/log.h"

#include <algorithm>
#include <format>
#include <vector>

namespace rocjitsu {

BarAccessTrace::BarAccessTrace(const RegisterSymbols &symbols)
    : BarAccessTrace(symbols, Config{}) {}

BarAccessTrace::BarAccessTrace(const RegisterSymbols &symbols, Config config)
    : symbols_(symbols), config_(config) {}

uint64_t BarAccessTrace::make_key(int bar, uint64_t offset) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(bar)) << 56) |
         (offset & 0x00ffffffffffffffULL);
}

void BarAccessTrace::record_rejected(int bar, uint64_t offset, std::size_t width, bool write) {
  const uint64_t key = make_key(bar, offset);
  std::lock_guard lock(mutex_);
  Site &site = rejected_[key];
  site.bar = bar;
  site.offset = offset;
  site.width = width;
  if (write) {
    ++site.writes;
  } else {
    ++site.reads;
  }
}

void BarAccessTrace::record(int bar, uint64_t offset, std::size_t width, bool write, bool modeled) {
  const uint64_t key = make_key(bar, offset);

  std::unique_lock lock(mutex_);

  if (!modeled) {
    Site &site = unmodeled_[key];
    site.bar = bar;
    site.offset = offset;
    site.width = width;
    if (write) {
      ++site.writes;
    } else {
      ++site.reads;
    }
  }

  // A write means the driver acted on something it read, so any polling loop it
  // was in has made progress. Only an unbroken run of reads of one register is
  // evidence of a wait that will not end.
  if (write) {
    repeated_read_count_ = 0;
    repeated_read_warned_ = false;
    return;
  }

  if (repeated_read_count_ != 0 && repeated_read_key_ == key) {
    ++repeated_read_count_;
  } else {
    repeated_read_key_ = key;
    repeated_read_count_ = 1;
    repeated_read_warned_ = false;
  }

  if (repeated_read_count_ < config_.spin_threshold || repeated_read_warned_) {
    return;
  }
  repeated_read_warned_ = true;
  ++spin_warnings_;

  const std::string_view name = symbols_.lookup(bar, offset);
  const uint32_t count = repeated_read_count_;
  lock.unlock();

  util::Logger::warn(
      std::format("vfu: guest has read bar{} +{:#x} ({}) {} times with no intervening write; "
                  "the driver is likely spinning on a status bit this device never sets",
                  bar, offset, name.empty() ? "unnamed register" : name, count));
}

std::string BarAccessTrace::unmodeled_report() const {
  return render(unmodeled_, "unmodeled register(s)");
}

std::string BarAccessTrace::rejected_report() const {
  return render(rejected_, "refused access(es)");
}

std::string BarAccessTrace::render(const std::unordered_map<uint64_t, Site> &from,
                                   std::string_view heading) const {
  std::vector<Site> sites;
  {
    const std::lock_guard lock(mutex_);
    sites.reserve(from.size());
    for (const auto &[key, site] : from) {
      sites.push_back(site);
    }
  }
  if (sites.empty()) {
    return {};
  }

  std::ranges::sort(sites, [](const Site &lhs, const Site &rhs) {
    const uint64_t lhs_total = lhs.reads + lhs.writes;
    const uint64_t rhs_total = rhs.reads + rhs.writes;
    if (lhs_total != rhs_total) {
      return lhs_total > rhs_total;
    }
    if (lhs.bar != rhs.bar) {
      return lhs.bar < rhs.bar;
    }
    return lhs.offset < rhs.offset;
  });

  std::string report = std::format("vfu: {} {}, most used first:\n", sites.size(), heading);
  for (const Site &site : sites) {
    const std::string_view name = symbols_.lookup(site.bar, site.offset);
    report +=
        std::format("  bar{} +{:#010x} width={} reads={} writes={} {}\n", site.bar, site.offset,
                    site.width, site.reads, site.writes, name.empty() ? "?" : name);
  }
  return report;
}

uint64_t BarAccessTrace::spin_warnings() const {
  const std::lock_guard lock(mutex_);
  return spin_warnings_;
}

// ---------------------------------------------------------------------------
// gap_report_json
// ---------------------------------------------------------------------------
// A hand-rolled JSON serialiser. The schema is simple and fixed, so pulling in
// a library dependency is not warranted. Every string that could contain
// metacharacters (register names) comes from a known-safe identifier set, but
// is escaped anyway to keep the output valid under all future names.
// ---------------------------------------------------------------------------

namespace {

/// @brief Escape @p s for inclusion inside a JSON double-quoted string.
std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
    case '"':  out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n";  break;
    case '\r': out += "\\r";  break;
    case '\t': out += "\\t";  break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
      } else {
        out += c;
      }
    }
  }
  return out;
}

/// @brief Serialise one Site map (unmodeled or rejected) into a JSON array.
/// @param[in] from   The map to serialise.
/// @param[in] with_name Include the "name" field (unmodeled registers only).
/// @param[in] symbols Register name table for lookup.
std::string sites_to_json_array(
    const std::unordered_map<uint64_t, BarAccessTrace::SiteForJson> &from,
    bool with_name,
    const RegisterSymbols &symbols) {
  std::vector<BarAccessTrace::SiteForJson> sites;
  sites.reserve(from.size());
  for (const auto &[key, site] : from) {
    sites.push_back(site);
  }

  std::ranges::sort(sites, [](const BarAccessTrace::SiteForJson &lhs,
                               const BarAccessTrace::SiteForJson &rhs) {
    const uint64_t lt = lhs.reads + lhs.writes;
    const uint64_t rt = rhs.reads + rhs.writes;
    if (lt != rt) return lt > rt;
    if (lhs.bar != rhs.bar) return lhs.bar < rhs.bar;
    return lhs.offset < rhs.offset;
  });

  std::string out = "[\n";
  for (std::size_t i = 0; i < sites.size(); ++i) {
    const auto &s = sites[i];
    out += std::format("    {{\"bar\":{},\"offset_hex\":\"{:#010x}\",\"width\":{},"
                       "\"reads\":{},\"writes\":{}",
                       s.bar, s.offset, s.width, s.reads, s.writes);
    if (with_name) {
      const std::string_view name = symbols.lookup(s.bar, s.offset);
      out += std::format(",\"name\":\"{}\"", json_escape(name));
    }
    out += '}';
    if (i + 1 < sites.size()) out += ',';
    out += '\n';
  }
  out += "  ]";
  return out;
}

} // namespace

std::string BarAccessTrace::gap_report_json() const {
  // Snapshot both maps and the spin counter under one lock acquisition.
  std::unordered_map<uint64_t, SiteForJson> unmodeled_snap;
  std::unordered_map<uint64_t, SiteForJson> rejected_snap;
  uint64_t spin_snap = 0;
  {
    const std::lock_guard lock(mutex_);
    unmodeled_snap.reserve(unmodeled_.size());
    for (const auto &[key, site] : unmodeled_) {
      unmodeled_snap[key] = {site.reads, site.writes, site.bar, site.offset, site.width};
    }
    rejected_snap.reserve(rejected_.size());
    for (const auto &[key, site] : rejected_) {
      rejected_snap[key] = {site.reads, site.writes, site.bar, site.offset, site.width};
    }
    spin_snap = spin_warnings_;
  }

  std::string out = "{\n";
  out += "  \"schema_version\": 1,\n";
  out += "  \"unmodeled_registers\": ";
  out += sites_to_json_array(unmodeled_snap, /*with_name=*/true, symbols_);
  out += ",\n";
  out += "  \"rejected_accesses\": ";
  out += sites_to_json_array(rejected_snap, /*with_name=*/false, symbols_);
  out += ",\n";
  out += std::format("  \"spin_warnings\": {}\n", spin_snap);
  out += "}\n";
  return out;
}

} // namespace rocjitsu
