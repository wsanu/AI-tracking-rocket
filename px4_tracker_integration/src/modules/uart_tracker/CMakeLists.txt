px4_add_module(
	MODULE modules__uart_tracker
	MAIN uart_tracker
	SRCS
		UartTracker.cpp
		UartTracker.hpp
	DEPENDS
	)
