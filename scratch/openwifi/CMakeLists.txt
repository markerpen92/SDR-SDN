include(FindPkgConfig)

if(PKG_CONFIG_FOUND)
  pkg_check_modules(LIBNL libnl-3.0 REQUIRED)
  pkg_check_modules(LIBNL_GENL libnl-genl-3.0 REQUIRED)
endif()


# if(PKG_CONFIG_FOUND AND LIBNL_FOUND)
#   include_directories(${LIBNL_INCLUDE_DIRS})
#   link_libraries(${LIBNL_LIBRARIES})
# endif()

if(PKG_CONFIG_FOUND AND LIBNL_GENL_FOUND)
  include_directories(${LIBNL_GENL_INCLUDE_DIRS})
  link_libraries(${LIBNL_GENL_LIBRARIES})
endif()

## raw socket helper
build_exec(
  EXECNAME raw-sock-creator-clone
  SOURCE_FILES raw-sock-creator.cc
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/openwifi
)

## program for testing if the include works
# build_exec(
#   EXECNAME test_include
#   SOURCE_FILES test_include.cc
#   LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
#   EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/openwifi
# )

## program for testing FdNetDevice send and parameter change
build_exec(
  EXECNAME fd-emu-send
  SOURCE_FILES fd-emu-send.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/openwifi
)

## program for two sta
build_exec(
  EXECNAME fd-emu-two-sta
  SOURCE_FILES fd-emu-two-sta.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/openwifi
)

## program for fowarding
build_exec(
  EXECNAME fd-emu-fwd
  SOURCE_FILES fd-emu-fwd.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/openwifi
)