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
Read target tracking frames from a UART vision module and publish tracker_target.
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("uart_tracker", "module");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_PARAM_STRING('d', "/dev/ttyS2", nullptr, "UART device", false);
	PRINT_MODULE_USAGE_PARAM_INT('b', 115200, 9600, 921600, "UART baudrate", true);
	PRINT_MODULE_USAGE_ARG("--width <px>", "image width", true);
	PRINT_MODULE_USAGE_ARG("--height <px>", "image height", true);
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
	case ParserState::SyncA:
		_state = (byte == kSyncA) ? ParserState::SyncB : ParserState::SyncA;
		break;

	case ParserState::SyncB:
		_state = (byte == kSyncB) ? ParserState::Length : ParserState::SyncA;
		break;

	case ParserState::Length:
		if (byte == 0 || byte > kMaxFrameLength) {
			_parse_error_count++;
			_state = ParserState::SyncA;
			break;
		}

		_frame_length = byte;
		_body_index = 0;
		_body[_body_index++] = byte;
		_state = ParserState::Body;
		break;

	case ParserState::Body:
		_body[_body_index++] = byte;

		if (_body_index >= _frame_length + 1) {
			_state = ParserState::CrcLow;
		}

		break;

	case ParserState::CrcLow:
		_crc_low = byte;
		_state = ParserState::CrcHigh;
		break;

	case ParserState::CrcHigh: {
			const uint16_t received_crc = (uint16_t)_crc_low | ((uint16_t)byte << 8);
			const uint16_t computed_crc = crc16_ccitt_false(_body, _frame_length + 1);

			if (received_crc == computed_crc) {
				handle_frame(&_body[1], _frame_length);

			} else {
				_parse_error_count++;
			}

			_state = ParserState::SyncA;
			break;
		}
	}
}

void UartTracker::handle_frame(const uint8_t *body, uint8_t length)
{
	if (length < 1 || body[0] != kTrackerMsgId) {
		_parse_error_count++;
		return;
	}

	DecodedTarget target{};

	if (!decode_tracker_payload(&body[1], length - 1, target)) {
		_parse_error_count++;
		return;
	}

	_frame_count++;
	publish_target(target);
}

bool UartTracker::decode_tracker_payload(const uint8_t *payload, uint8_t length, DecodedTarget &target)
{
	if (length != kTargetPayloadLength) {
		return false;
	}

	target.image_x = read_u16_le(&payload[0]);
	target.image_y = read_u16_le(&payload[2]);
	target.box_w = read_u16_le(&payload[4]);
	target.box_h = read_u16_le(&payload[6]);
	target.confidence = payload[8];
	target.valid = (payload[9] & 0x01) != 0;
	target.source_age_ms = read_u32_le(&payload[10]);

	return target.image_x < _image_width && target.image_y < _image_height && target.confidence <= 100;
}

void UartTracker::publish_target(const DecodedTarget &target)
{
	tracker_target_s msg{};
	msg.timestamp = hrt_absolute_time();
	msg.valid = target.valid;
	msg.target_id = 0;
	msg.image_x = target.image_x;
	msg.image_y = target.image_y;
	msg.box_w = target.box_w;
	msg.box_h = target.box_h;
	msg.confidence = (float)target.confidence * 0.01f;

	const float centered_x = ((float)target.image_x - ((float)_image_width * 0.5f)) / ((float)_image_width * 0.5f);
	const float centered_y = ((float)target.image_y - ((float)_image_height * 0.5f)) / ((float)_image_height * 0.5f);

	msg.bearing_x_rad = centered_x * (_hfov_rad * 0.5f);
	msg.bearing_y_rad = -centered_y * (_vfov_rad * 0.5f);
	msg.size_ratio = ((float)target.box_w * (float)target.box_h) / ((float)_image_width * (float)_image_height);
	msg.source_age_ms = target.source_age_ms;
	msg.frame_count = _frame_count;
	msg.parse_error_count = _parse_error_count;

	_tracker_target_pub.publish(msg);
}

uint16_t UartTracker::crc16_ccitt_false(const uint8_t *data, uint8_t length)
{
	uint16_t crc = 0xFFFF;

	for (uint8_t i = 0; i < length; i++) {
		crc ^= (uint16_t)data[i] << 8;

		for (uint8_t bit = 0; bit < 8; bit++) {
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
		}
	}

	return crc;
}

uint16_t UartTracker::read_u16_le(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t UartTracker::read_u32_le(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

float UartTracker::deg_to_rad(float deg)
{
	return deg * (float)M_PI / 180.f;
}

extern "C" __EXPORT int uart_tracker_main(int argc, char *argv[])
{
	return UartTracker::main(argc, argv);
}
