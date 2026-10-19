INCLUDE(BuildPlugin)
include_directories(SYSTEM ${FFTW3F_INCLUDE_DIRS})
LINK_LIBRARIES(${FFTW3F_LIBRARIES})

BUILD_PLUGIN(formantshifter FormantShifter.cpp FormantShifterControls.cpp FormantShifterControlDialog.cpp
	MOCFILES FormantShifterControls.h FormantShifterControlDialog.h)
