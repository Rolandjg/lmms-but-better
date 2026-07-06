# VST3 hosting is currently only implemented for Linux (dlopen + X11 embedding)
if(NOT LMMS_BUILD_LINUX)
	return()
endif()

INCLUDE(BuildPlugin)

BUILD_PLUGIN(vst3base
	vst3_base.cpp
	Vst3Basics.cpp
	Vst3HostApp.cpp
	Vst3Manager.cpp
	Vst3Module.cpp
	Vst3Plugin.cpp
	Vst3SdkIids.cpp
	Vst3SubPluginFeatures.cpp
	Vst3ViewBase.cpp
	Vst3X11Helpers.cpp
	vst3sdk/pluginterfaces/base/conststringtable.cpp
	vst3sdk/pluginterfaces/base/coreiids.cpp
	vst3sdk/pluginterfaces/base/funknown.cpp
	vst3sdk/pluginterfaces/base/ustring.cpp
	MOCFILES Vst3Plugin.h Vst3ViewBase.h
	EXPORT_BASE_NAME vst3base
	LINK SHARED
)

TARGET_INCLUDE_DIRECTORIES(vst3base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# vendored VST3 interface headers (MIT licensed, see vst3sdk/pluginterfaces/LICENSE.txt)
TARGET_INCLUDE_DIRECTORIES(vst3base SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk")
TARGET_LINK_LIBRARIES(vst3base ${CMAKE_DL_LIBS})

# xcb for sending synthetic ConfigureNotify events to embedded editors
FIND_PACKAGE(PkgConfig REQUIRED)
PKG_CHECK_MODULES(XCB REQUIRED IMPORTED_TARGET xcb)
TARGET_LINK_LIBRARIES(vst3base PkgConfig::XCB)
