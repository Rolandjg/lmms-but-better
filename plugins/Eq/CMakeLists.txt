INCLUDE(BuildPlugin)
include_directories(SYSTEM ${FFTW3F_INCLUDE_DIRS})
LINK_LIBRARIES(${FFTW3F_LIBRARIES})
BUILD_PLUGIN(eq EqEffect.cpp EqControls.cpp EqControlsDialog.cpp EqDisplay.cpp EqAnalyser.cpp
EqAnalyser.h EqFilter.h
MOCFILES EqControls.h EqControlsDialog.h EqDisplay.h)
