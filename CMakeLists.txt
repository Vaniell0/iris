cmake_minimum_required(VERSION 3.28)
project(iris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Tests — optional; when OFF GTest is not required (e.g. irish-only build).
# ---------------------------------------------------------------------------
option(IRIS_BUILD_TESTS "Build GTest test targets" ON)
if(IRIS_BUILD_TESTS)
    find_package(GTest REQUIRED)
    include(GoogleTest)
endif()

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
# std::meta (P2996) automatic type reflection — requires GCC 16+, C++26.
# libiris stays C++23; only the test target needs -freflection / C++26.
# ---------------------------------------------------------------------------
option(IRIS_STDMETA "Enable std::meta (P2996) automatic type reflection (GCC 16+)" OFF)

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

if(IRIS_STDMETA)
    # INTERFACE: propagates IRIS_STDMETA=1 to all consumers; libiris itself
    # is not recompiled — reflect.hpp is header-only user code.
    target_compile_definitions(iris INTERFACE IRIS_STDMETA=1)
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
# irish — interpreter binary (opt-in; depends on irisos)
# ---------------------------------------------------------------------------
option(IRIS_IRISH "Build the irish interpreter binary" OFF)

if(IRIS_IRISH)
    find_package(replxx QUIET HINTS
        "${replxx_ROOT}/share/cmake/replxx"
        "$ENV{replxx_ROOT}/share/cmake/replxx"
        "/nix/store" PATH_SUFFIXES "share/cmake/replxx")
    if(NOT replxx_FOUND)
        # fallback: scan nix store for replxx cmake config
        file(GLOB _replxx_hints "/nix/store/*replxx*/share/cmake/replxx")
        if(_replxx_hints)
            list(GET _replxx_hints 0 _replxx_dir)
            find_package(replxx QUIET HINTS "${_replxx_dir}")
        endif()
    endif()
    if(replxx_FOUND)
        set(IRIS_REPLXX ON)
        message(STATUS "replxx found — REPL will have history and highlighting")
    else()
        message(STATUS "replxx not found — REPL falls back to fgets")
    endif()
    set(IRISH_INC include src ${CMAKE_SOURCE_DIR})

    add_library(irish_lexer STATIC
        src/irish/lexer/lexer.cpp)
    target_include_directories(irish_lexer PUBLIC ${IRISH_INC})

    add_library(irish_parser STATIC
        src/irish/parser/parser.cpp)
    target_include_directories(irish_parser PUBLIC ${IRISH_INC})
    target_link_libraries(irish_parser PUBLIC irish_lexer)

    add_library(irish_session STATIC
        src/irish/session/session.cpp)
    target_include_directories(irish_session PUBLIC ${IRISH_INC})
    target_link_libraries(irish_session PUBLIC iris)

    add_library(irish_checker STATIC
        src/irish/checker/checker.cpp)
    target_include_directories(irish_checker PUBLIC ${IRISH_INC})
    target_link_libraries(irish_checker PUBLIC irish_parser irish_session iris)

    add_library(irish_eval STATIC src/irish/exec/eval.cpp)
    target_include_directories(irish_eval PUBLIC ${IRISH_INC})
    target_link_libraries(irish_eval PUBLIC iris irish_parser)

    add_library(irish_backends STATIC
        src/irish/backend/os_irsh.cpp
        src/irish/backend/base_irsh.cpp
        src/irish/backend/plugin_loader.cpp)
    target_include_directories(irish_backends PUBLIC ${IRISH_INC})
    target_link_libraries(irish_backends PUBLIC iris irish_checker irish_eval ${CMAKE_DL_LIBS})
    if(IRIS_OS_BACKEND)
        target_link_libraries(irish_backends PUBLIC irisos)
    endif()

    add_library(irish_exec STATIC src/irish/exec/executor.cpp)
    target_include_directories(irish_exec PUBLIC ${IRISH_INC})
    target_link_libraries(irish_exec PUBLIC irish_checker irish_session iris irish_backends irish_eval)
    if(IRIS_OS_BACKEND)
        target_link_libraries(irish_exec PUBLIC irisos)
    endif()

    add_executable(irish src/irish/main.cpp)
    target_include_directories(irish PRIVATE ${IRISH_INC})
    target_link_libraries(irish PRIVATE
        irish_exec irish_backends irish_checker irish_parser irish_session irish_lexer iris)
    if(IRIS_OS_BACKEND)
        target_link_libraries(irish PRIVATE irisos)
    endif()
    if(IRIS_REPLXX)
        target_link_libraries(irish PRIVATE replxx::replxx)
        target_compile_definitions(irish PRIVATE IRIS_HAS_REPLXX=1)
    endif()

    if(IRIS_BUILD_TESTS)
        # Per-module tests
        add_executable(iris_lexer_tests  tests/test_lexer.cpp)
        add_executable(iris_parser_tests tests/test_parser.cpp)
        add_executable(iris_checker_tests tests/test_checker.cpp)

        foreach(t iris_lexer_tests iris_parser_tests iris_checker_tests)
            target_include_directories(${t} PRIVATE ${IRISH_INC})
            target_link_libraries(${t} GTest::gtest_main)
        endforeach()
        target_link_libraries(iris_lexer_tests  irish_lexer)
        target_link_libraries(iris_parser_tests irish_parser)
        target_link_libraries(iris_checker_tests irish_checker irish_backends)
        if(IRIS_OS_BACKEND)
            target_link_libraries(iris_checker_tests irisos)
        endif()
        gtest_add_tests(TARGET iris_lexer_tests  SOURCES tests/test_lexer.cpp)
        gtest_add_tests(TARGET iris_parser_tests  SOURCES tests/test_parser.cpp)
        gtest_add_tests(TARGET iris_checker_tests SOURCES tests/test_checker.cpp)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

if(IRIS_BUILD_TESTS)
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
gtest_add_tests(TARGET iris_core_tests SOURCES
    tests/test_registry.cpp
    tests/test_channel.cpp
    tests/test_fn_backend.cpp
    tests/test_ipc_backend.cpp
)

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
    gtest_add_tests(TARGET iris_java_tests SOURCES
        tests/test_java_backend.cpp
        tests/test_runtime_manager.cpp
        tests/test_iris_handle.cpp
    )

    # Inspection tests — separate process because freeze() is irreversible
    add_executable(iris_inspection_tests tests/test_inspection.cpp)
    target_include_directories(iris_inspection_tests PRIVATE
        include src ${CMAKE_SOURCE_DIR} ${JNI_INCLUDE_DIRS}
    )
    target_link_libraries(iris_inspection_tests iris GTest::gtest_main ${JNI_LIBRARIES})
    gtest_add_tests(TARGET iris_inspection_tests SOURCES tests/test_inspection.cpp)
endif()

if(IRIS_STDMETA)
    add_executable(iris_meta_tests tests/test_reflection.cpp)
    set_target_properties(iris_meta_tests PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_SCAN_FOR_MODULES OFF
    )
    target_compile_options(iris_meta_tests PRIVATE -freflection)
    target_compile_definitions(iris_meta_tests PRIVATE IRIS_STDMETA=1)
    target_include_directories(iris_meta_tests PRIVATE include src ${CMAKE_SOURCE_DIR})
    target_link_libraries(iris_meta_tests iris GTest::gtest_main)
    gtest_add_tests(TARGET iris_meta_tests SOURCES tests/test_reflection.cpp)
endif()

if(IRIS_STDEXEC)
    add_executable(iris_exec_tests tests/test_execution.cpp)
    # Disable C++ module scanning — stdexec triggers a GCC ICE under -fmodules.
    set_target_properties(iris_exec_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    target_include_directories(iris_exec_tests PRIVATE include src ${CMAKE_SOURCE_DIR})
    target_link_libraries(iris_exec_tests iris GTest::gtest_main)
    gtest_add_tests(TARGET iris_exec_tests SOURCES tests/test_execution.cpp)
endif()
endif() # IRIS_BUILD_TESTS

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
