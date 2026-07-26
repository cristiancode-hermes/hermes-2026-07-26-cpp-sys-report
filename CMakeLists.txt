cmake_minimum_required(VERSION 3.28)
project(cpp-sys-report VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──
include(FetchContent)

# spdlog (header-only logging)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.1
)
FetchContent_MakeAvailable(spdlog)

# CLI11 (argument parsing)
FetchContent_Declare(CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.5.0
)
FetchContent_MakeAvailable(CLI11)

# nlohmann_json (JSON output)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Library (all sources except main.cpp) ──
file(GLOB_RECURSE LIB_SRCS src/*.cpp)
list(FILTER LIB_SRCS EXCLUDE REGEX "main\\.cpp$")
file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)

add_library(sysreport-lib STATIC ${LIB_SRCS} ${HEADERS})
target_include_directories(sysreport-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sysreport-lib PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json)

# ── Executable (entry point only) ──
add_executable(sysreport src/main.cpp)
target_link_libraries(sysreport PRIVATE sysreport-lib CLI11::CLI11)

# ── Tests ──
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
  )
  FetchContent_MakeAvailable(Catch2)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Install ──
install(TARGETS sysreport DESTINATION bin)
