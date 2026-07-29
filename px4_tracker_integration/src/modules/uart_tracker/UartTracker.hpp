#pragma once

#include <px4_platform_common/module.h>
#include <uORB/Publication.hpp>
#include <uORB/topics/tracker_target.h>

#include <stdint.h>

class UartTracker : public ModuleBase<UartTracker>
{
public:
	UartTracker(const char *device, int baudrate, uint16_t image_width, uint16_t image_height,
		    float hfov_deg, float vfov_deg);
	~UartTracker() override;

	static int task_spawn(int argc, char *argv[]);
	static UartTracker *instantiate(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);

	int print_status() override;
	void run() override;

private:
	struct DecodedTarget {
		bool valid{false};
		uint16_t image_x{0};
		uint16_t image_y{0};
		uint16_t box_w{0};
		uint16_t box_h{0};
		uint8_t confidence{0};
		uint32_t source_age_ms{0};
	};

	enum class ParserState : uint8_t {
		SyncA,
		SyncB,
		Length,
		Body,
		CrcLow,
		CrcHigh,
	};

	static constexpr uint8_t kSyncA = 0xAA;
	static constexpr uint8_t kSyncB = 0x55;
	static constexpr uint8_t kTrackerMsgId = 0x01;
	static constexpr uint8_t kMaxFrameLength = 64;
	static constexpr uint8_t kTargetPayloadLength = 14;

	bool open_uart();
	void close_uart();
	bool configure_uart();
	void parse_byte(uint8_t byte);
	void handle_frame(const uint8_t *body, uint8_t length);
	bool decode_tracker_payload(const uint8_t *payload, uint8_t length, DecodedTarget &target);
	void publish_target(const DecodedTarget &target);

	static uint16_t crc16_ccitt_false(const uint8_t *data, uint8_t length);
	static uint16_t read_u16_le(const uint8_t *data);
	static uint32_t read_u32_le(const uint8_t *data);
	static float deg_to_rad(float deg);

	char _device[32]{};
	int _baudrate{115200};
	int _fd{-1};

	uint16_t _image_width{1280};
	uint16_t _image_height{720};
	float _hfov_rad{0.f};
	float _vfov_rad{0.f};

	ParserState _state{ParserState::SyncA};
	uint8_t _frame_length{0};
	uint8_t _body[kMaxFrameLength]{};
	uint8_t _body_index{0};
	uint8_t _crc_low{0};

	uint32_t _frame_count{0};
	uint32_t _parse_error_count{0};

	uORB::Publication<tracker_target_s> _tracker_target_pub{ORB_ID(tracker_target)};
};
