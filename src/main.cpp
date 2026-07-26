#include <iostream>
#include <print>
#include <cstdio>
#include <string>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "sysreport/report.hpp"

using namespace sysreport;

int main(int argc, char** argv) {
  CLI::App app{"sysreport — Linux System Report Generator"};
  app.description("Read multiple /proc and /sys sources and produce a comprehensive system report");

  bool flag_cpu = false;
  bool flag_memory = false;
  bool flag_uptime = false;
  bool flag_load = false;
  bool flag_kernel = false;
  bool flag_thermal = false;
  bool flag_disk = false;
  bool flag_all = true;  // Default: all sections
  bool flag_json = false;

  app.add_flag("--cpu", flag_cpu, "Include CPU information");
  app.add_flag("--memory,-m", flag_memory, "Include memory information");
  app.add_flag("--uptime,-u", flag_uptime, "Include uptime information");
  app.add_flag("--load,-l", flag_load, "Include load average information");
  app.add_flag("--kernel,-k", flag_kernel, "Include kernel/hostname information");
  app.add_flag("--thermal,-t", flag_thermal, "Include thermal zone temperatures");
  app.add_flag("--disk,-d", flag_disk, "Include disk I/O summary");
  app.add_flag("--json,-j", flag_json, "Output in JSON format");

  CLI11_PARSE(app, argc, argv);

  // If any specific flags are set, disable --all for those categories
  bool has_specific = flag_cpu || flag_memory || flag_uptime || flag_load ||
                      flag_kernel || flag_thermal || flag_disk;
  if (has_specific) {
    flag_all = false;
  }

  bool include_cpu = flag_all || flag_cpu;
  bool include_memory = flag_all || flag_memory;
  bool include_uptime = flag_all || flag_uptime;
  bool include_load = flag_all || flag_load;
  bool include_kernel = flag_all || flag_kernel;
  bool include_thermal = flag_all || flag_thermal;
  bool include_disk = flag_all || flag_disk;

  spdlog::info("Gathering system report...");

  auto report = build_report(include_cpu, include_memory, include_uptime,
                             include_load, include_kernel, include_thermal,
                             include_disk);

  if (flag_json) {
    std::println(std::cout, "{}", report_to_json(report).dump(2));
  } else {
    print_text_report(report);
  }

  return 0;
}
