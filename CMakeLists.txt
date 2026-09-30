cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

# knot: Canonical JSON for Matrix, as C++26 modules -- a type's JSON read into
# the type by a reader derived from the type while the program is compiled.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 26)
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

# splice: the variant knot's values, tags and choices are held in, and the
# visiting. Built with knot's step count, so that they share one std.
set(SPLICE_CONSTEXPR_STEPS ${KNOT_CONSTEXPR_STEPS} CACHE STRING
  "clang's -fconstexpr-steps for splice, as knot sets it")
cme_declare_port(NAME splice PROVIDES splice
  GITHUB_REPOSITORY j4niwzis/splice
  GIT_TAG 39bc67a00185f488c989d3c18b98e5995e4dc8dd
  LICENSE GPL-3.0-only
  SYSTEM NEVER
  TARGETS splice)
cme_options(splice "SPLICE_TESTS OFF")
cme_find_package(splice REQUIRED)

# One library, libknot.a, with its modules in one file set: modules are what
# separates it, and archives did not need to as well. Reading and writing
# share the schema, the format and the value, so it has no components.
add_library(knot)
target_sources(knot PUBLIC FILE_SET CXX_MODULES BASE_DIRS src
  FILES src/schema.cc src/format.cc src/value.cc src/read.cc src/write.cc src/knot.cc)
target_compile_features(knot PUBLIC cxx_std_26)
target_link_libraries(knot PUBLIC Boost::pfr splice)
target_compile_options(knot PUBLIC
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${KNOT_CONSTEXPR_STEPS}>)

# The names the targets used to have, each the one library now.
add_library(knot::knot ALIAS knot)
foreach(name IN ITEMS schema format value read write)
  add_library(knot::${name} ALIAS knot)
  add_library(knot_${name} ALIAS knot)
endforeach()

if(KNOT_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
