cmake_minimum_required(VERSION 3.20)
project(wlclip-watch C CXX)  # C is for the wayland-scanner output; CXX is main.cpp

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # so clangd / lsp pick up the build

# warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

# --- libwayland-client (we only need the client side) ----------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)

# --- wayland-scanner: generates C glue from protocol XML -------------------
# Most Wayland protocols (including ext-data-control) are defined in XML.
# `wayland-scanner` is a code generator that emits both a C header and a C
# implementation file from that XML. We compile both into our binary.
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# We use ext-data-control-v1: the official, cross-compositor clipboard-manager
# protocol. It replaces the older wlr-data-control-unstable-v1, which is now
# deprecated. The XML is vendored in protocols/ (copied from the system's
# wayland-protocols package, staging/ set) so the build is self-contained.
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/protocols)
set(PROTO_NAME ext-data-control-v1)
set(PROTO_XML ${PROTO_DIR}/${PROTO_NAME}.xml)
set(PROTO_HEADER ${CMAKE_BINARY_DIR}/${PROTO_NAME}-client-protocol.h)
set(PROTO_SOURCE ${CMAKE_BINARY_DIR}/${PROTO_NAME}-protocol.c)

add_custom_command(
    OUTPUT ${PROTO_HEADER}
    COMMAND ${WAYLAND_SCANNER} client-header ${PROTO_XML} ${PROTO_HEADER}
    DEPENDS ${PROTO_XML}
    COMMENT "wayland-scanner: generating client header for ${PROTO_NAME}"
    VERBATIM
)
add_custom_command(
    OUTPUT ${PROTO_SOURCE}
    # `public-code` exposes the wl_interface symbols (vs `private-code` which
    # makes them static). We need them visible because main.cpp references
    # them directly when calling wl_registry_bind / get_data_device.
    COMMAND ${WAYLAND_SCANNER} public-code ${PROTO_XML} ${PROTO_SOURCE}
    DEPENDS ${PROTO_XML}
    COMMENT "wayland-scanner: generating glue code for ${PROTO_NAME}"
    VERBATIM
)

# --- the binary ------------------------------------------------------------
add_executable(wlclip-watch
    src/main.cpp
    ${PROTO_SOURCE}
    ${PROTO_HEADER}
)
target_include_directories(wlclip-watch PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(wlclip-watch PRIVATE PkgConfig::WAYLAND)

# Optional install target so `cmake --install build` puts it in your PATH
include(GNUInstallDirs)
install(TARGETS wlclip-watch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
