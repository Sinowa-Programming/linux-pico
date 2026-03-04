cmake_minimum_required(VERSION 3.12)
project(UsbHost LANGUAGES C)

# Use C++17
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find the PkgConfig tool
find_package(PkgConfig REQUIRED)

# 2. Use PkgConfig to find libusb-1.0
# This creates variables starting with 'LIBUSB' (e.g., LIBUSB_LIBRARIES, LIBUSB_INCLUDE_DIRS)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

file(GLOB_RECURSE C_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_executable(usb_host ${C_SOURCES})

# 3. Add libusb include directories so #include <libusb-1.0/libusb.h> works
target_include_directories(usb_host PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${LIBUSB_INCLUDE_DIRS}
)

# 4. Link the libusb library to your executable
target_link_libraries(usb_host PRIVATE
    ${LIBUSB_LIBRARIES}
)
