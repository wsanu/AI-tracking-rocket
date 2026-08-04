uint64 timestamp

bool valid
uint8 target_id

uint16 image_x
uint16 image_y
uint16 box_w
uint16 box_h

float32 confidence
float32 bearing_x_rad
float32 bearing_y_rad
float32 size_ratio

uint32 source_age_ms
uint32 frame_count
uint32 parse_error_count
# TRACK flight-mode additions. The legacy fields above are retained for
# protocol diagnostics, the dashboard and the optional gimbal action path.
uint64 timestamp_sample

uint8 TRACKING_STATE_NONE = 0
uint8 TRACKING_STATE_DETECTED = 1
uint8 TRACKING_STATE_TRACKING = 2
uint8 TRACKING_STATE_PREDICTED = 3
uint8 tracking_state

uint32 sequence

# Unit line-of-sight vector in PX4 body FRD. With the camera mounted upward,
# an image-centre target is [0, 0, -1].
float32[3] direction_body

# Normalized image coordinates in [-1, 1] and normalized target area.
float32 image_x_normalized
float32 image_y_normalized
float32 target_size