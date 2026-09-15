cmake_minimum_required(VERSION 3.16)

project(multipart
    VERSION 1.0.0
    DESCRIPTION "A small, self-contained C++17 multipart parser and serializer"
    HOMEPAGE_URL "https://github.com/wanghanqing/multipart"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MULTIPART_BUILD_TESTS     "Build the multipart test suite"          ON)
option(MULTIPART_BUILD_EXAMPLES   "Build the multipart examples"            ON)
option(MULTIPART_INSTALL          "Generate install target"                 ON)
option(MULTIPART_PEDANTIC         "Enable extra warnings"                   ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MULTIPART_IS_TOP_LEVEL ON)
else()
    set(MULTIPART_IS_TOP_LEVEL OFF)
endif()

# ---------------------------------------------------------------------------
# The header-only library target
# ---------------------------------------------------------------------------
add_library(multipart INTERFACE)
add_library(multipart::multipart ALIAS multipart)

target_include_directories(multipart INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(multipart INTERFACE cxx_std_17)

if(MULTIPART_PEDANTIC)
    target_compile_options(multipart INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>)
endif()

# ---------------------------------------------------------------------------
# Install rules (headers + CMake package config)
# ---------------------------------------------------------------------------
if(MULTIPART_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(TARGETS multipart EXPORT multipartTargets)
    install(EXPORT multipartTargets
        FILE multipartTargets.cmake
        NAMESPACE multipart::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/multipart)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/multipartConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/multipartConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/multipart)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/multipartConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/multipartConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/multipartConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/multipart)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/multipart.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/multipart.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/multipart.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

# ---------------------------------------------------------------------------
# Tests & examples
# ---------------------------------------------------------------------------
if(MULTIPART_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MULTIPART_BUILD_EXAMPLES AND MULTIPART_IS_TOP_LEVEL)
    add_subdirectory(examples)
endif()