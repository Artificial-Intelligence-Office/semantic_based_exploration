/*
 * Copyright 2015 Andreas Bircher, ASL, ETH Zurich, Switzerland
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RRTTREE_HPP_
#define RRTTREE_HPP_
#include <thread>
#include <chrono>
#include <cstdlib>
#include <rrt_explorer/rrt_core.h>
#include <rrt_explorer/rrt_tree.h>
#include <std_srvs/Empty.h>
#include <trajectory_msgs/MultiDOFJointTrajectory.h>
#include <fstream>
/* sqrt example */
#include <stdio.h>      /* printf */
#include <math.h>       /* sqrt */
#include <tf/transform_datatypes.h>
using namespace std;

rrtNBV::RrtTree::RrtTree()
  : rrtNBV::TreeBase::TreeBase()
{
  kdTree_ = kd_create(3);
  iterationCount_ = 0;
  for (int i = 0; i < 4; i++) {
    inspectionThrottleTime_.push_back(ros::Time::now().toSec());
  }

  // If logging is required, set up files here
  bool ifLog = false;
  std::string ns = ros::this_node::getName();
  ros::param::get(ns + "/nbvp/log/on", ifLog);
  if (ifLog) {
    time_t rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime(&rawtime);
    logFilePath_ = ros::package::getPath("rrt_explorer") + "/data/"
        + std::to_string(ptm->tm_year + 1900) + "_" + std::to_string(ptm->tm_mon + 1) + "_"
        + std::to_string(ptm->tm_mday) + "_" + std::to_string(ptm->tm_hour) + "_"
        + std::to_string(ptm->tm_min) + "_" + std::to_string(ptm->tm_sec);
    system(("mkdir -p " + logFilePath_).c_str());
    logFilePath_ += "/";
    fileResponse_.open((logFilePath_ + "response.txt").c_str(), std::ios::out);
    filePath_.open((logFilePath_ + "path.txt").c_str(), std::ios::out);
  }
}

rrtNBV::RrtTree::RrtTree(mesh::StlMesh * mesh, volumetric_mapping::OctomapManager * manager)
{
  mesh_ = mesh;
  manager_ = manager;
  kdTree_ = kd_create(3);
  iterationCount_ = 0;
  for (int i = 0; i < 4; i++)
  {
    inspectionThrottleTime_.push_back(ros::Time::now().toSec());
  }
  // If logging is required, set up files here
  bool ifLog = false;
  std::string ns = ros::this_node::getName();
  ros::param::get(ns + "/nbvp/log/on", ifLog);
  if (ifLog) {
    time_t rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime(&rawtime);
    logFilePath_ = ros::package::getPath("rrt_explorer") + "/data/"
        + std::to_string(ptm->tm_year + 1900) + "_" + std::to_string(ptm->tm_mon + 1) + "_"
        + std::to_string(ptm->tm_mday) + "_" + std::to_string(ptm->tm_hour) + "_"
        + std::to_string(ptm->tm_min) + "_" + std::to_string(ptm->tm_sec);
    system(("mkdir -p " + logFilePath_).c_str());
    logFilePath_ += "/";
    fileResponse_.open((logFilePath_ + "response.txt").c_str(), std::ios::out);
    filePath_.open((logFilePath_ + "path.txt").c_str(), std::ios::out);
  }
}

rrtNBV::RrtTree::~RrtTree()
{
  delete rootNode_;
  kd_free(kdTree_);
  if (fileResponse_.is_open()) {
    fileResponse_.close();
  }
  if (fileTree_.is_open()) {
    fileTree_.close();
  }
  if (filePath_.is_open()) {
    filePath_.close();
  }
}

void rrtNBV::RrtTree::setStateFromPoseMsg(const geometry_msgs::PoseWithCovarianceStamped& pose)
{
  // Get latest transform to the planning frame and transform the pose
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  try {
    listener.lookupTransform(params_.navigationFrame_, pose.header.frame_id, pose.header.stamp,
                             transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return;
  }

  tf::Pose poseTF;
  tf::poseMsgToTF(pose.pose.pose, poseTF);
  tf::Vector3 position = poseTF.getOrigin();
  position = transform * position;
  tf::Quaternion quat = poseTF.getRotation();
  quat = transform * quat;
  root_[0] = position.x();
  root_[1] = position.y();
  root_[2] = position.z();
  root_[3] = tf::getYaw(quat);

  // Log the vehicle response in the planning frame
  static double logThrottleTime = ros::Time::now().toSec();
  if (ros::Time::now().toSec() - logThrottleTime > params_.log_throttle_) {
    logThrottleTime += params_.log_throttle_;
    if (params_.log_) {
      for (int i = 0; i < root_.size() - 1; i++) {
        fileResponse_ << root_[i] << ",";
      }
      fileResponse_ << root_[root_.size() - 1] << "\n";
    }
  }
  // Update the inspected parts of the mesh using the current position
  if (ros::Time::now().toSec() - inspectionThrottleTime_[0] > params_.inspection_throttle_) {
    inspectionThrottleTime_[0] += params_.inspection_throttle_;
    if (mesh_) {
      geometry_msgs::Pose poseTransformed;
      tf::poseTFToMsg(transform * poseTF, poseTransformed);
      mesh_->setPeerPose(poseTransformed, 0);
      mesh_->incorporateViewFromPoseMsg(poseTransformed, 0);
      // Publish the mesh marker for visualization in rviz
      visualization_msgs::Marker inspected;
      inspected.ns = "meshInspected";
      inspected.id = 0;
      inspected.header.seq = inspected.id;
      inspected.header.stamp = pose.header.stamp;
      inspected.header.frame_id = params_.navigationFrame_;
      inspected.type = visualization_msgs::Marker::TRIANGLE_LIST;
      inspected.lifetime = ros::Duration(10);
      inspected.action = visualization_msgs::Marker::ADD;
      inspected.pose.position.x = 0.0;
      inspected.pose.position.y = 0.0;
      inspected.pose.position.z = 0.0;
      inspected.pose.orientation.x = 0.0;
      inspected.pose.orientation.y = 0.0;
      inspected.pose.orientation.z = 0.0;
      inspected.pose.orientation.w = 1.0;
      inspected.scale.x = 1.0;
      inspected.scale.y = 1.0;
      inspected.scale.z = 1.0;
      visualization_msgs::Marker uninspected = inspected;
      uninspected.header.seq++;
      uninspected.id++;
      uninspected.ns = "meshUninspected";
      mesh_->assembleMarkerArray(inspected, uninspected);
      if (inspected.points.size() > 0) {
        params_.inspectionPath_.publish(inspected);
      }
      if (uninspected.points.size() > 0) {
        params_.inspectionPath_.publish(uninspected);
      }
    }
  }
}

void rrtNBV::RrtTree::setStateFromPoseStampedMsg(const geometry_msgs::PoseStamped& pose)
{
  // Get latest transform to the planning frame and transform the pose
  //ROS_INFO("pose callback");
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  std::cout<<"pose.header.frame_id  " << pose.header.frame_id << std::endl << std::flush; 
  try {
    listener.lookupTransform(params_.navigationFrame_, pose.header.frame_id, pose.header.stamp,
                             transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return;
  }

  tf::Pose poseTF;
  tf::poseMsgToTF(pose.pose, poseTF);
  tf::Vector3 position = poseTF.getOrigin();
  position = transform * position;
  tf::Quaternion quat = poseTF.getRotation();
  quat = transform * quat;
  root_[0] = position.x();
  root_[1] = position.y();
  root_[2] = position.z();
  root_[3] = tf::getYaw(quat);

  // debugiing 
  geometry_msgs::PoseStamped poseMsg;
  poseMsg.header.stamp       = ros::Time::now();
  poseMsg.header.frame_id    = params_.navigationFrame_;
  poseMsg.pose.position.x    = root_[0];
  poseMsg.pose.position.y    = root_[1];
  poseMsg.pose.position.z    = root_[2];
  tf::Quaternion quat2;
  quat2.setEuler(0.0, 0.0, root_[3]);
  poseMsg.pose.orientation.x = quat2.x();
  poseMsg.pose.orientation.y = quat2.y();
  poseMsg.pose.orientation.z = quat2.z();
  poseMsg.pose.orientation.w = quat2.w();
  params_.transfromedPoseDebug.publish(poseMsg);

  // Log the vehicle response in the planning frame
  static double logThrottleTime = ros::Time::now().toSec();
  if (ros::Time::now().toSec() - logThrottleTime > params_.log_throttle_) {
    logThrottleTime += params_.log_throttle_;
    if (params_.log_) {
      for (int i = 0; i < root_.size() - 1; i++) {
        fileResponse_ << root_[i] << ",";
      }
      fileResponse_ << root_[root_.size() - 1] << "\n";
    }
  }
  
  // Update the inspected parts of the mesh using the current position
  if (ros::Time::now().toSec() - inspectionThrottleTime_[0] > params_.inspection_throttle_) {
    inspectionThrottleTime_[0] += params_.inspection_throttle_;
    if (mesh_) {
      geometry_msgs::Pose poseTransformed;
      tf::poseTFToMsg(transform * poseTF, poseTransformed);
      mesh_->setPeerPose(poseTransformed, 0);
      mesh_->incorporateViewFromPoseMsg(poseTransformed, 0);
      // Publish the mesh marker for visualization in rviz
      visualization_msgs::Marker inspected;
      inspected.ns = "meshInspected";
      inspected.id = 0;
      inspected.header.seq = inspected.id;
      inspected.header.stamp = pose.header.stamp;
      inspected.header.frame_id = params_.navigationFrame_;
      inspected.type = visualization_msgs::Marker::TRIANGLE_LIST;
      inspected.lifetime = ros::Duration(10);
      inspected.action = visualization_msgs::Marker::ADD;
      inspected.pose.position.x = 0.0;
      inspected.pose.position.y = 0.0;
      inspected.pose.position.z = 0.0;
      inspected.pose.orientation.x = 0.0;
      inspected.pose.orientation.y = 0.0;
      inspected.pose.orientation.z = 0.0;
      inspected.pose.orientation.w = 1.0;
      inspected.scale.x = 1.0;
      inspected.scale.y = 1.0;
      inspected.scale.z = 1.0;
      visualization_msgs::Marker uninspected = inspected;
      uninspected.header.seq++;
      uninspected.id++;
      uninspected.ns = "meshUninspected";
      mesh_->assembleMarkerArray(inspected, uninspected);
      if (inspected.points.size() > 0) {
        params_.inspectionPath_.publish(inspected);
      }
      if (uninspected.points.size() > 0) {
        params_.inspectionPath_.publish(uninspected);
      }
    }
  }
}

void rrtNBV::RrtTree::setStateFromOdometryMsg(const nav_msgs::Odometry& pose)
{
  // Get latest transform to the planning frame and transform the pose
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  try {
    listener.lookupTransform(params_.navigationFrame_, pose.header.frame_id, pose.header.stamp,
                             transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return;
  }
  tf::Pose poseTF;
  tf::poseMsgToTF(pose.pose.pose, poseTF);
  tf::Vector3 position = poseTF.getOrigin();
  position = transform * position;
  tf::Quaternion quat = poseTF.getRotation();
  quat = transform * quat;
  root_[0] = position.x();
  root_[1] = position.y();
  root_[2] = position.z();
  root_[3] = tf::getYaw(quat);

  // Log the vehicle response in the planning frame
  static double logThrottleTime = ros::Time::now().toSec();
  if (ros::Time::now().toSec() - logThrottleTime > params_.log_throttle_) {
    logThrottleTime += params_.log_throttle_;
    if (params_.log_) {
      for (int i = 0; i < root_.size() - 1; i++) {
        fileResponse_ << root_[i] << ",";
      }
      fileResponse_ << root_[root_.size() - 1] << "\n";
    }
  }
  // Update the inspected parts of the mesh using the current position
  if (ros::Time::now().toSec() - inspectionThrottleTime_[0] > params_.inspection_throttle_) {
    inspectionThrottleTime_[0] += params_.inspection_throttle_;
    if (mesh_) {
      geometry_msgs::Pose poseTransformed;
      tf::poseTFToMsg(transform * poseTF, poseTransformed);
      mesh_->setPeerPose(poseTransformed, 0);
      mesh_->incorporateViewFromPoseMsg(poseTransformed, 0);
      // Publish the mesh marker for visualization in rviz
      visualization_msgs::Marker inspected;
      inspected.ns = "meshInspected";
      inspected.id = 0;
      inspected.header.seq = inspected.id;
      inspected.header.stamp = pose.header.stamp;
      inspected.header.frame_id = params_.navigationFrame_;
      inspected.type = visualization_msgs::Marker::TRIANGLE_LIST;
      inspected.lifetime = ros::Duration(10);
      inspected.action = visualization_msgs::Marker::ADD;
      inspected.pose.position.x = 0.0;
      inspected.pose.position.y = 0.0;
      inspected.pose.position.z = 0.0;
      inspected.pose.orientation.x = 0.0;
      inspected.pose.orientation.y = 0.0;
      inspected.pose.orientation.z = 0.0;
      inspected.pose.orientation.w = 1.0;
      inspected.scale.x = 1.0;
      inspected.scale.y = 1.0;
      inspected.scale.z = 1.0;
      visualization_msgs::Marker uninspected = inspected;
      uninspected.header.seq++;
      uninspected.id++;
      uninspected.ns = "meshUninspected";
      mesh_->assembleMarkerArray(inspected, uninspected);
      if (inspected.points.size() > 0) {
        params_.inspectionPath_.publish(inspected);
      }
      if (uninspected.points.size() > 0) {
        params_.inspectionPath_.publish(uninspected);
      }
    }
  }
}

bool rrtNBV::RrtTree::iterate(int iterations)
{
  //    ROS_INFO("iterate callback");

  // In this function a new configuration is sampled and added to the tree.
  StateVec newState;

  // Sample over a sphere with the radius of the maximum diagonal of the exploration
  // space. Throw away samples outside the sampling region it exiting is not allowed
  // by the corresponding parameter. This method is to not bias the tree towards the
  // center of the exploration space.
  double radius = sqrt(
        SQ(params_.minX_ - params_.maxX_) + SQ(params_.minY_ - params_.maxY_)
        + SQ(params_.minZ_ - params_.maxZ_));
  bool solutionFound = false;
  while (!solutionFound)
  {
    for (int i = 0; i < 3; i++)
    {
      newState[i] = 2.0 * radius * (((double) rand()) / ((double) RAND_MAX) - 0.5);
    }
    if (SQ(newState[0]) + SQ(newState[1]) + SQ(newState[2]) > pow(radius, 2.0))
      continue;
    // Offset new state by root
    newState += rootNode_->state_;
    if (!params_.softBounds_) {
      if (newState.x() < params_.minX_ + 0.5 * params_.boundingBox_.x()) {
        continue;
      } else if (newState.y() < params_.minY_ + 0.5 * params_.boundingBox_.y()) {
        continue;
      } else if (newState.z() < params_.minZ_ + 0.5 * params_.boundingBox_.z()) {
        continue;
      } else if (newState.x() > params_.maxX_ - 0.5 * params_.boundingBox_.x()) {
        continue;
      } else if (newState.y() > params_.maxY_ - 0.5 * params_.boundingBox_.y()) {
        continue;
      } else if (newState.z() > params_.maxZ_ - 0.5 * params_.boundingBox_.z()) {
        continue;
      }
    }
    solutionFound = true;
  }

  //std::cout << "NewState  1 "  <<newState.x() << "   " <<newState.y() << "   " <<newState.z()  << std::endl << std::flush ; 
  // Find nearest neighbour
  kdres * nearest = kd_nearest3(kdTree_, newState.x(), newState.y(), newState.z());
  if (kd_res_size(nearest) <= 0)
  {
    //  ROS_ERROR("IN") ; 
    kd_res_free(nearest);
    return false;
  }
    // ROS_ERROR("OUT") ; 

  rrtNBV::Node * newParent = (rrtNBV::Node*) kd_res_item_data(nearest);
  kd_res_free(nearest);

  // Check for collision of new connection plus some overshoot distance.
  Eigen::Vector3d origin(newParent->state_[0], newParent->state_[1], newParent->state_[2]);
  // std::cout << "origin 2 "  <<origin[0] << "   " <<origin[1] << "   " <<origin[2]  << std::endl << std::flush; 

  Eigen::Vector3d direction(newState[0] - origin[0], newState[1] - origin[1],
      newState[2] - origin[2]);
  if (direction.norm() > params_.extensionRange_)
  {
    direction = params_.extensionRange_ * direction.normalized();
  }
   
   //std::cout << "direction  "  <<direction[0] << "   " <<direction[1] << "   " <<direction[2]  << std::endl < std::flush ; 

  newState[0] = origin[0] + direction[0];
  newState[1] = origin[1] + direction[1];
  newState[2] = origin[2] + direction[2];
 //   std::cout << "NewState  3"  <<newState[0] << "   " <<newState[1] << "   " <<newState[2]  << std::endl << std::flush ; 

  volumetric_mapping::OctomapManager::CellStatus cellStatus;
  cellStatus = manager_->getLineStatusBoundingBox(
        origin, direction + origin + direction.normalized() * params_.dOvershoot_,
        params_.boundingBox_);
 // ROS_INFO("params_.boundingBox_ %f    %f    %f  ",params_.boundingBox_[0], params_.boundingBox_[1], params_.boundingBox_[2]);
 // ROS_INFO("params_.dOvershoot_ %f     ",params_.dOvershoot_);
    

  if (cellStatus == volumetric_mapping::OctomapManager::CellStatus::kFree)// || cellStatus == volumetric_mapping::OctomapManager::CellStatus::kUnknown)
  {
    if(cellStatus == volumetric_mapping::OctomapManager::CellStatus::kFree)
    {
      ROS_INFO("   - Ray is Free");
    }
    else
    {
      ROS_INFO("   - Ray is Unknown - here");
    }
    
    // Sample the new orientation
    newState[3] = 2.0 * M_PI * (((double) rand()) / ((double) RAND_MAX) - 0.5);
    // Create new node and insert into tree
    rrtNBV::Node * newNode = new rrtNBV::Node;
    newNode->state_ = newState;
    newNode->parent_ = newParent;
    newNode->distance_ = newParent->distance_ + direction.norm();
    newParent->children_.push_back(newNode);
    //gain with distance
    if (iterations == 1)
    newNode->gain_ = newParent->gain_ + gain(newNode->state_) * exp(-params_.degressiveCoeff_ * newNode->distance_);
    //ROS_INFO("GAIN IS:%f",newNode->gain_);

    kd_insert3(kdTree_, newState.x(), newState.y(), newState.z(), newNode);
   
    //std::cout << "NewState 4"  <<newState.x() << "   " <<newState.y() << "   " <<newState.z()  << std::endl ; 
    // Display new node
    publishNode(newNode);
    //std::cout << "newNode->gain_"  << newNode->gain_ << " bestGain_  " <<bestGain_ << std::endl ; 

    // Update best IG and node if applicable
    if (newNode->gain_ > bestGain_)
    {
      bestGain_ = newNode->gain_;
      bestNode_ = newNode;
    }
    counter_++;
    return true;
    //ROS_INFO("bestGain_ IS:%f",bestGain_);


  }
  return false;
}

bool rrtNBV::RrtTree::iterateDeep(int iterations, double informationGain, int numOfSamples)
{
      ROS_INFO("iterateDeep callback");

  double viewGain = 0 ;
  // In this function a new configuration is sampled and added to the tree.
  StateVec newState;
  // Sample over a sphere with the radius of the maximum diagonal of the exploration
  // space. Throw away samples outside the sampling region it exiting is not allowed
  // by the corresponding parameter. This method is to not bias the tree towards the
  // center of the exploration space.
  double radius = sqrt(
        SQ(params_.minX_ - params_.maxX_) + SQ(params_.minY_ - params_.maxY_)
        + SQ(params_.minZ_ - params_.maxZ_));

  bool solutionFound = false;
  while (!solutionFound)
  {
    for (int i = 0; i < 3; i++)
    {
      newState[i] = 2.0 * radius * (((double) rand()) / ((double) RAND_MAX) - 0.5);
    }
    if (SQ(newState[0]) + SQ(newState[1]) + SQ(newState[2]) > pow(radius, 2.0))
      continue;
    // Offset new state by root
    newState += rootNode_->state_;
    if (!params_.softBounds_) {
      if (newState.x() < params_.minX_ + 0.5 * params_.boundingBox_.x()) {
        continue;
      } else if (newState.y() < params_.minY_ + 0.5 * params_.boundingBox_.y()) {
        continue;
      } else if (newState.z() < params_.minZ_ + 0.5 * params_.boundingBox_.z()) {
        continue;
      } else if (newState.x() > params_.maxX_ - 0.5 * params_.boundingBox_.x()) {
        continue;
      } else if (newState.y() > params_.maxY_ - 0.5 * params_.boundingBox_.y()) {
        continue;
      } else if (newState.z() > params_.maxZ_ - 0.5 * params_.boundingBox_.z()) {
        continue;
      }
    }
    solutionFound = true;
  }
  //ROS_WARN("Sample Point genrated inside the exploration aera and not in collision with the bounding box -the bounding box is the robot dimensions");
  visualization_msgs::Marker samplePoint;
  tf::Quaternion quatSamplePoint;
  samplePoint.header.stamp = ros::Time::now();
  samplePoint.header.seq = iterations ;
  samplePoint.header.frame_id = params_.navigationFrame_;
  samplePoint.id = iterations ;
  samplePoint.ns = "workspace";
  samplePoint.type = visualization_msgs::Marker::CUBE;
  samplePoint.action = visualization_msgs::Marker::ADD;
  samplePoint.pose.position.x = newState.x();
  samplePoint.pose.position.y = newState.y();
  samplePoint.pose.position.z = newState.z();
  quatSamplePoint.setEuler(0.0, 0.0, 0.0);
  samplePoint.pose.orientation.x = quatSamplePoint.x();
  samplePoint.pose.orientation.y = quatSamplePoint.y();
  samplePoint.pose.orientation.z = quatSamplePoint.z();
  samplePoint.pose.orientation.w = quatSamplePoint.w();
  samplePoint.scale.x = 0.5;
  samplePoint.scale.y = 0.5;
  samplePoint.scale.z = 0.5;
  samplePoint.color.r = 1;
  samplePoint.color.g = 0;
  samplePoint.color.b = 0;
  samplePoint.color.a = 1;
  samplePoint.lifetime = ros::Duration(0.0);
  samplePoint.frame_locked = false;
  params_.sampledPoints_.publish(samplePoint);

  kdres * nearest = kd_nearest3(kdTree_, newState.x(), newState.y(), newState.z());
  if (kd_res_size(nearest) <= 0)
  {
    kd_res_free(nearest);
    return false;
  }
  rrtNBV::Node * newParent = (rrtNBV::Node*) kd_res_item_data(nearest);
  kd_res_free(nearest);

  // Origin Point
  Eigen::Vector3d origin(newParent->state_[0], newParent->state_[1], newParent->state_[2]);
  // Check for collision of new connection plus some overshoot distance.
  Eigen::Vector3d direction(newState[0] - origin[0], newState[1] - origin[1], newState[2] - origin[2]);
  // default extension range is = 1
  if (direction.norm() > params_.extensionRange_)
  {
    direction = params_.extensionRange_ * direction.normalized();
  }
  // New Random Sampled Point called NewState
  newState[0] = origin[0] + direction[0];
  newState[1] = origin[1] + direction[1];
  newState[2] = origin[2] + direction[2];
  //std::cout << "SAMPLE POSE "  << newState[0] << "  " << newState[1] << "  " << newState[2] << std::endl << std::flush ;
  // std::cout << "PARENT POSE "  << origin[0] << "  " << origin[1] << "  " << origin[2] << std::endl << std::flush ;
  Eigen::Vector3d  startPoint = origin ;
  Eigen::Vector3d  endPoint = direction + origin + direction.normalized() * params_.dOvershoot_ ;

  // This shows the nearset point from the current point to the direction of the sampled point
  samplePoint.header.stamp = ros::Time::now();
  samplePoint.header.seq = iterations+100 ;
  samplePoint.header.frame_id = params_.navigationFrame_;
  samplePoint.id = iterations+100 ;
  samplePoint.ns = "workspace";
  samplePoint.type = visualization_msgs::Marker::CUBE;
  samplePoint.action = visualization_msgs::Marker::ADD;
  samplePoint.pose.position.x = newState[0];
  samplePoint.pose.position.y = newState[1];
  samplePoint.pose.position.z = newState[2];
  tf::Quaternion quatSamplePointParentNew;
  quatSamplePointParentNew.setEuler(0.0, 0.0, 0.0);
  samplePoint.pose.orientation.x = quatSamplePointParentNew.x();
  samplePoint.pose.orientation.y = quatSamplePointParentNew.y();
  samplePoint.pose.orientation.z = quatSamplePointParentNew.z();
  samplePoint.pose.orientation.w = quatSamplePointParentNew.w();
  samplePoint.scale.x = 0.5;
  samplePoint.scale.y = 0.5;
  samplePoint.scale.z = 0.5;
  samplePoint.color.r = 0;
  samplePoint.color.g = 0;
  samplePoint.color.b = 1;
  samplePoint.color.a = 1;
  samplePoint.lifetime = ros::Duration(0.0);
  samplePoint.frame_locked = false;
  params_.sampledPoints_.publish(samplePoint);
  //sleep(1.0) ;
  // This shows the origin point
  samplePoint.header.stamp = ros::Time::now();
  samplePoint.header.seq = iterations+10 ;
  samplePoint.header.frame_id = params_.navigationFrame_;
  samplePoint.id = iterations+10 ;
  samplePoint.ns = "workspace";
  samplePoint.type = visualization_msgs::Marker::CYLINDER;
  samplePoint.action = visualization_msgs::Marker::ADD;
  samplePoint.pose.position.x = startPoint[0];
  samplePoint.pose.position.y = startPoint[1];
  samplePoint.pose.position.z = startPoint[2];
  tf::Quaternion quatSamplePointParent;
  quatSamplePointParent.setEuler(0.0, 0.0, 0.0);
  samplePoint.pose.orientation.x = quatSamplePointParent.x();
  samplePoint.pose.orientation.y = quatSamplePointParent.y();
  samplePoint.pose.orientation.z = quatSamplePointParent.z();
  samplePoint.pose.orientation.w = quatSamplePointParent.w();
  samplePoint.scale.x = 0.5;
  samplePoint.scale.y = 0.5;
  samplePoint.scale.z = 0.5;
  samplePoint.color.r = 0;
  samplePoint.color.g = 1;
  samplePoint.color.b = 0;
  samplePoint.color.a = 1;
  samplePoint.lifetime = ros::Duration(0.0);
  samplePoint.frame_locked = false;
  params_.sampledPoints_.publish(samplePoint);
  //sleep(1.0) ;
  //ROS_WARN("End Point");
  // This shows the nearset point from the current point to the direction of the sampled point
  samplePoint.header.stamp = ros::Time::now();
  samplePoint.header.seq = iterations+300 ;
  samplePoint.header.frame_id = params_.navigationFrame_;
  samplePoint.id = iterations+300 ;
  samplePoint.ns = "workspace";
  samplePoint.type = visualization_msgs::Marker::SPHERE;
  samplePoint.action = visualization_msgs::Marker::ADD;
  samplePoint.pose.position.x = endPoint[0];
  samplePoint.pose.position.y = endPoint[1];
  samplePoint.pose.position.z = endPoint[2];
  tf::Quaternion quatEndPoint;
  quatEndPoint.setEuler(0.0, 0.0, 0.0);
  samplePoint.pose.orientation.x = quatEndPoint.x();
  samplePoint.pose.orientation.y = quatEndPoint.y();
  samplePoint.pose.orientation.z = quatEndPoint.z();
  samplePoint.pose.orientation.w = quatEndPoint.w();
  samplePoint.scale.x = 0.5;
  samplePoint.scale.y = 0.5;
  samplePoint.scale.z = 0.5;
  samplePoint.color.r = 0;
  samplePoint.color.g = 0;
  samplePoint.color.b = 0;
  samplePoint.color.a = 1;
  samplePoint.lifetime = ros::Duration(0.0);
  samplePoint.frame_locked = false;
  params_.sampledPoints_.publish(samplePoint);
  //sleep(1.0) ;
  // This shows the bounding box
  visualization_msgs::Marker p;
  p.header.stamp = ros::Time::now();
  p.header.seq = 10000;
  p.header.frame_id = params_.navigationFrame_;
  p.id = 10000;
  p.ns = "workspace";
  p.type = visualization_msgs::Marker::CUBE;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = origin[0] ;
  p.pose.position.y = origin[1] ;
  p.pose.position.z = origin[2] ;
  tf::Quaternion quat;
  quat.setEuler(0.0, 0.0, 0.0);
  p.pose.orientation.x = quat.x();
  p.pose.orientation.y = quat.y();
  p.pose.orientation.z = quat.z();
  p.pose.orientation.w = quat.w();
  p.scale.x = params_.boundingBox_[0] ;
  p.scale.y = params_.boundingBox_[1] ;
  p.scale.z = params_.boundingBox_[2] ;
  p.color.r = 200.0 / 255.0;
  p.color.g = 100.0 / 255.0;
  p.color.b = 50.0 / 225.0;
  p.color.a = 1;
  p.lifetime = ros::Duration(0.0);
  p.frame_locked = false;
  params_.inspectionPath_.publish(p);

  //ROS_INFO("Check if the path from the origin to the sampled point is free or not");
  volumetric_mapping::OctomapManager::CellStatus cellStatus;
  cellStatus = manager_->getLineStatusBoundingBox(startPoint, endPoint, params_.boundingBox_);
  if (cellStatus == volumetric_mapping::OctomapManager::CellStatus::kFree)// || cellStatus == volumetric_mapping::OctomapManager::CellStatus::kUnknown)
  {
    // ROS_WARN("SAMPLED CELL IS FREE");
    // sample a random Orientation
    newState[3] = 2.0 * M_PI * (((double) rand()) / ((double) RAND_MAX) - 0.5);
    // Create new node and insert into tree
    rrtNBV::Node * newNode = new rrtNBV::Node;
    newNode->state_ = newState;
    newNode->parent_ = newParent;   // same as origin same as start point
    newNode->distance_ = newParent->distance_ +  direction.norm();;
    ROS_INFO (" direction.norm()%f\n", direction.norm());
    ROS_INFO ("newParent->distance_%f\n", newParent->distance_) ;

    // ROS_INFO("Calculate the View IG by summing all the entropies for all the cells in the view");
    //viewGain = gainDeep(newNode->state_);
    //newNode->gain_ = newParent->gain_ + gainPureEntropy(newNode->state_) * exp(-params_.degressiveCoeff_ * newNode->distance_);
    int dinsity =0 ;
    viewGain = gainDinsity(newNode->state_, dinsity);
    newNode->gain_ =  newParent->gain_ + (viewGain/dinsity) ; //* exp(-params_.degressiveCoeff_ * newNode->distance_);

    //ROS_INFO("GAIN IS:%f",newNode->gain_);
    //ROS_INFO("informationGain IS:%f", newNode->gain_ );
    // Display new node
    publishNode(newNode);
    // Update best IG and node if applicable
    if (newNode->gain_ > bestGain_)
    {
      bestGain_ = newNode->gain_;
      bestNode_ = newNode;
    }
  }
  else
  {
    // ROS_WARN("SAMPLED CELL IS NOT FREE");
    bestGain_ = rootNode_->gain_;
    bestNode_ = rootNode_;
  }
}

 
// R: Currently Not Used 
double rrtNBV::RrtTree::gainPureEntropy(StateVec state)
{
  visualization_msgs::Marker p;
  // This function computes the gain
  double gain = 0.0;
  const double disc = manager_->getResolution();
  Eigen::Vector3d origin(state[0], state[1], state[2]);
  Eigen::Vector3d vec;
  double rangeSq = pow(params_.gainRange_, 2.0);
  // Iterate over all nodes within the allowed distance
  for (vec[0] = std::max(state[0] - params_.gainRange_, params_.minX_);
       vec[0] < std::min(state[0] + params_.gainRange_, params_.maxX_); vec[0] += disc) {
    for (vec[1] = std::max(state[1] - params_.gainRange_, params_.minY_);
         vec[1] < std::min(state[1] + params_.gainRange_, params_.maxY_); vec[1] += disc) {
      for (vec[2] = std::max(state[2] - params_.gainRange_, params_.minZ_);
           vec[2] < std::min(state[2] + params_.gainRange_, params_.maxZ_); vec[2] += disc) {
        Eigen::Vector3d dir = vec - origin;
        // Skip if distance is too large
        if (dir.transpose().dot(dir) > rangeSq) {
          continue;
        }
        bool insideAFieldOfView = false;
        // Check that voxel center is inside one of the fields of view.
        for (typename std::vector<std::vector<Eigen::Vector3d>>::iterator itCBN = params_
             .camBoundNormals_.begin(); itCBN != params_.camBoundNormals_.end(); itCBN++) {
          bool inThisFieldOfView = true;
          for (typename std::vector<Eigen::Vector3d>::iterator itSingleCBN = itCBN->begin();
               itSingleCBN != itCBN->end(); itSingleCBN++) {
            Eigen::Vector3d normal = Eigen::AngleAxisd(state[3], Eigen::Vector3d::UnitZ())
                * (*itSingleCBN);
            double val = dir.dot(normal.normalized());
            if (val < SQRT2 * disc) {
              inThisFieldOfView = false;
              break;
            }
          }
          if (inThisFieldOfView) {
            insideAFieldOfView = true;
            break;
          }
        }
        if (!insideAFieldOfView) {
          continue;
        }
        // Check cell status and add to the gain considering the corresponding factor.
        double probability;
        volumetric_mapping::OctomapManager::CellStatus node = manager_->getCellProbabilityPoint(
              vec, &probability);

        double entropy , p ;
        if (node == volumetric_mapping::OctomapManager::CellStatus::kUnknown )
          p = 0.5 ;
        else
          p = probability ;
        entropy= -p * std::log(p) - ((1-p) * std::log(1-p));
        gain += abs(entropy);

      }
    }
  }
  return gain;
}

// R: Currently Not Used 
double rrtNBV::RrtTree::gainDinsity(StateVec state , int &dinsity)
{
  int numOfOccupiedCells = 0 ;
  int numOfCells = 0 ;
  visualization_msgs::Marker p;
  // This function computes the gain
  double gain = 0.0;
  const double disc = manager_->getResolution();
  Eigen::Vector3d origin(state[0], state[1], state[2]);
  Eigen::Vector3d vec;
  double rangeSq = pow(params_.gainRange_, 2.0);
  // Iterate over all nodes within the allowed distance
  for (vec[0] = std::max(state[0] - params_.gainRange_, params_.minX_);
       vec[0] < std::min(state[0] + params_.gainRange_, params_.maxX_); vec[0] += disc) {
    for (vec[1] = std::max(state[1] - params_.gainRange_, params_.minY_);
         vec[1] < std::min(state[1] + params_.gainRange_, params_.maxY_); vec[1] += disc) {
      for (vec[2] = std::max(state[2] - params_.gainRange_, params_.minZ_);
           vec[2] < std::min(state[2] + params_.gainRange_, params_.maxZ_); vec[2] += disc) {
        numOfCells++;
        Eigen::Vector3d dir = vec - origin;
        // Skip if distance is too large
        if (dir.transpose().dot(dir) > rangeSq) {
          continue;
        }
        bool insideAFieldOfView = false;
        // Check that voxel center is inside one of the fields of view.
        for (typename std::vector<std::vector<Eigen::Vector3d>>::iterator itCBN = params_
             .camBoundNormals_.begin(); itCBN != params_.camBoundNormals_.end(); itCBN++) {
          bool inThisFieldOfView = true;
          for (typename std::vector<Eigen::Vector3d>::iterator itSingleCBN = itCBN->begin();
               itSingleCBN != itCBN->end(); itSingleCBN++) {
            Eigen::Vector3d normal = Eigen::AngleAxisd(state[3], Eigen::Vector3d::UnitZ())
                * (*itSingleCBN);
            double val = dir.dot(normal.normalized());
            if (val < SQRT2 * disc) {
              inThisFieldOfView = false;
              break;
            }
          }
          if (inThisFieldOfView) {
            insideAFieldOfView = true;
            break;
          }
        }
        if (!insideAFieldOfView) {
          continue;
        }
        // Check cell status and add to the gain considering the corresponding factor.
        double probability;
        volumetric_mapping::OctomapManager::CellStatus node = manager_->getCellProbabilityPoint(
              vec, &probability);

        if (node == volumetric_mapping::OctomapManager::CellStatus::kOccupied )
          numOfOccupiedCells++ ;

        double entropy , p ;
        if (node == volumetric_mapping::OctomapManager::CellStatus::kUnknown )
          p = 0.5 ;
        else
          p = probability ;
        entropy= -p * std::log(p) - ((1-p) * std::log(1-p));
        gain += abs(entropy);

      }
    }
  }

  dinsity = numOfOccupiedCells/numOfCells ;
  return gain;
}

// R: Currently Not Used  
void rrtNBV::RrtTree::initializeDeep()
{
  // ROS_WARN("EXPLORATION AREA");
  // Publish visualization of total exploration area
  visualization_msgs::Marker p;
  p.header.stamp = ros::Time::now();
  p.header.seq = 0;
  p.header.frame_id = params_.navigationFrame_;
  p.id = 0;
  p.ns = "workspace";
  p.type = visualization_msgs::Marker::CUBE;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = 0.5 * (params_.minX_ + params_.maxX_);
  p.pose.position.y = 0.5 * (params_.minY_ + params_.maxY_);
  p.pose.position.z = 0.5 * (params_.minZ_ + params_.maxZ_);
  tf::Quaternion quat;
  quat.setEuler(0.0, 0.0, 0.0);
  p.pose.orientation.x = quat.x();
  p.pose.orientation.y = quat.y();
  p.pose.orientation.z = quat.z();
  p.pose.orientation.w = quat.w();
  p.scale.x = params_.maxX_ - params_.minX_;
  p.scale.y = params_.maxY_ - params_.minY_;
  p.scale.z = params_.maxZ_ - params_.minZ_;
  p.color.r = 200.0 / 255.0;
  p.color.g = 100.0 / 255.0;
  p.color.b = 0.0;
  p.color.a = 0.1;
  p.lifetime = ros::Duration(0.0);
  p.frame_locked = false;
  params_.explorationarea_.publish(p);

  // Create The Root Node with is the current location of the robot
  // root_ variable is assigned from the position callbak function
  rootNode_ = new Node;
  rootNode_->distance_ = 0.0;
  rootNode_->gain_ = params_.zero_gain_; // ?? is it information gain the global variable or zero_gain
  rootNode_->parent_ = NULL;
  rootNode_->state_ = root_;

  // create tree and push the root node to it as the first node
  kdTree_ = kd_create(3);
  kd_insert3(kdTree_, rootNode_->state_.x(), rootNode_->state_.y(), rootNode_->state_.z(),rootNode_);
  if (params_.log_) {
    if (fileTree_.is_open()) {
      fileTree_.close();
    }
    fileTree_.open((logFilePath_ + "tree" + std::to_string(iterationCount_) + ".txt").c_str(),
                   std::ios::out);
  }
  iterationCount_++;

  geometry_msgs::PoseStamped poseMsg;
  poseMsg.header.stamp       = ros::Time::now();
  poseMsg.header.frame_id    = params_.navigationFrame_;
  poseMsg.pose.position.x    = rootNode_->state_.x();
  poseMsg.pose.position.y    = rootNode_->state_.y();
  poseMsg.pose.position.z    = rootNode_->state_.z();
  // The orientation does not really matter
  tf::Quaternion quat2;
  quat2.setEuler(0.0, 0.0, root_[3]);
  poseMsg.pose.orientation.x = quat2.x();
  poseMsg.pose.orientation.y = quat2.y();
  poseMsg.pose.orientation.z = quat2.z();
  poseMsg.pose.orientation.w = quat2.w();
  params_.rootNodeDebug.publish(poseMsg);
}

void rrtNBV::RrtTree::initialize()
{
ROS_INFO("initialize");
// This function is to initialize the tree, including insertion of remainder of previous best branch.
  g_ID_ = 0;
  // Remove last segment from segment list (multi agent only)
  int i;
  for (i = 0; i < agentNames_.size(); i++) {
    if (agentNames_[i].compare(params_.navigationFrame_) == 0) {
      break;
    }
  }
  // Initialize kd-tree with root node and prepare log file
  kdTree_ = kd_create(3);

  if (params_.log_) {
    if (fileTree_.is_open()) {
      fileTree_.close();
    }
    fileTree_.open((logFilePath_ + "tree" + std::to_string(iterationCount_) + ".txt").c_str(),
                   std::ios::out);
  }

  rootNode_ = new Node;
  rootNode_->distance_ = 0.0;
  rootNode_->gain_ = params_.zero_gain_;
  rootNode_->parent_ = NULL;

  if (params_.exact_root_)
  {
    if (iterationCount_ <= 1)
    {
      exact_root_ = root_;
    }
    rootNode_->state_ = exact_root_;
  }
  else
  {
    rootNode_->state_ = root_;
  }
  kd_insert3(kdTree_, rootNode_->state_.x(), rootNode_->state_.y(), rootNode_->state_.z(),rootNode_);
  iterationCount_++;

  geometry_msgs::PoseStamped poseMsg;
  poseMsg.header.stamp       = ros::Time::now();
  poseMsg.header.frame_id    = params_.navigationFrame_;
  poseMsg.pose.position.x    = rootNode_->state_.x();
  poseMsg.pose.position.y    = rootNode_->state_.y();
  poseMsg.pose.position.z    = rootNode_->state_.z();
  // The orientation does not really matter
  tf::Quaternion quat2;
  quat2.setEuler(0.0, 0.0, root_[3]);
  poseMsg.pose.orientation.x = quat2.x();
  poseMsg.pose.orientation.y = quat2.y();
  poseMsg.pose.orientation.z = quat2.z();
  poseMsg.pose.orientation.w = quat2.w();
  params_.rootNodeDebug.publish(poseMsg);

  // Insert all nodes of the remainder of the previous best branch, checking for collisions and
  // recomputing the gain.
  for (typename std::vector<StateVec>::reverse_iterator iter = bestBranchMemory_.rbegin();
       iter != bestBranchMemory_.rend(); ++iter) {
    StateVec newState = *iter;
    kdres * nearest = kd_nearest3(kdTree_, newState.x(), newState.y(), newState.z());
    if (kd_res_size(nearest) <= 0) {
      kd_res_free(nearest);
      continue;
    }
    rrtNBV::Node * newParent = (rrtNBV::Node *) kd_res_item_data(
          nearest);
    kd_res_free(nearest);

    // Check for collision
    Eigen::Vector3d origin(newParent->state_[0], newParent->state_[1], newParent->state_[2]);
    Eigen::Vector3d direction(newState[0] - origin[0], newState[1] - origin[1],
        newState[2] - origin[2]);
    if (direction.norm() > params_.extensionRange_) {
      direction = params_.extensionRange_ * direction.normalized();
    }
    newState[0] = origin[0] + direction[0];
    newState[1] = origin[1] + direction[1];
    newState[2] = origin[2] + direction[2];
    if (volumetric_mapping::OctomapManager::CellStatus::kFree
        == manager_->getLineStatusBoundingBox(
          origin, direction + origin + direction.normalized() * params_.dOvershoot_,
          params_.boundingBox_)) {
      // Create new node and insert into tree
      rrtNBV::Node * newNode = new rrtNBV::Node;
      newNode->state_ = newState;
      newNode->parent_ = newParent;
      newNode->distance_ = newParent->distance_ + direction.norm();
      newParent->children_.push_back(newNode);
      // gain with distance
      newNode->gain_ = newParent->gain_+ gain(newNode->state_) ;//* exp(-params_.degressiveCoeff_ * newNode->distance_);
      // Gain without distance
      //newNode->gain_ = newParent->gain_ ; //+ gain(newNode->state_) * exp(-params_.degressiveCoeff_ * newNode->distance_);

      kd_insert3(kdTree_, newState.x(), newState.y(), newState.z(), newNode);

      // Display new node
      publishNode(newNode);
      // Update best IG and node if applicable
      if (newNode->gain_ > bestGain_) {
        bestGain_ = newNode->gain_;
        bestNode_ = newNode;
      }
      counter_++;
    }
  }

  // Publish visualization of total exploration area
  visualization_msgs::Marker p;
  p.header.stamp = ros::Time::now();
  p.header.seq = 0;
  p.header.frame_id = params_.navigationFrame_;
  p.id = 0;
  p.ns = "workspace";
  p.type = visualization_msgs::Marker::CUBE;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = 0.5 * (params_.minX_ + params_.maxX_);
  p.pose.position.y = 0.5 * (params_.minY_ + params_.maxY_);
  p.pose.position.z = 0.5 * (params_.minZ_ + params_.maxZ_);
  tf::Quaternion quat;
  quat.setEuler(0.0, 0.0, 0.0);
  p.pose.orientation.x = quat.x();
  p.pose.orientation.y = quat.y();
  p.pose.orientation.z = quat.z();
  p.pose.orientation.w = quat.w();
  p.scale.x = params_.maxX_ - params_.minX_;
  p.scale.y = params_.maxY_ - params_.minY_;
  p.scale.z = params_.maxZ_ - params_.minZ_;
  p.color.r = 200.0 / 255.0;
  p.color.g = 100.0 / 255.0;
  p.color.b = 0.0;
  p.color.a = 0.1;
  p.lifetime = ros::Duration(0.0);
  p.frame_locked = false;
  params_.inspectionPath_.publish(p);

}

std::vector<geometry_msgs::Pose> rrtNBV::RrtTree::getBestEdge(std::string targetFrame)
{
  // This function returns the first edge of the best branch
  std::vector<geometry_msgs::Pose> ret;
  rrtNBV::Node * current = bestNode_;
  if (current->parent_ != NULL) {
    while (current->parent_ != rootNode_ && current->parent_ != NULL) {
      current = current->parent_;
    }
    ret = samplePath(current->parent_->state_, current->state_, targetFrame);
    history_.push(current->parent_->state_);
    exact_root_ = current->state_;
  }
  

  return ret;
}

double rrtNBV::RrtTree::getBestGain()
{
  return bestGain_;
}

geometry_msgs::Pose rrtNBV::RrtTree::getBestEdgeDeep(std::string targetFrame)
{
  //ROS_INFO("the best gain in this iteration is %f", bestGain_) ;
  //std::cout << "bestNode NODE " << bestNode_->state_[0] ;
  // This function returns the first edge of the best branch
  geometry_msgs::Pose ret;
  ret.position.x = bestNode_->state_[0] ;
  ret.position.y = bestNode_->state_[1] ;
  ret.position.z = bestNode_->state_[2] ;
  float yaw = bestNode_->state_[3] ;
  tf::Quaternion q = tf::createQuaternionFromYaw(yaw) ;
  ret.orientation.x = q[0] ;
  ret.orientation.y = q[1] ;
  ret.orientation.z = q[2] ;
  ret.orientation.w = q[3] ;
  return ret;
}

double rrtNBV::RrtTree::gain(StateVec state)
{
    ROS_INFO("GAIN");

  // This function computes the gain
  double gain = 0.0;
  const double disc = manager_->getResolution();
  Eigen::Vector3d origin(state[0], state[1], state[2]);
  Eigen::Vector3d vec;
  double rangeSq = pow(params_.gainRange_, 2.0);

  // Iterate over all nodes within the allowed distance
  for (vec[0] = std::max(state[0] - params_.gainRange_, params_.minX_);
       vec[0] < std::min(state[0] + params_.gainRange_, params_.maxX_); vec[0] += disc) {
    for (vec[1] = std::max(state[1] - params_.gainRange_, params_.minY_);
         vec[1] < std::min(state[1] + params_.gainRange_, params_.maxY_); vec[1] += disc) {
      for (vec[2] = std::max(state[2] - params_.gainRange_, params_.minZ_);
           vec[2] < std::min(state[2] + params_.gainRange_, params_.maxZ_); vec[2] += disc) {

        Eigen::Vector3d dir = vec - origin;
        // Skip if distance is too large
        if (dir.transpose().dot(dir) > rangeSq) {
          continue;
        }
        bool insideAFieldOfView = false;
        // Check that voxel center is inside one of the fields of view.
        for (typename std::vector<std::vector<Eigen::Vector3d>>::iterator itCBN = params_
             .camBoundNormals_.begin(); itCBN != params_.camBoundNormals_.end(); itCBN++) {
          bool inThisFieldOfView = true;
          for (typename std::vector<Eigen::Vector3d>::iterator itSingleCBN = itCBN->begin();
               itSingleCBN != itCBN->end(); itSingleCBN++) {
            Eigen::Vector3d normal = Eigen::AngleAxisd(state[3], Eigen::Vector3d::UnitZ())
                * (*itSingleCBN);
            double val = dir.dot(normal.normalized());
            if (val < SQRT2 * disc) {
              inThisFieldOfView = false;
              break;
            }
          }
          if (inThisFieldOfView) {
            insideAFieldOfView = true;
            break;
          }
        }
        if (!insideAFieldOfView) {
          continue;
        }
        // Check cell status and add to the gain considering the corresponding factor.
        double probability;
        volumetric_mapping::OctomapManager::CellStatus node = manager_->getCellProbabilityPoint(
              vec, &probability);
        
                
        double maxThreshold = 0.5 * std::log(0.5) + ((1-0.5) * std::log(1-0.5));

        //*************** Pure Entropy ***************************** //
        double entropy;
        entropy= probability * std::log(probability) + ((1-probability) * std::log(1-probability));
        //entropy= entropy/maxThreshold ; 
        //gain += abs(entropy);

        // ************** Semantic gain ************************* // 
                               
         // Semantic gain    
        double s_gain = manager_->getCellIneterestGain(vec);

          double semantic_entropy ;
          semantic_entropy= -s_gain * std::log(s_gain) - ((1-s_gain) * std::log(1-s_gain));
          //semantic_entropy = semantic_entropy/maxThreshold ; 
          
            //gain += abs(entropy);
            //gain += entropy ;
            //gain+=semantic_entropy ;
                    
          gain = gain + entropy + semantic_entropy ;

         //std::cout << "entropy" << entropy <<  std::endl ;
         //std::cout << "semantic_entropy" << semantic_entropy <<  std::endl ;
                

        //*************** RRT IG *********************************** //
       /* if (node == volumetric_mapping::OctomapManager::CellStatus::kUnknown) {
          // Rayshooting to evaluate inspectability of cell
          if (volumetric_mapping::OctomapManager::CellStatus::kOccupied
              != this->manager_->getVisibility(origin, vec, false)) {
            gain += params_.igUnmapped_;
            // TODO: Add probabilistic gain
            //gain += params_.igProbabilistic_ * PROBABILISTIC_MODEL(probability);
          }
        } else if (node == volumetric_mapping::OctomapManager::CellStatus::kOccupied) {
          // Rayshooting to evaluate inspectability of cell
          if (volumetric_mapping::OctomapManager::CellStatus::kOccupied
              != this->manager_->getVisibility(origin, vec, false)) {
            gain += params_.igOccupied_;
            // TODO: Add probabilistic gain
            // gain += params_.igProbabilistic_ * PROBABILISTIC_MODEL(probability);
          }
        } else {
          // Rayshooting to evaluate inspectability of cell
          if (volumetric_mapping::OctomapManager::CellStatus::kOccupied
              != this->manager_->getVisibility(origin, vec, false)) {
            gain += params_.igFree_;
            // TODO: Add probabilistic gain
            // gain += params_.igProbabilistic_ * PROBABILISTIC_MODEL(probability);
          }
          
          
        }
        */
        

      }
    }
  }

  // Scale with volume
  //gain *= pow(disc, 3.0);
  // Check the gain added by inspectable surface
  if (mesh_) {
    // ROS_INFO("****gain added by inspectable surface*****");
    tf::Transform transform;
    transform.setOrigin(tf::Vector3(state.x(), state.y(), state.z()));
    tf::Quaternion quaternion;
    quaternion.setEuler(0.0, 0.0, state[3]);
    transform.setRotation(quaternion);
    gain += params_.igArea_ * mesh_->computeInspectableArea(transform);
  }
  return gain;
}

std::vector<geometry_msgs::Pose> rrtNBV::RrtTree::getPathBackToPrevious(
    std::string targetFrame)
{
  std::vector<geometry_msgs::Pose> ret;
  if (history_.empty()) {
    return ret;
  }
  ret = samplePath(root_, history_.top(), targetFrame);
  history_.pop();
  return ret;
}

void rrtNBV::RrtTree::memorizeBestBranch()
{
  bestBranchMemory_.clear();
  Node * current = bestNode_;
  while (current->parent_ && current->parent_->parent_) {
    bestBranchMemory_.push_back(current->state_);
    current = current->parent_;
  }
}

void rrtNBV::RrtTree::clear()
{
  delete rootNode_;
  rootNode_ = NULL;

  counter_ = 0;
  bestGain_ = params_.zero_gain_;
  bestNode_ = NULL;

  kd_free(kdTree_);
}

void rrtNBV::RrtTree::publishNode(Node * node)
{
  visualization_msgs::Marker p;
  p.header.stamp = ros::Time::now();
  p.header.seq = g_ID_;
  p.header.frame_id = params_.navigationFrame_;
  p.id = g_ID_;
  g_ID_++;
  p.ns = "vp_tree";
  p.type = visualization_msgs::Marker::ARROW;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = node->state_[0];
  p.pose.position.y = node->state_[1];
  p.pose.position.z = node->state_[2];
  tf::Quaternion quat;
  quat.setEuler(0.0, 0.0, node->state_[3]);
  p.pose.orientation.x = quat.x();
  p.pose.orientation.y = quat.y();
  p.pose.orientation.z = quat.z();
  p.pose.orientation.w = quat.w();
  p.scale.x = std::max(node->gain_ / 20.0, 0.05);
  p.scale.y = 0.1;
  p.scale.z = 0.1;
  p.color.r = 0;
  p.color.g = 0;
  p.color.b = 1.0;
  p.color.a = 1.0;
  p.lifetime = ros::Duration();
  p.frame_locked = false;
  params_.inspectionPath_.publish(p);

  if (!node->parent_)
    return;

  p.id = g_ID_;
  g_ID_++;
  p.ns = "vp_branches";
  p.type = visualization_msgs::Marker::ARROW;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = node->parent_->state_[0];
  p.pose.position.y = node->parent_->state_[1];
  p.pose.position.z = node->parent_->state_[2];
  Eigen::Quaternion<float> q;
  Eigen::Vector3f init(1.0, 0.0, 0.0);
  Eigen::Vector3f dir(node->state_[0] - node->parent_->state_[0],
      node->state_[1] - node->parent_->state_[1],
      node->state_[2] - node->parent_->state_[2]);
  q.setFromTwoVectors(init, dir);
  q.normalize();
  p.pose.orientation.x = q.x();
  p.pose.orientation.y = q.y();
  p.pose.orientation.z = q.z();
  p.pose.orientation.w = q.w();
  p.scale.x = dir.norm();
  p.scale.y = 0.03;
  p.scale.z = 0.03;
  p.color.r = 1;
  p.color.g = 0;
  p.color.b = 0;
  p.color.a = 1.0;
  p.lifetime = ros::Duration();
  p.frame_locked = false;
  params_.inspectionPath_.publish(p);

  if (params_.log_) {
    for (int i = 0; i < node->state_.size(); i++) {
      fileTree_ << node->state_[i] << ",";
    }
    fileTree_ << node->gain_ << ",";
    for (int i = 0; i < node->parent_->state_.size(); i++) {
      fileTree_ << node->parent_->state_[i] << ",";
    }
    fileTree_ << node->parent_->gain_ << "\n";
  }

}

std::vector<geometry_msgs::Pose> rrtNBV::RrtTree::samplePath(StateVec start, StateVec end,
                                                             std::string targetFrame)
{
  std::vector<geometry_msgs::Pose> ret;
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  try {
    listener.lookupTransform(targetFrame, params_.navigationFrame_, ros::Time(0), transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return ret;
  }
  Eigen::Vector3d distance(end[0] - start[0], end[1] - start[1], end[2] - start[2]);
  double yaw_direction = end[3] - start[3];
  if (yaw_direction > M_PI) {
    yaw_direction -= 2.0 * M_PI;
  }
  if (yaw_direction < -M_PI) {
    yaw_direction += 2.0 * M_PI;
  }
  double disc = std::min(params_.dt_ * params_.v_max_ / distance.norm(),
                         params_.dt_ * params_.dyaw_max_ / abs(yaw_direction));
  assert(disc > 0.0);
  for (double it = 0.0; it <= 1.0; it += disc) {
    tf::Vector3 origin((1.0 - it) * start[0] + it * end[0], (1.0 - it) * start[1] + it * end[1],
        (1.0 - it) * start[2] + it * end[2]);
    double yaw = start[3] + yaw_direction * it;
    if (yaw > M_PI)
      yaw -= 2.0 * M_PI;
    if (yaw < -M_PI)
      yaw += 2.0 * M_PI;
    tf::Quaternion quat;
    quat.setEuler(0.0, 0.0, yaw);
    origin = transform * origin;
    quat = transform * quat;
    tf::Pose poseTF(quat, origin);
    geometry_msgs::Pose pose;
    tf::poseTFToMsg(poseTF, pose);
    ret.push_back(pose);
    if (params_.log_) {
      filePath_ << poseTF.getOrigin().x() << ",";
      filePath_ << poseTF.getOrigin().y() << ",";
      filePath_ << poseTF.getOrigin().z() << ",";
      filePath_ << tf::getYaw(poseTF.getRotation()) << "\n";
    }
  }
  return ret;
}
#endif


