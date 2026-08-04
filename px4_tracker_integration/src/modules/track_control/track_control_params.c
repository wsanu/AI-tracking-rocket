/**
 * TRACK target timeout
 *
 * The last valid line-of-sight direction is held for this interval.
 *
 * @unit s
 * @min 0.05
 * @max 2.0
 * @decimal 2
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_TGT_TIMEOUT, 0.30f);

/**
 * TRACK target-loss failover delay
 *
 * Target attitude is frozen after TRK_TGT_TIMEOUT. At this total target age,
 * TRACK requests the configured safe mode.
 *
 * @unit s
 * @min 0.10
 * @max 5.0
 * @decimal 2
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_LOST_DELAY, 0.80f);

/**
 * TRACK minimum target confidence
 *
 * @min 0.0
 * @max 1.0
 * @decimal 2
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_CONF_MIN, 0.50f);

/**
 * TRACK maximum twist rate
 *
 * @unit deg/s
 * @min 0
 * @max 360
 * @decimal 0
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_TWIST_MAX, 90.0f);

/**
 * TRACK twist stick deadzone
 *
 * @min 0.0
 * @max 0.5
 * @decimal 2
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_TWIST_DZ, 0.05f);

/**
 * TRACK direction filter time constant
 *
 * @unit s
 * @min 0.0
 * @max 2.0
 * @decimal 2
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_DIR_TC, 0.08f);

/**
 * TRACK maximum attitude setpoint rate
 *
 * @unit deg/s
 * @min 10
 * @max 720
 * @decimal 0
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_ATT_RATE_MAX, 120.0f);

/**
 * TRACK maximum tilt from NED down
 *
 * @unit deg
 * @min 5
 * @max 85
 * @decimal 0
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_TILT_MAX, 60.0f);

/**
 * TRACK entry and reacquisition blend time
 *
 * @unit s
 * @min 0.0
 * @max 3.0
 * @decimal 2
 * @group TRACK Control
 */
PARAM_DEFINE_FLOAT(TRK_ENTRY_T, 0.50f);

/**
 * TRACK target-loss action
 *
 * Stabilized is safe without position. Position may only be selected when a
 * valid local position is guaranteed by the vehicle integration.
 *
 * @value 0 Stabilized
 * @value 1 Position
 * @group TRACK Control
 */
PARAM_DEFINE_INT32(TRK_LOST_ACT, 0);