cmake_minimum_required(VERSION 3.31)
project(xml_to_c LANGUAGES CXX)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UBSan" OFF)
option(ENABLE_GLIBCXX_DEBUG "Enable libstdc++ debug iterators" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Global warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnull-dereference)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-fno-omit-frame-pointer)
  if (ENABLE_ASAN)
    add_link_options(-fsanitize=address)
    add_compile_options(-fsanitize=address)
  endif()
  if (ENABLE_UBSAN)
    add_link_options(-fsanitize=undefined)
    add_compile_options(-fsanitize=undefined)
  endif()
  if (ENABLE_GLIBCXX_DEBUG AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_definitions(_GLIBCXX_ASSERTIONS)
  endif()
endif()

# Library target (header-only pattern)
add_library(xml_to_c INTERFACE)
target_include_directories(xml_to_c INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# -------- pugixml configuration ------------------
option(XML_TO_C_USE_VENDORED_PUGIXML "Use vendored pugixml" ON)
set(XML_TO_C_PUGIXML_PATH "" CACHE STRING "Custom path to pugixml headers (e.g., /usr/include)")

if(XML_TO_C_PUGIXML_PATH)
    # User-specified custom path
    target_include_directories(xml_to_c INTERFACE ${XML_TO_C_PUGIXML_PATH})
    message(STATUS "xml_to_c: Using pugixml from custom path: ${XML_TO_C_PUGIXML_PATH}")
elseif(XML_TO_C_USE_VENDORED_PUGIXML)
    # Vendored pugixml (default)
    add_library(pugixml_vendored STATIC third_party/pugixml/pugixml.cpp)
    target_include_directories(pugixml_vendored PUBLIC third_party/pugixml)
    target_link_libraries(xml_to_c INTERFACE pugixml_vendored)
    message(STATUS "xml_to_c: Using vendored pugixml")
else()
    # User's project must provide pugixml (via find_package, FetchContent, or manual include path)
    message(STATUS "xml_to_c: Assuming pugixml is provided by parent project")
endif()

# -------- GoogleTest via FetchContent ----------
include(FetchContent)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
# For sanitizer builds, turn off gtest internal sanitizers
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# -------- CLI ----------
add_executable(xml2c src/main.cpp)
target_link_libraries(xml2c PRIVATE xml_to_c)

# -------- Tests ----------
add_executable(tests tests/test_xml_to_c.cpp)
target_link_libraries(tests PRIVATE xml_to_c GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(tests)
