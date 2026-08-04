#pragma once

#include <drivers/drv_hrt.h>
#include <lib/slew_rate/SlewRate.hpp>
#include <matrix/matrix/math.hpp>
#include <px4_platform_common/module.h>
#include <px4_platform_common/module_params.h>
#include <px4_platform_common/px4_work_queue/WorkItem.hpp>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/SubscriptionCallback.hpp>
#include <uORB/topics/action_request.h>
#include <uORB/topics/manual_control_setpoint.h>
#include <uORB/topics/parameter_update.h>
#include <uORB/topics/track_status.h>
#include <uORB/topics/tracker_target.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_attitude_setpoint.h>
#include <uORB/topics/vehicle_control_mode.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/vehicle_status.h>

using namespace time_literals;

class TrackControl : public ModuleBase<TrackControl>, public ModuleParams, public px4::WorkItem
{
public:
	TrackControl();
	~TrackControl() override = default;

	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);

	bool init();
	int print_status() override;

private:
	enum class State : uint8_t {
		Inactive,
		EntryBlend,
		Active,
		TargetGrace,
		Failover,
	};

	void Run() override;
	void enterTrack(const matrix::Quatf &q, hrt_abstime now);
	void leaveTrack();
	bool updateTargetDirection(const matrix::Quatf &q, hrt_abstime now);
	matrix::Quatf buildTrackAttitude(bool &tilt_limited);
	matrix::Quatf interpolateQuaternion(const matrix::Quatf &from, const matrix::Quatf &to, float alpha) const;
	matrix::Quatf limitAttitudeRate(const matrix::Quatf &desired, float dt, bool &limited) const;
	matrix::Vector3f limitDesiredBodyZ(const matrix::Vector3f &desired_z, bool &limited) const;
	matrix::Vector3f rotateAroundAxis(const matrix::Vector3f &vector, const matrix::Vector3f &axis, float angle) const;
	float expoDeadzone(float input) const;
	float throttleCurve(float throttle_input) const;
	void requestFailover(uint8_t reason, hrt_abstime now);
	void publishSetpoint(const matrix::Quatf &q_d, float thrust, bool reset_integral, hrt_abstime now);
	void publishStatus(const matrix::Quatf &q, hrt_abstime now, uint32_t target_age_us,
			   bool target_valid, bool attitude_limited, float twist_rate, float thrust);

	uORB::SubscriptionInterval _parameter_update_sub{ORB_ID(parameter_update), 1_s};
	uORB::Subscription _manual_control_setpoint_sub{ORB_ID(manual_control_setpoint)};
	uORB::Subscription _track_target_sub{ORB_ID(tracker_target)};
	uORB::Subscription _vehicle_control_mode_sub{ORB_ID(vehicle_control_mode)};
	uORB::Subscription _vehicle_land_detected_sub{ORB_ID(vehicle_land_detected)};
	uORB::Subscription _vehicle_status_sub{ORB_ID(vehicle_status)};
	uORB::SubscriptionCallbackWorkItem _vehicle_attitude_sub{this, ORB_ID(vehicle_attitude)};

	uORB::Publication<action_request_s> _action_request_pub{ORB_ID(action_request)};
	uORB::Publication<track_status_s> _track_status_pub{ORB_ID(track_status)};
	uORB::Publication<vehicle_attitude_setpoint_s> _attitude_setpoint_pub{ORB_ID(vehicle_attitude_setpoint)};

	manual_control_setpoint_s _manual{};
	tracker_target_s _target{};
	vehicle_control_mode_s _control_mode{};
	vehicle_status_s _vehicle_status{};

	State _state{State::Inactive};
	matrix::Vector3f _filtered_direction_ned{0.f, 0.f, -1.f};
	matrix::Vector3f _reference_x_ned{1.f, 0.f, 0.f};
	matrix::Quatf _entry_q{};
	matrix::Quatf _last_q_sp{};

	hrt_abstime _last_run{0};
	hrt_abstime _entry_timestamp{0};
	hrt_abstime _last_target_filter_timestamp{0};
	hrt_abstime _last_valid_target_timestamp{0};
	uint32_t _last_target_sequence{0};
	float _twist_angle{0.f};
	float _last_thrust{0.f};
	uint8_t _failure_reason{track_status_s::FAILURE_NONE};
	bool _target_initialized{false};
	bool _failover_requested{false};
	bool _landed{true};
	bool _spooled_up{false};

	SlewRate<float> _manual_throttle_minimum{0.f};
	SlewRate<float> _manual_throttle_maximum{0.f};

	DEFINE_PARAMETERS(
		(ParamFloat<px4::params::TRK_TGT_TIMEOUT>) _param_target_timeout,
		(ParamFloat<px4::params::TRK_LOST_DELAY>) _param_lost_delay,
		(ParamFloat<px4::params::TRK_CONF_MIN>) _param_confidence_min,
		(ParamFloat<px4::params::TRK_TWIST_MAX>) _param_twist_max,
		(ParamFloat<px4::params::TRK_TWIST_DZ>) _param_twist_deadzone,
		(ParamFloat<px4::params::TRK_DIR_TC>) _param_direction_tc,
		(ParamFloat<px4::params::TRK_ATT_RATE_MAX>) _param_attitude_rate_max,
		(ParamFloat<px4::params::TRK_TILT_MAX>) _param_tilt_max,
		(ParamFloat<px4::params::TRK_ENTRY_T>) _param_entry_time,
		(ParamInt<px4::params::TRK_LOST_ACT>) _param_lost_action,
		(ParamFloat<px4::params::MPC_MANTHR_MIN>) _param_mpc_manthr_min,
		(ParamFloat<px4::params::MPC_THR_MAX>) _param_mpc_thr_max,
		(ParamFloat<px4::params::MPC_THR_HOVER>) _param_mpc_thr_hover,
		(ParamInt<px4::params::MPC_THR_CURVE>) _param_mpc_thr_curve,
		(ParamFloat<px4::params::COM_SPOOLUP_TIME>) _param_com_spoolup_time
	)
};