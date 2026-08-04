# scratch/CMakeLists.txt
# Copy this entire file to: <ns-3-root>/scratch/CMakeLists.txt
# Then re-configure: ./ns3 configure && ./ns3 build

set(target_prefix scratch_)

function(create_scratch source_files)
  # Return early if no sources in the subdirectory
  list(LENGTH source_files number_sources)
  if(number_sources EQUAL 0)
    return()
  endif()

  # If the scratch has more than a source file, we need to find the source with
  # the main function
  set(scratch_src)
  foreach(source_file ${source_files})
    file(READ ${source_file} source_file_contents)
    string(REGEX MATCHALL "main[(| (]" main_position "${source_file_contents}")
    if(CMAKE_MATCH_0)
      set(scratch_src ${source_file})
    endif()
  endforeach()

  if(NOT scratch_src)
    return()
  endif()

  # Get parent directory name
  get_filename_component(scratch_dirname ${scratch_src} DIRECTORY)
  string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" scratch_dirname
                 "${scratch_dirname}"
  )
  string(REPLACE "/" "_" scratch_dirname "${scratch_dirname}")

  # Get source name
  get_filename_component(scratch_name ${scratch_src} NAME_WE)

  set(target_prefix scratch_)
  if(scratch_dirname)
    # Join the names together if dirname is not the scratch folder
    set(target_prefix scratch${scratch_dirname}_)
  endif()

  # Get source absolute path and transform into relative path
  get_filename_component(scratch_src ${scratch_src} ABSOLUTE)
  get_filename_component(scratch_absolute_directory ${scratch_src} DIRECTORY)
  string(REPLACE "${PROJECT_SOURCE_DIR}" "${CMAKE_OUTPUT_DIRECTORY}"
                 scratch_directory ${scratch_absolute_directory}
  )
  build_exec(
          EXECNAME ${scratch_name}
          EXECNAME_PREFIX ${target_prefix}
          SOURCE_FILES "${source_files}"
          LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
          EXECUTABLE_DIRECTORY_PATH ${scratch_directory}/
  )
endfunction()

# Scan *.cc files in scratch/ and build a target for each single-file scratch.
file(GLOB single_source_file_scratches CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# These link scratch/Rtp/ sources; build separately below (not as single-file scratches).
list(FILTER single_source_file_scratches EXCLUDE REGEX "mesh-emu-openwifi\\.cc$")
list(FILTER single_source_file_scratches EXCLUDE REGEX "mesh-emu-openwifi-rtp\\.cc$")
list(FILTER single_source_file_scratches EXCLUDE REGEX "mesh-emu-m13\\.cc$")
list(FILTER single_source_file_scratches EXCLUDE REGEX "mesh-emu-m13-OpenWifi\\.cc$")

foreach(scratch_src ${single_source_file_scratches})
  create_scratch(${scratch_src})
endforeach()

# mesh-emu-openwifi + custom RTP applications in scratch/Rtp/
set(mesh_emu_openwifi_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh-emu-openwifi.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-client.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-server.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-helper.cc
)
create_scratch("${mesh_emu_openwifi_sources}")
target_include_directories(scratch_mesh-emu-openwifi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Rtp)

# mesh-emu-openwifi-rtp + H.264 RTP helpers in scratch/Rtp/
set(mesh_emu_openwifi_rtp_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh-emu-openwifi-rtp.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-h264-client.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-h264-helper.cc
)
create_scratch("${mesh_emu_openwifi_rtp_sources}")
target_include_directories(scratch_mesh-emu-openwifi-rtp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Rtp)

# mesh-emu-m13 + custom RTP applications in scratch/Rtp/
set(mesh_emu_m13_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh-emu-m13.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-client.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-server.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-helper.cc
)
create_scratch("${mesh_emu_m13_sources}")
target_include_directories(scratch_mesh-emu-m13 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Rtp)

# mesh-emu-m13-OpenWifi + custom RTP applications in scratch/Rtp/
set(mesh_emu_m13_openwifi_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh-emu-m13-OpenWifi.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-client.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-server.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/Rtp/rtp-helper.cc
)
create_scratch("${mesh_emu_m13_openwifi_sources}")
target_include_directories(scratch_mesh-emu-m13-OpenWifi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Rtp)

# Scan *.cc files in scratch subdirectories and build a target for each subdirectory
file(
  GLOB_RECURSE scratch_subdirectories
  CONFIGURE_DEPENDS
  LIST_DIRECTORIES true
  ${CMAKE_CURRENT_SOURCE_DIR}/**
)
# Filter out files
foreach(entry ${scratch_subdirectories})
  if(NOT (IS_DIRECTORY ${entry}))
    list(REMOVE_ITEM scratch_subdirectories ${entry})
  endif()
endforeach()

foreach(subdir ${scratch_subdirectories})
  if(EXISTS ${subdir}/CMakeLists.txt)
    # If the subdirectory contains a CMakeLists.txt file
    # we let the CMake file manage the source files
    #
    # Use this if you want to link to external libraries
    # without creating a module
    add_subdirectory(${subdir})
  else()
    # Otherwise we pick all the files in the subdirectory
    # and create a scratch for them automatically
    file(GLOB scratch_sources CONFIGURE_DEPENDS ${subdir}/[^.]*.cc)
    create_scratch("${scratch_sources}")
  endif()
endforeach()

