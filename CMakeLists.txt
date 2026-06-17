cmake_minimum_required(VERSION 3.28)
project(iris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(DEFINED ENV{JAVA_HOME})
    set(JAVA_HOME                "$ENV{JAVA_HOME}")
    set(JAVA_JVM_LIBRARY         "$ENV{JAVA_HOME}/lib/openjdk/lib/server/libjvm.so")
    set(JAVA_AWT_LIBRARY         "$ENV{JAVA_HOME}/lib/openjdk/lib/server/libjvm.so")
    set(JAVA_INCLUDE_PATH        "$ENV{JAVA_HOME}/include")
    set(JAVA_INCLUDE_PATH2       "$ENV{JAVA_HOME}/include/linux")
    set(JAVA_AWT_INCLUDE_PATH    "$ENV{JAVA_HOME}/include")
endif()
find_package(JNI REQUIRED)
find_package(GTest REQUIRED)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(iris SHARED
    src/registry.cpp
    src/backend/java/java_backend.cpp
    src/os/ls.cpp
    src/os/ps.cpp
    src/os/env.cpp
)

target_include_directories(iris PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/iris>
    ${JNI_INCLUDE_DIRS}
)

target_link_libraries(iris PRIVATE ${JNI_LIBRARIES})

target_compile_options(iris PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(iris_tests
    tests/test_registry.cpp
    tests/test_java_backend.cpp
    tests/test_os.cpp
    tests/test_channel.cpp
)

target_include_directories(iris_tests PRIVATE
    include
    src
    ${CMAKE_SOURCE_DIR}
    ${JNI_INCLUDE_DIRS}
)

target_link_libraries(iris_tests
    iris
    GTest::gtest_main
    ${JNI_LIBRARIES}
)

include(GoogleTest)
gtest_discover_tests(iris_tests)

# ---------------------------------------------------------------------------
# Inspection tests (separate binary — freeze() is irreversible per-process)
# ---------------------------------------------------------------------------
add_executable(iris_inspection_tests
    tests/test_inspection.cpp
)

target_include_directories(iris_inspection_tests PRIVATE
    include
    src
    ${CMAKE_SOURCE_DIR}
    ${JNI_INCLUDE_DIRS}
)

target_link_libraries(iris_inspection_tests
    iris
    GTest::gtest_main
    ${JNI_LIBRARIES}
)

gtest_discover_tests(iris_inspection_tests)

# ---------------------------------------------------------------------------
# Examples (optional)
# ---------------------------------------------------------------------------
option(IRIS_BUILD_EXAMPLES "Build examples" OFF)
if(IRIS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS iris
    EXPORT IrisTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iris)
install(DIRECTORY sdk/     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iris/sdk
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(EXPORT IrisTargets
    FILE      IrisTargets.cmake
    NAMESPACE iris::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iris
)

configure_package_config_file(
    cmake/IrisConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/IrisConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iris
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/IrisConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iris
)
