cmake_minimum_required(VERSION 3.11)

project(opusfile LANGUAGES C)
add_library(opusfile)

find_package(ogg QUIET)
if (NOT ogg_FOUND)
	set(INSTALL_DOCS OFF CACHE BOOL "")
	set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "")
	set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "")
	set(BUILD_TESTING OFF CACHE BOOL "")
	
	FetchContent_Declare(
		ogg GIT_REPOSITORY https://github.com/xiph/ogg.git
		GIT_SHALLOW 1
	)
	FetchContent_MakeAvailable(ogg)
	target_include_directories(opusfile PUBLIC "${ogg_SOURCE_DIR}/include")
endif()

find_package(opus QUIET)
if (NOT opus_FOUND)
	set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "")
	set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "")
	
	message(WARNING "rodent: opusfile - cmake hacks")
	# seems opus\cmake\CFeatureCheck.cmake should use PROJECT_SOURCE_DIR instead of CMAKE_SOURCE_DIR
	# also using older version because of the same stuff in opus\cmake\OpusConfig.cmake
	set(COMPILE_VLA_SUPPORTED FALSE CACHE BOOL "" FORCE)

	FetchContent_Declare(
		opus
		GIT_REPOSITORY  https://github.com/xiph/opus.git
		GIT_TAG         fc13987b1ad0d55908528598d0f30b364cc62030
	)
	FetchContent_MakeAvailable(opus)
	target_include_directories(opusfile PUBLIC "${opus_SOURCE_DIR}/include")
endif()

FetchContent_Declare(
	opusfile_src GIT_REPOSITORY https://github.com/xiph/opusfile.git
	GIT_SHALLOW 1
)
FetchContent_Populate(opusfile_src)

set(INCLUDE_DIR "${opusfile_src_SOURCE_DIR}/include")
set(SOURCE_DIR "${opusfile_src_SOURCE_DIR}/src")
set(SOURCES
	"${SOURCE_DIR}/info.c"
	"${SOURCE_DIR}/internal.c"
	"${SOURCE_DIR}/opusfile.c"
	"${SOURCE_DIR}/stream.c"
	"${SOURCE_DIR}/wincerts.c"
)
target_include_directories(opusfile PRIVATE ${SOURCE_DIR} ${INCLUDE_DIR})
target_sources(opusfile PRIVATE ${SOURCES})

target_include_directories(opusfile PUBLIC ${INCLUDE_DIR})
target_link_libraries(opusfile PUBLIC ogg opus)

