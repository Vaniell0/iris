cmake_minimum_required(VERSION 3.28)

# jni_bridge
add_executable(example_jni_bridge jni_bridge/main.cpp)
target_include_directories(example_jni_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${JNI_INCLUDE_DIRS}
)
target_link_libraries(example_jni_bridge PRIVATE iris ${JNI_LIBRARIES})

# os_pipeline
add_executable(example_os_pipeline os_pipeline/main.cpp)
target_include_directories(example_os_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(example_os_pipeline PRIVATE iris)
