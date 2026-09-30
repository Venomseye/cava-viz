cmake_minimum_required(VERSION 3.16)
project(cava-viz VERSION 1.2.0 LANGUAGES CXX)

# ── Language standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# NOTE: CMAKE_CXX_FLAGS_RELEASE is deliberately NOT overridden here.  A plain
# set() shadows any -DCMAKE_CXX_FLAGS_RELEASE=... given on the command line
# (which is how the release workflow and distro packagers strip CPU-specific
# flags) and silently bakes -march=native into distributable binaries.
# The CMake default for GCC/Clang Release is already "-O3 -DNDEBUG".
option(NATIVE_ARCH
       "Tune for this machine's CPU (-march=native). NEVER use for packages/releases." OFF)

set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")

# ── Required dependencies ──────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Threads   REQUIRED)

pkg_check_modules(FFTW3   REQUIRED fftw3)
pkg_check_modules(NCURSES REQUIRED ncursesw)

# ── Optional audio backends ────────────────────────────────────────────────────
option(ENABLE_PULSEAUDIO "Enable PulseAudio backend" ON)
option(ENABLE_PIPEWIRE   "Enable PipeWire backend"   ON)

# Pre-initialise so the sanity-check below is correct even when a backend is
# explicitly disabled via -DENABLE_PULSEAUDIO=OFF / -DENABLE_PIPEWIRE=OFF.
set(PULSEAUDIO_FOUND FALSE)
set(PIPEWIRE_FOUND   FALSE)

# ── Core sources (always compiled) ────────────────────────────────────────────
set(CORE_SOURCES
    src/main.cpp
    src/fft_processor.cpp
    src/renderer.cpp
    src/config.cpp
    src/user_theme.cpp
    src/audio_utils.cpp
)

add_executable(viz ${CORE_SOURCES})

# Make the CMake project version available at compile time.
# main.cpp guards against the macro being absent with a fallback definition,
# so builds without CMake (e.g. a hand-written Makefile) still compile cleanly.
target_compile_definitions(viz PRIVATE
    CAVA_VIZ_VERSION="${PROJECT_VERSION}"
)

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

if(NATIVE_ARCH)
    target_compile_options(viz PRIVATE -march=native)
    message(STATUS "NATIVE_ARCH        : ON  (binary will NOT be portable)")
endif()

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

# Man page
install(FILES man/cava-viz.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# Shell completions
install(FILES completions/cava-viz.bash
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
    RENAME viz)
install(FILES completions/cava-viz.zsh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions
    RENAME _viz)
install(FILES completions/cava-viz.fish
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d
    RENAME viz.fish)

# Example user themes
install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/cava-viz/themes
    FILES_MATCHING PATTERN "*.theme")

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()

    # config round-trip tests
    add_executable(test_config
        tests/test_config.cpp
        src/config.cpp)
    target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(test_config PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(test_config PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME config COMMAND test_config)

    # user theme parser tests
    add_executable(test_user_theme
        tests/test_user_theme.cpp
        src/user_theme.cpp
        src/config.cpp)
    target_include_directories(test_user_theme PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(test_user_theme PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(test_user_theme PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME user_theme COMMAND test_user_theme)

    # FFT / smoothing regression tests (needs FFTW + threads, no audio device)
    add_executable(test_fft_processor
        tests/test_fft_processor.cpp
        src/fft_processor.cpp)
    target_include_directories(test_fft_processor PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${FFTW3_INCLUDE_DIRS})
    target_link_libraries(test_fft_processor PRIVATE
        ${FFTW3_LIBRARIES} Threads::Threads m)
    target_compile_options(test_fft_processor PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(test_fft_processor PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME fft_processor COMMAND test_fft_processor)
endif()

# ── Summary ────────────────────────────────────────────────────────────────────
message(STATUS "--------------------------------------------")
message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "Install dir : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Tests       : ${BUILD_TESTS}")
message(STATUS "--------------------------------------------")
