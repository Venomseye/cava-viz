cmake_minimum_required(VERSION 3.15)
project(cli_visualizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    set(PLATFORM "Windows")
elseif(APPLE)
    set(PLATFORM "macOS")
else()
    set(PLATFORM "Linux")
endif()

message(STATUS "Detected platform: ${PLATFORM}")

find_package(PkgConfig REQUIRED)

# FFTW3 (Required)
pkg_check_modules(FFTW3 REQUIRED fftw3)
message(STATUS "✓ FFTW3 found: ${FFTW3_LIBRARIES}")

# PulseAudio (Required)
pkg_check_modules(PULSEAUDIO REQUIRED libpulse-simple libpulse)
message(STATUS "✓ PulseAudio found: ${PULSEAUDIO_LIBRARIES}")

# ALSA (Optional - for volume control)
pkg_check_modules(ALSA QUIET alsa)
if(ALSA_FOUND)
    message(STATUS "✓ ALSA found via pkg-config")
    set(HAVE_ALSA TRUE)
else()
    # Try finding ALSA manually
    find_library(ASOUND_LIBRARY asound)
    find_path(ASOUND_INCLUDE_DIR alsa/asoundlib.h)
    if(ASOUND_LIBRARY AND ASOUND_INCLUDE_DIR)
        message(STATUS "✓ ALSA found manually: ${ASOUND_LIBRARY}")
        set(HAVE_ALSA TRUE)
        set(ALSA_LIBRARIES ${ASOUND_LIBRARY})
        set(ALSA_INCLUDE_DIRS ${ASOUND_INCLUDE_DIR})
    else()
        message(STATUS "⚠ ALSA not found - volume control disabled")
        set(HAVE_ALSA FALSE)
    endif()
endif()

# Curses (Required)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)
message(STATUS "✓ Curses found: ${CURSES_LIBRARIES}")

# Create executable
add_executable(viz visualizer.cpp)

# Include directories
target_include_directories(viz PRIVATE
    ${FFTW3_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${CURSES_INCLUDE_DIRS})

if(HAVE_ALSA)
    target_include_directories(viz PRIVATE ${ALSA_INCLUDE_DIRS})
endif()

# Link libraries
target_link_libraries(viz PRIVATE
    ${FFTW3_LIBRARIES}
    ${PULSEAUDIO_LIBRARIES}
    ${CURSES_LIBRARIES}
    m
    pthread)

if(HAVE_ALSA)
    target_link_libraries(viz PRIVATE ${ALSA_LIBRARIES})
endif()

# Compiler flags
if(MSVC)
    target_compile_options(viz PRIVATE /O3 /W4 /EHsc)
else()
    target_compile_options(viz PRIVATE
        -O2
        -std=c++17
        -Wall
        -Wextra
        -Werror=format-security)
endif()

# Preprocessor definitions
if(HAVE_ALSA)
    target_compile_definitions(viz PRIVATE HAVE_ALSA=1)
else()
    target_compile_definitions(viz PRIVATE HAVE_ALSA=0)
endif()

# Install
install(TARGETS viz RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Build Info
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════════════╗")
message(STATUS "║          Terminal Audio Visualizer - Build Config            ║")
message(STATUS "╠══════════════════════════════════════════════════════════════╣")
message(STATUS "║ Platform              : ${PLATFORM}")
message(STATUS "║")
message(STATUS "║ Required Dependencies:")
message(STATUS "║   ✓ FFTW3              : ${FFTW3_LIBRARIES}")
message(STATUS "║   ✓ PulseAudio         : ${PULSEAUDIO_LIBRARIES}")
message(STATUS "║   ✓ Curses             : ${CURSES_LIBRARIES}")
message(STATUS "║")
message(STATUS "║ Optional Dependencies:")
if(HAVE_ALSA)
message(STATUS "║   ✓ ALSA               : ${ALSA_LIBRARIES}")
else()
message(STATUS "║   ✗ ALSA               : Not found")
endif()
message(STATUS "║")
message(STATUS "╠══════════════════════════════════════════════════════════════╣")
message(STATUS "║ Build Commands:")
message(STATUS "║")
message(STATUS "║   mkdir build && cd build")
message(STATUS "║   cmake .. -DCMAKE_BUILD_TYPE=Release")
message(STATUS "║   cmake --build . -j$(nproc)")
message(STATUS "║   sudo cmake --install .")
message(STATUS "║   viz")
message(STATUS "║")
message(STATUS "╚══════════════════════════════════════════════════════════════╝")
message(STATUS "")
