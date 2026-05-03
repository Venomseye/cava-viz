cmake_minimum_required(VERSION 3.16)
project(cava-viz VERSION 1.1.0 LANGUAGES CXX)

# ── Language standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")

# ── Required dependencies ──────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Threads   REQUIRED)

pkg_check_modules(FFTW3   REQUIRED fftw3)
pkg_check_modules(NCURSES REQUIRED ncursesw)

# ── Optional audio backends ────────────────────────────────────────────────────
option(ENABLE_PULSEAUDIO "Enable PulseAudio backend" ON)
option(ENABLE_PIPEWIRE   "Enable PipeWire backend"   ON)

# ── Core sources (always compiled) ────────────────────────────────────────────
set(CORE_SOURCES
    src/main.cpp
    src/fft_processor.cpp
    src/renderer.cpp
    src/config.cpp
)

add_executable(viz ${CORE_SOURCES})

target_include_directories(viz PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FFTW3_INCLUDE_DIRS}
    ${NCURSES_INCLUDE_DIRS}
)

target_link_libraries(viz PRIVATE
    ${FFTW3_LIBRARIES}
    ${NCURSES_LIBRARIES}
    Threads::Threads
    m
)

target_compile_options(viz PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    ${FFTW3_CFLAGS_OTHER}
    ${NCURSES_CFLAGS_OTHER}
)

# ── PulseAudio backend ─────────────────────────────────────────────────────────
if(ENABLE_PULSEAUDIO)
    pkg_check_modules(PULSEAUDIO      IMPORTED_TARGET libpulse)
    pkg_check_modules(PULSEAUDIO_SIMPLE IMPORTED_TARGET libpulse-simple)
    if(PULSEAUDIO_FOUND AND PULSEAUDIO_SIMPLE_FOUND)
        target_sources(viz PRIVATE src/pulse_capture.cpp)
        target_compile_definitions(viz PRIVATE HAVE_PULSEAUDIO)
        # libpulse-simple contains pa_simple_new/read/free
        # libpulse contains pa_strerror and core types
        target_link_libraries(viz PRIVATE
            PkgConfig::PULSEAUDIO
            PkgConfig::PULSEAUDIO_SIMPLE
        )
        message(STATUS "PulseAudio backend : ENABLED  (${PULSEAUDIO_VERSION})")
    else()
        message(STATUS "PulseAudio backend : DISABLED (libpulse or libpulse-simple not found)")
    endif()
endif()

# ── PipeWire backend ───────────────────────────────────────────────────────────
if(ENABLE_PIPEWIRE)
    pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
    pkg_check_modules(SPA      IMPORTED_TARGET libspa-0.2)
    if(PIPEWIRE_FOUND AND SPA_FOUND)
        target_sources(viz PRIVATE src/pipewire_capture.cpp)
        target_compile_definitions(viz PRIVATE HAVE_PIPEWIRE)
        target_link_libraries(viz PRIVATE
            PkgConfig::PIPEWIRE
            PkgConfig::SPA
        )
        # Suppress -Wpedantic only for this file: SPA system headers use C99
        # compound literals inside macros which are valid C but not strict C++.
        set_source_files_properties(src/pipewire_capture.cpp PROPERTIES
            COMPILE_OPTIONS "-Wno-pedantic"
        )
        message(STATUS "PipeWire backend   : ENABLED  (${PIPEWIRE_VERSION})")
    else()
        message(STATUS "PipeWire backend   : DISABLED (libpipewire-0.3 or libspa-0.2 not found)")
    endif()
endif()

# ── Sanity-check: at least one backend must be available ──────────────────────
if(NOT PULSEAUDIO_FOUND AND NOT PIPEWIRE_FOUND)
    message(WARNING "No audio backend found. Install libpulse or libpipewire-0.3.")
endif()

# ── Install ────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS viz DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Summary ────────────────────────────────────────────────────────────────────
message(STATUS "--------------------------------------------")
message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "Install dir : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "--------------------------------------------")
