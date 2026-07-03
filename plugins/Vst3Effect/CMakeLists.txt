if(NOT LMMS_BUILD_LINUX)
	return()
endif()

include(BuildPlugin)
link_directories("${CMAKE_CURRENT_BINARY_DIR}/..")
link_libraries(-Wl,--enable-new-dtags)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN")

BUILD_PLUGIN(vst3effect Vst3Effect.cpp Vst3Effect.h
	MOCFILES Vst3Effect.h
	EMBEDDED_RESOURCES logo.svg)
target_link_libraries(vst3effect vst3base)
