/*+-------------------------------------------------------------------------+
  |                       MultiVehicle simulator (libmvsim)                 |
  |                                                                         |
  | Copyright (C) 2014-2022  Jose Luis Blanco Claraco                       |
  | Copyright (C) 2017  Borys Tymchenko (Odessa Polytechnic University)     |
  | Distributed under 3-clause BSD License                                  |
  |   See COPYING                                                           |
  +-------------------------------------------------------------------------+ */

#include "mvsim/mvsim_node_core.h"

/*------------------------------------------------------------------------------
 * main()
 * Main function to set up ROS node.
 *----------------------------------------------------------------------------*/

int main(int argc, char** argv)
{
	// Set up ROS.
#if PACKAGE_ROS_VERSION == 1
	ros::init(argc, argv, "mvsim");
	ros::NodeHandle n;
#else
	rclcpp::init(argc, argv);
	auto n = rclcpp::Node::make_shared("mvsim");

#endif

	// Create a "Node" object.
	MVSimNode node(n);

	// Declare variables that can be modified by launch file or command line.
	int rate;
	std::string world_file;

	// Initialize node parameters from launch file or command line.
	// Use a private node handle so that multiple instances of the node can be
	// run simultaneously
	// while using different parameters.
	ros::NodeHandle private_node_handle_("~");
	private_node_handle_.param("simul_rate", rate, 100);
	private_node_handle_.param("world_file", world_file, std::string(""));

	// Launch mvsim:
	node.launch_mvsim_server();

	// Init world model:
	if (!world_file.empty()) node.loadWorldModel(world_file);

	// Attach world as a mvsim communications node:
	node.mvsim_world_.connectToServer();

#if PACKAGE_ROS_VERSION == 1
	// Set up a dynamic reconfigure server.
	// Do this before parameter server, else some of the parameter server
	// values can be overwritten.
	dynamic_reconfigure::Server<mvsim::mvsimNodeConfig> dr_srv;
	dynamic_reconfigure::Server<mvsim::mvsimNodeConfig>::CallbackType cb;
	cb = boost::bind(&MVSimNode::configCallback, &node, _1, _2);
	dr_srv.setCallback(cb);
#endif

	// Tell ROS how fast to run this node.
	ros::Rate r(rate);

	// Main loop.
	while (n.ok())
	{
		node.spin();
		ros::spinOnce();
		r.sleep();
	}

	return 0;

}  // end main()
