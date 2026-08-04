#include "UartTracker.hpp"

#include <px4_platform_common/getopt.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/posix.h>
#include <drivers/drv_hrt.h>

#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

UartTracker::UartTracker(const char *device, int baudrate, uint16_t image_width, uint16_t image_height,
			 float hfov_deg, float vfov_deg, bool gimbal_action_enabled, float action_gain,
			 float action_deadband_rad) :
	_baudrate(baudrate),
	_image_width(image_width),
	_image_height(image_height),
	_hfov_rad(deg_to_rad(hfov_deg)),
	_vfov_rad(deg_to_rad(vfov_deg)),
	_gimbal_action_enabled(gimbal_action_enabled),
	_action_gain(action_gain),
	_action_deadband_rad(action_deadband_rad)
{
	strncpy(_device, device, sizeof(_device) - 1);
}

UartTracker::~UartTracker()
{
	close_uart();
}

int UartTracker::task_spawn(int argc, char *argv[])
{
	_task_id = px4_task_spawn_cmd("uart_tracker",
				      SCHED_DEFAULT,
				      SCHED_PRIORITY_DEFAULT,
				      PX4_STACK_ADJUSTED(3000),
				      (px4_main_t)&run_trampoline,
				      (char *const *)argv);

	if (_task_id < 0) {
		_task_id = -1;
		return -errno;
	}

	return 0;
}

UartTracker *UartTracker::instantiate(int argc, char *argv[])
{
	const char *device = "/dev/ttyS7";
	int baudrate = 115200;
	uint16_t image_width = 1280;
	uint16_t image_height = 720;
	float hfov_deg = 62.f;
	float vfov_deg = 48.f;
	bool gimbal_action_enabled = false;
	float action_gain = 1.f;
	float action_deadband_deg = 0.5f;

	int ch;
	int myoptind = 1;
	const char *myoptarg = nullptr;

	while ((ch = px4_getopt(argc, argv, "d:b:", &myoptind, &myoptarg)) != EOF) {
		switch (ch) {
		case 'd':
			device = myoptarg;
			break;

		case 'b':
			baudrate = atoi(myoptarg);
			break;

		default:
			return nullptr;
		}
	}

	for (int i = myoptind; i < argc; i++) {
		if (!strcmp(argv[i], "--width") && i + 1 < argc) {
			image_width = (uint16_t)atoi(argv[++i]);

		} else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
			image_height = (uint16_t)atoi(argv[++i]);

		} else if (!strcmp(argv[i], "--hfov") && i + 1 < argc) {
			hfov_deg = strtof(argv[++i], nullptr);

		} else if (!strcmp(argv[i], "--vfov") && i + 1 < argc) {
			vfov_deg = strtof(argv[++i], nullptr);

		} else if (!strcmp(argv[i], "--action") && i + 1 < argc) {
			const char *action = argv[++i];

			if (!strcmp(action, "none")) {
				gimbal_action_enabled = false;

			} else if (!strcmp(action, "gimbal")) {
				gimbal_action_enabled = true;

			} else {
				PX4_ERR("unknown action: %s", action);
				return nullptr;
			}

		} else if (!strcmp(argv[i], "--action-gain") && i + 1 < argc) {
			action_gain = strtof(argv[++i], nullptr);

		} else if (!strcmp(argv[i], "--deadband-deg") && i + 1 < argc) {
			action_deadband_deg = strtof(argv[++i], nullptr);

		} else {
			PX4_ERR("unknown option: %s", argv[i]);
			return nullptr;
		}
	}

	return new UartTracker(device, baudrate, image_width, image_height, hfov_deg, vfov_deg, gimbal_action_enabled,
			       action_gain, deg_to_rad(action_deadband_deg));
}

int UartTracker::custom_command(int argc, char *argv[])
{
	return print_usage("unknown command");
}

int UartTracker::print_usage(const char *reason)
{
	if (reason) {
		PX4_WARN("%s", reason);
	}

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
Read Huiyan V3.1 tracking feedback frames from UART and publish tracker_target.
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("uart_tracker", "module");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_PARAM_STRING('d', "/dev/ttyS7", nullptr, "UART device", false);
	PRINT_MODULE_USAGE_PARAM_INT('b', 115200, 9600, 921600, "UART baudrate", true);
	PRINT_MODULE_USAGE_ARG("--width <px>", "video output width", true);
	PRINT_MODULE_USAGE_ARG("--height <px>", "video output height", true);
	PRINT_MODULE_USAGE_ARG("--hfov <deg>", "horizontal camera field of view", true);
	PRINT_MODULE_USAGE_ARG("--vfov <deg>", "vertical camera field of view", true);
	PRINT_MODULE_USAGE_ARG("--action <none|gimbal>", "convert valid UART target frames into an action output", true);
	PRINT_MODULE_USAGE_ARG("--action-gain <value>", "gimbal action gain before clamping to -1..1", true);
	PRINT_MODULE_USAGE_ARG("--deadband-deg <deg>", "ignore target bearing error below this angle", true);
	PRINT_MODULE_USAGE_COMMAND("stop");
	PRINT_MODULE_USAGE_COMMAND("status");
	return PX4_OK;
}

int UartTracker::print_status()
{
	PX4_INFO("device: %s, baudrate: %d", _device, _baudrate);
	PX4_INFO("frames: %" PRIu32 ", parse errors: %" PRIu32, _frame_count, _parse_error_count);
	PX4_INFO("AI detection frames: %" PRIu32 ", targets: %u/%u", _detection_frame_count,
		 (unsigned)_last_detection_current_targets, (unsigned)_last_detection_total_targets);
	PX4_INFO("heartbeats: %" PRIu32 ", last count: %u, error: 0x%08" PRIx32, _heartbeat_count,
		 (unsigned)_last_heartbeat_counter, _last_heartbeat_error_code);
	PX4_INFO("gimbal action: %s, gain: %.2f, deadband: %.4f rad", _gimbal_action_enabled ? "on" : "off",
		 (double)_action_gain, (double)_action_deadband_rad);
	PX4_INFO("action timeout: 500 ms, count: %" PRIu32, _action_timeout_count);
	return PX4_OK;
}

void UartTracker::run()
{
	if (!open_uart()) {
		PX4_ERR("failed to open UART");
		return;
	}

	uint8_t buffer[64];
	pollfd fds{};
	fds.fd = _fd;
	fds.events = POLLIN;

	while (!should_exit()) {
		const int ret = px4_poll(&fds, 1, 100);

		if (ret < 0) {
			px4_usleep(10000);

		} else if (ret > 0 && (fds.revents & POLLIN)) {
			const ssize_t bytes_read = ::read(_fd, buffer, sizeof(buffer));

			if (bytes_read > 0) {
				for (ssize_t i = 0; i < bytes_read; i++) {
					parse_byte(buffer[i]);
				}
			}
		}

		check_action_timeout();
	}

	if (_gimbal_action_enabled) {
		DecodedTarget stopped_target{};
		stopped_target.running = false;
		stopped_target.confidence = 0;
		stopped_target.image_x = _image_width / 2;
		stopped_target.image_y = _image_height / 2;
		publish_target(stopped_target);
	}
}

bool UartTracker::open_uart()
{
	_fd = ::open(_device, O_RDONLY | O_NOCTTY | O_NONBLOCK);

	if (_fd < 0) {
		PX4_ERR("open %s failed", _device);
		return false;
	}

	if (!configure_uart()) {
		close_uart();
		return false;
	}

	return true;
}

void UartTracker::close_uart()
{
	if (_fd >= 0) {
		::close(_fd);
		_fd = -1;
	}
}

bool UartTracker::configure_uart()
{
	struct termios uart_config {};

	if (tcgetattr(_fd, &uart_config) < 0) {
		PX4_ERR("tcgetattr failed");
		return false;
	}

	cfmakeraw(&uart_config);
	uart_config.c_cflag |= (CLOCAL | CREAD);
#ifdef CRTSCTS
	uart_config.c_cflag &= ~CRTSCTS;
#endif

	speed_t speed = B115200;

	switch (_baudrate) {
	case 57600: speed = B57600; break;
	case 115200: speed = B115200; break;
#ifdef B230400
	case 230400: speed = B230400; break;
#endif
#ifdef B460800
	case 460800: speed = B460800; break;
#endif
#ifdef B921600
	case 921600: speed = B921600; break;
#endif
	default:
		PX4_WARN("unsupported baudrate %d, using 115200", _baudrate);
		speed = B115200;
		break;
	}

	cfsetispeed(&uart_config, speed);
	cfsetospeed(&uart_config, speed);

	if (tcsetattr(_fd, TCSANOW, &uart_config) < 0) {
		PX4_ERR("tcsetattr failed");
		return false;
	}

	return true;
}

void UartTracker::parse_byte(uint8_t byte)
{
	switch (_state) {
	case ParserState::Head0:
		_state = (byte == kFeedbackHead0) ? ParserState::Head1 : ParserState::Head0;
		break;

	case ParserState::Head1:
		_state = (byte == kFeedbackHead1) ? ParserState::Cmd0 : ParserState::Head0;
		break;

	case ParserState::Cmd0:
		_cmd0 = byte;
		_state = ParserState::Cmd1;
		break;

	case ParserState::Cmd1:
		_cmd1 = byte;
		_state = ParserState::Length;
		break;

	case ParserState::Length:
		_payload_length = byte;
		_payload_index = 0;
		_state = (_payload_length == 0) ? ParserState::Checksum : ParserState::Payload;
		break;

	case ParserState::Payload:
		_payload[_payload_index++] = byte;

		if (_payload_index >= _payload_length) {
			_state = ParserState::Checksum;
		}

		break;

	case ParserState::Checksum:
		_checksum = byte;
		_state = ParserState::End;
		break;

	case ParserState::End: {
			if (byte != kFeedbackEnd) {
				_parse_error_count++;
				_state = ParserState::Head0;
				break;
			}

			const uint8_t expected_checksum =
				(uint8_t)(_cmd0 + _cmd1 + _payload_length + checksum8(_payload, _payload_length));

			if (_checksum == expected_checksum) {
				handle_frame(_cmd0, _cmd1, _payload, _payload_length);

			} else {
				_parse_error_count++;
			}

			_state = ParserState::Head0;
			break;
		}
	}
}

void UartTracker::handle_frame(uint8_t cmd0, uint8_t cmd1, const uint8_t *payload, uint8_t length)
{
	if (cmd0 != kPeriodicCmd) {
		return;
	}

	DecodedTarget target{};

	if (cmd1 == kMissDistanceCmd) {
		if (!decode_miss_distance_payload(payload, length, target)) {
			_parse_error_count++;
			return;
		}

		_frame_count++;
		_last_target_frame_timestamp = hrt_absolute_time();
		_action_timeout_triggered = false;
		publish_target(target);
		return;
	}

	if (cmd1 == kDetectionCmd) {
		if (!decode_detection_payload(payload, length, target)) {
			_parse_error_count++;
			return;
		}

		_frame_count++;
		_detection_frame_count++;
		_last_target_frame_timestamp = hrt_absolute_time();
		_action_timeout_triggered = false;
		publish_target(target);
		return;
	}

	if (cmd1 == kHeartbeatCmd) {
		handle_heartbeat_payload(payload, length);
		return;
	}
}

bool UartTracker::decode_miss_distance_payload(const uint8_t *payload, uint8_t length, DecodedTarget &target)
{
	if (length != kMissDistancePayloadLength) {
		return false;
	}

	const uint8_t status = payload[0];
	const bool angle_mode = (status & (1 << 2)) != 0;
	const bool tracker_stopped = (status & (1 << 1)) != 0;
	const bool data_valid = (status & (1 << 0)) != 0;

	target.running = !tracker_stopped;
	target.angle_mode = angle_mode;
	target.valid = data_valid && !tracker_stopped;
	target.target_id = payload[1];
	target.box_w = read_u16_le(&payload[10]);
	target.box_h = read_u16_le(&payload[12]);
	target.confidence = target.valid ? 100 : 0;

	if (angle_mode) {
		const float offset_right_deg = read_float_le(&payload[2]);
		const float offset_up_deg = read_float_le(&payload[6]);

		target.bearing_rad_valid = true;
		target.bearing_x_rad = deg_to_rad(offset_right_deg);
		target.bearing_y_rad = deg_to_rad(offset_up_deg);
		target.image_x = _image_width / 2;
		target.image_y = _image_height / 2;
		return true;
	}

	const int32_t offset_right_px = read_i32_le(&payload[2]);
	const int32_t offset_up_px = read_i32_le(&payload[6]);
	const int32_t center_x = (int32_t)_image_width / 2;
	const int32_t center_y = (int32_t)_image_height / 2;

	target.image_x = clamp_u16_from_i32(center_x + offset_right_px, _image_width > 0 ? _image_width - 1 : 0);
	target.image_y = clamp_u16_from_i32(center_y - offset_up_px, _image_height > 0 ? _image_height - 1 : 0);

	return true;
}

bool UartTracker::decode_detection_payload(const uint8_t *payload, uint8_t length, DecodedTarget &target)
{
	if (length < kDetectionHeaderLength) {
		return false;
	}

	const uint8_t total_targets = payload[1];
	const uint8_t current_targets = payload[2];

	if (current_targets > kMaxDetectionTargets) {
		return false;
	}

	const uint16_t expected_length = kDetectionHeaderLength + (uint16_t)current_targets * kDetectionTargetLength;

	if (length < expected_length) {
		return false;
	}

	_last_detection_total_targets = total_targets;
	_last_detection_current_targets = current_targets;

	target.running = true;
	target.valid = false;
	target.confidence = 0;
	target.image_x = _image_width / 2;
	target.image_y = _image_height / 2;

	uint8_t best_confidence = 0;
	uint8_t best_index = 0;

	for (uint8_t i = 0; i < current_targets; i++) {
		const uint8_t offset = kDetectionHeaderLength + i * kDetectionTargetLength;
		const uint8_t confidence = payload[offset + 2];

		if (i == 0 || confidence > best_confidence) {
			best_confidence = confidence;
			best_index = i;
		}
	}

	if (current_targets == 0) {
		return true;
	}

	const uint8_t offset = kDetectionHeaderLength + best_index * kDetectionTargetLength;
	const uint16_t box_x = read_u16_le(&payload[offset + 3]);
	const uint16_t box_y = read_u16_le(&payload[offset + 5]);
	const uint16_t box_w = read_u16_le(&payload[offset + 7]);
	const uint16_t box_h = read_u16_le(&payload[offset + 9]);
	const uint16_t max_x = _image_width > 0 ? _image_width - 1 : 0;
	const uint16_t max_y = _image_height > 0 ? _image_height - 1 : 0;

	target.target_id = payload[offset];
	target.target_type = payload[offset + 1];
	target.confidence = best_confidence;
	target.box_w = box_w;
	target.box_h = box_h;
	target.image_x = clamp_u16_from_i32((int32_t)box_x + ((int32_t)box_w / 2), max_x);
	target.image_y = clamp_u16_from_i32((int32_t)box_y + ((int32_t)box_h / 2), max_y);
	target.valid = best_confidence > 0 && box_w > 0 && box_h > 0;

	return true;
}

void UartTracker::handle_heartbeat_payload(const uint8_t *payload, uint8_t length)
{
	if (length < kHeartbeatPayloadLength) {
		_parse_error_count++;
		return;
	}

	_heartbeat_count++;
	_last_heartbeat_counter = read_u16_le(&payload[0]);
	_last_heartbeat_error_code = read_u32_le(&payload[2]);
}

void UartTracker::publish_target(const DecodedTarget &target)
{
	tracker_target_s msg{};
	msg.timestamp = hrt_absolute_time();
	msg.timestamp_sample = msg.timestamp;
	msg.valid = target.valid;
	msg.target_id = target.target_id;
	msg.image_x = target.image_x;
	msg.image_y = target.image_y;
	msg.box_w = target.box_w;
	msg.box_h = target.box_h;
	msg.confidence = (float)target.confidence * 0.01f;

	if (target.bearing_rad_valid) {
		msg.bearing_x_rad = target.bearing_x_rad;
		msg.bearing_y_rad = target.bearing_y_rad;

	} else {
		const float centered_x = ((float)target.image_x - ((float)_image_width * 0.5f)) / ((float)_image_width * 0.5f);
		const float centered_y = ((float)target.image_y - ((float)_image_height * 0.5f)) / ((float)_image_height * 0.5f);

		msg.bearing_x_rad = centered_x * (_hfov_rad * 0.5f);
		msg.bearing_y_rad = -centered_y * (_vfov_rad * 0.5f);
	}

	msg.size_ratio = ((float)target.box_w * (float)target.box_h) / ((float)_image_width * (float)_image_height);
	msg.source_age_ms = target.source_age_ms;
	msg.frame_count = _frame_count;
	msg.parse_error_count = _parse_error_count;
	msg.tracking_state = !target.running ? tracker_target_s::TRACKING_STATE_NONE :
			     (target.valid ? tracker_target_s::TRACKING_STATE_TRACKING : tracker_target_s::TRACKING_STATE_PREDICTED);
	msg.sequence = _frame_count;

	// Default upward-camera mounting: optical axis is body -Z, image right is
	// body +Y and image up is body +X. The TRACK controller consumes this FRD
	// unit vector and does not need image dimensions or camera FOV.
	const float direction_x = tanf(msg.bearing_y_rad);
	const float direction_y = tanf(msg.bearing_x_rad);
	const float direction_z = -1.f;
	const float direction_norm = sqrtf(direction_x * direction_x + direction_y * direction_y + direction_z * direction_z);
	msg.direction_body[0] = direction_x / direction_norm;
	msg.direction_body[1] = direction_y / direction_norm;
	msg.direction_body[2] = direction_z / direction_norm;
	msg.image_x_normalized = ((float)target.image_x - ((float)_image_width * 0.5f)) /
				 ((float)_image_width * 0.5f);
	msg.image_y_normalized = ((float)target.image_y - ((float)_image_height * 0.5f)) /
				 ((float)_image_height * 0.5f);
	msg.target_size = msg.size_ratio;
	_tracker_target_pub.publish(msg);

	// Temporary read-only bridge used by the USB dashboard during integration.
	debug_array_s debug{};
	debug.timestamp = msg.timestamp;
	debug.id = 0x8100;
	strncpy(debug.name, "TRK_V31", sizeof(debug.name) - 1);
	debug.data[0] = target.valid ? 1.f : 0.f;
	debug.data[1] = target.running ? 1.f : 0.f;
	debug.data[2] = target.angle_mode ? 1.f : 0.f;
	debug.data[3] = (float)target.target_id;
	debug.data[4] = (float)target.image_x;
	debug.data[5] = (float)target.image_y;
	debug.data[6] = (float)target.box_w;
	debug.data[7] = (float)target.box_h;
	debug.data[8] = msg.bearing_x_rad;
	debug.data[9] = msg.bearing_y_rad;
	debug.data[10] = msg.size_ratio;
	debug.data[11] = (float)_frame_count;
	debug.data[12] = (float)_parse_error_count;
	_debug_array_pub.publish(debug);
	publish_gimbal_action(msg, target);
}

void UartTracker::publish_gimbal_action(const tracker_target_s &msg, const DecodedTarget &target)
{
	if (!_gimbal_action_enabled) {
		return;
	}

	float yaw_rate = 0.f;
	float pitch_rate = 0.f;

	if (target.valid && target.running) {
		const float bearing_x = fabsf(msg.bearing_x_rad) < _action_deadband_rad ? 0.f : msg.bearing_x_rad;
		const float bearing_y = fabsf(msg.bearing_y_rad) < _action_deadband_rad ? 0.f : msg.bearing_y_rad;
		const float half_hfov = _hfov_rad > 0.f ? _hfov_rad * 0.5f : 1.f;
		const float half_vfov = _vfov_rad > 0.f ? _vfov_rad * 0.5f : 1.f;

		yaw_rate = clamp_float((bearing_x / half_hfov) * _action_gain, -1.f, 1.f);
		pitch_rate = clamp_float((-bearing_y / half_vfov) * _action_gain, -1.f, 1.f);
	}

	gimbal_manager_set_manual_control_s action{};
	action.timestamp = msg.timestamp;
	action.origin_sysid = 1;
	action.origin_compid = 1;
	action.target_system = 0;
	action.target_component = 0;
	action.flags = 0;
	action.gimbal_device_id = 0;
	action.pitch = (float)NAN;
	action.yaw = (float)NAN;
	action.pitch_rate = pitch_rate;
	action.yaw_rate = yaw_rate;
	_gimbal_action_pub.publish(action);
}

void UartTracker::check_action_timeout()
{
	if (!_gimbal_action_enabled || _last_target_frame_timestamp == 0 || _action_timeout_triggered
	    || hrt_elapsed_time(&_last_target_frame_timestamp) <= kActionTimeoutUs) {
		return;
	}

	DecodedTarget timeout_target{};
	timeout_target.running = false;
	timeout_target.confidence = 0;
	timeout_target.image_x = _image_width / 2;
	timeout_target.image_y = _image_height / 2;
	timeout_target.source_age_ms = kActionTimeoutUs / 1000;
	_action_timeout_triggered = true;
	_action_timeout_count++;
	publish_target(timeout_target);
}

uint8_t UartTracker::checksum8(const uint8_t *data, uint16_t length)
{
	uint32_t sum = 0;

	for (uint16_t i = 0; i < length; i++) {
		sum += data[i];
	}

	return (uint8_t)(sum & 0xff);
}

uint16_t UartTracker::read_u16_le(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

int32_t UartTracker::read_i32_le(const uint8_t *data)
{
	return (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
}

uint32_t UartTracker::read_u32_le(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

float UartTracker::read_float_le(const uint8_t *data)
{
	uint32_t raw = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
	float value{};
	memcpy(&value, &raw, sizeof(value));
	return value;
}

float UartTracker::deg_to_rad(float deg)
{
	return deg * (float)M_PI / 180.f;
}

float UartTracker::clamp_float(float value, float min_value, float max_value)
{
	if (value < min_value) {
		return min_value;
	}

	if (value > max_value) {
		return max_value;
	}

	return value;
}

uint16_t UartTracker::clamp_u16_from_i32(int32_t value, uint16_t max_value)
{
	if (value < 0) {
		return 0;
	}

	if (value > (int32_t)max_value) {
		return max_value;
	}

	return (uint16_t)value;
}

extern "C" __EXPORT int uart_tracker_main(int argc, char *argv[])
{
	return UartTracker::main(argc, argv);
}
