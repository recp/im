cmake_minimum_required(VERSION 3.13)
project(im
  VERSION 0.1.0
  HOMEPAGE_URL https://github.com/recp/im
  DESCRIPTION "fast & flexible image library ( currently loading only )"
  LANGUAGES C
)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(IM_BUILD)
option(IM_SHARED "Shared build" ON)
option(IM_STATIC "Static build" OFF)
option(IM_USE_TEST "Enable Tests" OFF)
# the codecs alone, from memory (im_load_mem, im_info_mem, im_free): JPEG,
# JPEG 2000 and their pool; no file layer, no other format, no submodule,
# no framework. IM_NO_THREADS: no thread layer either (the pool inline;
# WASI without -pthread is so on its own)
option(IM_CODECS_ONLY "JPEG and JPEG 2000 from memory alone" OFF)
option(IM_NO_THREADS "No threads (decoders run on the caller's)" OFF)
# every symbol under a prefix (an identifier's start, e.g. app_): for a
# codec-only build linked beside another copy of im
set(IM_PREFIX "" CACHE STRING "A prefix for im's symbols (codec-only builds)")

# The formats built in: all, or some of these (png alone needs the defl
# and huff submodules)
set(IM_ALL_FORMATS png bmp ppm qoi tga jpg jp2 jxl heic)
set(IM_FORMATS "all" CACHE STRING "Formats built in: all, or a list of ${IM_ALL_FORMATS}")
if(IM_CODECS_ONLY)
  set(IM_ON jpg jp2)
elseif(IM_FORMATS STREQUAL "all")
  set(IM_ON ${IM_ALL_FORMATS})
else()
  set(IM_ON ${IM_FORMATS})
  foreach(f ${IM_ON})
    if(NOT f IN_LIST IM_ALL_FORMATS)
      message(FATAL_ERROR "IM_FORMATS: no format '${f}' (${IM_ALL_FORMATS})")
    endif()
  endforeach()
endif()
if(NOT APPLE)
  # Apple's decoders, Apple's platforms alone
  list(REMOVE_ITEM IM_ON jxl heic)
endif()

if(NOT IM_STATIC AND IM_SHARED)
  set(IM_BUILD SHARED)
else(IM_STATIC)
  set(IM_BUILD STATIC)
endif()

# Dependencies && Submodules
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE AND "png" IN_LIST IM_ON)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

if(MSVC)
  add_definitions(-D_WINDOWS -D_USRDLL)

  if(NOT CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DNDEBUG)
    add_compile_options(/W3 /Ox /Gy /Oi /TC)
    foreach(flag_var
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
      string(REGEX REPLACE "/RTC(su|[1su])" "" ${flag_var} "${${flag_var}}")
    endforeach(flag_var)
  endif()
else()
  add_compile_options(-Wall)

  if(NOT CMAKE_BUILD_TYPE MATCHES Debug)
    add_compile_options(-O3)
  endif()
endif()

get_directory_property(hasParent PARENT_DIRECTORY)

if(NOT hasParent AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(GNUInstallDirs)

# Target Start
add_library(${PROJECT_NAME} ${IM_BUILD} "")

# Set C standard - try C2x/C23 if available, fallback to C11
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
  set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 23)
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 11)
endif()

if(IM_SHARED)
  add_definitions(-DIM_EXPORTS)
else()
  target_compile_definitions(${PROJECT_NAME} PUBLIC -DIM_STATIC)
endif()

if("png" IN_LIST IM_ON)
  add_subdirectory(deps/defl)
  if(NOT TARGET huff)
    add_subdirectory(deps/huff)
  endif()
endif()
if(IM_CODECS_ONLY)
  # README's codec list
  target_sources(${PROJECT_NAME} PRIVATE
    src/load.c
    src/io/jpg/jpg.c src/io/jpg/arith.c src/io/jpg/huff.c src/io/jpg/idct.c src/io/jpg/sample.c
    src/io/jp2/jp2.c
    src/thread/pool.c)
  if(NOT IM_NO_THREADS)
    if(WIN32)
      target_sources(${PROJECT_NAME} PRIVATE src/win/thread.c)
    else()
      target_sources(${PROJECT_NAME} PRIVATE src/posix/thread.c)
    endif()
  endif()
  target_compile_definitions(${PROJECT_NAME} PRIVATE IM_CODECS_ONLY=1)
else()
  add_subdirectory(src)
endif()
if(IM_NO_THREADS)
  target_compile_definitions(${PROJECT_NAME} PRIVATE IM_NO_THREADS=1)
endif()
if(IM_PREFIX)
  target_compile_definitions(${PROJECT_NAME} PUBLIC IM_PREFIX=${IM_PREFIX})
endif()

# the formats picked: src/formats.h's IM_WITH_<FORMAT> (all where none is)
if(NOT IM_FORMATS STREQUAL "all" OR NOT APPLE)
  set(IM_FORMAT_DEFS IM_SELECTED_FORMATS=1)
  foreach(f ${IM_ON})
    string(TOUPPER ${f} F)
    list(APPEND IM_FORMAT_DEFS IM_WITH_${F}=1)
  endforeach()
  target_compile_definitions(${PROJECT_NAME} PRIVATE ${IM_FORMAT_DEFS})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
                              VERSION ${PROJECT_VERSION} 
                            SOVERSION ${PROJECT_VERSION_MAJOR})

if(WIN32)
  # Because SOVERSION has no effect to file naming on Windows
  set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_NAME ${PROJECT_NAME}-${PROJECT_VERSION_MAJOR})
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if("png" IN_LIST IM_ON)
  target_link_libraries(${PROJECT_NAME}
      PRIVATE defl
      PRIVATE huff
  )
endif()

if(APPLE AND NOT IM_CODECS_ONLY)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE "-framework Foundation"
        PRIVATE "-framework CoreImage"
        PRIVATE "-framework CoreGraphics"
        PRIVATE "-framework ImageIO"
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

if(IM_USE_TEST)
  enable_testing()
  add_subdirectory(test)
endif()

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY DESTINATION  ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION  ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION  ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})

if(NOT CPack_CMake_INCLUDED)
  include(CPack)
endif()
