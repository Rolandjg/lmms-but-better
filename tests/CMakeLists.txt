include(CTest)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/MidiNotePitchTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/TimelineTest.cpp
	src/core/SongExportTest.cpp
	src/tracks/AutomationTrackTest.cpp
)

foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(LMMS_TEST_NAME ${LMMS_TEST_SRC} NAME_WE)

	add_executable(${LMMS_TEST_NAME} ${LMMS_TEST_SRC})
	add_test(NAME ${LMMS_TEST_NAME} COMMAND ${LMMS_TEST_NAME})

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(${LMMS_TEST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)

	target_static_libraries("${LMMS_TEST_NAME}" PRIVATE lmmsobjs)
	target_link_libraries(${LMMS_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)

	target_compile_features(${LMMS_TEST_NAME} PRIVATE cxx_std_20)
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING)
endforeach()

if(TARGET vst3base)
	add_library(Vst3TestPlugin MODULE plugins/Vst3TestPlugin.cpp
		../plugins/Vst3Base/Vst3SdkIids.cpp
		../plugins/Vst3Base/vst3sdk/pluginterfaces/base/coreiids.cpp
		../plugins/Vst3Base/vst3sdk/pluginterfaces/base/funknown.cpp)
	target_include_directories(Vst3TestPlugin PRIVATE ../plugins/Vst3Base/vst3sdk)
	target_compile_features(Vst3TestPlugin PRIVATE cxx_std_20)
	if(LMMS_BUILD_APPLE)
		set_target_properties(Vst3TestPlugin PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION vst3
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/plugins/Vst3TestPlugin.plist.in")
		target_link_libraries(Vst3TestPlugin PRIVATE ${COREFOUNDATION_FRAMEWORK})
	endif()
	add_executable(Vst3HostTest plugins/Vst3HostTest.cpp)
	target_include_directories(Vst3HostTest PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
	target_static_libraries(Vst3HostTest PRIVATE lmmsobjs)
	target_link_libraries(Vst3HostTest PRIVATE vst3base ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
	target_compile_features(Vst3HostTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3HostTest PRIVATE LMMS_TESTING VST3_TEST_MODULE="$<TARGET_FILE:Vst3TestPlugin>")
	add_dependencies(Vst3HostTest Vst3TestPlugin)
	if(TARGET vst3instrument AND TARGET vst3effect)
		add_dependencies(Vst3HostTest vst3instrument vst3effect)
		target_compile_definitions(Vst3HostTest PRIVATE LMMS_TEST_VST3_WRAPPERS)
	endif()
	set_target_properties(Vst3HostTest PROPERTIES ENABLE_EXPORTS ON)
	add_test(NAME Vst3HostTest COMMAND Vst3HostTest)
	set_tests_properties(Vst3HostTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data" TIMEOUT 120)
endif()

# Developer tool: renders native plugin UIs to PNG and sanity-checks effect output.
# Build with `make PluginSnapshot`; not part of the test suite.
add_executable(PluginSnapshot EXCLUDE_FROM_ALL plugins/PluginSnapshot.cpp)
target_include_directories(PluginSnapshot PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
target_static_libraries(PluginSnapshot PRIVATE lmmsobjs)
target_link_libraries(PluginSnapshot PRIVATE ${QT_LIBRARIES})
target_compile_features(PluginSnapshot PRIVATE cxx_std_20)
set_target_properties(PluginSnapshot PROPERTIES ENABLE_EXPORTS ON)
