#include "ros/ros.h"
#include <ros/package.h>
#include <sensor_msgs/PointCloud2.h>
#include <tf/tf.h>
#include <tf_conversions/tf_eigen.h>
#include <geometry_msgs/Pose.h>
#include <eigen_conversions/eigen_msg.h>
#include <Eigen/Geometry>
#include <Eigen/Dense>
#include <visualization_msgs/Marker.h>
#include <geometry_msgs/PoseArray.h>
#include "tf/transform_listener.h"
#include "sensor_msgs/PointCloud.h"
#include <sensor_msgs/point_cloud_conversion.h>
#include "tf/message_filter.h"
#include "message_filters/subscriber.h"
#include "geometry_msgs/PoseStamped.h"
#include "nav_msgs/Odometry.h"
#include <deque>
#include <visualization_msgs/MarkerArray.h>
#include <std_msgs/Bool.h>
#include <math.h>
#include <cmath>
//PCL
#include <iostream>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/common/eigen.h>
#include <pcl/common/transforms.h>
#include <pcl/range_image/range_image.h>
#include <pcl/filters/voxel_grid.h>
#include <culling/occlusion_culling.h>

// octomap
#include <octomap_msgs/Octomap.h>
#include <octomap/ColorOcTree.h>
#include <octomap/octomap.h>
#include <octomap/OcTree.h>
#include <octomap_msgs/conversions.h>
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>
#include <pcl/point_types.h>
#include <string>

using namespace std;

geometry_msgs::PoseStamped current_pose ;
bool receivedNewPose = false ;
typedef pcl::PointXYZRGB pointType;
typedef pcl::PointCloud<pointType> PointCloud;

void CurrentPoseCallback(const geometry_msgs::PoseStamped::ConstPtr & msg)
{
    ROS_INFO("current pose callback ") ;
    current_pose = *msg ;
    receivedNewPose= true ;
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "current_view_extraction");
    ros::NodeHandle nh;
    ros::Rate loopRate(10);

    // Publishers and subscribers
    ros::Publisher originalPointcloudPub            = nh.advertise<sensor_msgs::PointCloud2>("original_pointcloud", 10);
    ros::Publisher currentPointcloudTransferedPub   = nh.advertise<sensor_msgs::PointCloud2>("current_viewpoint_pointcloud_transfered", 10);//current_viewpoint_pointcloud_transfered
    ros::Publisher currentPointcloudFrustumPub      = nh.advertise<sensor_msgs::PointCloud2>("current_viewpoint_frustum_pointcloud", 10);
    ros::Publisher currentPointcloudPub             = nh.advertise<sensor_msgs::PointCloud2>("current_viewpoint_pointcloud", 10);
   // ros::Publisher accumulatedPointcloudPub         = nh.advertise<sensor_msgs::PointCloud2>("accumulated_pointclouds", 10);
    ros::Publisher sensorFOVPub                     = nh.advertise<visualization_msgs::MarkerArray>("sensor_fov", 100);
    ros::Publisher rayPub                           = nh.advertise<visualization_msgs::Marker>("ray_casts", 10);
    ros::Publisher occupancyGridPub                 = nh.advertise<sensor_msgs::PointCloud2>("occupancy_grid_cloud", 10);

    ros::Subscriber current_pose_sub                = nh.subscribe("current_pose", 10, CurrentPoseCallback);

    // PointCloud and sensor msgs defenitions
    PointCloud::Ptr originalCloud(new PointCloud);
    PointCloud::Ptr currentViewConvertedCloudOutPtr(new PointCloud);
    PointCloud::Ptr currentViewCloudOutPtr(new PointCloud);
    PointCloud::Ptr accomulatedCloudPtr(new PointCloud);
    PointCloud::Ptr frustumCloudPtr(new PointCloud);
    
    visualization_msgs::MarkerArray sensorFOV;
    visualization_msgs::Marker rayLines;
    PointCloud tempCloud,tempCloudOut,tempCloudAdd,frustumCloud;
    sensor_msgs::PointCloud2 originalCloudMsg,currentViewCloudMsg,accomulatedCloudMsg,currentTransferedViewCloudMsg,currentCloudFrustumMsg,occupancyGridCloud;

    std::string pcdFileName,robotFrame,worldFrame;
    nh.param<std::string>("pcd_input_file", pcdFileName, std::string("env2.pcd"));
    nh.param<std::string>("robot_frame", robotFrame, std::string("/base_point_cloud"));
    nh.param<std::string>("tf_frame", worldFrame, std::string("/world"));

    ROS_INFO("PCD File %s, Robot Frame:%s, tf_frame:%s",pcdFileName.c_str(), robotFrame.c_str(), worldFrame.c_str());

    // Load the original map
    std::string path = ros::package::getPath("usar_exploration");
    std::string pcdFilePath = path + "/resources/pcd/" + pcdFileName;       

    ROS_INFO("Loading File%s",pcdFilePath.c_str());
    pcl::io::loadPCDFile<pointType> (pcdFilePath, *originalCloud);
    ROS_INFO("Done, size:%d",originalCloud->points.size()); 

    //Publish the original map once
    pcl::toROSMsg(*originalCloud, originalCloudMsg); //cloud of original
    originalCloudMsg.header.stamp = ros::Time::now();
    originalCloudMsg.header.frame_id = "world";

    originalPointcloudPub.publish(originalCloudMsg);
    OcclusionCulling<pointType> occlusionCulling(nh, pcdFilePath);

    // create listener for the sensor position
    tf::TransformListener listener;
    tf::StampedTransform transform;

    while (ros::ok())
    {
        listener.waitForTransform(robotFrame, worldFrame, ros::Time(0), ros::Duration(0.1));
        try{
            listener.lookupTransform(robotFrame, worldFrame,ros::Time(0), transform);
        }
        catch (tf::TransformException ex){
            ROS_ERROR("%s",ex.what());
            ros::Duration(1.0).sleep();
        }

        if(receivedNewPose)
        {
            ROS_INFO ("view point position %f" , current_pose.pose.position.x ) ;
            
            ros::Time tic = ros::Time::now();
            tempCloud = occlusionCulling.extractVisibleSurface(current_pose.pose);  // point cloud in world frame
            ros::Time toc = ros::Time::now();
            ROS_INFO("Occlusion Culling took:%f", toc.toSec() - tic.toSec());

            frustumCloud = occlusionCulling.getFrustumCloud();
            rayLines = occlusionCulling.getRays();
            occupancyGridCloud = occlusionCulling.getOccupancyGridCloud();
            sensorFOV = occlusionCulling.getFOV();
        
            receivedNewPose = false;
        }

        sensorFOVPub.publish(sensorFOV);

        rayLines.header.frame_id = worldFrame;
        rayLines.header.stamp = ros::Time::now();
        rayPub.publish(rayLines);

        occupancyGridPub.publish(occupancyGridCloud);

        currentViewCloudOutPtr->points = tempCloud.points;
        pcl::toROSMsg(*currentViewCloudOutPtr, currentViewCloudMsg);

        frustumCloudPtr->points = frustumCloud.points;
        pcl::toROSMsg(*frustumCloudPtr, currentCloudFrustumMsg);

        pcl_ros::transformPointCloud(tempCloud, tempCloudOut, transform); // convert the point cloud in camera frame
        currentViewConvertedCloudOutPtr->points = tempCloudOut.points;
        pcl::toROSMsg(*currentViewConvertedCloudOutPtr, currentTransferedViewCloudMsg);

        //tempCloudAdd += tempCloud;
        //accomulatedCloudPtr->points = tempCloudAdd.points;
        //pcl::toROSMsg(*accomulatedCloudPtr, accomulatedCloudMsg);
        // Publishing Point Cloud msgs

        currentCloudFrustumMsg.header.stamp = ros::Time::now();
        currentCloudFrustumMsg.header.frame_id = worldFrame;
        currentPointcloudFrustumPub.publish(currentCloudFrustumMsg);

        currentViewCloudMsg.header.stamp = ros::Time::now();
        currentViewCloudMsg.header.frame_id = worldFrame;
        currentPointcloudPub.publish(currentViewCloudMsg);

        //accomulatedCloudMsg.header.stamp = ros::Time::now();
        //accomulatedCloudMsg.header.frame_id = worldFrame;
        //accumulatedPointcloudPub.publish(accomulatedCloudMsg);

        currentTransferedViewCloudMsg.header.stamp = ros::Time::now();
        currentTransferedViewCloudMsg.header.frame_id = robotFrame;
        currentPointcloudTransferedPub.publish(currentTransferedViewCloudMsg);

        ros::spinOnce();
        loopRate.sleep();
    }
    return 0;
}



