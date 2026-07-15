# Benchmarks — Iris vs Protobuf vs FlatBuffers, plus Iris-specific micro-benchmarks.
#
# Configure with -DIRIS_BUILD_BENCHMARKS=ON from the root CMakeLists.txt.
# Each library case (Protobuf, FlatBuffers) is built only if its dependency
# is discoverable — you can run the Iris-only cases without the other two.
#
# Dependencies:
#   Google Benchmark (required)   — measurement harness
#   Protobuf         (optional)   — comparison case
#   FlatBuffers      (optional)   — comparison case
#
# Run:
#   cmake --build build --target iris_benchmarks
#   ./build/benchmarks/bench_iris            --benchmark_out=results/iris.json
#   ./build/benchmarks/bench_protobuf        --benchmark_out=results/protobuf.json
#   ./build/benchmarks/bench_flatbuffers     --benchmark_out=results/flatbuffers.json
#   ./build/benchmarks/bench_iris_specific   --benchmark_out=results/iris_specific.json

find_package(benchmark REQUIRED)

add_custom_target(iris_benchmarks)

# ── Iris (always built when benchmarks are enabled) ──────────────────────────
add_executable(bench_iris src/bench_iris.cpp)
target_link_libraries(bench_iris PRIVATE iris benchmark::benchmark)
target_include_directories(bench_iris PRIVATE ${CMAKE_SOURCE_DIR})
add_dependencies(iris_benchmarks bench_iris)

add_executable(bench_iris_specific src/bench_iris_specific.cpp)
target_link_libraries(bench_iris_specific PRIVATE iris benchmark::benchmark)
target_include_directories(bench_iris_specific PRIVATE ${CMAKE_SOURCE_DIR})
add_dependencies(iris_benchmarks bench_iris_specific)

# ── Protobuf (optional) ──────────────────────────────────────────────────────
find_package(Protobuf QUIET)
if(Protobuf_FOUND)
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS schemas/dir_entry.proto)
    add_executable(bench_protobuf src/bench_protobuf.cpp ${PROTO_SRCS})
    target_include_directories(bench_protobuf PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(bench_protobuf PRIVATE benchmark::benchmark ${Protobuf_LIBRARIES})
    add_dependencies(iris_benchmarks bench_protobuf)
    message(STATUS "benchmarks: Protobuf case ENABLED")
else()
    message(STATUS "benchmarks: Protobuf case DISABLED (find_package(Protobuf) failed)")
endif()

# ── FlatBuffers (optional) ───────────────────────────────────────────────────
find_package(Flatbuffers QUIET)
if(NOT Flatbuffers_FOUND)
    # Some distros ship it as `flatbuffers` lower-case.
    find_package(flatbuffers QUIET)
endif()

find_program(FLATC_EXECUTABLE flatc)

if(Flatbuffers_FOUND OR flatbuffers_FOUND)
    if(FLATC_EXECUTABLE)
        set(FBS_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/dir_entry_generated.h)
        add_custom_command(
            OUTPUT  ${FBS_GENERATED}
            COMMAND ${FLATC_EXECUTABLE} --cpp -o ${CMAKE_CURRENT_BINARY_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/schemas/dir_entry.fbs
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/schemas/dir_entry.fbs
            COMMENT "flatc: generating dir_entry_generated.h"
        )
        add_executable(bench_flatbuffers src/bench_flatbuffers.cpp ${FBS_GENERATED})
        target_include_directories(bench_flatbuffers PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        if(TARGET flatbuffers::flatbuffers)
            target_link_libraries(bench_flatbuffers PRIVATE benchmark::benchmark flatbuffers::flatbuffers)
        else()
            target_link_libraries(bench_flatbuffers PRIVATE benchmark::benchmark flatbuffers)
        endif()
        add_dependencies(iris_benchmarks bench_flatbuffers)
        message(STATUS "benchmarks: FlatBuffers case ENABLED")
    else()
        message(STATUS "benchmarks: FlatBuffers found but flatc not on PATH — skipping")
    endif()
else()
    message(STATUS "benchmarks: FlatBuffers case DISABLED (find_package(Flatbuffers) failed)")
endif()
