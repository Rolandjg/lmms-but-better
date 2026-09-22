INCLUDE(BuildPlugin)

BUILD_PLUGIN(
	flanger FlangerEffect.cpp FlangerControls.cpp FlangerControlsDialog.cpp
	MOCFILES FlangerControls.h FlangerControlsDialog.h
)
