#pragma once

#include <px4_platform_common/module.h>
#include <uORB/Publication.hpp>
#include <uORB/topics/debug_array.h>
#include <uORB/topics/gimbal_manager_set_manual_control.h>
#include <uORB/topics/tracker_target.h>

#include <stdint.h>

class UartTracker : public ModuleBase<UartTracker>
{
public:
	UartTracker(const char *device, int baudrate, uint16_t image_width, uint16_t image_height,
		    float hfov_deg, float vfov_deg, bool gimbal_action_enabled, float action_gain,
		    float action_deadband_rad);
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
		bool running{false};
		bool angle_mode{false};
		bool bearing_rad_valid{false};
		uint8_t target_id{0};
		uint8_t target_type{0};
		uint16_t image_x{0};
		uint16_t image_y{0};
		uint16_t box_w{0};
		uint16_t box_h{0};
		uint8_t confidence{100};
		float bearing_x_rad{0.f};
		float bearing_y_rad{0.f};
		uint32_t source_age_ms{0};
	};

	enum class ParserState : uint8_t {
		Head0,
		Head1,
		Cmd0,
		Cmd1,
		Length,
		Payload,
		Checksum,
		End,
	};

	static constexpr uint8_t kFeedbackHead0 = 0x78;
	static constexpr uint8_t kFeedbackHead1 = 0x07;
	static constexpr uint8_t kFeedbackEnd = 0x79;
	static constexpr uint8_t kPeriodicCmd = 0x00;
	static constexpr uint8_t kMissDistanceCmd = 0x81;
	static constexpr uint8_t kDetectionCmd = 0x82;
	static constexpr uint8_t kHeartbeatCmd = 0x83;
	static constexpr uint8_t kMaxPayloadLength = 255;
	static constexpr uint8_t kMissDistancePayloadLength = 14;
	static constexpr uint8_t kDetectionHeaderLength = 3;
	static constexpr uint8_t kDetectionTargetLength = 11;
	static constexpr uint8_t kMaxDetectionTargets = 22;
	static constexpr uint8_t kHeartbeatPayloadLength = 6;
	static constexpr uint64_t kActionTimeoutUs = 500000;

	bool open_uart();
	void close_uart();
	bool configure_uart();
	void parse_byte(uint8_t byte);
	void handle_frame(uint8_t cmd0, uint8_t cmd1, const uint8_t *payload, uint8_t length);
	bool decode_miss_distance_payload(const uint8_t *payload, uint8_t length, DecodedTarget &target);
	bool decode_detection_payload(const uint8_t *payload, uint8_t length, DecodedTarget &target);
	void handle_heartbeat_payload(const uint8_t *payload, uint8_t length);
	void publish_target(const DecodedTarget &target);
	void publish_gimbal_action(const tracker_target_s &msg, const DecodedTarget &target);
	void check_action_timeout();

	static uint8_t checksum8(const uint8_t *data, uint16_t length);
	static uint16_t read_u16_le(const uint8_t *data);
	static int32_t read_i32_le(const uint8_t *data);
	static uint32_t read_u32_le(const uint8_t *data);
	static float read_float_le(const uint8_t *data);
	static float deg_to_rad(float deg);
	static float clamp_float(float value, float min_value, float max_value);
	static uint16_t clamp_u16_from_i32(int32_t value, uint16_t max_value);

	char _device[32]{};
	int _baudrate{115200};
	int _fd{-1};

	uint16_t _image_width{1280};
	uint16_t _image_height{720};
	float _hfov_rad{0.f};
	float _vfov_rad{0.f};
	bool _gimbal_action_enabled{false};
	float _action_gain{1.f};
	float _action_deadband_rad{0.01f};

	ParserState _state{ParserState::Head0};
	uint8_t _cmd0{0};
	uint8_t _cmd1{0};
	uint8_t _payload_length{0};
	uint8_t _payload[kMaxPayloadLength]{};
	uint8_t _payload_index{0};
	uint8_t _checksum{0};

	uint32_t _frame_count{0};
	uint32_t _parse_error_count{0};
	uint32_t _detection_frame_count{0};
	uint32_t _heartbeat_count{0};
	uint16_t _last_heartbeat_counter{0};
	uint32_t _last_heartbeat_error_code{0};
	uint8_t _last_detection_total_targets{0};
	uint8_t _last_detection_current_targets{0};
	uint64_t _last_target_frame_timestamp{0};
	uint32_t _action_timeout_count{0};
	bool _action_timeout_triggered{false};

	uORB::Publication<tracker_target_s> _tracker_target_pub{ORB_ID(tracker_target)};
	uORB::Publication<debug_array_s> _debug_array_pub{ORB_ID(debug_array)};
	uORB::Publication<gimbal_manager_set_manual_control_s> _gimbal_action_pub{ORB_ID(gimbal_manager_set_manual_control)};
};
