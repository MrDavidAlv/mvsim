/**
  */

#include "mvsim/mvsim_node_core.h"
#include <rapidxml_utils.hpp>
#include <iostream>

#include <mrpt/system/os.h> // kbhit()

#include <mrpt_bridge/laser_scan.h>
#include <mrpt_bridge/map.h>
#include <nav_msgs/MapMetaData.h>
#include <nav_msgs/GetMap.h>

#include <nav_msgs/Odometry.h>

/*------------------------------------------------------------------------------
 * MVSimNode()
 * Constructor.
 *----------------------------------------------------------------------------*/
MVSimNode::MVSimNode(ros::NodeHandle &n) :
	realtime_factor_ (1.0),
	gui_refresh_period_ms_ (75),
	m_show_gui       (true),
	m_n              (n),
	t_old_           (-1),
	world_init_ok_   (false),
	m_period_ms_publish_tf (20),
	m_period_ms_teleop_refresh(100),
	m_teleop_idx_veh (0)
{
	// Launch GUI thread:
	thread_params_.obj = this;
	thGUI_ = mrpt::system::createThreadRef( &MVSimNode::thread_update_GUI, thread_params_);
}

void MVSimNode::loadWorldModel(const std::string &world_xml_file)
{
	ROS_INFO("[MVSimNode] Loading world file: %s",world_xml_file.c_str());

	// Load from XML:
	rapidxml::file<> fil_xml(world_xml_file.c_str());
	mvsim_world_.load_from_XML( fil_xml.data(), world_xml_file );

	ROS_INFO("[MVSimNode] World file load done.");
	world_init_ok_	= true;
}


/*------------------------------------------------------------------------------
 * ~MVSimNode()
 * Destructor.
 *----------------------------------------------------------------------------*/

MVSimNode::~MVSimNode()
{
	thread_params_.closing = true;
	mrpt::system::joinThread( thGUI_ );
}

/*------------------------------------------------------------------------------
 * publishMessage()
 * Publish the message.
 *----------------------------------------------------------------------------*/

void MVSimNode::publishMessage(ros::Publisher *pub_message)
{
/*  mvsim::NodeExampleData msg;
  msg.message = message;
  msg.a = a;
  msg.b = b;

  pub_message->publish(msg);*/
}

/*------------------------------------------------------------------------------
 * messageCallback()
 * Callback function for subscriber.
 *----------------------------------------------------------------------------*/

/*void MVSimNode::messageCallback(const node_example::NodeExampleData::ConstPtr &msg)
{
  message = msg->message;
  a = msg->a;
  b = msg->b;

  // Note that these are only set to INFO so they will print to a terminal for example purposes.
  // Typically, they should be DEBUG.
  ROS_INFO("message is %s", message.c_str());
  ROS_INFO("sum of a + b = %d", a + b);
}*/

/*------------------------------------------------------------------------------
 * configCallback()
 * Callback function for dynamic reconfigure server.
 *----------------------------------------------------------------------------*/

void MVSimNode::configCallback(mvsim::mvsimNodeConfig &config, uint32_t level)
{
	// Set class variables to new values. They should match what is input at the dynamic reconfigure GUI.
	//  message = config.message.c_str();
	ROS_INFO("MVSimNode::configCallback() called.");

	mvsim_world_.set_simul_timestep( config.simul_timestep );

	if (mvsim_world_.is_GUI_open() && !config.show_gui)
		mvsim_world_.close_GUI();

}

// Process pending msgs, run real-time simulation, etc.
void MVSimNode::spin()
{
	using namespace mvsim;

	// Do simulation itself:
	// ========================================================================
	// Handle 1st iter:
	if (t_old_<0) t_old_ = realtime_tictac_.Tac();
	// Compute how much time has passed to simulate in real-time:
	double t_new = realtime_tictac_.Tac();
	double incr_time = realtime_factor_ * (t_new-t_old_);

	if (incr_time < mvsim_world_.get_simul_timestep())  // Just in case the computer is *really fast*...
		return;

	// Simulate:
	mvsim_world_.run_simulation(incr_time);

	//t_old_simul = world.get_simul_time();
	t_old_ = t_new;


	// House-keeping to be done at each iteration
	// ========================================================================
	const World::TListVehicles &vehs = mvsim_world_.getListOfVehicles();

	if (m_tim_publish_tf.Tac()>m_period_ms_publish_tf*1e-3)
	{
		m_tim_publish_tf.Tic();

		for (World::TListVehicles::const_iterator it = vehs.begin(); it!=vehs.end();++it)
		{
			const std::string &sVehName = it->first;
			const VehicleBase * veh = it->second;

			// get and broadcast the ground-truth vehicle pose:
			const mrpt::math::TPose3D & gh_veh_pose = veh->getPose();

			if (1)
			{
				this->broadcastTF_GTPose(gh_veh_pose,sVehName);
			}
			else
			{
				this->broadcastTF_Odom(gh_veh_pose,sVehName);
			}
		}
	} // end publish tf

	// GUI msgs, teleop, etc.
	// ========================================================================
	if (m_tim_teleop_refresh.Tac()>m_period_ms_teleop_refresh*1e-3)
	{
		m_tim_teleop_refresh.Tic();

		std::string txt2gui_tmp;
		World::TGUIKeyEvent keyevent = m_gui_key_events;

		// Global keys:
		switch (keyevent.keycode)
		{
		//case 27: do_exit=true; break;
		case '1': case '2': case '3': case '4': case '5': case '6':
			m_teleop_idx_veh = keyevent.keycode-'1';
			break;
		};

		{ // Test: Differential drive: Control raw forces
			txt2gui_tmp+=mrpt::format("Selected vehicle: %u/%u\n", static_cast<unsigned>(m_teleop_idx_veh+1),static_cast<unsigned>(vehs.size()) );
			if (vehs.size()>m_teleop_idx_veh)
			{
				// Get iterator to selected vehicle:
				World::TListVehicles::const_iterator it_veh = vehs.begin();
				std::advance(it_veh, m_teleop_idx_veh);

				// Get speed: ground truth
				{
					const vec3 &vel = it_veh->second->getVelocityLocal();
					txt2gui_tmp+=mrpt::format("gt. vel: lx=%7.03f, ly=%7.03f, w= %7.03fdeg/s\n", vel.vals[0], vel.vals[1], mrpt::utils::RAD2DEG(vel.vals[2]) );
				}
				// Get speed: ground truth
				{
					const vec3 &vel = it_veh->second->getVelocityLocalOdoEstimate();
					txt2gui_tmp+=mrpt::format("odo vel: lx=%7.03f, ly=%7.03f, w= %7.03fdeg/s\n", vel.vals[0], vel.vals[1], mrpt::utils::RAD2DEG(vel.vals[2]) );
				}

				// Generic teleoperation interface for any controller that supports it:
				{
					ControllerBaseInterface *controller = it_veh->second->getControllerInterface();
					ControllerBaseInterface::TeleopInput teleop_in;
					ControllerBaseInterface::TeleopOutput teleop_out;
					teleop_in.keycode = keyevent.keycode;
					controller->teleop_interface(teleop_in,teleop_out);
					txt2gui_tmp+=teleop_out.append_gui_lines;
				}

			}
		}

		m_msg2gui = txt2gui_tmp;  // send txt msgs to show in the GUI

		// Clear the keystroke buffer
		if (keyevent.keycode!=0)
			m_gui_key_events = World::TGUIKeyEvent();

	} // end refresh teleop stuff



}


/*------------------------------------------------------------------------------
 * thread_update_GUI()
 *----------------------------------------------------------------------------*/
void MVSimNode::thread_update_GUI(TThreadParams &thread_params)
{
	using namespace mvsim;

	MVSimNode *obj = thread_params.obj;

	while (!thread_params.closing)
	{
		if (obj->world_init_ok_ && obj->m_show_gui)
		{
			World::TUpdateGUIParams guiparams;
			guiparams.msg_lines = obj->m_msg2gui;

			obj->mvsim_world_.update_GUI(&guiparams);

			// Send key-strokes to the main thread:
			if(guiparams.keyevent.keycode!=0) obj->m_gui_key_events = guiparams.keyevent;
		}
		mrpt::system::sleep(obj->gui_refresh_period_ms_);
	}
}


/** Publish the ground truth pose of a robot to tf as: map -> <ROBOT>/base_link */
void MVSimNode::broadcastTF_GTPose(const mrpt::math::TPose3D &pose, const std::string &robotName)
{
	broadcastTF(pose,"/map","/"+robotName+"/base_link");
}

/** Publish "odometry" for a robot to tf as: odom -> <ROBOT>/base_link */
void MVSimNode::broadcastTF_Odom(const mrpt::math::TPose3D &pose,const std::string &robotName)
{
	const std::string sOdomName      ="/"+robotName+"/odom";
	const std::string sChildrenFrame ="/"+robotName+"/base_link";

	broadcastTF(pose,sOdomName, sChildrenFrame);

	// Apart from TF, publish to the "odom" topic as well
	if (!m_odo_publisher)  // 1st usage: Create publisher
	{
		ros::NodeHandle nh;
		m_odo_publisher = nh.advertise<nav_msgs::Odometry>("/"+robotName+"/odom", 10);
	}


	nav_msgs::Odometry odoMsg;

	odoMsg.pose.pose.position.x = pose.x;
	odoMsg.pose.pose.position.y = pose.y;
	odoMsg.pose.pose.position.z = pose.z;

	tf::Quaternion quat;
	quat.setEuler( pose.roll, pose.pitch, pose.yaw );

	odoMsg.pose.pose.orientation.x = quat.x();
	odoMsg.pose.pose.orientation.y = quat.y();
	odoMsg.pose.pose.orientation.z = quat.z();
	odoMsg.pose.pose.orientation.w = quat.w();

	// first, we'll populate the header for the odometry msg
	odoMsg.header.frame_id = sOdomName;
	odoMsg.child_frame_id  = sChildrenFrame;

	// publish:
	m_odo_publisher.publish(odoMsg);
}

/** Publish pose to tf: parentFrame -> <ROBOT>/base_link */
void MVSimNode::broadcastTF(
	const mrpt::math::TPose3D &pose,
	const std::string &parentFrame,
	const std::string &childFrame)
{
	tf::Matrix3x3 rot;
	rot.setEulerYPR(pose.yaw,pose.pitch,pose.roll);
	const tf::Transform tr(rot, tf::Vector3(pose.x,pose.y,pose.z) );

	tf_br_.sendTransform(tf::StampedTransform(tr, ros::Time::now(), parentFrame, childFrame));
}


// ROS: Publish grid map for visualization purposes:
#if 0
void onNewWorldElement()
{
	static ros::Publisher pub_map_ros  = n_.advertise<nav_msgs::OccupancyGrid>("map", 1, true);
	static ros::Publisher pub_metadata = n_.advertise<nav_msgs::MapMetaData>("map_metadata", 1, true);

	nav_msgs::GetMap::Response resp_ros;

	if(1/*debug_*/) printf("gridMap[0]:  %i x %i @ %4.3fm/p, %4.3f, %4.3f, %4.3f, %4.3f\n",
					  m_grid.getSizeX(), m_grid.getSizeY(), m_grid.getResolution(),
					  m_grid.getXMin(), m_grid.getYMin(),
					  m_grid.getXMax(), m_grid.getYMax());

	mrpt_bridge::convert(m_grid, resp_ros_.map);

	static size_t loop_count = 0;
	resp_ros.map.header.stamp = ros::Time::now();
	resp_ros.map.header.seq = loop_count++;

	if(pub_map_ros.getNumSubscribers() > 0)
	{
		pub_map_ros.publish(resp_ros_.map );
	}
	if(pub_metadata.getNumSubscribers() > 0)
	{
		pub_metadata.publish( resp_ros_.map.info );
	}
}
#endif
