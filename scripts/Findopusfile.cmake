find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
	pkg_check_modules(PC_OPUSFILE opusfile)
endif()

find_library(OPUSFILE_LIBRARY
  NAMES opusfile
  HINTS ${HINTS_OPUSFILE_LIBDIR} ${PC_OPUSFILE_LIBDIR} ${PC_OPUSFILE_LIBRARY_DIRS}
  PATHS ${PATHS_OPUSFILE_LIBDIR}
)
find_path(OPUSFILE_INCLUDEDIR opusfile.h
  PATH_SUFFIXES opus
  HINTS ${HINTS_OPUSFILE_INCLUDEDIR} ${PC_OPUSFILE_INCLUDEDIR} ${PC_OPUSFILE_INCLUDE_DIRS}
  PATHS ${PATHS_OPUSFILE_INCLUDEDIR}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
	opusfile
	REQUIRED_VARS
		OPUSFILE_LIBRARY
		OPUSFILE_INCLUDEDIR
)
mark_as_advanced(OPUSFILE_LIBRARY OPUSFILE_INCLUDEDIR)

if (opusfile_FOUND)
  if (NOT TARGET opusfile)
	add_library(opusfile UNKNOWN IMPORTED)
    set_target_properties(opusfile PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${OPUSFILE_INCLUDEDIR}")
	set_target_properties(opusfile PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${OPUSFILE_LIBRARY}")
  endif()
endif()

