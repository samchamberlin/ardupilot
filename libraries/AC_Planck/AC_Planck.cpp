#include <AC_Planck/AC_Planck.h>
#include <AP_HAL/AP_HAL.h>
#include "../ArduCopter/defines.h"
#include <AP_Logger/AP_Logger.h>
#include <AP_Motors/AP_Motors.h>

void AC_Planck::handle_planck_mavlink_msg(const mavlink_channel_t &chan, const mavlink_message_t *mav_msg,
    AP_AHRS &ahrs)
{
  switch(mav_msg->msgid)
  {
     case MAVLINK_MSG_ID_PLANCK_STATUS:
     {
        _chan = chan; //Set the channel based on the incoming status message
        mavlink_planck_status_t ps;
        mavlink_msg_planck_status_decode(mav_msg, &ps);
        _status.timestamp_ms = AP_HAL::millis();
        _status.takeoff_ready = (bool)ps.takeoff_ready;
        _status.land_ready = (bool)ps.land_ready;
        _status.commbox_ok = (bool)(ps.failsafe & 0x01);
        _status.commbox_gps_ok = (bool)(ps.failsafe & 0x02);
        _status.tracking_tag = (bool)(ps.status & 0x01);
        _status.tracking_commbox_gps = (bool)(ps.status & 0x02);
        _status.takeoff_complete = (bool)ps.takeoff_complete;
        _status.at_location = (bool)ps.at_location;

        //_was_at_location is special, as it is only triggered once per event
        //on the planck side. set the flag but also the oneshot value
        if(!_was_at_location && _status.at_location) {
          _was_at_location = true;
        }
        break;
    }

     case MAVLINK_MSG_ID_PLANCK_CMD_MSG:
     {
        mavlink_planck_cmd_msg_t pc;
        mavlink_msg_planck_cmd_msg_decode(mav_msg, &pc);

        //position data
        _cmd.pos.lat = pc.lat;
        _cmd.pos.lng = pc.lon;
        _cmd.pos.alt = pc.alt * 100; //m->cm

         switch(pc.frame) {
            case MAV_FRAME_GLOBAL_RELATIVE_ALT:
            case MAV_FRAME_GLOBAL_RELATIVE_ALT_INT:
              _cmd.pos.relative_alt = true;
              _cmd.pos.terrain_alt = false;
              break;
            case MAV_FRAME_GLOBAL_TERRAIN_ALT:
            case MAV_FRAME_GLOBAL_TERRAIN_ALT_INT:
              _cmd.pos.relative_alt = true;
              _cmd.pos.terrain_alt = true;
              break;
            case MAV_FRAME_GLOBAL:
            case MAV_FRAME_GLOBAL_INT:
            default:
              // Copter does not support navigation to absolute altitudes. This convert the WGS84 altitude
              // to a home-relative altitude before passing it to the navigation controller
              _cmd.pos.alt -= ahrs.get_home().alt;
              _cmd.pos.relative_alt = true;
              _cmd.pos.terrain_alt = false;
              break;
        }

        //velocity
        _cmd.vel_cms.x = pc.vel[0] * 100.;
        _cmd.vel_cms.y = pc.vel[1] * 100.;
        _cmd.vel_cms.z = pc.vel[2] * 100.;

        //acceleration
        _cmd.accel_cmss.x = pc.acc[0] * 100.;
        _cmd.accel_cmss.y = pc.acc[1] * 100.;
        _cmd.accel_cmss.z = pc.acc[2] * 100.;

        //Attitude
        _cmd.att_cd.x = ToDeg(pc.att[0]) * 100.;
        _cmd.att_cd.y = ToDeg(pc.att[1]) * 100.;
        _cmd.att_cd.z = ToDeg(pc.att[2]) * 100.;

        //Determine which values are good
        bool use_pos = (pc.type_mask & 0x0007) == 0x0007;
        bool use_vel = (pc.type_mask & 0x0038) == 0x0038;
        bool use_vz  = (pc.type_mask & 0x0020) == 0x0020;
        bool use_acc = (pc.type_mask & 0x01C0) == 0x01C0;
        bool use_att = (pc.type_mask & 0x0E00) == 0x0E00;
        bool use_y   = (pc.type_mask & 0x0800) == 0x0800;
        bool use_yr  = (pc.type_mask & 0x1000) == 0x1000;

        _cmd.is_yaw_rate = use_yr;

        //Determine the command type based on the typemask
        //If position bits are set, this is a position command
        if(use_pos && !use_vel)
          _cmd.type = POSITION;

        //If position and velocity are set, this is a posvel
        else if(use_pos && use_vel)
          _cmd.type = POSVEL;

        //If velocity is set, this is a velocity command
        else if(use_vel)
          _cmd.type = VELOCITY;

        //If attitude and vz and yaw/yawrate are set, this is an attitude command
        else if(use_vz && !use_acc && use_att && (use_y || use_yr))
          _cmd.type = ATTITUDE;

        //If accel and vz and yaw/yawrate are set, this is an accel command
        else if(use_vz && use_acc && !use_att && (use_y || use_yr))
          _cmd.type = ACCEL;

        //Otherwise we don't know what this is
        else
          _cmd.type = NONE;

        //This is a new command
        _cmd.is_new = true;
        break;
    }

    case MAVLINK_MSG_ID_PLANCK_LANDING_TAG_ESTIMATE_NED:
    {
      mavlink_planck_landing_tag_estimate_ned_t pl;
      mavlink_msg_planck_landing_tag_estimate_ned_decode(mav_msg, &pl);

      //Position
      _tag_est.tag_pos_cm.x = pl.x * 100.;
      _tag_est.tag_pos_cm.y = pl.y * 100.;
      _tag_est.tag_pos_cm.z = pl.z * 100.;

      //velocity
      _tag_est.tag_vel_cms.x = pl.vx * 100.;
      _tag_est.tag_vel_cms.y = pl.vy * 100.;
      _tag_est.tag_vel_cms.z = pl.vz * 100.;

      //Attitude
      _tag_est.tag_att_cd.x = ToDeg(pl.roll) * 100.;
      _tag_est.tag_att_cd.y = ToDeg(pl.pitch) * 100.;
      _tag_est.tag_att_cd.z = ToDeg(pl.yaw) * 100.;

      _tag_est.timestamp_us = pl.ap_timestamp_us;
      break;
    }

    case MAVLINK_MSG_ID_PLANCK_DECK_TETHER_STATUS:
    {
      mavlink_planck_deck_tether_status_t ts;
      mavlink_msg_planck_deck_tether_status_decode(mav_msg, &ts);
      _tether_status.timestamp_ms = AP_HAL::millis();
      _tether_status.cable_out_m = ts.CABLE_OUT * 0.3048; //feet to meters
      _tether_status.spool_status = ts.SPOOL_STATUS;

      bool high_tension =  (ts.SPOOL_STATUS == PLANCK_DECK_SPOOL_LOCKED) && (ts.CABLE_TENSION > 75);
      bool entered_high_tension = high_tension && !_tether_status.high_tension;
      bool exited_high_tension = !high_tension && _tether_status.high_tension;

      //If we've transitioned into high tension, record altitude and timestamps
      if(entered_high_tension) {
        gcs().send_text(MAV_SEVERITY_INFO, "Tether Tension Mode Change: High Tension");
      } else if(exited_high_tension) {
        gcs().send_text(MAV_SEVERITY_INFO, "Tether Tension Mode Change: Nominal");
      }

      //Always update the latest altitudes when we get new tension information.
      //This allows us to know the altitudes when the high tension event ocurred, or
      //when we lost comms with the ground
      if(!high_tension || entered_high_tension) {
        _tether_status.high_tension_timestamp_ms = AP_HAL::millis();

        if(_status.tracking_tag) {
          _tether_status.high_tension_tag_alt_cm = _tag_est.tag_pos_cm.z;
        } else {
          _tether_status.high_tension_tag_alt_cm = 0.0f;
        }

        Location current_loc;
        ahrs.get_position(current_loc);
        int32_t alt_above_home_cm = 3048; //100ft default
        if(!current_loc.get_alt_cm(Location::AltFrame::ABOVE_HOME, alt_above_home_cm)) {
          alt_above_home_cm = 3048;
        }
        _tether_status.high_tension_alt_cm = (float)alt_above_home_cm;
      }

      _tether_status.high_tension = high_tension;

      AP::logger().Write("PDTS", "TimeUS,TSct,TSss,tsHT,tsCO", "QBBBf",
                         AP_HAL::micros64(),
                         (uint8_t)ts.CABLE_TENSION,
                         (uint8_t)ts.SPOOL_STATUS,
                         (uint8_t)_tether_status.high_tension,
                         (float)_tether_status.cable_out_m);
      break;
    }

    default:
      break;
  }
}

void AC_Planck::request_takeoff(const float alt)
{
  //Send a takeoff command message to planck
  mavlink_msg_planck_cmd_request_send(
    _chan,
    mavlink_system.sysid,         //uint8_t target_system
    PLANCK_CTRL_COMP_ID,   //uint8_t target_component,
    PLANCK_CMD_REQ_TAKEOFF,//uint8_t type
    alt,                   //float param1
    0,0,0,0,0);
}

void AC_Planck::request_alt_change(const float alt, const float rate_up_cms, const float rate_down_cms)
{
  //Only altitude is valid
  uint8_t valid = 0b00000100;
  uint32_t muxed_rates = mux_rates(rate_up_cms,rate_down_cms);

  mavlink_msg_planck_cmd_request_send(
    _chan,
    mavlink_system.sysid,
    PLANCK_CTRL_COMP_ID,
    PLANCK_CMD_REQ_MOVE_TARGET,
    (float)valid,     //param1
    0,                //param2
    0,                //param3
    alt,              //param4
    0,                //param5
    *reinterpret_cast<float*>(&(muxed_rates)));           //param6
}

void AC_Planck::request_rtb(const float alt, const float rate_up, const float rate_down, const float rate_xy)
{
  //Send an RTL command message to planck
  mavlink_msg_planck_cmd_request_send(
    _chan,
    mavlink_system.sysid,         //uint8_t target_system
    PLANCK_CTRL_COMP_ID,   //uint8_t target_component,
    PLANCK_CMD_REQ_RTB,    //uint8_t type
    alt,                   //float param1
    rate_up,               //float param2
    rate_down,             //float param3
    rate_xy,               //float param4
    0,0);
}

void AC_Planck::request_land(const float descent_rate)
{
  //Send a land command message to planck
  mavlink_msg_planck_cmd_request_send(
    _chan,
    mavlink_system.sysid,         //uint8_t target_system
    PLANCK_CTRL_COMP_ID,   //uint8_t target_component,
    PLANCK_CMD_REQ_LAND,   //uint8_t type
    descent_rate,          //float param1
    0,0,0,0,0);
}

//Move the current tracking target, either to an absolute offset or by a rate
void AC_Planck::request_move_target(const Vector3f offset_cmd_NED, const bool is_rate, const float rate_up_cms, const float rate_down_cms)
{
  //all directions and are valid
  uint8_t valid = 0b00000111;
  uint32_t muxed_rates = mux_rates(rate_up_cms,rate_down_cms);

  mavlink_msg_planck_cmd_request_send(
    _chan,
    mavlink_system.sysid,
    PLANCK_CTRL_COMP_ID,
    PLANCK_CMD_REQ_MOVE_TARGET,
    (float)valid,     //param1
    offset_cmd_NED.x, //param2
    offset_cmd_NED.y, //param3
    offset_cmd_NED.z, //param4
    is_rate,          //param5
    *reinterpret_cast<float*>(&(muxed_rates)));               //param6

  //If the target has moved, the _was_at_location flag must go false until we
  //hear otherwise from planck
  _was_at_location = false;
}

void AC_Planck::stop_commanding(void)
{
  mavlink_msg_planck_cmd_request_send(
    _chan,
    mavlink_system.sysid,         //uint8_t target_system
    PLANCK_CTRL_COMP_ID,   //uint8_t target_component,
    PLANCK_CMD_REQ_STOP,   //uint8_t type
    0,0,0,0,0,0);
}

//Get an accel, yaw, z_rate command
bool AC_Planck::get_accel_yaw_zrate_cmd(Vector3f &accel_cmss, float &yaw_cd, float &vz_cms, bool &is_yaw_rate)
{
  if(!_cmd.is_new) return false;
  accel_cmss = _cmd.accel_cmss;
  yaw_cd = _cmd.att_cd.z;
  vz_cms = _cmd.vel_cms.z;
  is_yaw_rate = _cmd.is_yaw_rate;
  _cmd.is_new = false;
  return true;
}

//Get an attitude command
bool AC_Planck::get_attitude_zrate_cmd(Vector3f &att_cd, float &vz_cms, bool &is_yaw_rate)
{
  if(!_cmd.is_new) return false;
  att_cd = _cmd.att_cd;
  vz_cms = _cmd.vel_cms.z;
  is_yaw_rate = _cmd.is_yaw_rate;
  _cmd.is_new = false;
  return true;
}

//Get a velocity, yaw command
bool AC_Planck::get_velocity_cmd(Vector3f &vel_cms)
{
  if(!_cmd.is_new) return false;
  vel_cms = _cmd.vel_cms;
  _cmd.is_new = false;
  return true;
}

//Get a position command
bool AC_Planck::get_position_cmd(Location &loc)
{
  if(!_cmd.is_new) return false;
  loc = _cmd.pos;
  _cmd.is_new = false;
  return true;
}

//Get a position, velocity cmd
bool AC_Planck::get_posvel_cmd(Location &loc, Vector3f &vel_cms, float &yaw_cd, bool &is_yaw_rate)
{
  if(!_cmd.is_new) return false;
  loc = _cmd.pos;
  vel_cms = _cmd.vel_cms;
  yaw_cd = _cmd.att_cd.z;
  is_yaw_rate = _cmd.is_yaw_rate;
  _cmd.is_new = false;
  return true;
}

uint32_t AC_Planck::mux_rates(float rate_up,  float rate_down)
{
  if (rate_down<0)
    rate_down*=-1;

  if (rate_up<0)
    rate_up*=-1;

  rate_up = std::fmin(rate_up,32767);
  rate_down = std::fmin(rate_down,32767);

  uint32_t muxed_rates = ((uint32_t(rate_up) << 16) | uint32_t(rate_down));
  muxed_rates = (muxed_rates & 0x7FFF7FFF) | 0x00008000;
  return muxed_rates;
}

bool AC_Planck::check_for_high_tension_timeout(float ht_tether_spd) {
  //No failure if not flying
  if(AP_Motors::get_singleton()->get_spool_state() == AP_Motors::SpoolState::SHUT_DOWN) {
    return false;
  }

  //No comms from the tether
  bool tether_comms_failed = is_tether_timed_out();

  //Determine if high tension has been or should have been triggered
  bool high_tension_triggered = _tether_status.high_tension || tether_comms_failed;

  //Hasn't failed if not triggered
  if(!high_tension_triggered) {
    _tether_status.sent_failed_message = false;
    return false;
  }

  //The amount of time to wait for high tension to timeout is a function of
  //initial altitude when the high tension event ocurred. Use tag altitude if available
  float timeout_s = 0;
  const float reel_rate_cms = std::fmaxf(1,ht_tether_spd); //~1.25ft/s
  if(!is_equal(_tether_status.high_tension_tag_alt_cm,0.0f)) {
    timeout_s = _tether_status.high_tension_tag_alt_cm / reel_rate_cms;
  } else {
    timeout_s = _tether_status.high_tension_alt_cm / reel_rate_cms;
  }

  //Account for the 10s spent in "locked" mode, but only in a comms or position loss state.
  //Note: we only check the commbox state, as thats what the tether logic uses
  bool pos_reference_good = get_commbox_state() || get_tag_tracking_state();
  if(!pos_reference_good || tether_comms_failed) {
    timeout_s += 10.;
  }

  //If this was due to a comms loss, add an additional 5s for the comm loss timeout
  if(!_tether_status.high_tension) { //did not get a high-tension indication from DECK due to comms loss
    timeout_s += 5.;
  }

  //Add a 2s buffer, limit
  timeout_s = constrain_float((timeout_s + 2.), 5., 120.); //5s to 2 minutes

  uint32_t timeout_time_ms = _tether_status.high_tension_timestamp_ms + ((int)(timeout_s) * 1000);
  bool timed_out = AP_HAL::millis() > timeout_time_ms;

  //If no timeout, it hasn't failed yet
  if(!timed_out) {
    _tether_status.sent_failed_message = false;
    return false;
  }

  if(timed_out && !_tether_status.sent_failed_message) {
    gcs().send_text(MAV_SEVERITY_CRITICAL, "Tether high-tension timeout!");
    _tether_status.sent_failed_message = true;
  }
  return timed_out;
}

void AC_Planck::override_with_zero_vel_cmd() {
  _cmd.zero();
  _cmd.type = VELOCITY;
  _cmd.timestamp_ms = AP_HAL::millis();
  _cmd.is_new = true;
}

void AC_Planck::override_with_zero_att_cmd() {
  _cmd.zero();
  _cmd.type = ATTITUDE;
  _cmd.timestamp_ms = AP_HAL::millis();
  _cmd.is_new = true;
}

bool AC_Planck::is_tether_timed_out() {
  bool timed_out = ((AP_HAL::millis() - _tether_status.timestamp_ms) > 5000);
  if(timed_out && !_tether_status.comms_timed_out) {
    gcs().send_text(MAV_SEVERITY_CRITICAL, "Tether comms timed out");
  } else if (!timed_out && _tether_status.comms_timed_out) {
    gcs().send_text(MAV_SEVERITY_INFO, "Tether comms restored");
  }
  _tether_status.comms_timed_out = timed_out;
  return timed_out;
}

