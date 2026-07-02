cmake_minimum_required(VERSION 3.16)
project(snapvault_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_library(svcore_lib
  src/sha256.cpp
  src/chunker.cpp
  src/fs_util.cpp
  src/content_store.cpp
  src/manifest.cpp
  src/engine.cpp
)
target_include_directories(svcore_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(svcore src/main.cpp)
target_link_libraries(svcore PRIVATE svcore_lib)

enable_testing()

add_executable(test_hash_chunk tests/test_hash_chunk.cpp)
target_link_libraries(test_hash_chunk PRIVATE svcore_lib)
add_test(NAME hash_chunk COMMAND test_hash_chunk)

add_executable(test_snapshot tests/test_snapshot.cpp)
target_link_libraries(test_snapshot PRIVATE svcore_lib)
add_test(NAME snapshot COMMAND test_snapshot)

add_executable(test_gc tests/test_gc.cpp)
target_link_libraries(test_gc PRIVATE svcore_lib)
add_test(NAME gc COMMAND test_gc)

add_executable(test_diff tests/test_diff.cpp)
target_link_libraries(test_diff PRIVATE svcore_lib)
add_test(NAME diff COMMAND test_diff)

add_executable(test_retain tests/test_retain.cpp)
target_link_libraries(test_retain PRIVATE svcore_lib)
add_test(NAME retain COMMAND test_retain)

add_executable(test_pitr tests/test_pitr.cpp)
target_link_libraries(test_pitr PRIVATE svcore_lib)
add_test(NAME pitr COMMAND test_pitr)
