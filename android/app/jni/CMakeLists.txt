cmake_minimum_required(VERSION 3.18)

# Native build for the Android port of Seven Kingdoms: Ancient Adversaries.
#
# The game sources are NOT copied: they are compiled straight from ../../src
# and ../../include of this repository. The source lists are read from the
# automake files (src/Makefile.am etc.) so this build always follows the
# desktop build.
#
# Third-party dependencies (SDL2, OpenAL-soft) are downloaded by
# app/build.gradle into a folder outside the repository and passed in here.

project(sevenkaa C CXX)

set(SEVENKAA_ROOT "" CACHE PATH "Root of the 7kaa repository")
set(SDL2_SRC      "" CACHE PATH "Path to the SDL2 source tree")
set(OPENAL_SRC    "" CACHE PATH "Path to the OpenAL-soft source tree")

foreach(_var SEVENKAA_ROOT SDL2_SRC OPENAL_SRC)
	if(NOT EXISTS "${${_var}}")
		message(FATAL_ERROR "${_var} is not set or does not exist: '${${_var}}'")
	endif()
endforeach()

# Limit parallel compiler processes so the build does not exhaust the PC's
# memory (ninja otherwise starts one per CPU thread + 2). Set before the
# dependencies are added so SDL2/OpenAL-soft use the same pools.
set(SEVENKAA_JOBS 3 CACHE STRING "Max parallel compile jobs")
set_property(GLOBAL PROPERTY JOB_POOLS sevenkaa_compile=${SEVENKAA_JOBS} sevenkaa_link=1)
set(CMAKE_JOB_POOL_COMPILE sevenkaa_compile)
set(CMAKE_JOB_POOL_LINK sevenkaa_link)

#---------------------------------------------------------------------------
# SDL2 (shared library: SDLActivity loads libSDL2.so)
#---------------------------------------------------------------------------
set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_SRC}" sdl2 EXCLUDE_FROM_ALL)

#---------------------------------------------------------------------------
# OpenAL-soft (static, OpenSL ES backend)
#---------------------------------------------------------------------------
set(LIBTYPE STATIC)
set(ALSOFT_UTILS          OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS          OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL        OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS  OFF CACHE BOOL "" FORCE)
set(ALSOFT_NO_CONFIG_UTIL ON  CACHE BOOL "" FORCE)
set(ALSOFT_BACKEND_OBOE   OFF CACHE BOOL "" FORCE)
set(ALSOFT_REQUIRE_OPENSL ON  CACHE BOOL "" FORCE)
add_subdirectory("${OPENAL_SRC}" openal EXCLUDE_FROM_ALL)

#---------------------------------------------------------------------------
# Source lists from automake
#---------------------------------------------------------------------------
# Reads "<listname> = a.cpp b.cpp \ ..." from an automake file.
function(sevenkaa_am_sources out_var am_file list_name)
	file(READ "${am_file}" _am)
	string(REGEX REPLACE "\\\\[ \t]*\r?\n" " " _am "${_am}")   # join continued lines
	string(REGEX MATCH "(^|\n)${list_name}[ \t]*=([^\n]*)" _line "${_am}")
	if(NOT _line)
		message(FATAL_ERROR "${list_name} not found in ${am_file}")
	endif()
	string(REGEX REPLACE "[ \t\r]+" ";" _files "${CMAKE_MATCH_2}")
	get_filename_component(_dir "${am_file}" DIRECTORY)
	set(_result "")
	foreach(_f ${_files})
		if(_f MATCHES "\\.(cpp|c)$")
			list(APPEND _result "${_dir}/${_f}")
		endif()
	endforeach()
	set(${out_var} ${_result} PARENT_SCOPE)
endfunction()

set(SRC "${SEVENKAA_ROOT}/src")
sevenkaa_am_sources(GAME_SOURCES   "${SRC}/Makefile.am"                 "7kaa_SOURCES")
sevenkaa_am_sources(IMGFUN_SOURCES "${SRC}/imgfun/generic/Makefile.am"  "libimgfun_generic_a_SOURCES")
sevenkaa_am_sources(AUDIO_SOURCES  "${SRC}/openal/Makefile.am"          "libaudio_openal_a_SOURCES")
sevenkaa_am_sources(MP_SOURCES     "${SRC}/enet/Makefile.am"            "libmultiplayer_a_SOURCES")

set(ANDROID_SOURCES
	src/android_data.cpp
	src/android_glob.cpp
	src/android_touch.cpp
)

#---------------------------------------------------------------------------
# The game: libmain.so (loaded by SDLActivity)
#---------------------------------------------------------------------------
add_library(main SHARED
	${GAME_SOURCES}
	${IMGFUN_SOURCES}
	${AUDIO_SOURCES}
	${MP_SOURCES}
	${ANDROID_SOURCES}
)

set_target_properties(main PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS ON
)

# compat/ first: it overrides <glob.h> (bionic has glob() only from API 28).
target_include_directories(main BEFORE PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/compat"
)
target_include_directories(main PRIVATE
	"${SRC}"
	"${SEVENKAA_ROOT}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(main PRIVATE
	# Every game source is compiled with the generated config header,
	# exactly like the desktop build (GLOBAL_CFLAGS in configure.ac).
	-include "${CMAKE_CURRENT_SOURCE_DIR}/config.h"
	# The game data structures and logic assume signed char (ARM defaults
	# to unsigned char). configure.ac forces this too.
	-fsigned-char
	# 1990s code: keep the noise down so real errors stand out.
	-Wno-everything
	-Wno-c++11-narrowing
)

target_compile_definitions(main PRIVATE main=SDL_main)

target_link_libraries(main PRIVATE
	SDL2::SDL2
	OpenAL
	android
	log
	m
)
