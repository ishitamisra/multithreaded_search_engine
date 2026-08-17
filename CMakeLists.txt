cmake_minimum_required(VERSION 3.16)
project(multithreaded_search_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(Threads REQUIRED)
find_package(CURL QUIET)

if(CURL_FOUND)
  message(STATUS "libcurl found: crawler will support http/https fetching")
else()
  message(WARNING "libcurl NOT found: crawler will build without network fetch support (local-file mode only). Install libcurl4-openssl-dev to enable it.")
endif()

file(GLOB ENGINE_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_library(searchengine STATIC ${ENGINE_SOURCES})
target_include_directories(searchengine PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(searchengine PUBLIC Threads::Threads)

if(CURL_FOUND)
  target_compile_definitions(searchengine PUBLIC ENGINE_HAVE_CURL=1)
  target_link_libraries(searchengine PUBLIC CURL::libcurl)
endif()

# ---- CLI tools ----
add_executable(build_index tools/build_index_main.cpp)
target_link_libraries(build_index PRIVATE searchengine)

add_executable(crawl tools/crawl_main.cpp)
target_link_libraries(crawl PRIVATE searchengine)

add_executable(query tools/query_main.cpp)
target_link_libraries(query PRIVATE searchengine)

add_executable(serve tools/serve_main.cpp)
target_link_libraries(serve PRIVATE searchengine)

add_executable(pagerank_tool tools/pagerank_main.cpp)
target_link_libraries(pagerank_tool PRIVATE searchengine)

# ---- Tests ----
enable_testing()
file(GLOB TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/test_*.cpp)
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE searchengine)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
