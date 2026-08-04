px4_add_module(
	MODULE modules__track_control
	MAIN track_control
	COMPILE_FLAGS
		${MAX_CUSTOM_OPT_LEVEL}
	SRCS
		TrackControl.cpp
		TrackControl.hpp
	DEPENDS
		mathlib
		px4_work_queue
	)