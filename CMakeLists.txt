cmake_minimum_required(VERSION 3.20)
project(BloomForge VERSION 3.0.0 LANGUAGES CXX)

option(BLOOMFORGE_BUILD_TESTS "Build tests" ON)
option(BLOOMFORGE_BUILD_BENCHMARKS "Build benchmarks" ON)
option(BLOOMFORGE_BUILD_EXAMPLES "Build examples" ON)
option(BLOOMFORGE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer (Clang/GCC)" OFF)

if(BLOOMFORGE_ENABLE_SANITIZERS AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(bloomforge src/bloom_filter.cpp)
add_library(BloomForge::bloomforge ALIAS bloomforge)
target_compile_features(bloomforge PUBLIC cxx_std_20)
target_include_directories(bloomforge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
if(MSVC)
  target_compile_options(bloomforge PRIVATE /W4 /permissive-)
else()
  target_compile_options(bloomforge PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(BLOOMFORGE_BUILD_TESTS)
  enable_testing()
  add_executable(bloomforge_tests tests/bloom_filter_tests.cpp)
  target_link_libraries(bloomforge_tests PRIVATE BloomForge::bloomforge)
  add_test(NAME bloomforge_tests COMMAND bloomforge_tests)
endif()

if(BLOOMFORGE_BUILD_BENCHMARKS)
  add_executable(bloomforge_benchmark benchmarks/benchmark.cpp)
  target_link_libraries(bloomforge_benchmark PRIVATE BloomForge::bloomforge)
endif()

if(BLOOMFORGE_BUILD_EXAMPLES)
  add_executable(bloomforge_example examples/basic_usage.cpp)
  target_link_libraries(bloomforge_example PRIVATE BloomForge::bloomforge)
endif()

include(GNUInstallDirs)
install(TARGETS bloomforge EXPORT BloomForgeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT BloomForgeTargets NAMESPACE BloomForge:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BloomForge)
