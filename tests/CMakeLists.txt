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
	src/core/LoudnessAnalyzerTest.cpp
	src/core/LimiterDspTest.cpp
	src/core/FormantDspTest.cpp
	src/core/TimelineTest.cpp
	src/core/SongExportTest.cpp
	src/core/ProjectPersistenceTest.cpp
	src/core/LatencyCompensationTest.cpp
	src/core/SampleClipFadeTest.cpp
	src/core/ScriptApiTest.cpp
	src/gui/PianoRollRenderingTest.cpp
	src/gui/MixerChannelLcdSpinBoxTest.cpp
	src/gui/CommandPaletteTest.cpp
	src/gui/MidiCaptureTest.cpp
	src/gui/BounceFreezeTest.cpp
	src/gui/ScriptConsoleTest.cpp
	src/gui/AudioFileProcessorViewTest.cpp
	src/gui/FormantShifterViewTest.cpp
	src/gui/InstrumentWindowTabsTest.cpp
	src/gui/FunctionGeneratorTest.cpp
	src/gui/WorkspaceControlsTest.cpp
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

	if(LMMS_BUILD_WIN32)
		# LMMS plugin DLLs import the core from "lmms.exe". Windows binds those
		# imports to an already running module of that name, so naming the test
		# lmms.exe lets tests load plugins like LMMS itself does. Each test gets
		# its own directory to keep the names apart.
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES
			OUTPUT_NAME lmms
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${LMMS_TEST_NAME}"
			ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${LMMS_TEST_NAME}")
	endif()
endforeach()

set_target_properties(PianoRollRenderingTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(PianoRollRenderingTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(MixerChannelLcdSpinBoxTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(MixerChannelLcdSpinBoxTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(CommandPaletteTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(CommandPaletteTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(FormantShifterViewTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(FormantShifterViewTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(AudioFileProcessorViewTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(AudioFileProcessorViewTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(InstrumentWindowTabsTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(InstrumentWindowTabsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(MidiCaptureTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(MidiCaptureTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(ScriptApiTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(ScriptApiTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)

set_target_properties(ScriptConsoleTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(ScriptConsoleTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 90)

if(TARGET vst3base)
	# third-party SDK sources, built without warnings as in plugins/Vst3Base
	set_source_files_properties(
		../plugins/Vst3Base/vst3sdk/pluginterfaces/base/conststringtable.cpp
		../plugins/Vst3Base/vst3sdk/pluginterfaces/base/coreiids.cpp
		../plugins/Vst3Base/vst3sdk/pluginterfaces/base/funknown.cpp
		../plugins/Vst3Base/vst3sdk/pluginterfaces/base/ustring.cpp
		PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/wd4477,-w>")
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
	target_link_libraries(Vst3HostTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
	target_compile_features(Vst3HostTest PRIVATE cxx_std_20)
	target_compile_definitions(Vst3HostTest PRIVATE LMMS_TESTING VST3_TEST_MODULE="$<TARGET_FILE:Vst3TestPlugin>")
	add_dependencies(Vst3HostTest Vst3TestPlugin)
	if(LMMS_BUILD_WIN32)
		# LMMS plugin DLLs import the LMMS core from lmms.exe, so vst3base.dll
		# cannot be loaded into another executable. Compile the host into the
		# test instead, and place the test beside Vst3Scanner.exe, which the
		# host looks for next to the module containing its code.
		set(VST3BASE_DIR "${CMAKE_SOURCE_DIR}/plugins/Vst3Base")
		target_sources(Vst3HostTest PRIVATE
			${VST3BASE_DIR}/Vst3Basics.cpp
			${VST3BASE_DIR}/Vst3HostApp.cpp
			${VST3BASE_DIR}/Vst3Manager.cpp
			${VST3BASE_DIR}/Vst3Module.cpp
			${VST3BASE_DIR}/Vst3Plugin.cpp
			${VST3BASE_DIR}/Vst3Plugin.h
			${VST3BASE_DIR}/Vst3SdkIids.cpp
			${VST3BASE_DIR}/Vst3SubPluginFeatures.cpp
			${VST3BASE_DIR}/Vst3ViewBase.cpp
			${VST3BASE_DIR}/Vst3ViewBase.h
			${VST3BASE_DIR}/vst3sdk/pluginterfaces/base/conststringtable.cpp
			${VST3BASE_DIR}/vst3sdk/pluginterfaces/base/coreiids.cpp
			${VST3BASE_DIR}/vst3sdk/pluginterfaces/base/funknown.cpp
			${VST3BASE_DIR}/vst3sdk/pluginterfaces/base/ustring.cpp)
		target_include_directories(Vst3HostTest PRIVATE
			"${VST3BASE_DIR}" "${CMAKE_BINARY_DIR}/plugins/Vst3Base")
		target_include_directories(Vst3HostTest SYSTEM PRIVATE "${VST3BASE_DIR}/vst3sdk")
		target_compile_definitions(Vst3HostTest PRIVATE VST3BASE_STATIC_DEFINE)
		set_target_properties(Vst3HostTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
		add_dependencies(Vst3HostTest Vst3Scanner)
	else()
		target_link_libraries(Vst3HostTest PRIVATE vst3base)
	endif()
	if(TARGET vst3instrument AND TARGET vst3effect AND NOT LMMS_BUILD_WIN32)
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

# Developer tool: renders the main windows to PNG for reviewing theme changes.
add_executable(UiSnapshot EXCLUDE_FROM_ALL plugins/UiSnapshot.cpp)
target_include_directories(UiSnapshot PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
target_static_libraries(UiSnapshot PRIVATE lmmsobjs)
target_link_libraries(UiSnapshot PRIVATE ${QT_LIBRARIES})
target_compile_features(UiSnapshot PRIVATE cxx_std_20)
set_target_properties(UiSnapshot PROPERTIES ENABLE_EXPORTS ON)

set_tests_properties(FunctionGeneratorTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 60)

set_tests_properties(WorkspaceControlsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 60)

set_target_properties(BounceFreezeTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(BounceFreezeTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LMMS_PLUGIN_DIR=${CMAKE_BINARY_DIR}/plugins;LMMS_DATA_DIR=${CMAKE_SOURCE_DIR}/data"
	TIMEOUT 60)
