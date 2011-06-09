# - Try to find CASTOR 
#  (See http://savannah.cern.ch/files/?group=castor)
#  Check for rfio_api.h, stager_api.h for CASTOR 2 and libshift
#
#  CASTOR_INCLUDE_DIR - where to find rfio_api.h, etc.
#  CASTOR_LIBRARIES   - List of libraries when using ....
#  CASTOR_FOUND       - True if CASTOR 2  libraries found.

set(CASTOR_FOUND FALSE)
set(CASTOR_LIBRARIES)

if(CASTOR_INCLUDE_DIR)
  set(CASTOR_FIND_QUIETLY 1)
endif()

find_path(CASTOR_INCLUDE_DIR NAMES rfio_api.h PATHS 
  $ENV{CASTOR_DIR}/include
  /cern/pro/include
  /cern/new/include
  /cern/old/include
  /opt/shift/include
  /usr/local/shift/include
  /usr/include/shift
  /usr/local/include/shift 
  /usr/include 
  /usr/local/include
)
if(CASTOR_INCLUDE_DIR)
  file(READ ${CASTOR_INCLUDE_DIR}/patchlevel.h contents)
  string(REGEX MATCH   "BASEVERSION[ ]*[\"][ ]*([^ \"]+)" cont ${contents})
  string(REGEX REPLACE "BASEVERSION[ ]*[\"][ ]*([^ \"]+)" "\\1" CASTOR_VERSION ${cont})
endif()

find_library(CASTOR_shift_LIBRARY NAMES shift shiftmd PATHS
  $ENV{CASTOR_DIR}/lib
  /cern/pro/lib 
  /cern/new/lib
  /cern/old/lib 
  /opt/shift/lib 
  /usr/local/shift/lib
  /usr/lib/shift
  /usr/local/lib/shift
  /usr/lib64
  /usr/lib
  /usr/local/lib
)

if(CASTOR_shift_LIBRARY)
  set(CASTOR_LIBRARIES ${CASTOR_LIBRARIES} ${CASTOR_shift_LIBRARY})
endif()

if(CASTOR_INCLUDE_DIR AND CASTOR_LIBRARIES)
  set(CASTOR_FOUND TRUE)
  if(NOT CASTOR_FIND_QUIETLY)
    message(STATUS "Found Castor version ${CASTOR_VERSION} at ${CASTOR_INCLUDE_DIR}")
  endif()
endif()

mark_as_advanced(
  CASTOR_shift_LIBRARY
  CASTOR_INCLUDE_DIR
)


