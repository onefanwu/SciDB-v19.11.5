include_directories("include")

set(scidbtestcommon_src
    src/helper.cpp # scidbtest, scidbtestharness
    src/errdb.cpp  # scidbtest, scidbtestharness
    src/Exceptions.cpp # scidbtest, scidbtestharness
    src/defaultexecutor.cpp # scidbtest, scidbtestharness
    src/executorfactory.cpp # scidbtest, scidbtestharness
    src/harnesstestexecutor.cpp # scidbtest, scidbtestharness

    src/reporter.cpp  #scidbtestharness, preparecdashreport_sources
    src/xmlarchive.cpp  #scidbtestharness, preparecdashreport_sources
    )
add_library(scidbtestcommon STATIC ${scidbtestcommon_src})

target_compile_definitions(scidbtestcommon PRIVATE "SCIDB_CLIENT")
target_link_libraries(scidbtestcommon PUBLIC Boost::boost)
set_source_files_properties(src/defaultexecutor.cpp
                            src/helper.cpp
                            src/reporter.cpp
  PROPERTIES
  COMPILE_FLAGS "-Wno-conversion"
  )

#_____________________________________________________________________________________________________________
# scidbtest #
#___________#
set(scidbtest_src
    src/scidbtestcaseexecutor.cpp
    src/scidbtest.cpp
)
add_executable(scidbtest ${scidbtest_src})
target_compile_definitions(scidbtest PRIVATE "SCIDB_CLIENT")
target_link_libraries(scidbtest
  PUBLIC Boost::filesystem
         Boost::program_options
         Boost::regex
         Boost::serialization
         Boost::system
         ${LOG4CXX_LIBRARIES}
         scidbtestcommon
         scidbclient
         ${LIBRT_LIBRARIES}
  )
if(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)
	set_target_properties(scidbtest PROPERTIES COMPILE_DEFINITIONS "BOOST_FS_VER=2")
else(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)
	set_target_properties(scidbtest PROPERTIES COMPILE_DEFINITIONS "BOOST_FS_VER=3")
endif(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)

#_____________________________________________________________________________________________________________
# scidbtestharness #
#__________________#

set(scidbtestharness_src
	src/harness.cpp
	src/manager.cpp
	src/suite.cpp
	src/main.cpp
)
set_source_files_properties(src/harness.cpp
                            src/manager.cpp
                            src/suite.cpp
  PROPERTIES
  COMPILE_FLAGS "-Wno-conversion"
  )

scidb_add_executable(scidbtestharness ${scidbtestharness_src})
scidb_install(TARGETS scidbtestharness
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT scidb-dev-tools
)

set_target_properties(scidbtestharness PROPERTIES
  COMPILE_FLAGS "-Wno-conversion"
  )
target_link_libraries(scidbtestharness
  PUBLIC scidbclient
         scidbtestcommon
         ${LIBRT_LIBRARIES}
  )

  target_link_libraries(scidbtestharness
    PUBLIC Boost::filesystem
           Boost::program_options
           Boost::regex
           Boost::serialization
           Boost::system
           Boost::thread
           ${LOG4CXX_LIBRARIES}
    )

target_compile_definitions(scidbtestharness PRIVATE "SCIDB_CLIENT")

if(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)
        set_target_properties(scidbtestharness PROPERTIES COMPILE_DEFINITIONS "BOOST_FS_VER=2")
else(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)
        set_target_properties(scidbtestharness PROPERTIES COMPILE_DEFINITIONS "BOOST_FS_VER=3")
endif(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)

install(PROGRAMS scidbtestprep.py
                 mu_admin.py
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT scidb-dev-tools
  )

install(FILES log4j.properties
        # DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}
        DESTINATION etc
        COMPONENT scidb-dev-tools
  )

#_____________________________________________________________________________________________________________
# CDASH report #
#______________#

set(preparecdashreport_sources
	src/cdashreportstructs.cpp
	src/cdashreportapp.cpp
	src/preparecdashreport.cpp
)

scidb_add_executable(preparecdashreport ${preparecdashreport_sources})
target_compile_definitions(preparecdashreport PRIVATE "SCIDB_CLIENT")
target_link_libraries(preparecdashreport
  PUBLIC scidbtestcommon
         Boost::filesystem
         Boost::program_options
         Boost::regex
         Boost::serialization
         Boost::system
         ${LOG4CXX_LIBRARIES}
         ${LIBRT_LIBRARIES}
         Threads::Threads
  )
if(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)
        set_target_properties(preparecdashreport PROPERTIES COMPILE_DEFINITIONS "BOOST_FS_VER=2")
else(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)
        set_target_properties(preparecdashreport PROPERTIES COMPILE_DEFINITIONS "BOOST_FS_VER=3")
endif(${Boost_MAJOR_VERSION} LESS 2 AND ${Boost_MINOR_VERSION} LESS 46)

#_____________________________________________________________________________________________________________
# misc #
#______#

set(arg_separator_sources
	src/arg_separator.cc
)

scidb_add_executable(arg_separator ${arg_separator_sources})

scidb_install(TARGETS arg_separator
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT scidb-dev-tools
)

set(DISABLE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/testcases/disable.tests")
