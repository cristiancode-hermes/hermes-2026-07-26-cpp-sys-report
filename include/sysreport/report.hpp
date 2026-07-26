#pragma once

#include <string>
#include <vector>
#include <optional>
#include <expected>
#include <nlohmann/json.hpp>
#include <cstdint>

using json = nlohmann::json;

namespace sysreport {

// ── Error type ──
struct Error {
  int code;
  std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

// ── Data structures ──

struct CpuInfo {
  std::string model_name;
  int core_count = 0;
  int thread_count = 0;
  int physical_packages = 0;
  std::string architecture;
  std::vector<int> core_ids;
};

struct MemoryInfo {
  uint64_t total_kb = 0;
  uint64_t free_kb = 0;
  uint64_t available_kb = 0;
  uint64_t buffers_kb = 0;
  uint64_t cached_kb = 0;
  uint64_t swap_total_kb = 0;
  uint64_t swap_free_kb = 0;
};

struct UptimeInfo {
  double uptime_seconds = 0;
  double idle_seconds = 0;
  uint64_t boot_time_epoch = 0;
};

struct LoadInfo {
  double load_1 = 0;
  double load_5 = 0;
  double load_15 = 0;
  int running = 0;
  int total_threads = 0;
  int last_pid = 0;
};

struct KernelInfo {
  std::string hostname;
  std::string os_release;
  std::string version;
  std::string machine;
};

struct ThermalZone {
  std::string name;
  std::string type;
  double temp_celsius = 0;
};

struct DiskSummary {
  uint64_t total_reads = 0;
  uint64_t total_writes = 0;
  uint64_t total_read_sectors = 0;
  uint64_t total_write_sectors = 0;
  uint64_t total_read_time_ms = 0;
  uint64_t total_write_time_ms = 0;
  int device_count = 0;
};

struct SystemReport {
  CpuInfo cpu;
  MemoryInfo memory;
  UptimeInfo uptime;
  LoadInfo load;
  KernelInfo kernel;
  std::vector<ThermalZone> thermal_zones;
  DiskSummary disk;
};

// ── Readers ──

Result<CpuInfo> read_cpu_info();
Result<MemoryInfo> read_memory_info();
Result<UptimeInfo> read_uptime_info();
Result<LoadInfo> read_load_info();
Result<KernelInfo> read_kernel_info();
Result<std::vector<ThermalZone>> read_thermal_zones();
Result<DiskSummary> read_disk_summary();

// ── Build report (select categories) ──

SystemReport build_report(bool include_cpu, bool include_memory, bool include_uptime,
                          bool include_load, bool include_kernel, bool include_thermal,
                          bool include_disk);

// ── Formatters ──

json report_to_json(const SystemReport& report);
void print_text_report(const SystemReport& report);

} // namespace sysreport
