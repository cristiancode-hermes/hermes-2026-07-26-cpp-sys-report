#include "sysreport/report.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <charconv>
#include <thread>

#include <iostream>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace sysreport {

// ── Helpers ──

static std::optional<std::string> read_file(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Trim trailing whitespace from a string view
static void trim_trailing(std::string_view& sv) {
  while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\n' || sv.back() == '\r'))
    sv.remove_suffix(1);
}

// ── CPU Info ──

Result<CpuInfo> read_cpu_info() {
  auto content = read_file("/proc/cpuinfo");
  if (!content) return std::unexpected(Error{1, "Cannot read /proc/cpuinfo"});

  CpuInfo info;
  int processor_count = 0;
  int core_id_set = 0;
  std::vector<int> seen_core_ids;

  std::istringstream stream(*content);
  std::string line;
  while (std::getline(stream, line)) {
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;

    std::string_view key(line.data(), colon);
    std::string_view val(line.data() + colon + 1, line.size() - colon - 1);

    // Trim trailing from key (tabs before colon in /proc/cpuinfo)
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
      key.remove_suffix(1);
    // Trim leading from value
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
      val.remove_prefix(1);
    trim_trailing(val);

    if (key == "processor") {
      processor_count++;
    } else if (key == "model name") {
      if (info.model_name.empty())
        info.model_name = val;
    } else if (key == "cpu cores") {
      int cores = 0;
      std::from_chars(val.data(), val.data() + val.size(), cores);
      if (cores > 0) info.core_count = cores;
    } else if (key == "siblings") {
      int threads = 0;
      std::from_chars(val.data(), val.data() + val.size(), threads);
      if (threads > 0) info.thread_count = threads;
    } else if (key == "physical id") {
      int pkg = 0;
      std::from_chars(val.data(), val.data() + val.size(), pkg);
      if (pkg + 1 > info.physical_packages) info.physical_packages = pkg + 1;
    } else if (key == "core id") {
      if (!core_id_set) {
        int cid = 0;
        std::from_chars(val.data(), val.data() + val.size(), cid);
        if (std::find(seen_core_ids.begin(), seen_core_ids.end(), cid) == seen_core_ids.end()) {
          seen_core_ids.push_back(cid);
        }
      }
    } else if (key == "flags") {
      // Detect architecture from flags
      if (val.find("lm") != std::string_view::npos)
        info.architecture = "x86_64";
    }
  }

  // Detect architecture from /proc/version or uname
  if (info.architecture.empty()) {
    auto version = read_file("/proc/version");
    if (version) {
      if (version->find("aarch64") != std::string::npos || version->find("arm64") != std::string::npos)
        info.architecture = "aarch64";
      else
        info.architecture = "x86_64";
    }
  }

  info.core_ids = std::move(seen_core_ids);
  if (info.core_count == 0) info.core_count = processor_count;
  if (info.thread_count == 0) info.thread_count = processor_count;

  return info;
}

// ── Memory Info ──

Result<MemoryInfo> read_memory_info() {
  auto content = read_file("/proc/meminfo");
  if (!content) return std::unexpected(Error{2, "Cannot read /proc/meminfo"});

  MemoryInfo info;
  std::istringstream stream(*content);
  std::string line;

  while (std::getline(stream, line)) {
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;

    std::string key = line.substr(0, colon);
    auto val_start = line.find_first_not_of(" \t", colon + 1);
    if (val_start == std::string::npos) continue;
    auto val_end = line.find(' ', val_start);
    std::string val_str = (val_end == std::string::npos) ? line.substr(val_start) : line.substr(val_start, val_end - val_start);

    uint64_t val = 0;
    std::from_chars(val_str.data(), val_str.data() + val_str.size(), val);

    if (key == "MemTotal") info.total_kb = val;
    else if (key == "MemFree") info.free_kb = val;
    else if (key == "MemAvailable") info.available_kb = val;
    else if (key == "Buffers") info.buffers_kb = val;
    else if (key == "Cached") info.cached_kb = val;
    else if (key == "SwapTotal") info.swap_total_kb = val;
    else if (key == "SwapFree") info.swap_free_kb = val;
  }

  return info;
}

// ── Uptime Info ──

Result<UptimeInfo> read_uptime_info() {
  auto uptime_content = read_file("/proc/uptime");
  if (!uptime_content) return std::unexpected(Error{3, "Cannot read /proc/uptime"});

  UptimeInfo info;
  std::sscanf(uptime_content->c_str(), "%lf %lf", &info.uptime_seconds, &info.idle_seconds);

  // Read boot time from /proc/stat
  auto stat_content = read_file("/proc/stat");
  if (stat_content) {
    std::istringstream stream(*stat_content);
    std::string line;
    while (std::getline(stream, line)) {
      if (line.starts_with("btime ")) {
        std::string_view val(line.data() + 6, line.size() - 6);
        while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
        std::from_chars(val.data(), val.data() + val.size(), info.boot_time_epoch);
        break;
      }
    }
  }

  return info;
}

// ── Load Info ──

Result<LoadInfo> read_load_info() {
  auto content = read_file("/proc/loadavg");
  if (!content) return std::unexpected(Error{4, "Cannot read /proc/loadavg"});

  LoadInfo info;
  std::sscanf(content->c_str(), "%lf %lf %lf %d/%d %d",
              &info.load_1, &info.load_5, &info.load_15,
              &info.running, &info.total_threads, &info.last_pid);
  return info;
}

// ── Kernel Info ──

Result<KernelInfo> read_kernel_info() {
  KernelInfo info;

  auto hostname = read_file("/proc/sys/kernel/hostname");
  if (hostname) {
    while (!hostname->empty() && (hostname->back() == '\n' || hostname->back() == '\r'))
      hostname->pop_back();
    info.hostname = *hostname;
  }

  auto version = read_file("/proc/version");
  if (version) {
    while (!version->empty() && (version->back() == '\n' || version->back() == '\r'))
      version->pop_back();
    info.version = *version;

    // Try to extract OS release info
    auto os_rel = read_file("/etc/os-release");
    if (os_rel) {
      std::istringstream stream(*os_rel);
      std::string line;
      while (std::getline(stream, line)) {
        if (line.starts_with("PRETTY_NAME=")) {
          auto quote_start = line.find('"');
          auto quote_end = line.rfind('"');
          if (quote_start != std::string::npos && quote_end > quote_start) {
            info.os_release = line.substr(quote_start + 1, quote_end - quote_start - 1);
          }
          break;
        }
      }
    }
    if (info.os_release.empty()) info.os_release = "Linux";
  }

  // Get machine architecture
  FILE* uname_p = popen("uname -m 2>/dev/null", "r");
  if (uname_p) {
    char buf[128] = {};
    if (fgets(buf, sizeof(buf), uname_p)) {
      std::string m(buf);
      while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) m.pop_back();
      info.machine = m;
    }
    pclose(uname_p);
  }

  return info;
}

// ── Thermal Zones ──

Result<std::vector<ThermalZone>> read_thermal_zones() {
  std::vector<ThermalZone> zones;
  const std::string thermal_dir = "/sys/class/thermal";

  if (!fs::is_directory(thermal_dir)) {
    return zones; // No thermal zones available
  }

  for (const auto& entry : fs::directory_iterator(thermal_dir)) {
    if (!entry.is_directory()) continue;

    std::string name = entry.path().filename().string();
    if (name.find("thermal_zone") != 0) continue;

    ThermalZone zone;
    zone.name = name;

    // Read type
    auto type_path = entry.path() / "type";
    auto type_content = read_file(type_path.string());
    if (type_content) {
      while (!type_content->empty() && (type_content->back() == '\n' || type_content->back() == '\r'))
        type_content->pop_back();
      zone.type = *type_content;
    }

    // Read temperature (millidegrees)
    auto temp_path = entry.path() / "temp";
    auto temp_content = read_file(temp_path.string());
    if (temp_content) {
      uint64_t millideg = 0;
      auto val_start = temp_content->find_first_not_of(" \t\n\r");
      if (val_start != std::string::npos) {
        std::string_view sv(temp_content->data() + val_start, temp_content->size() - val_start);
        std::from_chars(sv.data(), sv.data() + sv.size(), millideg);
        zone.temp_celsius = static_cast<double>(millideg) / 1000.0;
      }
    }

    zones.push_back(std::move(zone));
  }

  return zones;
}

// ── Disk Summary ──

Result<DiskSummary> read_disk_summary() {
  auto content = read_file("/proc/diskstats");
  if (!content) return std::unexpected(Error{5, "Cannot read /proc/diskstats"});

  DiskSummary summary;
  std::istringstream stream(*content);
  std::string line;

  while (std::getline(stream, line)) {
    int major = 0, minor = 0;
    char dev_name[64] = {};
    uint64_t reads = 0, read_merged = 0, read_sectors = 0, read_time = 0;
    uint64_t writes = 0, write_merged = 0, write_sectors = 0, write_time = 0;
    uint64_t io_in_progress = 0, io_time = 0, weighted_io_time = 0;

    int parsed = std::sscanf(line.c_str(),
      "%d %d %63s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
      &major, &minor, dev_name,
      &reads, &read_merged, &read_sectors, &read_time,
      &writes, &write_merged, &write_sectors, &write_time,
      &io_in_progress, &io_time, &weighted_io_time);

    if (parsed < 14) continue;

    std::string dev(dev_name);

    // Skip virtual devices
    if (dev.starts_with("ram") || dev.starts_with("loop") || dev.starts_with("dm-"))
      continue;

    // Skip partitions (only count whole devices - ending with a letter typically)
    bool is_partition = false;
    for (char c : dev) {
      if (std::isdigit(c)) { is_partition = true; break; }
    }
    if (is_partition) continue;

    summary.total_reads += reads;
    summary.total_writes += writes;
    summary.total_read_sectors += read_sectors;
    summary.total_write_sectors += write_sectors;
    summary.total_read_time_ms += read_time;
    summary.total_write_time_ms += write_time;
    summary.device_count++;
  }

  return summary;
}

// ── Build full report ──

SystemReport build_report(bool include_cpu, bool include_memory, bool include_uptime,
                          bool include_load, bool include_kernel, bool include_thermal,
                          bool include_disk) {
  SystemReport report;

  if (include_cpu) {
    auto cpu = read_cpu_info();
    if (cpu) report.cpu = *cpu;
    else spdlog::warn("CPU info: {}", cpu.error().message);
  }

  if (include_memory) {
    auto mem = read_memory_info();
    if (mem) report.memory = *mem;
    else spdlog::warn("Memory info: {}", mem.error().message);
  }

  if (include_uptime) {
    auto uptime = read_uptime_info();
    if (uptime) report.uptime = *uptime;
    else spdlog::warn("Uptime info: {}", uptime.error().message);
  }

  if (include_load) {
    auto load = read_load_info();
    if (load) report.load = *load;
    else spdlog::warn("Load info: {}", load.error().message);
  }

  if (include_kernel) {
    auto kernel = read_kernel_info();
    if (kernel) report.kernel = *kernel;
    else spdlog::warn("Kernel info: {}", kernel.error().message);
  }

  if (include_thermal) {
    auto thermal = read_thermal_zones();
    if (thermal) report.thermal_zones = *thermal;
    else spdlog::warn("Thermal zones: {}", thermal.error().message);
  }

  if (include_disk) {
    auto disk = read_disk_summary();
    if (disk) report.disk = *disk;
    else spdlog::warn("Disk summary: {}", disk.error().message);
  }

  return report;
}

// ── JSON formatter ──

json report_to_json(const SystemReport& report) {
  json j;

  // CPU
  j["cpu"] = {
    {"model_name", report.cpu.model_name},
    {"core_count", report.cpu.core_count},
    {"thread_count", report.cpu.thread_count},
    {"physical_packages", report.cpu.physical_packages},
    {"architecture", report.cpu.architecture}
  };

  // Memory
  auto fmt_kb = [](uint64_t kb) -> json {
    return {{"kb", kb}, {"mb", static_cast<double>(kb) / 1024.0},
            {"gb", static_cast<double>(kb) / 1048576.0}};
  };
  j["memory"] = {
    {"total", fmt_kb(report.memory.total_kb)},
    {"free", fmt_kb(report.memory.free_kb)},
    {"available", fmt_kb(report.memory.available_kb)},
    {"used", fmt_kb(report.memory.total_kb > report.memory.available_kb ?
                     report.memory.total_kb - report.memory.available_kb : 0)},
    {"buffers", fmt_kb(report.memory.buffers_kb)},
    {"cached", fmt_kb(report.memory.cached_kb)},
    {"swap_total", fmt_kb(report.memory.swap_total_kb)},
    {"swap_free", fmt_kb(report.memory.swap_free_kb)}
  };

  // Uptime
  auto fmt_duration = [](double seconds) -> std::string {
    int d = static_cast<int>(seconds) / 86400;
    int h = (static_cast<int>(seconds) % 86400) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%dd %02d:%02d:%02d", d, h, m, s);
    return buf;
  };

  j["uptime"] = {
    {"uptime_seconds", report.uptime.uptime_seconds},
    {"uptime_human", fmt_duration(report.uptime.uptime_seconds)},
    {"idle_seconds", report.uptime.idle_seconds},
    {"boot_time_epoch", report.uptime.boot_time_epoch}
  };

  // Load
  j["load"] = {
    {"load_1m", report.load.load_1},
    {"load_5m", report.load.load_5},
    {"load_15m", report.load.load_15},
    {"running_processes", report.load.running},
    {"total_processes", report.load.total_threads},
    {"last_pid", report.load.last_pid}
  };

  // Kernel
  j["kernel"] = {
    {"hostname", report.kernel.hostname},
    {"os_release", report.kernel.os_release},
    {"version", report.kernel.version},
    {"machine", report.kernel.machine}
  };

  // Thermal
  json thermal_array = json::array();
  for (const auto& z : report.thermal_zones) {
    thermal_array.push_back({
      {"name", z.name},
      {"type", z.type},
      {"temperature_celsius", z.temp_celsius}
    });
  }
  j["thermal_zones"] = thermal_array;

  // Disk
  j["disk"] = {
    {"total_reads", report.disk.total_reads},
    {"total_writes", report.disk.total_writes},
    {"total_read_sectors", report.disk.total_read_sectors},
    {"total_write_sectors", report.disk.total_write_sectors},
    {"total_read_time_ms", report.disk.total_read_time_ms},
    {"total_write_time_ms", report.disk.total_write_time_ms},
    {"device_count", report.disk.device_count}
  };

  return j;
}

// ── Text formatter ──

static std::string fmt_bytes(uint64_t bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
  return buf;
}

static std::string fmt_duration(double seconds) {
  int d = static_cast<int>(seconds) / 86400;
  int h = (static_cast<int>(seconds) % 86400) / 3600;
  int m = (static_cast<int>(seconds) % 3600) / 60;
  int s = static_cast<int>(seconds) % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%dd %02d:%02d:%02d", d, h, m, s);
  return buf;
}

void print_text_report(const SystemReport& report) {
  // Header
  std::println(std::cout, "{:=^60}", "");
  std::println(std::cout, "{:^60}", " SYSTEM REPORT ");
  std::println(std::cout, "{:=^60}", "");

  // CPU
  if (!report.cpu.model_name.empty()) {
    std::println(std::cout, "");
    std::println(std::cout, "{}", "── CPU ──────────────────────────────────────────");
    std::println(std::cout, "  Model:       {}", report.cpu.model_name);
    std::println(std::cout, "  Cores:       {} physical, {} logical",
                 report.cpu.core_count, report.cpu.thread_count);
    if (report.cpu.physical_packages > 0)
      std::println(std::cout, "  Packages:    {}", report.cpu.physical_packages);
    std::println(std::cout, "  Arch:        {}", report.cpu.architecture);
  }

  // Memory
  if (report.memory.total_kb > 0) {
    std::println(std::cout, "");
    std::println(std::cout, "{}", "── MEMORY ───────────────────────────────────────");
    uint64_t used_kb = report.memory.total_kb - report.memory.available_kb;
    double used_pct = 100.0 * used_kb / report.memory.total_kb;

    std::println(std::cout, "  Total:       {} ({:.1f} GB)",
                 fmt_bytes(report.memory.total_kb * 1024),
                 static_cast<double>(report.memory.total_kb) / 1048576.0);
    std::println(std::cout, "  Used:        {} ({:.1f}%)",
                 fmt_bytes(used_kb * 1024), used_pct);
    std::println(std::cout, "  Available:   {}",
                 fmt_bytes(report.memory.available_kb * 1024));
    if (report.memory.swap_total_kb > 0) {
      std::println(std::cout, "  Swap Total:  {}",
                   fmt_bytes(report.memory.swap_total_kb * 1024));
      std::println(std::cout, "  Swap Free:   {}",
                   fmt_bytes(report.memory.swap_free_kb * 1024));
    }
  }

  // Uptime
  if (report.uptime.uptime_seconds > 0) {
    std::println(std::cout, "");
    std::println(std::cout, "{}", "── UPTIME ───────────────────────────────────────");
    std::println(std::cout, "  Uptime:      {}", fmt_duration(report.uptime.uptime_seconds));
    std::println(std::cout, "  Idle Time:   {}", fmt_duration(report.uptime.idle_seconds));
    if (report.uptime.boot_time_epoch > 0) {
      std::time_t boot = static_cast<std::time_t>(report.uptime.boot_time_epoch);
      char time_buf[64];
      std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&boot));
      std::println(std::cout, "  Boot Time:   {}", time_buf);
    }
  }

  // Load
  if (report.load.load_1 > 0 || report.load.total_threads > 0) {
    std::println(std::cout, "");
    std::println(std::cout, "{}", "── LOAD ─────────────────────────────────────────");
    std::println(std::cout, "  Load Avg:    {:.2f}, {:.2f}, {:.2f}",
                 report.load.load_1, report.load.load_5, report.load.load_15);
    std::println(std::cout, "  Processes:   {} running / {} total",
                 report.load.running, report.load.total_threads);
    std::println(std::cout, "  Last PID:    {}", report.load.last_pid);
  }

  // Kernel
  if (!report.kernel.hostname.empty()) {
    std::println(std::cout, "");
    std::println(std::cout, "{}", "── SYSTEM ───────────────────────────────────────");
    std::println(std::cout, "  Hostname:    {}", report.kernel.hostname);
    std::println(std::cout, "  OS:          {}", report.kernel.os_release);
    std::println(std::cout, "  Machine:     {}", report.kernel.machine);
    // Kernel version (first line only)
    auto ver = report.kernel.version;
    auto newline_pos = ver.find('\n');
    if (newline_pos != std::string::npos) ver = ver.substr(0, newline_pos);
    std::println(std::cout, "  Kernel:      {}", ver);
  }

  // Thermal
  if (!report.thermal_zones.empty()) {
    std::println(std::cout, "");
    std::println(std::cout, "{}", "── THERMAL ──────────────────────────────────────");
    for (const auto& z : report.thermal_zones) {
      std::println(std::cout, "  {:<12} {:>6.1f}°C  ({})",
                   z.type, z.temp_celsius, z.name);
    }
  }

  // Disk
  if (report.disk.device_count > 0) {
    std::println(std::cout, "");
    std::println(std::cout, "{}", "── DISK I/O ─────────────────────────────────────");
    std::println(std::cout, "  Devices:     {}", report.disk.device_count);
    std::println(std::cout, "  Total Reads: {} ({} sectors, {} ms)",
                 report.disk.total_reads,
                 report.disk.total_read_sectors,
                 report.disk.total_read_time_ms);
    std::println(std::cout, "  Total Writes: {} ({} sectors, {} ms)",
                 report.disk.total_writes,
                 report.disk.total_write_sectors,
                 report.disk.total_write_time_ms);
  }

  std::println(std::cout, "");
  std::println(std::cout, "{:=^60}", "");
}

} // namespace sysreport
