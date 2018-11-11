/*
 * WSG 50 ROS NODE FOR REFILLS PROJECT
 * Copyright (c) 2012, Robotnik Automation, SLL
 * Copyright 2018 Università della Campania Luigi Vanvitelli
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holders nor the names of their
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \author Marco Costanzo (marco.costanzo@unicampania.it)
 * \brief WSG-50 ROS driver.
 */


//------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <thread>
#include <chrono>

#include "wsg_50/common.h"
#include "wsg_50/cmd.h"
#include "wsg_50/msg.h"
#include "wsg_50/functions.h"

#include <ros/ros.h>
#include "std_msgs/String.h"
#include "std_srvs/Empty.h"
#include "sun_wsg50_common/Status.h"
#include "sun_wsg50_common/Move.h"
#include "sun_wsg50_common/Conf.h"
#include "sun_wsg50_common/Incr.h"
#include "sun_wsg50_common/Cmd.h"

#include "sensor_msgs/JointState.h"
#include "std_msgs/Float32.h"
//#include "sun_wsg50_common/Tactile.h"
#include "std_msgs/Bool.h"

using namespace std;


//------------------------------------------------------------------------
// Local macros
//------------------------------------------------------------------------


//------------------------------------------------------------------------
// Typedefs, enums, structs
//------------------------------------------------------------------------

#define GRIPPER_MAX_OPEN 110.0
#define GRIPPER_MIN_OPEN 0.0

//------------------------------------------------------------------------
// Global variables
//------------------------------------------------------------------------

float increment;
bool objectGraspped;

int g_timer_cnt = 0;
ros::Publisher g_pub_state, g_pub_joint, g_pub_moving;//, pub_tact0, pub_tact1;
bool g_ismoving = false, g_mode_script = false, g_mode_periodic = false, g_mode_polling = false;
float g_goal_position = NAN, g_goal_speed = NAN, g_speed = 10.0;
   
//------------------------------------------------------------------------
// Unit testing
//------------------------------------------------------------------------


//------------------------------------------------------------------------
// Local function prototypes
//------------------------------------------------------------------------


//------------------------------------------------------------------------
// Function implementation
//------------------------------------------------------------------------


bool moveSrv(sun_wsg50_common::Move::Request &req, sun_wsg50_common::Move::Response &res)
{
	if ( (req.width >= 0.0 && req.width <= 110.0) && (req.speed > 0.0 && req.speed <= 420.0) ){
  		ROS_INFO("Moving to %f position at %f mm/s.", req.width, req.speed);
		res.error = move(req.width, req.speed, false);
	}else if (req.width < 0.0 || req.width > 110.0){
		ROS_ERROR("Imposible to move to this position. (Width values: [0.0 - 110.0] ");
		res.error = 255;
		return false;
	}else{
	        ROS_WARN("Speed values are outside the gripper's physical limits ([0.1 - 420.0])  Using clamped values.");
		res.error = move(req.width, req.speed, false);
	}

	ROS_INFO("Target position reached.");
  	return true;
}

bool graspSrv(sun_wsg50_common::Move::Request &req, sun_wsg50_common::Move::Response &res)
{
	if ( (req.width >= 0.0 && req.width <= 110.0) && (req.speed > 0.0 && req.speed <= 420.0) ){
        ROS_INFO("Grasping object at %f with %f mm/s.", req.width, req.speed);
		res.error = grasp(req.width, req.speed);
	}else if (req.width < 0.0 || req.width > 110.0){
		ROS_ERROR("Imposible to move to this position. (Width values: [0.0 - 110.0] ");
		res.error = 255;
		return false;
	}else{
	        ROS_WARN("Speed or position values are outside the gripper's physical limits (Position: [0.0 - 110.0] / Speed: [0.1 - 420.0])  Using clamped values.");
		res.error = grasp(req.width, req.speed);
	}

	ROS_INFO("Object grasped correctly.");
	objectGraspped=true;
  	return true;
}

bool incrementSrv(sun_wsg50_common::Incr::Request &req, sun_wsg50_common::Incr::Response &res)
{
	if (req.direction == "open"){
	
		if (!objectGraspped){
		
			float currentWidth = getOpening();
			float nextWidth = currentWidth + req.increment;
			if ( (currentWidth < GRIPPER_MAX_OPEN) && nextWidth < GRIPPER_MAX_OPEN ){
				//grasp(nextWidth, 1);
				move(nextWidth,20, true);
				currentWidth = nextWidth;
			}else if( nextWidth >= GRIPPER_MAX_OPEN){
				//grasp(GRIPPER_MAX_OPEN, 1);
				move(GRIPPER_MAX_OPEN,1, true);
				currentWidth = GRIPPER_MAX_OPEN;
			}
		}else{
			ROS_INFO("Releasing object...");
			release(GRIPPER_MAX_OPEN, 20);
			objectGraspped = false;
		}
	}else if (req.direction == "close"){
	
		if (!objectGraspped){

			float currentWidth = getOpening();
			float nextWidth = currentWidth - req.increment;
		
			if ( (currentWidth > GRIPPER_MIN_OPEN) && nextWidth > GRIPPER_MIN_OPEN ){
				//grasp(nextWidth, 1);
				move(nextWidth,20, true);
				currentWidth = nextWidth;
			}else if( nextWidth <= GRIPPER_MIN_OPEN){
				//grasp(GRIPPER_MIN_OPEN, 1);
				move(GRIPPER_MIN_OPEN,1, true);
				currentWidth = GRIPPER_MIN_OPEN;
			}
		}
	}
	return true;
}

bool releaseSrv(sun_wsg50_common::Move::Request &req, sun_wsg50_common::Move::Response &res)
{
	if ( (req.width >= 0.0 && req.width <= 110.0) && (req.speed > 0.0 && req.speed <= 420.0) ){
  		ROS_INFO("Releasing to %f position at %f mm/s.", req.width, req.speed);
		res.error = release(req.width, req.speed);
	}else if (req.width < 0.0 || req.width > 110.0){
		ROS_ERROR("Imposible to move to this position. (Width values: [0.0 - 110.0] ");
		res.error = 255;
		return false;
	}else{
	        ROS_WARN("Speed or position values are outside the gripper's physical limits (Position: [0.0 - 110.0] / Speed: [0.1 - 420.0])  Using clamped values.");
		res.error = release(req.width, req.speed);
	}
	ROS_INFO("Object released correctly.");
  	return true;
}

bool homingSrv(std_srvs::Empty::Request &req, std_srvs::Empty::Request &res)
{
	ROS_INFO("Homing...");
	homing();
	ROS_INFO("Home position reached.");
	return true;
}

bool stopSrv(std_srvs::Empty::Request &req, std_srvs::Empty::Request &res)
{
	ROS_WARN("Stop!");
	stop();
	ROS_WARN("Stopped.");
	return true;
}

bool setAccSrv(sun_wsg50_common::Conf::Request &req, sun_wsg50_common::Conf::Response &res)
{
	setAcceleration(req.val);
	return true;
}

bool setForceSrv(sun_wsg50_common::Conf::Request &req, sun_wsg50_common::Conf::Response &res)
{
	setGraspingForceLimit(req.val);
	return true;
}

bool ackSrv(std_srvs::Empty::Request &req, std_srvs::Empty::Request &res)
{
	ack_fault();
	return true;
}

/** \brief Callback for goal_position topic (in appropriate modes) */
void position_cb(const sun_wsg50_common::Cmd::ConstPtr& msg)
{
    g_speed = msg->speed; g_goal_position = msg->pos;
    // timer_cb() will send command to gripper

    if (g_mode_periodic) {
        // Send command to gripper without waiting for a response
        // read_thread() handles responses
        // read/write may be simultaneous, therefore no mutex
        stop(true);
        if (move(g_goal_position, g_speed, false, true) != 0)
            ROS_ERROR("Failed to send MOVE command");
    }
}

/** \brief Callback for goal_speed topic (in appropriate modes) */
void speed_cb(const std_msgs::Float32::ConstPtr& msg)
{
    g_goal_speed = msg->data; g_speed = msg->data;
    // timer_cb() will send command to gripper
}

/** \brief Loop for state polling in modes script and polling. Also sends command in script mode. */
void timer_cb(const ros::TimerEvent& ev)
{
	//printf("Timer \n");
	// ==== Get state values by built-in commands ====
	gripper_response info;
	float acc = 0.0;
	info.speed = 0.0;

	ros::Time myTime = ros::Time::now(); //just for init

    if (g_mode_polling) {
		//printf("MODE_POLLING\n");
        const char * state = systemState();
        if (!state)
            return;
        info.state_text = std::string(state);
		info.position = getOpening();
		acc = getAcceleration();
		info.f_motor = getForce();//getGraspingForce();

    } else if (g_mode_script) {
		//printf("MODE_SCRIPT\n");
		// ==== Call custom measure-and-move command ====
		int res = 0;
		if (!isnan(g_goal_position)) {
			//printf("NOT NAN GOAL POSITION\n");
			ROS_INFO("Position command: pos=%5.1f, speed=%5.1f", g_goal_position, g_speed);
            res = script_measure_move(1, g_goal_position, g_speed, info);
		} else if (!isnan(g_goal_speed)) {
			//printf("NOT NAN GOAL SPEED\n");
			//ROS_INFO("Velocity command: speed=%5.1f", g_goal_speed);
            		res = script_measure_move(2, 0, g_goal_speed, info);
		} else{
			myTime = ros::Time::now(); //last point in the code where i can call ros:Time:now();
            		res = script_measure_move(0, 0, 0, info);
			//printf("else02\n");
		}
		if (!isnan(g_goal_position))
			g_goal_position = NAN;
		if (!isnan(g_goal_speed))
			g_goal_speed = NAN;
		//printf("CIAO\n");
		if (!res) {
			ROS_ERROR("Measure-and-move command failed");
			return;
		}
		//printf("CIAO2\n");
		// ==== Moving msg ====
		if (g_ismoving != info.ismoving) {
			std_msgs::Bool moving_msg;
			moving_msg.data = info.ismoving;
			g_pub_moving.publish(moving_msg);
			g_ismoving = info.ismoving;
		} //printf("CIAO3\n");
    } else
        return;

	// ==== Status msg ====
	sun_wsg50_common::Status status_msg;
	status_msg.status = info.state_text;
	status_msg.width = info.position;
	status_msg.speed = info.speed;
	status_msg.acc = acc;
	status_msg.force = info.f_motor;
	status_msg.force_finger0 = info.f_finger0;
	status_msg.force_finger1 = info.f_finger1;

	g_pub_state.publish(status_msg);
             

	// ==== Joint state msg ====
	sensor_msgs::JointState joint_states;
	joint_states.header.stamp = myTime; //ros::Time::now();;
	joint_states.header.frame_id = "wsg_50_gripper_base_link";
	joint_states.name.push_back("wsg_50_gripper_base_joint_gripper_left");
	joint_states.name.push_back("wsg_50_gripper_base_joint_gripper_right");
	joint_states.position.resize(2);

	joint_states.position[0] = -info.position/2000.0;
	joint_states.position[1] = info.position/2000.0;
	joint_states.velocity.resize(2);		
    	joint_states.velocity[0] = info.speed/1000.0;
    	joint_states.velocity[1] = info.speed/1000.0;
	joint_states.effort.resize(2);
	joint_states.effort[0] = info.f_motor;
	joint_states.effort[1] = info.f_motor;
	
	g_pub_joint.publish(joint_states);

	// printf("Timer, last duration: %6.1f\n", ev.profile.last_duration.toSec() * 1000.0);

	// ==== Tactile msg ====
    /*
    	sun_wsg50_common::Tactile finger_voltages;
	finger_voltages.voltages.data.resize(25);
	finger_voltages.header.stamp = myTime; //ros::Time::now(); //Please change to a time saved as soon as we got data
    	finger_voltages.header.frame_id = "fingertip0";  //Please change to a parameter
	finger_voltages.voltages.layout.dim.resize(1);
    	finger_voltages.voltages.layout.dim[0].label="voltage";
    	finger_voltages.voltages.layout.dim[0].size=25;
    	finger_voltages.voltages.layout.dim[0].stride=1;
    	finger_voltages.voltages.layout.data_offset=0;


        
	if(info.tact_finger0){
		for(int i=0;i < 25; i++)
			finger_voltages.voltages.data[i] = info.v_finger0[i];
		pub_tact0.publish(finger_voltages);
	}
	if(info.tact_finger1){
		for(int i=0;i < 25; i++)
			finger_voltages.voltages.data[i] = info.v_finger1[i];
		pub_tact1.publish(finger_voltages);
	}
    */

}


/** \brief Reads gripper responses in auto_update mode. The gripper pushes state messages in regular intervals. */
void read_thread(int interval_ms)
{
    ROS_INFO("Thread started");

    status_t status;
    int res;
    bool pub_state = false;

    double rate_exp = 1000.0 / (double)interval_ms;
    std::string names[3] = { "opening", "speed", "force" };

    // Prepare messages
    sun_wsg50_common::Status status_msg;
    status_msg.status = "UNKNOWN";

    sensor_msgs::JointState joint_states;
    joint_states.header.frame_id = "wsg_50_gripper_base_link";
    joint_states.name.push_back("wsg_50_gripper_base_joint_gripper_left");
    joint_states.name.push_back("wsg_50_gripper_base_joint_gripper_right");
    joint_states.position.resize(2);
    joint_states.velocity.resize(2);
    joint_states.effort.resize(2);

    // Request automatic updates (error checking is done below)
    getOpening(interval_ms);
    getSpeed(interval_ms);
    getForce(interval_ms);


    msg_t msg; msg.id = 0; msg.data = 0; msg.len = 0;
    int cnt[3] = {0,0,0};
    auto time_start = std::chrono::system_clock::now();


    while (g_mode_periodic) {
        // Receive gripper response
        msg_free(&msg);
        res = msg_receive( &msg );
        if (res < 0 || msg.len < 2) {
            ROS_ERROR("Gripper response failure: too short");
            continue;
        }

        float val = 0.0;
        status = cmd_get_response_status(msg.data);

        // Decode float for opening/speed/force
        if (msg.id >= 0x43 && msg.id <= 0x45 && msg.len == 6) {
            if (status != E_SUCCESS) {
                ROS_ERROR("Gripper response failure for opening/speed/force\n");
                continue;
            }
            val = convert(&msg.data[2]);
        }

        // Handle response types
        int motion = -1;  
        switch (msg.id) {
        /*** Opening ***/
        case 0x43:
            status_msg.width = val;
            pub_state = true;
            cnt[0]++;
            break;

        /*** Speed ***/
        case 0x44:
            status_msg.speed = val;
            cnt[1]++;
            break;

        /*** Force ***/
        case 0x45:
            status_msg.force = val;
            cnt[2]++;
            break;

        /*** Move ***/
        // Move commands are sent from outside this thread
        case 0x21:
            if (status == E_SUCCESS) {
                ROS_INFO("Position reached");
                motion = 0;
            } else if (status == E_AXIS_BLOCKED) {
                ROS_INFO("Axis blocked");
                motion = 0;
            } else if (status == E_CMD_PENDING) {
                ROS_INFO("Movement started");
                motion = 1;
            } else if (status == E_ALREADY_RUNNING) {
                ROS_INFO("Movement error: already running");
            } else if (status == E_CMD_ABORTED) {
                ROS_INFO("Movement aborted");
                motion = 0;
            } else {
                ROS_INFO("Movement error");
                motion = 0;
            }
            break;

        /*** Stop ***/
        // Stop commands are sent from outside this thread
        case 0x22:
            // Stop command; nothing to do
            break;
        default:
            ROS_INFO("Received unknown respone 0x%02x (%2dB)\n", msg.id, msg.len);
        }

        // ***** PUBLISH motion message
        if (motion == 0 || motion == 1) {
            std_msgs::Bool moving_msg;
            moving_msg.data = motion;
            g_pub_moving.publish(moving_msg);
            g_ismoving = motion;
        }

        // ***** PUBLISH state message & joint message
        if (pub_state) {
            pub_state = false;
            g_pub_state.publish(status_msg);

            joint_states.header.stamp = ros::Time::now();;
            joint_states.position[0] = -status_msg.width/2000.0;
            joint_states.position[1] = status_msg.width/2000.0;
            joint_states.velocity[0] = status_msg.speed/1000.0;
            joint_states.velocity[1] = status_msg.speed/1000.0;
            joint_states.effort[0] = status_msg.force;
            joint_states.effort[1] = status_msg.force;
            g_pub_joint.publish(joint_states);
        }

        // Check # of received messages regularly
        std::chrono::duration<float> t = std::chrono::system_clock::now() - time_start;
        double t_ = t.count();
        if (t_ > 5.0) {
            time_start = std::chrono::system_clock::now();
            //printf("Infos for %5.1fHz, %5.1fHz, %5.1fHz\n", (double)cnt[0]/t_, (double)cnt[1]/t_, (double)cnt[2]/t_);

            std::string info = "Rates for ";
            for (int i=0; i<3; i++) {
                double rate_is = (double)cnt[i]/t_;
                info += names[i] + ": " + std::to_string((int)rate_is) + "Hz, ";
                if (rate_is == 0.0)
                    ROS_ERROR("Did not receive data for %s", names[i].c_str());
            }
            ROS_DEBUG_STREAM((info + " expected: " + std::to_string((int)rate_exp) + "Hz").c_str());
            cnt[0] = 0; cnt[1] = 0; cnt[2] = 0;
        }


    }

    // Disable automatic updates
    // TODO: The functions will receive an unexpected response
    getOpening(0);
    getSpeed(0);
    getForce(0);

    ROS_INFO("Thread ended");
}

void sigint_handler(int sig) {
    ROS_INFO("Exiting...");
    g_mode_periodic = false;
    g_mode_script = false;
    g_mode_polling = false;
    ros::shutdown();
}

/**
 * The main function
 */

int main( int argc, char **argv )
{
   ros::init(argc, argv, "wsg_50");
   ros::NodeHandle nh("~");
   ros::NodeHandle nh_public;
   signal(SIGINT, sigint_handler);

   std::string ip, protocol, com_mode;
   int port, local_port;
   double rate, grasping_force;
   bool use_udp = false;

   nh.param("ip", ip, std::string("192.168.1.20"));
   nh.param("port", port, 1000);
   nh.param("local_port", local_port, 1501);
   nh.param("protocol", protocol, std::string(""));
   nh.param("com_mode", com_mode, std::string(""));
   nh.param("rate", rate, 1.0); // With custom script, up to 30Hz are possible
   nh.param("grasping_force", grasping_force, 0.0);
   string goal_speed_topic_str("");
   nh.param("goal_speed_topic", goal_speed_topic_str, string("goal_speed"));
   string status_topic_str("");
   nh.param("status_topic", status_topic_str, string("status"));
   string homing_srv_str("");
   nh.param("homing_srv", homing_srv_str, string("homing"));

   if (protocol == "udp")
       use_udp = true;
   else
       protocol = "tcp";
   if (com_mode == "script")
       g_mode_script = true;
   else if (com_mode == "auto_update")
       g_mode_periodic = true;
   else {
       com_mode = "polling";
       g_mode_polling = true;
   }

   ROS_INFO("Connecting to %s:%d (%s); communication mode: %s ...", ip.c_str(), port, protocol.c_str(), com_mode.c_str());

   // Connect to device using TCP/USP
   int res_con;
   if (!use_udp)
       res_con = cmd_connect_tcp( ip.c_str(), port );
   else
       res_con = cmd_connect_udp(local_port, ip.c_str(), port );

   if (res_con == 0 ) {
        ROS_INFO("Gripper connection stablished");

		// Services
        ros::ServiceServer moveSS, graspSS, releaseSS, homingSS, stopSS, ackSS, incrementSS, setAccSS, setForceSS;

        if (g_mode_script || g_mode_polling) {
            moveSS = nh_public.advertiseService("move", moveSrv);
            graspSS = nh_public.advertiseService("grasp", graspSrv);
            releaseSS = nh_public.advertiseService("release", releaseSrv);
            homingSS = nh_public.advertiseService(homing_srv_str, homingSrv);
            stopSS = nh_public.advertiseService("stop", stopSrv);
            ackSS = nh_public.advertiseService("ack", ackSrv);
            incrementSS = nh_public.advertiseService("move_incrementally", incrementSrv);

            setAccSS = nh_public.advertiseService("set_acceleration", setAccSrv);
            setForceSS = nh_public.advertiseService("set_force", setForceSrv);
            //pub_tact0 = nh_public.advertise<sun_wsg50_common::Tactile>("finger0/tactile_voltage", 1);
            //pub_tact1 = nh_public.advertise<sun_wsg50_common::Tactile>("finger1/tactile_voltage", 1);
        }

		// Subscriber
        ros::Subscriber sub_position, sub_speed;
        if (g_mode_script || g_mode_periodic)
            sub_position = nh_public.subscribe("goal_position", 5, position_cb);
        if (g_mode_script)
            sub_speed = nh_public.subscribe(goal_speed_topic_str, 1, speed_cb);

		// Publisher
		g_pub_state = nh_public.advertise<sun_wsg50_common::Status>(status_topic_str, 1);
		g_pub_joint = nh_public.advertise<sensor_msgs::JointState>("/wsg_50_driver/joint_states", 10);
        if (g_mode_script || g_mode_periodic)
            g_pub_moving = nh_public.advertise<std_msgs::Bool>("moving", 10);

		ROS_INFO("Ready to use, homing now...");
		homing();

		if (grasping_force > 0.0) {
			ROS_INFO("Setting grasping force limit to %5.1f", grasping_force);
			setGraspingForceLimit(grasping_force);
		}

        ROS_INFO("Init done. Starting timer/thread with target rate %.1f.", rate);
        std::thread th;
        ros::Timer tmr;
        if (g_mode_polling || g_mode_script)
            tmr = nh.createTimer(ros::Duration(1.0/rate), timer_cb);
        if (g_mode_periodic)
             th = std::thread(read_thread, (int)(1000.0/rate));

        ros::spin();

	} else {
        ROS_ERROR("Unable to connect, please check the port and address used.");
	}

   ROS_INFO("Exiting...");
   g_mode_periodic = false;
   g_mode_script = false;
   g_mode_polling = false;
   sleep(1);
   cmd_disconnect();

	return 0;

}


//------------------------------------------------------------------------
// Testing functions
//------------------------------------------------------------------------
