#include "TrackControl.hpp"

#include <mathlib/math/Functions.hpp>
#include <mathlib/math/Limits.hpp>
#include <px4_platform_common/log.h>

#include <float.h>
#include <math.h>

using namespace matrix;

TrackControl::TrackControl() :
	ModuleParams(nullptr),
	WorkItem(MODULE_NAME, px4::wq_configurations::nav_and_controllers)
{
	_manual_throttle_minimum.setSlewRate(0.05f);
	_manual_throttle_maximum.setSlewRate(0.5f);
}

bool TrackControl::init()
{
	if (!_vehicle_attitude_sub.registerCallback()) {
		PX4_ERR("callback registration failed");
		return false;
	}

	return true;
}

void TrackControl::Run()
{
	if (should_exit()) {
		_vehicle_attitude_sub.unregisterCallback();
		exit_and_cleanup();
		return;
	}

	if (_parameter_update_sub.updated()) {
		parameter_update_s update{};
		_parameter_update_sub.copy(&update);
		updateParams();
	}

	vehicle_attitude_s attitude{};

	if (!_vehicle_attitude_sub.update(&attitude)) {
		return;
	}

	const hrt_abstime now = hrt_absolute_time();
	const float dt = _last_run == 0 ? 0.01f :
			 math::constrain((attitude.timestamp_sample - _last_run) * 1e-6f, 0.0002f, 0.02f);
	_last_run = attitude.timestamp_sample;
	const Quatf q(attitude.q);

	_manual_control_setpoint_sub.update(&_manual);
	_vehicle_control_mode_sub.update(&_control_mode);

	if (_vehicle_status_sub.updated()) {
		_vehicle_status_sub.copy(&_vehicle_status);
		const bool armed = _vehicle_status.arming_state == vehicle_status_s::ARMING_STATE_ARMED;
		_spooled_up = armed && hrt_elapsed_time(&_vehicle_status.armed_time) > _param_com_spoolup_time.get() * 1_s;
	}

	if (_vehicle_land_detected_sub.updated()) {
		vehicle_land_detected_s land{};

		if (_vehicle_land_detected_sub.copy(&land)) {
			_landed = land.landed;
		}
	}

	if (_landed) {
		_manual_throttle_minimum.update(0.f, dt);

	} else {
		_manual_throttle_minimum.update(_param_mpc_manthr_min.get(), dt);
	}

	if (_spooled_up) {
		_manual_throttle_maximum.update(1.f, dt);

	} else {
		_manual_throttle_maximum.setForcedValue(0.f);
	}

	const bool target_fresh = updateTargetDirection(q, now);
	const uint64_t target_age = _last_valid_target_timestamp == 0 ? UINT64_MAX : now - _last_valid_target_timestamp;
	const uint32_t target_age_us = target_age > UINT32_MAX ? UINT32_MAX : (uint32_t)target_age;
	const bool track_mode = _vehicle_status.nav_state == vehicle_status_s::NAVIGATION_STATE_TRACK;

	if (!track_mode) {
		if (_state != State::Inactive) {
			leaveTrack();
		}

		return;
	}

	if (_state == State::Inactive) {
		enterTrack(q, now);
	}

	const bool control_mode_valid = _control_mode.flag_control_manual_enabled
					&& _control_mode.flag_control_attitude_enabled
					&& _control_mode.flag_control_rates_enabled
					&& _control_mode.flag_control_allocation_enabled;

	if (!control_mode_valid) {
		requestFailover(track_status_s::FAILURE_CONTROL_MODE_INVALID, now);
	}

	if (!_manual.valid || !PX4_ISFINITE(_manual.throttle) || !PX4_ISFINITE(_manual.yaw)) {
		requestFailover(track_status_s::FAILURE_MANUAL_CONTROL_INVALID, now);
	}

	if (!_target_initialized) {
		requestFailover(track_status_s::FAILURE_ENTRY_TARGET_INVALID, now);
	}

	bool reset_integral = false;

	if (target_fresh && _state == State::TargetGrace) {
		_state = State::EntryBlend;
		_entry_timestamp = now;
		_entry_q = _last_q_sp;
		_failure_reason = track_status_s::FAILURE_NONE;
		reset_integral = true;
	}

	if (!target_fresh && _target_initialized && _state != State::Failover) {
		_state = State::TargetGrace;
		_failure_reason = track_status_s::FAILURE_TARGET_STALE;

		if (target_age > (uint64_t)(_param_lost_delay.get() * 1e6f)) {
			requestFailover(track_status_s::FAILURE_TARGET_STALE, now);
		}
	}

	float twist_rate = 0.f;

	if (target_fresh && _state != State::Failover && _manual.valid) {
		const float yaw_input = expoDeadzone(_manual.yaw);
		twist_rate = yaw_input * math::radians(_param_twist_max.get());
		_twist_angle = matrix::wrap_pi(_twist_angle + twist_rate * dt);
	}

	bool attitude_limited = false;
	Quatf desired = _last_q_sp;

	if (target_fresh && _state != State::Failover) {
		bool tilt_limited = false;
		desired = buildTrackAttitude(tilt_limited);
		attitude_limited |= tilt_limited;

		if (_state == State::EntryBlend) {
			const float entry_time = math::max(_param_entry_time.get(), 0.001f);
			const float blend = math::constrain((now - _entry_timestamp) * 1e-6f / entry_time, 0.f, 1.f);
			desired = interpolateQuaternion(_entry_q, desired, blend);

			if (blend >= 1.f) {
				_state = State::Active;
			}
		}

		desired = limitAttitudeRate(desired, dt, attitude_limited);
	}

	const float thrust = (_manual.valid && PX4_ISFINITE(_manual.throttle)) ? throttleCurve(_manual.throttle) : _last_thrust;
	_last_thrust = thrust;
	publishSetpoint(desired, thrust, reset_integral, now);
	_last_q_sp = desired;
	publishStatus(q, now, target_age_us, target_fresh, attitude_limited, twist_rate, thrust);
}

void TrackControl::enterTrack(const Quatf &q, hrt_abstime now)
{
	_state = State::EntryBlend;
	_entry_timestamp = now;
	_entry_q = q;
	_last_q_sp = q;
	_reference_x_ned = Vector3f(Dcmf(q).col(0));
	_twist_angle = 0.f;
	_failure_reason = track_status_s::FAILURE_NONE;
	_failover_requested = false;
}

void TrackControl::leaveTrack()
{
	_state = State::Inactive;
	_target_initialized = false;
	_last_valid_target_timestamp = 0;
	_last_target_filter_timestamp = 0;
	_last_target_sequence = 0;
	_twist_angle = 0.f;
	_failure_reason = track_status_s::FAILURE_NONE;
	_failover_requested = false;
}

bool TrackControl::updateTargetDirection(const Quatf &q, hrt_abstime now)
{
	tracker_target_s update{};

	if (_track_target_sub.update(&update)) {
		_target = update;
		const uint64_t sample_timestamp = _target.timestamp_sample != 0 ? _target.timestamp_sample : _target.timestamp;
		const uint64_t message_age = sample_timestamp <= now ? now - sample_timestamp : UINT64_MAX;
		Vector3f direction_body(_target.direction_body);
		const bool valid = _target.valid && _target.confidence >= _param_confidence_min.get()
				   && direction_body.isAllFinite() && direction_body.norm() > 0.5f
				   && message_age <= (uint64_t)(_param_target_timeout.get() * 1e6f);

		if (valid && (_target.sequence != _last_target_sequence || !_target_initialized)) {
			direction_body.normalize();
			Vector3f direction_ned = Dcmf(q) * direction_body;
			direction_ned.normalize();

			if (_target_initialized) {
				const float sample_dt = math::constrain((sample_timestamp - _last_target_filter_timestamp) * 1e-6f,
								0.001f, 1.f);
				const float tc = math::max(_param_direction_tc.get(), 0.f);
				const float alpha = tc > FLT_EPSILON ? sample_dt / (tc + sample_dt) : 1.f;
				_filtered_direction_ned = (_filtered_direction_ned * (1.f - alpha) + direction_ned * alpha).normalized();

			} else {
				_filtered_direction_ned = direction_ned;
				_target_initialized = true;
			}

			_last_valid_target_timestamp = sample_timestamp;
			_last_target_filter_timestamp = sample_timestamp;
			_last_target_sequence = _target.sequence;
		}
	}

	return _target_initialized && _last_valid_target_timestamp <= now
	       && now - _last_valid_target_timestamp <= (uint64_t)(_param_target_timeout.get() * 1e6f);
}

Quatf TrackControl::buildTrackAttitude(bool &tilt_limited)
{
	Vector3f z_d = limitDesiredBodyZ(-_filtered_direction_ned, tilt_limited);
	Vector3f x_base = _reference_x_ned - z_d * z_d.dot(_reference_x_ned);

	if (x_base.norm() < 0.05f) {
		x_base = Vector3f(1.f, 0.f, 0.f) - z_d * z_d(0);
	}

	if (x_base.norm() < 0.05f) {
		x_base = Vector3f(0.f, 1.f, 0.f) - z_d * z_d(1);
	}

	x_base.normalize();
	_reference_x_ned = x_base;
	Vector3f x_d = rotateAroundAxis(x_base, z_d, _twist_angle).normalized();
	Vector3f y_d = z_d.cross(x_d).normalized();
	x_d = y_d.cross(z_d).normalized();

	Dcmf rotation;
	rotation.setCol(0, x_d);
	rotation.setCol(1, y_d);
	rotation.setCol(2, z_d);
	Quatf q_d(rotation);
	q_d.normalize();
	return q_d;
}

Quatf TrackControl::interpolateQuaternion(const Quatf &from, const Quatf &to, float alpha) const
{
	Quatf error = from.inversed() * to;

	if (error(0) < 0.f) {
		error *= -1.f;
	}

	return from * Quatf(AxisAnglef(Vector3f(AxisAnglef(error)) * math::constrain(alpha, 0.f, 1.f)));
}

Quatf TrackControl::limitAttitudeRate(const Quatf &desired, float dt, bool &limited) const
{
	Quatf error = _last_q_sp.inversed() * desired;

	if (error(0) < 0.f) {
		error *= -1.f;
	}

	AxisAnglef error_axis_angle(error);
	const float angle = error_axis_angle.angle();
	const float max_step = math::radians(_param_attitude_rate_max.get()) * dt;

	if (angle > max_step && angle > FLT_EPSILON) {
		limited = true;
		return _last_q_sp * Quatf(AxisAnglef(Vector3f(error_axis_angle) * (max_step / angle)));
	}

	return desired;
}

Vector3f TrackControl::limitDesiredBodyZ(const Vector3f &desired_z, bool &limited) const
{
	Vector3f z = desired_z.normalized();
	const float max_tilt = math::radians(_param_tilt_max.get());
	const float tilt = acosf(math::constrain(z(2), -1.f, 1.f));

	if (tilt <= max_tilt) {
		return z;
	}

	limited = true;
	Vector3f horizontal(z(0), z(1), 0.f);

	if (horizontal.norm() < FLT_EPSILON) {
		horizontal = Vector3f(1.f, 0.f, 0.f);
	}

	horizontal.normalize();
	return horizontal * sinf(max_tilt) + Vector3f(0.f, 0.f, cosf(max_tilt));
}

Vector3f TrackControl::rotateAroundAxis(const Vector3f &vector, const Vector3f &axis, float angle) const
{
	return vector * cosf(angle) + axis.cross(vector) * sinf(angle) + axis * axis.dot(vector) * (1.f - cosf(angle));
}

float TrackControl::expoDeadzone(float input) const
{
	const float deadzone = math::constrain(_param_twist_deadzone.get(), 0.f, 0.9f);
	const float magnitude = fabsf(input);

	if (magnitude <= deadzone) {
		return 0.f;
	}

	const float linear = (magnitude - deadzone) / (1.f - deadzone);
	const float shaped = 0.7f * linear + 0.3f * linear * linear * linear;
	return copysignf(shaped, input);
}

float TrackControl::throttleCurve(float throttle_input) const
{
	const float input = math::constrain(throttle_input, -1.f, 1.f);
	float thrust = 0.f;

	if (_param_mpc_thr_curve.get() == 1) {
		thrust = math::interpolate(input, -1.f, 1.f, _manual_throttle_minimum.getState(), _param_mpc_thr_max.get());

	} else if (input < 0.f) {
		thrust = math::interpolate(input, -1.f, 0.f, _manual_throttle_minimum.getState(), _param_mpc_thr_hover.get());

	} else {
		thrust = math::interpolate(input, 0.f, 1.f, _param_mpc_thr_hover.get(), _param_mpc_thr_max.get());
	}

	return math::min(thrust, _manual_throttle_maximum.getState());
}

void TrackControl::requestFailover(uint8_t reason, hrt_abstime now)
{
	_failure_reason = reason;
	_state = State::Failover;

	if (_failover_requested) {
		return;
	}

	action_request_s request{};
	request.timestamp = now;
	request.action = action_request_s::ACTION_SWITCH_MODE;
	request.source = action_request_s::SOURCE_RC_MODE_SLOT;
	request.mode = _param_lost_action.get() == 1 ? vehicle_status_s::NAVIGATION_STATE_POSCTL :
		       vehicle_status_s::NAVIGATION_STATE_STAB;
	_action_request_pub.publish(request);
	_failover_requested = true;
}

void TrackControl::publishSetpoint(const Quatf &q_d, float thrust, bool reset_integral, hrt_abstime now)
{
	vehicle_attitude_setpoint_s setpoint{};
	q_d.copyTo(setpoint.q_d);
	const Eulerf euler(q_d);
	setpoint.roll_body = euler.phi();
	setpoint.pitch_body = euler.theta();
	setpoint.yaw_body = euler.psi();
	setpoint.yaw_sp_move_rate = 0.f;
	setpoint.thrust_body[0] = 0.f;
	setpoint.thrust_body[1] = 0.f;
	setpoint.thrust_body[2] = -math::constrain(thrust, 0.f, 1.f);
	setpoint.reset_integral = reset_integral;
	setpoint.timestamp = now;
	_attitude_setpoint_pub.publish(setpoint);
}

void TrackControl::publishStatus(const Quatf &q, hrt_abstime now, uint32_t target_age_us,
				 bool target_valid, bool attitude_limited, float twist_rate, float thrust)
{
	track_status_s status{};
	status.timestamp = now;
	status.active = _state != State::Inactive;
	status.target_valid = target_valid;
	status.attitude_limited = attitude_limited;
	status.failsafe_active = _state == State::TargetGrace || _state == State::Failover;
	status.failure_reason = _failure_reason;
	status.target_age_us = target_age_us;
	const Vector3f filtered_body = Dcmf(q).transpose() * _filtered_direction_ned;
	filtered_body.copyTo(status.filtered_direction_body);
	_last_q_sp.copyTo(status.attitude_setpoint_q);
	status.twist_angle = _twist_angle;
	status.twist_rate = twist_rate;
	status.thrust_setpoint = thrust;
	_track_status_pub.publish(status);
}

int TrackControl::print_status()
{
	PX4_INFO("state: %u, target initialized: %s, failover requested: %s",
		 (unsigned)_state, _target_initialized ? "yes" : "no", _failover_requested ? "yes" : "no");
	PX4_INFO("twist: %.3f rad, thrust: %.3f", (double)_twist_angle, (double)_last_thrust);
	return PX4_OK;
}

int TrackControl::task_spawn(int argc, char *argv[])
{
	TrackControl *instance = new TrackControl();

	if (instance) {
		_object.store(instance);
		_task_id = task_id_is_work_queue;

		if (instance->init()) {
			return PX4_OK;
		}
	}

	delete instance;
	_object.store(nullptr);
	_task_id = -1;
	return PX4_ERROR;
}

int TrackControl::custom_command(int argc, char *argv[])
{
	return print_usage("unknown command");
}

int TrackControl::print_usage(const char *reason)
{
	if (reason) {
		PX4_WARN("%s", reason);
	}

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
TRACK flight-mode attitude setpoint generator for an upward-mounted camera.
It consumes tracker_target, RC yaw/throttle and vehicle attitude. PX4's
existing attitude, rate and control-allocation loops remain unchanged.
)DESCR_STR");
	PRINT_MODULE_USAGE_NAME("track_control", "controller");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return PX4_OK;
}

extern "C" __EXPORT int track_control_main(int argc, char *argv[])
{
	return TrackControl::main(argc, argv);
}