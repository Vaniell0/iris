cmake_minimum_required(VERSION 3.28)
project(iris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest REQUIRED)

# ---------------------------------------------------------------------------
# Java backend — optional; when ON the JVM is managed by RuntimeManager.
# ---------------------------------------------------------------------------
option(IRIS_JAVA_BACKEND "Compile JavaBackend into libiris (requires JNI)" ON)

if(IRIS_JAVA_BACKEND)
    if(DEFINED ENV{JAVA_HOME})
        set(JAVA_HOME             "$ENV{JAVA_HOME}")
        set(JAVA_JVM_LIBRARY      "$ENV{JAVA_HOME}/lib/openjdk/lib/server/libjvm.so")
        set(JAVA_AWT_LIBRARY      "$ENV{JAVA_HOME}/lib/openjdk/lib/server/libjvm.so")
        set(JAVA_INCLUDE_PATH     "$ENV{JAVA_HOME}/include")
        set(JAVA_INCLUDE_PATH2    "$ENV{JAVA_HOME}/include/linux")
        set(JAVA_AWT_INCLUDE_PATH "$ENV{JAVA_HOME}/include")
    endif()
    find_package(JNI REQUIRED)
endif()

# ---------------------------------------------------------------------------
# OS backend — separate libirisos.so; core libiris has no OS dependency.
# ---------------------------------------------------------------------------
option(IRIS_OS_BACKEND "Build libirisos — OS backend (ls/ps/env, Linux)" ON)

# ---------------------------------------------------------------------------
# std::execution (P2300) sender adaptors — requires stdexec (nvidia/stdexec).
# ---------------------------------------------------------------------------
option(IRIS_STDEXEC "Enable std::execution (P2300) sender adaptors via stdexec" OFF)

if(IRIS_STDEXEC)
    find_package(stdexec CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Core libiris — registry, IPC channel, C ABI, optional Java
# ---------------------------------------------------------------------------
set(IRIS_SOURCES
    src/registry.cpp
    src/backend/ipc/ipc.cpp
    src/iris_backend_handle.cpp
)
if(IRIS_JAVA_BACKEND)
    list(APPEND IRIS_SOURCES
        src/backend/java/java_backend.cpp
    )
endif()

add_library(iris SHARED ${IRIS_SOURCES})

target_include_directories(iris PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/iris>
)
target_include_directories(iris PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

if(IRIS_JAVA_BACKEND)
    target_include_directories(iris PUBLIC ${JNI_INCLUDE_DIRS})
    target_link_libraries(iris PRIVATE ${JNI_LIBRARIES})
    target_compile_definitions(iris PUBLIC IRIS_HAS_JAVA=1)
endif()

if(IRIS_STDEXEC)
    target_link_libraries(iris PUBLIC STDEXEC::stdexec)
    target_compile_definitions(iris PUBLIC IRIS_STDEXEC=1)
endif()

target_compile_options(iris PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

# ---------------------------------------------------------------------------
# libirisos — OS backend as a separate shared library
# ---------------------------------------------------------------------------
if(IRIS_OS_BACKEND)
    add_library(irisos SHARED
        src/backend/os/ls.cpp
        src/backend/os/ps.cpp
        src/backend/os/env.cpp
    )
    target_include_directories(irisos PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    )
    target_link_libraries(irisos PUBLIC iris)
    # INTERFACE: propagates IRIS_HAS_OS=1 to all targets that link irisos
    target_compile_definitions(irisos INTERFACE IRIS_HAS_OS=1)
    target_compile_options(irisos PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(iris_core_tests
    tests/test_registry.cpp
    tests/test_channel.cpp
    tests/test_fn_backend.cpp
    tests/test_ipc_backend.cpp
)
if(IRIS_OS_BACKEND)
    target_sources(iris_core_tests PRIVATE tests/test_os.cpp)
    target_link_libraries(iris_core_tests irisos)
endif()
target_include_directories(iris_core_tests PRIVATE
    include src ${CMAKE_SOURCE_DIR}
)
target_link_libraries(iris_core_tests iris GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(iris_core_tests)

if(IRIS_JAVA_BACKEND)
    add_executable(iris_java_tests
        tests/test_java_backend.cpp
        tests/test_runtime_manager.cpp
        tests/test_iris_handle.cpp
    )
    target_include_directories(iris_java_tests PRIVATE
        include src ${CMAKE_SOURCE_DIR} ${JNI_INCLUDE_DIRS}
    )
    target_link_libraries(iris_java_tests iris GTest::gtest_main ${JNI_LIBRARIES})
    if(IRIS_OS_BACKEND)
        target_link_libraries(iris_java_tests irisos)
    endif()
    gtest_discover_tests(iris_java_tests)

    # Inspection tests — separate process because freeze() is irreversible
    add_executable(iris_inspection_tests tests/test_inspection.cpp)
    target_include_directories(iris_inspection_tests PRIVATE
        include src ${CMAKE_SOURCE_DIR} ${JNI_INCLUDE_DIRS}
    )
    target_link_libraries(iris_inspection_tests iris GTest::gtest_main ${JNI_LIBRARIES})
    gtest_discover_tests(iris_inspection_tests)
endif()

if(IRIS_STDEXEC)
    add_executable(iris_exec_tests tests/test_execution.cpp)
    # Disable C++ module scanning — stdexec triggers a GCC ICE under -fmodules.
    set_target_properties(iris_exec_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_include_directories(iris_exec_tests PRIVATE include src ${CMAKE_SOURCE_DIR})
    target_link_libraries(iris_exec_tests iris GTest::gtest_main)
    # PRE_TEST: defer binary execution to ctest to avoid libstdc++ ABI issues in devShell.
    gtest_discover_tests(iris_exec_tests DISCOVERY_MODE PRE_TEST)
endif()

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
if(IRIS_OS_BACKEND)
    install(TARGETS irisos
        EXPORT IrisTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

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
