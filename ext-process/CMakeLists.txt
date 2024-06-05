check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources    
        # No tests
        )
endif()

# Look for Boost availability
find_package(Boost REQUIRED COMPONENTS headers)   # asio is header-only library
include_directories(${Boost_INCLUDE_DIRS})

build_lib(
    LIBNAME ext-process
    SOURCE_FILES
      model/ext-process.cc
    HEADER_FILES
      model/ext-process.h
    LIBRARIES_TO_LINK
      ${libcore}
      Boost::headers    # Dependency: Boost::asio (header)
    TEST_SOURCES
      test/ext-process-test-suite.cc
    # ${examples_as_tests_sources}
)
