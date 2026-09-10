// Copyright (c) 2026 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#include "rocjitsu/vm/amdgpu/pci/bar_access_trace.h"
#include "rocjitsu/vm/amdgpu/pci/register_symbols.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

constexpr int kRegisterBar = 5;

// Byte offsets of registers amdgpu reads before IP discovery, derived from the
// dword indices the driver defines in amdgpu_discovery.c.
constexpr uint64_t kRccConfigMemsize = 0xde3 * 4;
constexpr uint64_t kMp0SmnC2pmsg33 = 0x16061 * 4;
constexpr uint64_t kDriverScratch0 = 0x94 * 4;

rocjitsu::RegisterSymbols pre_discovery_symbols() {
  rocjitsu::RegisterSymbols symbols;
  rocjitsu::add_pre_discovery_symbols(symbols, kRegisterBar);
  return symbols;
}

TEST(RegisterSymbols, NamesPreDiscoveryRegistersByByteOffset) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();

  EXPECT_EQ(symbols.lookup(kRegisterBar, kRccConfigMemsize), "RCC_CONFIG_MEMSIZE");
  EXPECT_EQ(symbols.lookup(kRegisterBar, kMp0SmnC2pmsg33), "MP0_SMN_C2PMSG_33");
  EXPECT_EQ(symbols.lookup(kRegisterBar, kDriverScratch0), "DRIVER_SCRATCH_0");
}

TEST(RegisterSymbols, DoesNotNameAnUnknownOffsetOrTheWrongBar) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();

  EXPECT_TRUE(symbols.lookup(kRegisterBar, 0x12340).empty());
  EXPECT_TRUE(symbols.lookup(0, kRccConfigMemsize).empty());
}

TEST(BarAccessTrace, ReportsNothingWhenEveryAccessIsModeled) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols);

  trace.record(kRegisterBar, kRccConfigMemsize, 4, /*write=*/false, /*modeled=*/true);

  EXPECT_TRUE(trace.unmodeled_report().empty());
}

TEST(BarAccessTrace, RanksUnmodeledRegistersByUseAndNamesThem) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols);

  constexpr uint64_t kUnnamedOffset = 0x28a04;
  trace.record(kRegisterBar, kUnnamedOffset, 4, /*write=*/false, /*modeled=*/false);
  for (int i = 0; i < 3; ++i) {
    trace.record(kRegisterBar, kDriverScratch0, 4, /*write=*/false, /*modeled=*/false);
  }

  const std::string report = trace.unmodeled_report();
  const std::size_t scratch_pos = report.find("DRIVER_SCRATCH_0");
  const std::size_t unnamed_pos = report.find("0x00028a04");

  ASSERT_NE(scratch_pos, std::string::npos);
  ASSERT_NE(unnamed_pos, std::string::npos);
  EXPECT_LT(scratch_pos, unnamed_pos) << "the most used register must come first:\n" << report;
  EXPECT_NE(report.find("reads=3"), std::string::npos) << report;
}

TEST(BarAccessTrace, WarnsOnceWhenTheGuestSpinsOnOneRegister) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols, {.spin_threshold = 8});

  for (int i = 0; i < 64; ++i) {
    trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/true);
  }

  EXPECT_EQ(trace.spin_warnings(), 1u);
}

TEST(BarAccessTrace, DoesNotWarnWhenReadsAlternateBetweenRegisters) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols, {.spin_threshold = 8});

  for (int i = 0; i < 64; ++i) {
    trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/true);
    trace.record(kRegisterBar, kRccConfigMemsize, 4, /*write=*/false, /*modeled=*/true);
  }

  EXPECT_EQ(trace.spin_warnings(), 0u);
}

TEST(BarAccessTrace, TreatsAWriteAsProgressAndStartsTheRunOver) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols, {.spin_threshold = 8});

  for (int round = 0; round < 8; ++round) {
    for (int i = 0; i < 7; ++i) {
      trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/true);
    }
    trace.record(kRegisterBar, kDriverScratch0, 4, /*write=*/true, /*modeled=*/true);
  }

  EXPECT_EQ(trace.spin_warnings(), 0u);
}

TEST(BarAccessTrace, WarnsAgainForANewSpinEpisodeAfterProgress) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols, {.spin_threshold = 4});

  for (int i = 0; i < 16; ++i) {
    trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/true);
  }
  trace.record(kRegisterBar, kDriverScratch0, 4, /*write=*/true, /*modeled=*/true);
  for (int i = 0; i < 16; ++i) {
    trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/true);
  }

  EXPECT_EQ(trace.spin_warnings(), 2u);
}

// -------------------------------------------------------------------------
// gap_report_json tests
// -------------------------------------------------------------------------

TEST(BarAccessTraceJson, EmptyTraceProducesValidJsonWithEmptyArrays) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols);

  const std::string json = trace.gap_report_json();

  // Must be non-empty and parseable enough for a bot to consume.
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("\"schema_version\": 1"), std::string::npos) << json;
  EXPECT_NE(json.find("\"unmodeled_registers\": ["), std::string::npos) << json;
  EXPECT_NE(json.find("\"rejected_accesses\": ["), std::string::npos) << json;
  EXPECT_NE(json.find("\"spin_warnings\": 0"), std::string::npos) << json;
}

TEST(BarAccessTraceJson, UnmodeledRegisterAppearsWithNameAndCounts) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols);

  // Three reads of a known pre-discovery register.
  for (int i = 0; i < 3; ++i) {
    trace.record(kRegisterBar, kDriverScratch0, 4, /*write=*/false, /*modeled=*/false);
  }

  const std::string json = trace.gap_report_json();

  EXPECT_NE(json.find("\"DRIVER_SCRATCH_0\""), std::string::npos) << json;
  EXPECT_NE(json.find("\"reads\":3"), std::string::npos) << json;
  EXPECT_NE(json.find("\"writes\":0"), std::string::npos) << json;
  EXPECT_NE(json.find("\"bar\":5"), std::string::npos) << json;
}

TEST(BarAccessTraceJson, RanksUnmodeledRegistersMostFrequentFirst) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols);

  // Access kDriverScratch0 once, kMp0SmnC2pmsg33 three times — latter must
  // appear earlier in the JSON array.
  trace.record(kRegisterBar, kDriverScratch0, 4, /*write=*/false, /*modeled=*/false);
  for (int i = 0; i < 3; ++i) {
    trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/false);
  }

  const std::string json = trace.gap_report_json();

  const std::size_t mp0_pos = json.find("MP0_SMN_C2PMSG_33");
  const std::size_t scratch_pos = json.find("DRIVER_SCRATCH_0");
  ASSERT_NE(mp0_pos, std::string::npos) << json;
  ASSERT_NE(scratch_pos, std::string::npos) << json;
  EXPECT_LT(mp0_pos, scratch_pos) << "most-used register must appear first:\n" << json;
}

TEST(BarAccessTraceJson, RejectedAccessesAreInSeparateArray) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols);

  trace.record_rejected(kRegisterBar, 0x99900, 4, /*write=*/true);

  const std::string json = trace.gap_report_json();

  // The offset must be in the rejected_accesses array.
  const std::size_t rejected_start = json.find("\"rejected_accesses\":");
  ASSERT_NE(rejected_start, std::string::npos) << json;
  const std::size_t offset_pos = json.find("0x00099900", rejected_start);
  EXPECT_NE(offset_pos, std::string::npos) << json;

  // It must NOT appear under unmodeled_registers.
  const std::size_t unmodeled_start = json.find("\"unmodeled_registers\":");
  const std::size_t unmodeled_end = json.find("\"rejected_accesses\":");
  ASSERT_NE(unmodeled_start, std::string::npos);
  // The offset must not be between the two section markers.
  const std::size_t offset_in_unmodeled = json.find("0x00099900", unmodeled_start);
  EXPECT_GT(offset_in_unmodeled, unmodeled_end) << "rejected site leaked into unmodeled:\n" << json;
}

TEST(BarAccessTraceJson, SpinWarningsCountIsCorrect) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols, {.spin_threshold = 4});

  for (int i = 0; i < 16; ++i) {
    trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/true);
  }
  trace.record(kRegisterBar, kDriverScratch0, 4, /*write=*/true, /*modeled=*/true);
  for (int i = 0; i < 16; ++i) {
    trace.record(kRegisterBar, kMp0SmnC2pmsg33, 4, /*write=*/false, /*modeled=*/true);
  }

  const std::string json = trace.gap_report_json();

  EXPECT_NE(json.find("\"spin_warnings\": 2"), std::string::npos) << json;
}

TEST(BarAccessTraceJson, UnnamedRegisterHasEmptyNameField) {
  const rocjitsu::RegisterSymbols symbols = pre_discovery_symbols();
  rocjitsu::BarAccessTrace trace(symbols);

  constexpr uint64_t kUnnamedOffset = 0xabcd0;
  trace.record(kRegisterBar, kUnnamedOffset, 4, /*write=*/false, /*modeled=*/false);

  const std::string json = trace.gap_report_json();

  // The unnamed register must still appear.
  EXPECT_NE(json.find("0x000abcd0"), std::string::npos) << json;
  // Its name field must be the empty string.
  EXPECT_NE(json.find("\"name\":\"\""), std::string::npos) << json;
}

} // namespace
