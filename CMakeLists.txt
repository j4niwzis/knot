cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

# knot: Canonical JSON for Matrix, as C++23 modules -- a type's JSON read into
# the type by a reader derived from the type while the program is compiled.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

project(knot VERSION 0.1 LANGUAGES CXX)

option(KNOT_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
set(KNOT_SCAN_DIR "" CACHE PATH
  "A checkout of scan, for the tests of scan alone")

if(NOT COMMAND cme_find_package)
  set(CME_WITHOUT_PROVIDER ON)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_cme.cmake)
endif()

# Boost.PFR, as a module: the names and the members of an aggregate.
cme_find_package(Boost COMPONENTS pfr[modules] REQUIRED)

# The room for constant evaluation. Said as a usage requirement of every
# module, it also makes their settings differ from the std target's, so that
# they, what imports them and googletest share one std.
set(KNOT_CONSTEXPR_STEPS 300000000 CACHE STRING
  "clang's -fconstexpr-steps for knot and what imports it")

function(knot_module target file)
  add_library(${target})
  target_sources(${target}
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES ${file})
  target_compile_features(${target} PUBLIC cxx_std_23)
  target_link_libraries(${target} PUBLIC ${ARGN})
  target_compile_options(${target} PUBLIC
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${KNOT_CONSTEXPR_STEPS}>)
endfunction()

knot_module(knot_schema src/schema.cc Boost::pfr)
knot_module(knot_format src/format.cc knot_schema Boost::pfr)
knot_module(knot_value src/value.cc knot_schema)
knot_module(knot_read src/read.cc knot_format knot_value Boost::pfr)
knot_module(knot_write src/write.cc knot_format knot_value knot_read Boost::pfr)
knot_module(knot src/knot.cc knot_schema knot_format knot_value knot_read knot_write Boost::pfr)

add_library(knot::schema ALIAS knot_schema)
add_library(knot::format ALIAS knot_format)
add_library(knot::value ALIAS knot_value)
add_library(knot::read ALIAS knot_read)
add_library(knot::write ALIAS knot_write)
add_library(knot::knot ALIAS knot)

if(KNOT_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
