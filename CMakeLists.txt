cmake_minimum_required(VERSION 3.20)
project(wlclip-watch C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # so clangd / lsp pick up the build

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
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
    COMMAND ${WAYLAND_SCANNER} public-code ${PROTO_XML} ${PROTO_SOURCE}
    DEPENDS ${PROTO_XML}
    COMMENT "wayland-scanner: generating glue code for ${PROTO_NAME}"
    VERBATIM
)

add_executable(wlclip-watch
    src/main.cpp
    ${PROTO_SOURCE}
    ${PROTO_HEADER}
)
target_include_directories(wlclip-watch PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(wlclip-watch PRIVATE PkgConfig::WAYLAND)

include(GNUInstallDirs)
install(TARGETS wlclip-watch RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
