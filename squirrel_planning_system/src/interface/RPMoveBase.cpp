#include <ros/ros.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <boost/foreach.hpp>
#include <actionlib/client/simple_action_client.h>
#include "squirrel_planning_dispatch_msgs/ActionDispatch.h"
#include "squirrel_planning_dispatch_msgs/ActionFeedback.h"
#include "move_base_msgs/MoveBaseAction.h"
#include "mongodb_store/message_store.h"
#include "geometry_msgs/PoseStamped.h"
#include "squirrel_planning_system/RPMoveBase.h"

/* The implementation of RPMoveBase.h */
namespace KCL_rosplan {

	/* constructor */
	RPMoveBase::RPMoveBase(ros::NodeHandle &nh, std::string &actionserver)
	 : message_store(nh), action_client(actionserver, true) {
		
		// create the action client
		ROS_INFO("KCL: (MoveBase) waiting for action server to start on %s", actionserver.c_str());
		action_client.waitForServer();
		
		// create the action feedback publisher
		action_feedback_pub = nh.advertise<squirrel_planning_dispatch_msgs::ActionFeedback>("/kcl_rosplan/action_feedback", 10, true);
	}

	/* action dispatch callback */
	void RPMoveBase::dispatchCallback(const squirrel_planning_dispatch_msgs::ActionDispatch::ConstPtr& msg) {

		// ignore non-goto-waypoint actions
		if(0!=msg->name.compare("goto_waypoint")) return;

		ROS_INFO("KCL: (MoveBase) action recieved");

		// get waypoint ID from action dispatch
		std::string wpID;
		bool found = false;
		for(size_t i=0; i<msg->parameters.size(); i++) {
			if(0==msg->parameters[i].key.compare("to")) {
				wpID = msg->parameters[i].value;
				found = true;
			}
		}
		if(!found) {
			ROS_INFO("KCL: (MoveBase) aborting action dispatch; malformed parameters");
			return;
		}
		
		// get pose from message store
		std::vector< boost::shared_ptr<geometry_msgs::PoseStamped> > results;
		if(message_store.queryNamed<geometry_msgs::PoseStamped>(wpID, results)) {
			if(results.size()<1) {
				ROS_INFO("KCL: (MoveBase) aborting action dispatch; no matching wpID %s", wpID.c_str());
				return;
			}
			if(results.size()>1)
				ROS_INFO("KCL: (MoveBase) multiple waypoints share the same wpID");

			// dispatch MoveBase action
			move_base_msgs::MoveBaseGoal goal;
			goal.target_pose.header.frame_id = results[0]->header.frame_id;
			goal.target_pose.pose.position.x = results[0]->pose.position.x;
			goal.target_pose.pose.position.y = results[0]->pose.position.y;
			goal.target_pose.pose.orientation.w = 1;
			action_client.sendGoal(goal);

			// publish feedback (enabled)
			 squirrel_planning_dispatch_msgs::ActionFeedback fb;
			fb.action_id = msg->action_id;
			fb.status = "action enabled";
			action_feedback_pub.publish(fb);

			bool finished_before_timeout = action_client.waitForResult(ros::Duration(msg->duration));
			if (finished_before_timeout) {

				actionlib::SimpleClientGoalState state = action_client.getState();
				ROS_INFO("KCL: (MoveBase) action finished: %s", state.toString().c_str());
				
				// publish feedback (achieved)
				 squirrel_planning_dispatch_msgs::ActionFeedback fb;
				fb.action_id = msg->action_id;
				fb.status = "action achieved";
				action_feedback_pub.publish(fb);

			} else {

				// publish feedback (failed)
				 squirrel_planning_dispatch_msgs::ActionFeedback fb;
				fb.action_id = msg->action_id;
				fb.status = "action failed";
				action_feedback_pub.publish(fb);

				ROS_INFO("KCL: (MoveBase) action timed out");

			}

		} else ROS_INFO("KCL: (MoveBase) aborting action dispatch; query to sceneDB failed");
	}
} // close namespace

	/*-------------*/
	/* Main method */
	/*-------------*/

	int main(int argc, char **argv) {

		ros::init(argc, argv, "rosplan_interface_movebase");
		ros::NodeHandle nh;

		std::string actionserver;
		nh.param("action_server", actionserver, std::string("/move_base"));

		// create PDDL action subscriber
		KCL_rosplan::RPMoveBase rpmb(nh, actionserver);
	
		// listen for action dispatch
		ros::Subscriber ds = nh.subscribe("/kcl_rosplan/action_dispatch", 1000, &KCL_rosplan::RPMoveBase::dispatchCallback, &rpmb);
		ROS_INFO("KCL: (MoveBase) Ready to receive");

		ros::spin();
		return 0;
	}