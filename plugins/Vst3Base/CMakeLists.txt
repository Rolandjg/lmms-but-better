# Native VST3 hosting on Linux, macOS and Windows.
if(NOT (LMMS_BUILD_LINUX OR LMMS_BUILD_APPLE OR LMMS_BUILD_WIN32))
	return()
endif()

INCLUDE(BuildPlugin)

# The vendored SDK sources are third-party code: build them without warnings
# (e.g. their printf formats assume uint32 is unsigned int, but it is unsigned
# long on Windows). MSVC only gets the format warning disabled, since
# overriding its warning level emits a warning of its own.
set_source_files_properties(
	vst3sdk/pluginterfaces/base/conststringtable.cpp
	vst3sdk/pluginterfaces/base/coreiids.cpp
	vst3sdk/pluginterfaces/base/funknown.cpp
	vst3sdk/pluginterfaces/base/ustring.cpp
	PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/wd4477,-w>")

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
if(MSVC)
	# Exported host classes implement the SDK's pure (COM-style) interfaces,
	# which are not exported themselves. Like C4251 (see ErrorFlags.cmake),
	# this "non DLL-interface base class" warning doesn't apply: everything is
	# built with the same compiler and the interfaces carry no data.
	target_compile_options(vst3base PUBLIC /wd4275)
endif()

if(LMMS_BUILD_LINUX)
	target_sources(vst3base PRIVATE Vst3X11Helpers.cpp)
	# xcb for sending synthetic ConfigureNotify events to embedded editors
	FIND_PACKAGE(PkgConfig REQUIRED)
	PKG_CHECK_MODULES(XCB REQUIRED IMPORTED_TARGET xcb)
	TARGET_LINK_LIBRARIES(vst3base PkgConfig::XCB)
endif()

# This helper loads third-party factories outside the LMMS process.
add_executable(Vst3Scanner Vst3Scanner.cpp Vst3Basics.cpp Vst3HostApp.cpp
	Vst3SdkIids.cpp
	vst3sdk/pluginterfaces/base/coreiids.cpp
	vst3sdk/pluginterfaces/base/funknown.cpp)
target_include_directories(Vst3Scanner PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk")
# the scanner compiles the host sources itself instead of importing them from
# vst3base, which matters on Windows where exported classes become dllimport
target_compile_definitions(Vst3Scanner PRIVATE VST3BASE_STATIC_DEFINE)
target_link_libraries(Vst3Scanner Qt${QT_VERSION_MAJOR}::Widgets ${CMAKE_DL_LIBS})
install(TARGETS Vst3Scanner RUNTIME DESTINATION "${PLUGIN_DIR}")
add_dependencies(vst3base Vst3Scanner)

if(LMMS_BUILD_APPLE)
	find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
	find_library(SECURITY_FRAMEWORK Security REQUIRED)
	target_link_libraries(vst3base ${COREFOUNDATION_FRAMEWORK} ${SECURITY_FRAMEWORK})
	target_link_libraries(Vst3Scanner ${COREFOUNDATION_FRAMEWORK} ${SECURITY_FRAMEWORK})
endif()
set_target_properties(Vst3Scanner PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
