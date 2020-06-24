find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
	pkg_check_modules(PC_PORTAUDIO portaudio-2.0)
endif()

find_library(PORTAUDIO_LIBRARY
  NAMES portaudio
  HINTS ${HINTS_PORTAUDIO_LIBDIR} ${PC_PORTAUDIO_LIBDIR} ${PC_PORTAUDIO_LIBRARY_DIRS}
  PATHS ${PATHS_PORTAUDIO_LIBDIR}
)
find_path(PORTAUDIO_INCLUDEDIR portaudio.h
  HINTS ${HINTS_PORTAUDIO_INCLUDEDIR} ${PC_PORTAUDIO_INCLUDEDIR} ${PC_PORTAUDIO_INCLUDE_DIRS}
  PATHS ${PATHS_PORTAUDIO_INCLUDEDIR}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
	portaudio
	REQUIRED_VARS
		PORTAUDIO_LIBRARY
		PORTAUDIO_INCLUDEDIR
)
mark_as_advanced(PORTAUDIO_LIBRARY PORTAUDIO_INCLUDEDIR)

if (portaudio_FOUND)
  if (NOT TARGET portaudio)
	add_library(portaudio UNKNOWN IMPORTED)
    set_target_properties(portaudio PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${PORTAUDIO_INCLUDEDIR}")
	set_target_properties(portaudio PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${PORTAUDIO_LIBRARY}")
  endif()
endif()

