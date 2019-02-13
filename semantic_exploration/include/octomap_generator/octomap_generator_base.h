#ifndef OCTOMAP_GENERATOR_BASE_H
#define OCTOMAP_GENERATOR_BASE_H

#include <pcl/PCLPointCloud2.h>
#include <octomap/octomap.h>
#include <ros/time.h>
#include <kindr/minimal/quat-transformation.h>
#include <sensor_msgs/PointCloud2.h>

typedef kindr::minimal::QuatTransformation Transformation;

/**
 * Interface for octomap_generator for polymorphism
 * \author Xuan Zhang
 * \data Mai-July 2018
 */

enum VoxelStatus { kUnknown = 0, kOccupied, kFree };

inline octomap::point3d pointEigenToOctomap(const Eigen::Vector3d& point)
{
    return octomap::point3d(point.x(), point.y(), point.z());
}

inline Eigen::Vector3d pointOctomapToEigen(const octomap::point3d& point)
{
    return Eigen::Vector3d(point.x(), point.y(), point.z());
}

class OctomapGeneratorBase
{
  public:

    /// Desturctor
    virtual ~OctomapGeneratorBase(){}

    /// Set max range for point cloud insertion
    virtual void setMaxRange(float max_range) = 0;

    /// Set max range to perform raycasting on inserted points
    virtual void setRayCastRange(float raycast_range) = 0;

    /// Set clamping_thres_min, parameter for octomap
    virtual void setClampingThresMin(float clamping_thres_min) = 0;

    /// Set clamping_thres_max, parameter for octomap
    virtual void setClampingThresMax(float clamping_thres_max) = 0;

    /// Set resolution, parameter for octomap
    virtual void setResolution(float resolution) = 0;

    /// Set occupancy_thres, parameter for octomap
    virtual void setOccupancyThres(float occupancy_thres) = 0;

    /// Set prob_hit, parameter for octomap
    virtual void setProbHit(float prob_hit) = 0;

    /// Set prob_miss, parameter for octomap
    virtual void setProbMiss(float prob_miss) = 0;

    /**
     * \brief Insert point cloud into octree
     * \param cloud converted ros cloud to be inserted
     * \param sensorToWorld transform from sensor frame to world frame
     */
    virtual void insertPointCloud(const pcl::PCLPointCloud2::Ptr& cloud, const Eigen::Matrix4f& sensorToWorld) = 0;

    virtual void insertPointCloud(const sensor_msgs::PointCloud2::ConstPtr& cloud, const std::string &to_frame) = 0;

    /// Set whether use semantic color for serialization
    virtual void setUseSemanticColor(bool use) = 0;

    /// Get whether use semantic color for serialization
    virtual bool isUseSemanticColor() = 0;

    /// Get octree
    virtual octomap::AbstractOcTree* getOctree() = 0;

    /// Save octomap to a file. NOTE: Not tested
    virtual bool save(const char* filename) const = 0;

    virtual double getResolution() const = 0;

    virtual VoxelStatus getBoundingBoxStatus(const Eigen::Vector3d& center,
                                     const Eigen::Vector3d& bounding_box_size, bool stop_at_unknown_voxel) const = 0;

    virtual VoxelStatus getLineStatus(const Eigen::Vector3d& start,
                              const Eigen::Vector3d& end) const = 0;

    virtual VoxelStatus getLineStatusBoundingBox(
            const Eigen::Vector3d& start, const Eigen::Vector3d& end,
            const Eigen::Vector3d& bounding_box_size) const = 0;

    virtual VoxelStatus getVisibility(
            const Eigen::Vector3d& view_point, const Eigen::Vector3d& voxel_to_test,
            bool stop_at_unknown_cell) const = 0;

    virtual VoxelStatus getCellProbabilityPoint(
            const Eigen::Vector3d& point, double* probability) const = 0;

    virtual Eigen::Vector3d getMapSize() const = 0;

    virtual double getVisibilityLikelihood(const Eigen::Vector3d& view_point,
                                           const Eigen::Vector3d& voxel_to_test) const = 0;

    virtual bool getRearSideVoxel(const Eigen::Vector3d& view_point,
                                  const Eigen::Vector3d& voxel_to_test) const = 0;

    virtual int getCellIneterestCellType(double x, double y, double z) const = 0;

    virtual double getCellIneterestGain(const Eigen::Vector3d& point) const = 0;

    virtual bool lookupTransformation(const std::string& from_frame,
                                           const std::string& to_frame,
                                           const ros::Time& timestamp,
                                           Transformation* transform) = 0;
};

#endif//OCTOMAP_GENERATOR_BASE