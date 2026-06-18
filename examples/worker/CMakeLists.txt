# Reference worker — MODULE so it has no SONAME and is dlopen-only
add_library(iris_worker MODULE worker.cpp)
target_include_directories(iris_worker PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(iris_worker PRIVATE iris)
set_target_properties(iris_worker PROPERTIES
    PREFIX ""           # output: libiris_worker.so (not liblibiris_worker.so)
    OUTPUT_NAME "libiris_worker"
)

# Host executable that dlopen()s the worker
add_executable(iris_worker_host host.cpp)
target_include_directories(iris_worker_host PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/sdk
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(iris_worker_host PRIVATE iris ${CMAKE_DL_LIBS})
add_dependencies(iris_worker_host iris_worker)

# End-to-end test
add_test(NAME WorkerEndToEnd
    COMMAND iris_worker_host ${CMAKE_CURRENT_BINARY_DIR}/libiris_worker.so
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
