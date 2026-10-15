cmake_minimum_required(VERSION 3.20)

project(SwapDiffHive VERSION 1.0.1 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(SwapDiffHive src/FreeChain.cc)
add_library(SwapDiffHive::SwapDiffHive ALIAS SwapDiffHive)

target_include_directories(SwapDiffHive PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# the hive is a template: the consumer instantiates it, so the language level
# travels with the target instead of being set in this directory
target_compile_features(SwapDiffHive PUBLIC cxx_std_23)

# tests belong to this project's own build; a consumer that pulls the library in
# with add_subdirectory() or FetchContent gets the library only
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()

install(TARGETS SwapDiffHive
    EXPORT SwapDiffHiveTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT SwapDiffHiveTargets
    FILE SwapDiffHiveTargets.cmake
    NAMESPACE SwapDiffHive::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SwapDiffHive)

configure_package_config_file(cmake/SwapDiffHiveConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SwapDiffHiveConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SwapDiffHive)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SwapDiffHiveConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SwapDiffHiveConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/SwapDiffHiveConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SwapDiffHive)
