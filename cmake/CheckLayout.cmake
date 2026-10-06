# Checks that a directory holds the files NifSkope looks for next to its executable.
#   cmake -DAPP_DIR=<directory of the executable> [-DENTRIES="nif.xml|kfm.xml|..."] -P CheckLayout.cmake
# The names mirror the lookups in src/xml/nifxml.cpp (nif.xml), src/xml/kfmxml.cpp (kfm.xml),
# src/nifskope_ui.cpp (style.qss), src/gl/renderer.cpp (shaders/*.vert|frag|prog and the default textures),
# src/gl/gltex.cpp (shaders/*.dds, resolved against the working directory, which main() sets to this directory).
if(NOT APP_DIR)
	message(FATAL_ERROR "usage: cmake -DAPP_DIR=<dir> -P CheckLayout.cmake")
endif()
if(NOT ENTRIES)
	set(ENTRIES "nif.xml|kfm.xml|style.qss|shaders")
endif()
string(REPLACE "|" ";" ENTRIES "${ENTRIES}")

set(_problems "")
foreach(_entry IN LISTS ENTRIES)
	if(NOT EXISTS "${APP_DIR}/${_entry}")
		list(APPEND _problems "missing: ${_entry}")
	endif()
endforeach()

foreach(_xml nif.xml kfm.xml)
	if(EXISTS "${APP_DIR}/${_xml}")
		file(SIZE "${APP_DIR}/${_xml}" _size)
		if(_size LESS 1000)
			list(APPEND _problems "${_xml} is only ${_size} bytes")
		endif()
	endif()
endforeach()

if(EXISTS "${APP_DIR}/shaders")
	foreach(_dds white black gray magenta default_n cubemap blankdetailmap)
		if(NOT EXISTS "${APP_DIR}/shaders/${_dds}.dds")
			list(APPEND _problems "missing: shaders/${_dds}.dds")
		endif()
	endforeach()
	foreach(_ext vert frag prog)
		file(GLOB _found "${APP_DIR}/shaders/*.${_ext}")
		if(NOT _found)
			list(APPEND _problems "no shaders/*.${_ext}")
		endif()
	endforeach()
endif()

if(_problems)
	string(REPLACE ";" "\n  " _text "${_problems}")
	message(FATAL_ERROR "${APP_DIR} is not a complete NifSkope directory:\n  ${_text}")
endif()
message(STATUS "${APP_DIR}: layout ok")
