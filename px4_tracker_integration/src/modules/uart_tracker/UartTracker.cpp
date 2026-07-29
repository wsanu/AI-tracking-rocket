#include "UartTracker.hpp"

#include <px4_platform_common/getopt.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/posix.h>
#include <drivers/drv_hrt.h>

#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

UartTracker::UartTracker(const char *device, int baudrate, uint16_t image_width, uint16_t image_height,
			 float hfov_deg, float vfov_deg) :
	_baudrate(baudrate),
	_image_width(image_width),
	_image_height(image_height),
	_hfov_rad(deg_to_rad(hfov_deg)),
	_vfov_rad(deg_to_rad(vfov_deg))
{
	strncpy(_device, device, sizeof(_device) - 1);
}

UartTracker::~UartTracker()
{
	close_uart();
}

int UartTracker::task_spawn(int argc, char *argv[])
{
	UartTracker *instance = instantiate(argc, argv);

	if (instance == nullptr) {
		return PX4_ERROR;
	}

	_object.store(instance);
	_task_id = px4_task_spawn_cmd("uart_tracker",
				      SCHED_DEFAULT,
				      SCHED_PRIORITY_DEFAULT,
				      1800,
				      (px4_main_t)&run_trampoline,
				      nullptr);

	if (_task_id < 0) {
		_object.store(nullptr);
		delete instance;
		return PX4_ERROR;
	}

	return PX4_OK;
}

UartTracker *UartTracker::instantiate(int argc, char *argv[])
{
	const char *device = "/dev/ttyS2";
	int baudrate = 115200;
	uint16_t image_width = 1280;
	uint16_t image_height = 720;
	float hfov_deg = 62.f;
	float vfov_deg = 48.f;

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

		} else {
			PX4_ERR("unknown option: %s", argv[i]);
			return nullptr;
		}
	}

	return new UartTracker(device, baudrate, image_width, image_height, hfov_deg, vfov_deg);
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
	PRINT_MODULE_USAGE_PARAM_STRING('d', "/dev/ttyS2", nullptr, "UART device", false);
	PRINT_MODULE_USAGE_PARAM_INT('b', 115200, 9600, 921600, "UART baudrate", true);
	PRINT_MODULE_USAGE_ARG("--width <px>", "video output width", true);
	PRINT_MODULE_USAGE_ARG("--height <px>", "video output height", true);
	PRINT_MODULE_USAGE_ARG("--hfov <deg>", "horizontal camera field of view", true);
	PRINT_MODULE_USAGE_ARG("--vfov <deg>", "vertical camera field of view", true);
	PRINT_MODULE_USAGE_COMMAND("stop");
	PRINT_MODULE_USAGE_COMMAND("status");
	return PX4_OK;
}

int UartTracker::print_status()
{
	PX4_INFO("device: %s, baudrate: %d", _device, _baudrate);
	PX4_INFO("frames: %" PRIu32 ", parse errors: %" PRIu32, _frame_count, _parse_error_count);
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
			continue;
		}

		if (ret == 0 || !(fds.revents & POLLIN)) {
			continue;
		}

		const ssize_t bytes_read = ::read(_fd, buffer, sizeof(buffer));

		if (bytes_read <= 0) {
			continue;
		}

		for (ssize_t i = 0; i < bytes_read; i++) {
			parse_byte(buffer[i]);
		}
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

			uint8_t checksum_data[kMaxPayloadLength + 3]{};
			checksum_data[0] = _cmd0;
			checksum_data[1] = _cmd1;
			checksum_data[2] = _payload_length;
			memcpy(&checksum_data[3], _payload, _payload_length);

			if (_checksum == checksum8(checksum_data, (uint16_t)_payload_length + 3)) {
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
		publish_target(target);
		return;
	}

	if (cmd1 == kDetectionCmd) {
		// AI detection frames (00 82) are intentionally ignored for control until
		// the 11-byte per-target layout is confirmed from the vendor.
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

void UartTracker::publish_target(const DecodedTarget &target)
{
	tracker_target_s msg{};
	msg.timestamp = hrt_absolute_time();
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

	_tracker_target_pub.publish(msg);
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
