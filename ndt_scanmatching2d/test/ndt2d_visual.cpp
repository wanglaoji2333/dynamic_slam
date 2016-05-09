
#include <fstream>
#include <vector>
#include <string>
#include <chrono>

#include <sensor_msgs/PointCloud.h>
#include <laser_geometry/laser_geometry.h>
#include <sensor_msgs/LaserScan.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/registration/ndt.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <boost/thread/thread.hpp>

#include <ndt_scanmatching2d/ndt2d.h>
#include <ndt_scanmatching2d/d2d_ndt2d.h>

#include <Eigen/Dense>

#include <dynamic_slam_utils/eigen_tools.h>

using namespace pcl;

typedef pcl::PointCloud<pcl::PointXYZ> pcl_t;

double EPSILON = 0.001;
double MIN_DISPLACEMENT = 0.4;
double MIN_ROTATION = 0.3;
std::vector<pcl_t::Ptr> scans;
std::vector<Eigen::Vector3d> real_poses;
//std::vector<MatchResult> matches;
Registration<pcl::PointXYZ,pcl::PointXYZ> * matcher;
NormalDistributionsTransform<pcl::PointXYZ,pcl::PointXYZ> * proofer;


std::vector<std::string> split(std::string data, std::string token)
{
    std::vector<std::string> output;
    size_t pos = std::string::npos;
    do
    {
        pos = data.find(token);
        output.push_back(data.substr(0, pos));
        if (std::string::npos != pos)
            data = data.substr(pos + token.size());
    } while (std::string::npos != pos);
    return output;
}

void prepareLaserScans(std::string folder){
     sensor_msgs::PointCloud2 laser_pcl_msg;
     pcl_t laser_pcl;
     laser_geometry::LaserProjection projector;

    scans.clear();
    std::fstream laser_msg;
    std::stringstream file;
    file <<folder<<"/data.scans";
    laser_msg.open(file.str().c_str(),std::ios_base::in);
    if(!laser_msg){
        std::cout<<"File"<<folder<<"/data.scans NOT FOUND"<<std::endl;
    }
    std::string line;
    unsigned int seq =0;
    while(std::getline(laser_msg,line)){
        std::vector<std::string> parts = split(line,",");
        sensor_msgs::LaserScan msg;
        msg.header.seq = seq;
        msg.header.frame_id = "base_link";
        msg.angle_min = static_cast<float>(std::atof(parts[1].c_str()));
        msg.angle_max = 2.26456475258f;
        msg.range_min = 0.0230000000447f;
        msg.range_max = 60.0f;
        msg.time_increment =  1.73611115315e-05f;
        msg.angle_increment = static_cast<float>(std::atof(parts[2].c_str()));
        for(size_t i = 4; i< parts.size();++i){
          msg.ranges.push_back(static_cast<float>(std::atof(parts[i].c_str())));
        }
        // msg to pcl
        projector.projectLaser(msg, laser_pcl_msg);
        pcl::moveFromROSMsg(laser_pcl_msg, laser_pcl);
        scans.push_back(laser_pcl.makeShared());
        ++seq;
    }
    laser_msg.close();
}

void preparePoseData(std::string folder){
    real_poses.clear();
    std::fstream pose_msg;
    std::stringstream file;
    file <<folder<<"/data.poses";
    pose_msg.open(file.str().c_str(),std::ios_base::in);
    if(!pose_msg){
        std::cout<<"File"<<folder<<"/data.poses NOT FOUND"<<std::endl;
    }
    std::string line;
    Eigen::Vector3d pose;
    while(std::getline(pose_msg,line)){
     std::vector<std::string> parts = split(line,",");
     pose << std::atof(parts[1].c_str()),std::atof(parts[2].c_str()),std::atof(parts[3].c_str());
     real_poses.push_back(pose);
    }
  pose_msg.close();
}

void testMatch(size_t source_id, size_t target_id){

   std::chrono::time_point<std::chrono::system_clock> s_proof, e_proof, s_match, e_match;
  eigt::pose2d_t<double> real_trans = eigt::getPoseFromTransform(eigt::transBtwPoses(real_poses[target_id],real_poses[source_id]));
  std::chrono::duration<double> elapsed_seconds;
  pcl_t::Ptr output_pr(new pcl_t);
  pcl_t::Ptr output_m(new pcl_t);

  //prepare initial guess
  Eigen::Matrix4f guess = Eigen::Matrix4f::Identity();
  // Eigen::Matrix4f guess = eigt::convertFromTransform(eigt::transBtwPoses(
  //                             real_poses[target_id], real_poses[source_id])).cast<float>();
  // calculate proof
  s_proof = std::chrono::system_clock::now();
  proofer->setInputSource(scans[source_id]);
  proofer->setInputTarget(scans[target_id]);
  proofer->align(*output_pr,guess);
  e_proof = std::chrono::system_clock::now();

  //calculate my matcher
  s_match = std::chrono::system_clock::now();
  matcher->setInputSource(scans[source_id]);
  matcher->setInputTarget(scans[target_id]);
  matcher->align(*output_m, guess);
  e_match = std::chrono::system_clock::now();

  std::cout<<"sucess"<<matcher->hasConverged()<<std::endl;
  std::cout<<"result score: "<<matcher->getFitnessScore()<<std::endl;
  eigt::pose2d_t<double> calc_trans = eigt::getPoseFromTransform(
      eigt::convertToTransform<double>(
          matcher->getFinalTransformation().cast<double>()));
  eigt::pose2d_t<double> proof_trans = eigt::getPoseFromTransform(
      eigt::convertToTransform<double>(
          proofer->getFinalTransformation().cast<double>()));
  elapsed_seconds = (e_proof - s_proof);
  std::cout<<"PROOF TRANSFORM:"<<proof_trans.transpose()<<" calc time: "<<elapsed_seconds.count()<<std::endl;
  elapsed_seconds = (e_match - s_match);
  std::cout<<"CALC TRANSFORM:"<<calc_trans.transpose()<<" calc time: "<<elapsed_seconds.count()<<std::endl;
  std::cout<<"DIFF:"<<(proof_trans - calc_trans).cwiseAbs().transpose()<<std::endl<<std::endl;

  // Transforming unfiltered, input cloud using found transform.
    pcl_t::Ptr output(new pcl_t);
    pcl::transformPointCloud (*scans[source_id], *output,eigt::convertFromTransform(eigt::getTransFromPose(calc_trans)).cast<float>());
    // Initializing point cloud visualizer
    boost::shared_ptr<pcl::visualization::PCLVisualizer>
    viewer_final (new pcl::visualization::PCLVisualizer ("3D Viewer"));
    viewer_final->setBackgroundColor (0, 0, 0);

    // Coloring and visualizing target cloud (red).
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ>
    target_color (scans[target_id], 255, 0, 0);
    viewer_final->addPointCloud<pcl::PointXYZ> (scans[target_id], target_color, "target cloud");
    viewer_final->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE,
                                                    1, "target cloud");

    // Coloring and visualizing transformed input cloud (green).
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ>
    output_color (output, 0, 255, 0);
    viewer_final->addPointCloud<pcl::PointXYZ> (output, output_color, "output cloud");
    viewer_final->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE,
                                                    1, "output cloud");

    // Coloring and visualizing input cloud (blue).
    pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ>
    source_color (output, 100, 0, 255);
    viewer_final->addPointCloud<pcl::PointXYZ> (scans[source_id], source_color, "source cloud");
    viewer_final->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE,
                                                    1, "source cloud");

    // Starting visualizer
    viewer_final->addCoordinateSystem (1.0, "global");
    viewer_final->initCameraParameters ();
    viewer_final->setCameraPosition(0,0,10,0,0,0);

    // Wait until visualizer window is closed.
    while (!viewer_final->wasStopped ())
    {
      viewer_final->spinOnce (100);
      boost::this_thread::sleep (boost::posix_time::microseconds (100000));
    }
}


void test()
{
  //matches.clear();
  size_t target = 0;
  std::cout<<"start test"<<std::endl;
  for(size_t i =0; i< 499; ++i){
    eigt::transform2d_t<double> real_trans = eigt::transBtwPoses(real_poses[target],real_poses[i]);
    if (eigt::getAngle(real_trans) > MIN_ROTATION ||
        eigt::getDisplacement(real_trans) > MIN_DISPLACEMENT){
      std::cout<<"matching "<<i<< "  "<<target<<std::endl;
      testMatch(i, target);
      target = i;
      //return;
    }
  }


}

int main(int argc, char **argv)
{
  std::vector<std::string> args(argv,argv+argc);
  if(args.size() != 3 && args[2] != "d2d" && args[2] != "basic"){
    std::cout << "Correct format of arguments: \n Path to the folder with data.poses, data.scans was not provided\n Calculation engine (d2d,basic)";
    std::cout<<std::endl;
    return 0;
  }
  //matcher->setResolution(1);
  preparePoseData(args[1]);
  prepareLaserScans(args[1]);
  proofer = new NormalDistributionsTransform<pcl::PointXYZ,pcl::PointXYZ>();
  if(args[2] == "basic"){
    matcher = new NormalDistributionsTransform2DEx<pcl::PointXYZ,pcl::PointXYZ>();
  }else if(args[2] == "d2d"){
    matcher = new D2DNormalDistributionsTransform2D<pcl::PointXYZ,pcl::PointXYZ>();
  }
  test();
  delete matcher;
  delete proofer;
  return 0;
}

