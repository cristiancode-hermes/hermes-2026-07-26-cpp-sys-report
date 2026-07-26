# System Report — sysreport

Generador de informes del sistema Linux en C++23 que lee múltiples fuentes de `/proc` y `/sys` y presenta un resumen completo del sistema.

## Características

- **Información CPU**: modelo, núcleos, arquitectura desde `/proc/cpuinfo`
- **Memoria**: total, usada, libre, buffers, caché desde `/proc/meminfo`
- **Uptime**: tiempo de actividad, tiempo idle, boot time desde `/proc/uptime` + `/proc/stat`
- **Carga del sistema**: load average, procesos activos/totales desde `/proc/loadavg`
- **Kernel**: versión, nombre del host, compilador desde `/proc/version` y `/proc/sys/kernel/hostname`
- **Térmico**: temperaturas de zonas térmicas desde `/sys/class/thermal/`
- **Disco**: I/O totals agregado desde `/proc/diskstats`
- **Salida en tabla legible o JSON** para integración con scripts
- **Selección por categorías**: flags independientes para cada sección

## Build

```bash
cmake -G "Unix Makefiles" -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Uso

```bash
# Reporte completo del sistema
./build/sysreport

# Solo CPU y memoria
./build/sysreport --cpu --memory

# Solo información térmica
./build/sysreport --thermal

# Salida JSON
./build/sysreport --json

# Selección múltiple
./build/sysreport --cpu --load --kernel --disk --json
```

## Tests

```bash
cd build && ctest --output-on-failure
```

## Tecnologías

- C++23
- CLI11 (argument parsing, FetchContent)
- nlohmann/json (salida JSON)
- spdlog (logging)
- Catch2 (tests, v3)
