# CMakeLists.txt has to be located in the project folder and cmake has to be
# executed from 'project/build' with 'cmake ../'.

# ${PROJECT_SOURCE_DIR} refers to the folder of the CMakeLists.txt (project)
# ${PROJECT_BINARY_DIR} refers to the folder from which cmake was executed (project/build).

cmake_minimum_required(VERSION 2.6)
find_package(Rock)
set(PROSILICA_SDK_URL "http://download.ros.org/downloads/Prosilica%20GigE%20SDK%201.20%20Linux.tgz")

# If the user specifies -DCMAKE_BUILD_TYPE on the command line, take their definition
# and dump it in the cache along with proper documentation, otherwise set CMAKE_BUILD_TYPE
# to Debug prior to calling PROJECT()
if(DEFINED CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of
build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug
Release RelWithDebInfo MinSizeRel.")
else()
   ##### Build types #################################################
   # single-configuration generator like Makefile generator creates following variables per default
   #
   # None (CMAKE_C_FLAGS or CMAKE_CXX_FLAGS used)
   # Debug (CMAKE_C_FLAGS_DEBUG or CMAKE_CXX_FLAGS_DEBUG)
   # Release (CMAKE_C_FLAGS_RELEASE or CMAKE_CXX_FLAGS_RELEASE)
   # RelWithDebInfo (CMAKE_C_FLAGS_RELWITHDEBINFO or CMAKE_CXX_FLAGS_RELWITHDEBINFO
   # MinSizeRel (CMAKE_C_FLAGS_MINSIZEREL or CMAKE_CXX_FLAGS_MINSIZEREL) 
   ####################################################################
   set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build,
options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release
RelWithDebInfo MinSizeRel.")
endif()

message("Build type set to: " ${CMAKE_BUILD_TYPE})

##### Set Project name and version #####################################
project(camera_prosilica_gige)
set(PROJECT_VERSION 1.0)
set(PROJECT_DESCRIPTION "Wrapper for Prosilica GigE Cameras")
##### End Set Project name and version #################################

rock_doxygen()

##### Download Prosilica SDK if not present ############################
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/Prosilica\ GigE\ SDK/inc-pc/PvApi.h)
  set(sdk_local_path ${CMAKE_CURRENT_SOURCE_DIR}/external/prosilica_sdk.tar.gz)
  # file(SIZE ${sdk_local_path} ${sdk_size})
  # set(sdk_empty ${sdk_size} EQUAL 0)
  # Simply delete the previously downloaded file and re-download it to make sure
  # it is neither corrupted or outdated
  # if (NOT EXISTS ${sdk_local_path} OR $sdk_empty)
    message(STATUS "downloading and unpacking the Prosilica SDK into external/, this may take a while")
    file(REMOVE ${sdk_local_path})
    file(DOWNLOAD "${PROSILICA_SDK_URL}" ${sdk_local_path}.partial
        STATUS sdk_download_status SHOW_PROGRESS)
    if (NOT sdk_download_status)
      message(FATAL_ERROR "could not download the prosilica SDK from ${PROSILICA_SDK_URL}")
    endif()
    file(RENAME ${sdk_local_path}.partial ${sdk_local_path})
  # endif()
  execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external
    COMMAND ${CMAKE_COMMAND} -E tar xzf ${sdk_local_path})
endif()

##### Specification of build directory #################################
# Specifies a common place where CMake should put all the executables.
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
# Specifies a common place where CMake should put all the libraries
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
# Include headers within source
# If you create subdirectories within source include headers with subdirectory, i.e. 
# #include "subdirectory/myheader.h"
include_directories(${PROJECT_SOURCE_DIR}/src)
set(PROSILICA_SDK_DIR ${PROJECT_SOURCE_DIR}/external/Prosilica\ GigE\ SDK)
include_directories(${PROSILICA_SDK_DIR}/inc-pc)
if (${CMAKE_SIZEOF_VOID_P} MATCHES 4)
    set(PROSILICA_ARCH x86)
elseif (${CMAKE_SIZEOF_VOID_P} MATCHES 8)
    set(PROSILICA_ARCH x64)
else (${CMAKE_SIZEOF_VOID_P} MATCHES 4)
    message(FATAL_ERROR "Only 32 and 64 bit is supported by the prosilica api")
endif (${CMAKE_SIZEOF_VOID_P} MATCHES 4)

link_directories(${PROSILICA_SDK_DIR}/lib-pc/${PROSILICA_ARCH}/4.2)
link_directories(${PROJECT_SOURCE_DIR}/build/lib)

include(FindPkgConfig)
pkg_check_modules(CAM_INTERFACE REQUIRED "camera_interface")
include_directories(${CAM_INTERFACE_INCLUDE_DIRS})
link_directories(${CAM_INTERFACE_LIBDIR})

pkg_check_modules(BASE_TYPES REQUIRED "base-types")
include_directories(${BASE_TYPES_INCLUDE_DIRS})
link_directories(${BASE_TYPES_LIBDIR})

#pkg_check_modules(OPENCV REQUIRED "opencv")
#include_directories(${OPENCV_INCLUDE_DIRS})
#link_directories(${OPENCV_LIBDIR})

##### End specification of build directory ##############################

# Process CMakeLists.txt in the following subdirectory
add_subdirectory(src)
add_subdirectory(test)

# make sure we build some of the useful examples
configure_file(${PROJECT_SOURCE_DIR}/external/ARCH.in ${PROSILICA_SDK_DIR}/examples/ARCH
    @ONLY)
add_custom_target(prosilica-examples ALL
    make -C ${PROSILICA_SDK_DIR}/examples/ListCameras sample
    COMMAND make -C ${PROSILICA_SDK_DIR}/examples/Ping sample)
install(FILES ${PROSILICA_SDK_DIR}/bin-pc/${PROSILICA_ARCH}/libPvAPI.so
    DESTINATION lib)
install(FILES ${PROSILICA_SDK_DIR}/examples/ListCameras/ListCameras
    DESTINATION bin
    RENAME prosilica_list_cameras
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES ${PROSILICA_SDK_DIR}/examples/Ping/Ping
    DESTINATION bin
    RENAME prosilica_ping
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

##### COPY Configuration files into build directory
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/configuration ${PROJECT_BINARY_DIR}/configuration)

# Workaround: Cleanup the in file from build directory
execute_process(COMMAND ${CMAKE_COMMAND} -E remove -f ${PROJECT_BINARY_DIR}/configuration/${PROJECT_NAME}.pc.in)

##### INSTALL Configuration ####################################################

# include/
# TODO: recursive copy with directories
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION include/${PROJECT_NAME}
	FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${PROJECT_SOURCE_DIR}/external/Prosilica\ GigE\ SDK/inc-pc/ DESTINATION include/${PROJECT_NAME}
	FILES_MATCHING PATTERN "*.h")

# scripts/
install(DIRECTORY ${PROJECT_SOURCE_DIR}/scripts/ DESTINATION scripts)

# configuration/<projectname>.pc
configure_file(${PROJECT_SOURCE_DIR}/configuration/${PROJECT_NAME}.pc.in
		${PROJECT_BINARY_DIR}/configuration/${PROJECT_NAME}.pc @ONLY)

# Install pkg-config file
install(FILES ${CMAKE_BINARY_DIR}/configuration/${PROJECT_NAME}.pc DESTINATION lib/pkgconfig)

##### End INSTALL Configuration ################################################
