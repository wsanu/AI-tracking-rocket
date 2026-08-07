/**
 * TRACK RC auxiliary input
 *
 * Selects the manual_control_setpoint AUX channel used by uart_tracker for
 * the three-position Huiyan detection and automatic-lock switch. Zero keeps
 * RC tracker control disabled and preserves manual UART command behavior.
 *
 * @value 0 Disabled
 * @value 1 AUX1
 * @value 2 AUX2
 * @value 3 AUX3
 * @value 4 AUX4
 * @value 5 AUX5
 * @value 6 AUX6
 * @min 0
 * @max 6
 * @group TRACK Control
 */
PARAM_DEFINE_INT32(TRK_RC_AUX, 0);
