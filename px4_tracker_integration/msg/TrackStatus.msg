uint64 timestamp

bool active
bool target_valid
bool attitude_limited
bool failsafe_active

uint8 FAILURE_NONE = 0
uint8 FAILURE_ENTRY_TARGET_INVALID = 1
uint8 FAILURE_TARGET_STALE = 2
uint8 FAILURE_MANUAL_CONTROL_INVALID = 3
uint8 FAILURE_CONTROL_MODE_INVALID = 4
uint8 failure_reason

uint32 target_age_us
float32[3] filtered_direction_body
float32[4] attitude_setpoint_q
float32 twist_angle
float32 twist_rate
float32 thrust_setpoint