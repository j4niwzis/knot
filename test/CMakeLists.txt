# The tests: googletest's, as a module, from cmake-everywhere.
#
# One executable a test case, so that a case compiles only what it uses:
#   format/  what the generator writes, as text
#   read/    documents read, and every way of not being canonical refused
#   scan/    scan alone, on formats of the kind knot.format writes; off unless
#            KNOT_SCAN_DIR says where a checkout of scan is
cme_declare_port(NAME googletest-modules PROVIDES GTest
  GITHUB_REPOSITORY j4niwzis/googletest-modules
  GIT_TAG 706920f1d230f0316d271466d8885735be3e300e
  LICENSE BSD-3-Clause
  SYSTEM NEVER
  TARGETS gtest_main)
cme_options(googletest-modules
  "BUILD_GMOCK OFF" "INSTALL_GTEST OFF" "gtest_build_modules ON"
  "gtest_build_tests OFF" "gtest_build_samples OFF")
cme_find_package(GTest REQUIRED)
if(TARGET GTest::gtest_main)
  set(knot_gtest GTest::gtest_main)
else()
  set(knot_gtest gtest_main)
endif()

function(knot_tests directory)
  file(GLOB sources CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/${directory}/*.cc)
  foreach(source IN LISTS sources)
    get_filename_component(name ${source} NAME_WE)
    set(target knot-${directory}-${name})
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${ARGN} ${knot_gtest})
    add_test(NAME ${directory}/${name} COMMAND ${target})
  endforeach()
endfunction()

knot_tests(format knot_format)
if(KNOT_SCAN_DIR)
  add_subdirectory(${KNOT_SCAN_DIR} ${CMAKE_BINARY_DIR}/_scan EXCLUDE_FROM_ALL)
  knot_tests(scan scan::scan)
endif()
knot_tests(read knot)
