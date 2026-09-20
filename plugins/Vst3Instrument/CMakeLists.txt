if(NOT (LMMS_BUILD_LINUX OR LMMS_BUILD_APPLE))
	return()
endif()

include(BuildPlugin)
link_directories("${CMAKE_CURRENT_BINARY_DIR}/..")
if(LMMS_BUILD_LINUX)
	link_libraries(-Wl,--enable-new-dtags)
	set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

BUILD_PLUGIN(vst3instrument Vst3Instrument.cpp Vst3Instrument.h
	MOCFILES Vst3Instrument.h
	EMBEDDED_RESOURCES logo.svg)
target_link_libraries(vst3instrument vst3base)
