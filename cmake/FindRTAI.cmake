#
# BSD 2-Clause License
#
#Copyright (c) 2009, Markus Rickert
#All rights reserved.
#
#Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
#
#*   Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
#*   Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
#
#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# modified for linuxcnc 2021 by end
#

include(FindPackageHandleStandardArgs)
include(SelectLibraryConfigurations)

find_path(
	RTAI_INCLUDE_DIR
	NAMES rtai_lxrt.h
	PATHS /usr/realtime/include
	PATH_SUFFIXES rtai
)
find_library(
	RTAI_LIBRARY_RELEASE
	NAMES lxrt
	PATHS /usr/realtime/lib
)
select_library_configurations(RTAI)

if(RTAI_INCLUDE_DIR AND EXISTS "${RTAI_INCLUDE_DIR}/rtai_config.h")
	file(STRINGS "${RTAI_INCLUDE_DIR}/rtai_config.h" _RTAI_VERSION_DEFINE REGEX "[\t ]*#define[\t ]+VERSION[\t ]+\"[^\"]*\".*")
	string(REGEX REPLACE "[\t ]*#define[\t ]+VERSION[\t ]+\"([^\"]*)\".*" "\\1" RTAI_VERSION "${_RTAI_VERSION_DEFINE}")
	unset(_RTAI_VERSION_DEFINE)
endif()

set(RTAI_INCLUDE_DIRS ${RTAI_INCLUDE_DIR})
set(RTAI_LIBRARIES ${RTAI_LIBRARIES} rt)

find_package_handle_standard_args(
	RTAI
	FOUND_VAR RTAI_FOUND
	REQUIRED_VARS RTAI_INCLUDE_DIR RTAI_LIBRARY
	VERSION_VAR RTAI_VERSION
)

if(RTAI_FOUND AND NOT TARGET RTAI::lxrt)
	add_library(RTAI::lxrt UNKNOWN IMPORTED)
	if(RTAI_LIBRARY_RELEASE)
		set_property(TARGET RTAI::lxrt APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
		set_target_properties(RTAI::lxrt PROPERTIES IMPORTED_LOCATION_RELEASE "${RTAI_LIBRARY_RELEASE}")
	endif()
	set_target_properties(RTAI::lxrt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${RTAI_INCLUDE_DIRS}")
	set_target_properties(RTAI::lxrt PROPERTIES INTERFACE_LINK_LIBRARIES "rt")
endif()

mark_as_advanced(RTAI_INCLUDE_DIR)
