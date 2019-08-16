cmake_minimum_required(VERSION 3.9)

project(rodent VERSION 0.0.1
               LANGUAGES CXX)

option(USE_RELEASE_PATHS "Use system-defined paths for user data instead of current directory" OFF)
set(FMT_SOURCE_PATH "" CACHE STRING "Path to fmt library source root - build into executable if set")

set(default_build_type "Release")



if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt AND NOT CMAKE_BUILD_TYPE)
	message("No build type specified - set to ${default_build_type}")
	set(CMAKE_BUILD_TYPE ${default_build_type} CACHE STRING "" FORCE)
endif()

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set_property(GLOBAL PROPERTY USE_FOLDERS ON)



add_executable(rodent)
set(TARGET rodent)

target_compile_features(${TARGET} PUBLIC cxx_std_17)
set_target_properties(${TARGET} PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_res)
if (ipo_res AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
	set_target_properties(${TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
	message("FLTO enabled")
else()
	message("FLTO not enabled")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release" OR "${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
		target_link_options(${TARGET} PRIVATE -s)
		message("Build stripped")
	elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
		target_compile_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
		target_link_options(${TARGET} PRIVATE -fsanitize=address -fsanitize=undefined)
		message("Build with sanitizers")
	endif()
endif()



if (NOT FMT_SOURCE_PATH)
	find_package(fmt REQUIRED)
else()
	add_subdirectory(${FMT_SOURCE_PATH})
endif()

find_package(Freetype REQUIRED)
find_package(GLEW     REQUIRED)
find_package(OpenGL   REQUIRED)
find_package(SDL2     REQUIRED)

target_include_directories(${TARGET} PRIVATE
	${FREETYPE_INCLUDE_DIRS}
	${GLEW_INCLUDE_DIRS}
)
target_link_libraries(${TARGET} PRIVATE
	fmt::fmt
	${FREETYPE_LIBRARIES}
	${GLEW_LIBRARIES}
	${OPENGL_LIBRARIES}
	SDL2::SDL2
)



function(add_src DIR)
	file(GLOB SRC "${PROJECT_SOURCE_DIR}/${DIR}/*.cpp")
	target_sources(${TARGET} PRIVATE ${SRC})
endfunction()

add_src(.)
add_src(client)
add_src(core)
add_src(game)
add_src(render)
add_src(utils)
add_src(vaslib)

add_src(external/Box2D/Collision)
add_src(external/Box2D/Collision/Shapes)
add_src(external/Box2D/Common)
add_src(external/Box2D/Dynamics)
add_src(external/Box2D/Dynamics/Contacts)
add_src(external/Box2D/Dynamics/Joints)

target_include_directories(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}")
target_include_directories(${TARGET} PRIVATE "${PROJECT_SOURCE_DIR}/external")

