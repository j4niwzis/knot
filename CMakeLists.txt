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
  GIT_TAG eb07788f6751dad1636aec9fd6bb134fe03a953f
  LICENSE GPL-3.0-only
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

# ---- What this library is, said by the library; and installing it ----
#
# A consumer writes find_package(knot) and this says the rest: where it
# comes from, what it needs, and what it produces -- installed beside the
# library by cmake-everywhere, under share/cmake-everywhere/ports, so that a
# prefix that has knot describes it too.
if(COMMAND cme_declare_port)
  cme_declare_port(
    NAME knot
    PROVIDES knot
    VERSION ${PROJECT_VERSION}
    GITHUB_REPOSITORY j4niwzis/knot
    GIT_TAG main
    LICENSE AGPL-3.0-only
    DEPENDS splice "boost-pfr[modules]"
    TARGETS knot::knot)
endif()
# The library, its module interface units -- a consumer compiles its own
# binary interfaces from them -- and a package: find_package(knot) on a
# machine that has it installed finds it there, by the names it has here
# too, and what it needs again from the ports it came from.
option(KNOT_INSTALL "Install knot and its CMake package" ${PROJECT_IS_TOP_LEVEL})
if(KNOT_INSTALL)
  set(knot_aliases "")
  foreach(name IN ITEMS schema format value read write)
    list(APPEND knot_aliases knot::${name} knot_${name})
  endforeach()
  cme_install_package(knot ALIASES ${knot_aliases})
endif()
