#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "sysreport/report.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace sysreport;

// ── Helper: check if /proc files are readable ──
static bool proc_available() {
  return fs::exists("/proc/stat") && fs::exists("/proc/version");
}

// ── Unit tests ──

TEST_CASE("CPU info reader", "[cpu]") {
  auto result = read_cpu_info();
  if (!proc_available()) {
    REQUIRE_FALSE(result.has_value());
    return;
  }
  REQUIRE(result.has_value());
  auto& info = *result;

  // Must have at least a model name
  REQUIRE_FALSE(info.model_name.empty());

  // Core count must be positive
  REQUIRE(info.core_count > 0);
  REQUIRE(info.thread_count > 0);

  // Architecture should be non-empty
  REQUIRE_FALSE(info.architecture.empty());

  // Thread count should be >= core count
  REQUIRE(info.thread_count >= info.core_count);

  std::cout << "  CPU: " << info.model_name << " | "
            << info.core_count << " cores / " << info.thread_count << " threads | "
            << info.architecture << std::endl;
}

TEST_CASE("Memory info reader", "[memory]") {
  auto result = read_memory_info();
  if (!proc_available()) {
    REQUIRE_FALSE(result.has_value());
    return;
  }
  REQUIRE(result.has_value());
  auto& info = *result;

  REQUIRE(info.total_kb > 0);
  REQUIRE(info.free_kb > 0);
  REQUIRE(info.available_kb > 0);
  REQUIRE(info.available_kb <= info.total_kb);

  std::cout << "  Memory: " << (info.total_kb / 1024) << " MB total, "
            << (info.available_kb / 1024) << " MB available" << std::endl;
}

TEST_CASE("Uptime info reader", "[uptime]") {
  auto result = read_uptime_info();
  if (!proc_available()) {
    REQUIRE_FALSE(result.has_value());
    return;
  }
  REQUIRE(result.has_value());
  auto& info = *result;

  // Uptime should be positive (system is running)
  REQUIRE(info.uptime_seconds > 0);

  // boot_time_epoch should be set
  REQUIRE(info.boot_time_epoch > 0);

  std::cout << "  Uptime: " << info.uptime_seconds << "s, Boot: "
            << info.boot_time_epoch << std::endl;
}

TEST_CASE("Load info reader", "[load]") {
  auto result = read_load_info();
  if (!proc_available()) {
    REQUIRE_FALSE(result.has_value());
    return;
  }
  REQUIRE(result.has_value());
  auto& info = *result;

  // Load averages should be non-negative
  REQUIRE(info.load_1 >= 0);
  REQUIRE(info.load_5 >= 0);
  REQUIRE(info.load_15 >= 0);

  // Total threads should be positive
  REQUIRE(info.total_threads > 0);

  std::cout << "  Load: " << info.load_1 << "/" << info.load_5 << "/" << info.load_15
            << " | " << info.running << "/" << info.total_threads << std::endl;
}

TEST_CASE("Kernel info reader", "[kernel]") {
  auto result = read_kernel_info();
  if (!proc_available()) {
    REQUIRE_FALSE(result.has_value());
    return;
  }
  REQUIRE(result.has_value());
  auto& info = *result;

  REQUIRE_FALSE(info.hostname.empty());
  REQUIRE_FALSE(info.version.empty());
  REQUIRE_FALSE(info.machine.empty());

  std::cout << "  Hostname: " << info.hostname << " | "
            << info.machine << " | " << info.os_release << std::endl;
}

TEST_CASE("Thermal zones reader", "[thermal]") {
  auto result = read_thermal_zones();
  // Thermal zones may not be available — that's fine
  REQUIRE(result.has_value());

  if (!result->empty()) {
    for (const auto& z : *result) {
      REQUIRE_FALSE(z.type.empty());
      std::cout << "  Thermal: " << z.type << " = " << z.temp_celsius << "°C" << std::endl;
    }
  } else {
    std::cout << "  No thermal zones available on this system" << std::endl;
  }
}

TEST_CASE("Disk summary reader", "[disk]") {
  auto result = read_disk_summary();

  if (proc_available() && fs::exists("/proc/diskstats")) {
    REQUIRE(result.has_value());
    auto& info = *result;

    // Should have at least some stats
    if (info.device_count > 0) {
      std::cout << "  Disk: " << info.device_count << " devices, "
                << info.total_reads << " reads, " << info.total_writes << " writes"
                << std::endl;
    }
  } else {
    REQUIRE_FALSE(result.has_value());
  }
}

TEST_CASE("JSON roundtrip", "[json]") {
  // Create a sample report with known values
  SystemReport report;

  report.cpu.model_name = "Test CPU";
  report.cpu.core_count = 4;
  report.cpu.thread_count = 8;
  report.cpu.architecture = "x86_64";

  report.memory.total_kb = 8388608;       // 8 GB
  report.memory.available_kb = 4194304;   // 4 GB
  report.memory.free_kb = 2097152;

  report.uptime.uptime_seconds = 86400;  // 1 day
  report.uptime.boot_time_epoch = 1700000000;

  report.load.load_1 = 1.5;
  report.load.load_5 = 1.2;
  report.load.load_15 = 1.0;
  report.load.total_threads = 200;

  report.kernel.hostname = "test-host";
  report.kernel.os_release = "TestOS 1.0";
  report.kernel.machine = "x86_64";

  ThermalZone tz;
  tz.name = "thermal_zone0";
  tz.type = "x86_pkg_temp";
  tz.temp_celsius = 45.0;
  report.thermal_zones.push_back(tz);

  report.disk.device_count = 2;
  report.disk.total_reads = 100000;
  report.disk.total_writes = 50000;

  // Convert to JSON
  json j = report_to_json(report);

  // Validate
  REQUIRE(j.contains("cpu"));
  REQUIRE(j["cpu"]["model_name"] == "Test CPU");
  REQUIRE(j["cpu"]["core_count"] == 4);
  REQUIRE(j["cpu"]["thread_count"] == 8);

  REQUIRE(j.contains("memory"));
  REQUIRE(j["memory"]["total"]["kb"] == 8388608);
  REQUIRE(j["memory"]["available"]["kb"] == 4194304);

  REQUIRE(j.contains("uptime"));
  REQUIRE(j["uptime"]["uptime_seconds"] == 86400);

  REQUIRE(j.contains("load"));
  REQUIRE(j["load"]["load_1m"] == 1.5);

  REQUIRE(j.contains("kernel"));
  REQUIRE(j["kernel"]["hostname"] == "test-host");

  REQUIRE(j.contains("thermal_zones"));
  REQUIRE(j["thermal_zones"].size() == 1);
  REQUIRE(j["thermal_zones"][0]["temperature_celsius"] == 45.0);

  REQUIRE(j.contains("disk"));
  REQUIRE(j["disk"]["device_count"] == 2);
}

TEST_CASE("JSON dump and re-parse", "[json]") {
  SystemReport report;
  report.cpu.model_name = "Parse Test CPU";
  report.cpu.core_count = 2;
  report.cpu.thread_count = 4;

  json j1 = report_to_json(report);
  std::string dumped = j1.dump();

  json j2 = json::parse(dumped);
  REQUIRE(j2["cpu"]["model_name"] == "Parse Test CPU");
  REQUIRE(j2["cpu"]["core_count"] == 2);
}

TEST_CASE("build_report selects sections", "[integration]") {
  if (!proc_available()) return;

  // Only CPU + Load
  auto report = build_report(true, false, false, true, false, false, false);

  REQUIRE_FALSE(report.cpu.model_name.empty());
  REQUIRE(report.memory.total_kb == 0);   // Not included
  REQUIRE(report.load.load_1 >= 0);
  REQUIRE(report.uptime.uptime_seconds == 0);  // Not included

  std::cout << "  Selective report: CPU + Load OK" << std::endl;
}

// ── Benchmark ──
TEST_CASE("proc reading performance", "[benchmark]") {
  if (!proc_available()) return;

  BENCHMARK("Read CPU info") {
    return read_cpu_info();
  };

  BENCHMARK("Read Memory info") {
    return read_memory_info();
  };
}
