# - Try to find PortAudio
# Once done this will define
#
#  PORTAUDIO_FOUND - system has PortAudio
#  PORTAUDIO_INCLUDE_DIRS - the PortAudio include directory
#  PORTAUDIO_LIBRARIES - Link these to use PortAudio
#  PORTAUDIO_DEFINITIONS - Compiler switches required for using PortAudio
#
#  Copyright (c) 2006 Andreas Schneider <mail@cynapses.org>
#
# Redistribution and use is allowed according to the terms of the New BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#

if (PORTAUDIO_LIBRARIES AND PORTAUDIO_INCLUDE_DIRS)
  # in cache already
  set(PORTAUDIO_FOUND TRUE)
else (PORTAUDIO_LIBRARIES AND PORTAUDIO_INCLUDE_DIRS)
  if (NOT WIN32)
    include(FindPkgConfig)
    pkg_check_modules(PORTAUDIO2 portaudio-2.0)
  endif (NOT WIN32)

  if (PORTAUDIO2_FOUND)
    set(PORTAUDIO_INCLUDE_DIRS
      ${PORTAUDIO2_INCLUDE_DIRS}
    )
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      set(PORTAUDIO_LIBRARIES "${PORTAUDIO2_LIBRARY_DIRS}/lib${PORTAUDIO2_LIBRARIES}.dylib")
    else (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      set(PORTAUDIO_LIBRARIES
        ${PORTAUDIO2_LIBRARIES}
      )
    endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(PORTAUDIO_VERSION
      19
    )
    set(PORTAUDIO_FOUND TRUE)
  else (PORTAUDIO2_FOUND)
    find_path(PORTAUDIO_INCLUDE_DIR
      NAMES
        portaudio.h
      PATHS
        /usr/include
        /usr/local/include
        /opt/local/include
        /sw/include
    )

    find_library(PORTAUDIO_LIBRARY
      NAMES
        portaudio
      PATHS
        /usr/lib
        /usr/local/lib
        /opt/local/lib
        /sw/lib
    )

    find_path(PORTAUDIO_LIBRARY_DIR
      NAMES
        portaudio
      PATHS
        /usr/lib
        /usr/local/lib
        /opt/local/lib
        /sw/lib
    )

    set(PORTAUDIO_INCLUDE_DIRS
      ${PORTAUDIO_INCLUDE_DIR}
    )
    set(PORTAUDIO_LIBRARIES
      ${PORTAUDIO_LIBRARY}
    )

    if (PORTAUDIO_INCLUDE_DIRS AND PORTAUDIO_LIBRARIES)
       set(PORTAUDIO_FOUND TRUE)
    endif (PORTAUDIO_INCLUDE_DIRS AND PORTAUDIO_LIBRARIES)

    if (PORTAUDIO_FOUND)
      if (NOT PortAudio_FIND_QUIETLY)
        message(STATUS "Found PortAudio: ${PORTAUDIO_LIBRARIES}")
      endif (NOT PortAudio_FIND_QUIETLY)
    else (PORTAUDIO_FOUND)
      if (PortAudio_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find PortAudio")
      endif (PortAudio_FIND_REQUIRED)
    endif (PORTAUDIO_FOUND)
  endif (PORTAUDIO2_FOUND)

  # show the PORTAUDIO_INCLUDE_DIRS and PORTAUDIO_LIBRARIES variables only in the advanced view
  mark_as_advanced(PORTAUDIO_INCLUDE_DIRS PORTAUDIO_LIBRARIES)

endif (PORTAUDIO_LIBRARIES AND PORTAUDIO_INCLUDE_DIRS) 