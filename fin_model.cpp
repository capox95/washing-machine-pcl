#define PCL_NO_PRECOMPILE
#include <pcl/pcl_macros.h>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/impl/pcd_io.hpp>
#include <pcl/common/transforms.h>
#include <pcl/surface/mls.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/filters/filter.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/segmentation/sac_segmentation.h>

#include <pcl/visualization/cloud_viewer.h>

#include <sstream> // std::stringstream

struct PointPico
{
    PCL_ADD_POINT4D;
    float noise;
    u_int16_t intensity;
    u_int8_t gray;

    EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
} EIGEN_ALIGN16;

POINT_CLOUD_REGISTER_POINT_STRUCT(PointPico,
                                  (float, x, x)(float, y, y)(float, z, z) //
                                  (float, noise, noise)(u_int16_t, intensity, intensity)(u_int8_t, gray, gray)

);

void converPointCloudXYZ(pcl::PointCloud<PointPico>::Ptr &source, pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud)
{
    cloud->width = source->width;
    cloud->height = source->height;
    cloud->resize(source->width * source->height);

    for (int i = 0; i < source->size(); i++)
    {
        cloud->points[i].x = source->points[i].x;
        cloud->points[i].y = source->points[i].y;
        cloud->points[i].z = source->points[i].z;
    }

    cloud->is_dense = false;
}

void computeNormalsSmooth(pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud, pcl::PointCloud<pcl::PointNormal> &mls_points)
{
    pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);
    pcl::MovingLeastSquares<pcl::PointXYZ, pcl::PointNormal> mls;
    mls.setComputeNormals(true);

    // Set parameters
    mls.setNumberOfThreads(4);
    mls.setInputCloud(cloud);
    mls.setPolynomialOrder(7);
    mls.setSearchMethod(tree);
    mls.setSearchRadius(0.03);

    // Reconstruct
    mls.process(mls_points);
    std::cout << "mls_points: " << mls_points.height << ", " << mls_points.width << std::endl;
}

void computeNormals(pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud, pcl::PointCloud<pcl::PointNormal>::Ptr &cloud_normals)
{
    cloud_normals->height = cloud->height;
    cloud_normals->width = cloud->width;
    cloud_normals->resize(cloud->size());
    for (int i = 0; i < cloud->size(); i++)
    {
        cloud_normals->points[i].x = cloud->points[i].x;
        cloud_normals->points[i].y = cloud->points[i].y;
        cloud_normals->points[i].z = cloud->points[i].z;
    }

    pcl::NormalEstimationOMP<pcl::PointNormal, pcl::PointNormal> nest;
    nest.setNumberOfThreads(4);
    nest.setRadiusSearch(0.01);
    nest.setInputCloud(cloud_normals);
    nest.compute(*cloud_normals);
}

void removeNans(pcl::PointCloud<pcl::PointXYZ>::Ptr &source_cloud)
{
    boost::shared_ptr<std::vector<int>> indices(new std::vector<int>);
    pcl::removeNaNFromPointCloud(*source_cloud, *indices);
    pcl::ExtractIndices<pcl::PointXYZ> extract;
    extract.setInputCloud(source_cloud);
    extract.setIndices(indices);
    extract.setNegative(false);
    extract.filter(*source_cloud);
}

void outliersRemoval(pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud, pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud_filtered)
{
    pcl::RadiusOutlierRemoval<pcl::PointXYZ> outrem;
    // build the filter
    outrem.setInputCloud(cloud);
    outrem.setRadiusSearch(0.01);
    outrem.setMinNeighborsInRadius(10);
    // apply filter
    outrem.filter(*cloud_filtered);
}

void connectedComponets(pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud, std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> &cloud_clusters)
{

    // Creating the KdTree object for the search method of the extraction
    pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);
    tree->setInputCloud(cloud);

    std::vector<pcl::PointIndices> cluster_indices;
    pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;
    ec.setClusterTolerance(0.005); // 1 cm
    ec.setMinClusterSize(1000);
    ec.setMaxClusterSize(25000000);
    ec.setSearchMethod(tree);
    ec.setInputCloud(cloud);
    ec.extract(cluster_indices);

    for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin(); it != cluster_indices.end(); ++it)
    {
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_cluster(new pcl::PointCloud<pcl::PointXYZ>);
        for (std::vector<int>::const_iterator pit = it->indices.begin(); pit != it->indices.end(); ++pit)
            cloud_cluster->points.push_back(cloud->points[*pit]); //*
        cloud_cluster->width = cloud_cluster->points.size();
        cloud_cluster->height = 1;
        cloud_cluster->is_dense = true;

        std::cout << "PointCloud representing the Cluster: " << cloud_cluster->points.size() << " data points." << std::endl;
        cloud_clusters.push_back(cloud_cluster);
    }

    std::cout << "number of clusters found: " << cluster_indices.size() << std::endl;
}

void splitPointNormal(pcl::PointCloud<pcl::PointNormal> &input, pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud,
                      pcl::PointCloud<pcl::Normal>::Ptr &normals)
{
    cloud->height = input.height;
    cloud->width = input.width;
    cloud->resize(input.size());
    normals->height = input.height;
    normals->width = input.width;
    normals->resize(input.size());
    for (int i = 0; i < input.size(); i++)
    {
        cloud->points[i].x = input.points[i].x;
        cloud->points[i].y = input.points[i].y;
        cloud->points[i].z = input.points[i].z;

        normals->points[i].normal_x = input.points[i].normal_x;
        normals->points[i].normal_y = input.points[i].normal_y;
        normals->points[i].normal_z = input.points[i].normal_z;
        normals->points[i].curvature = input.points[i].curvature;
    }
}

void colorMapCurvature(pcl::PointCloud<pcl::PointNormal> &input, pcl::PointCloud<pcl::PointXYZI>::Ptr &cloud)
{
    cloud->height = input.height;
    cloud->width = input.width;
    cloud->resize(input.size());
    for (int i = 0; i < input.size(); i++)
    {
        cloud->points[i].x = input.points[i].x;
        cloud->points[i].y = input.points[i].y;
        cloud->points[i].z = input.points[i].z;
        cloud->points[i].intensity = input.points[i].curvature;
    }
}

void boundingBox(pcl::PointCloud<pcl::PointXYZ>::Ptr &cloudSegmented)
{
    // Compute principal directions
    Eigen::Vector4f pcaCentroid;
    pcl::compute3DCentroid(*cloudSegmented, pcaCentroid);
    Eigen::Matrix3f covariance;
    computeCovarianceMatrixNormalized(*cloudSegmented, pcaCentroid, covariance);
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f> eigen_solver(covariance, Eigen::ComputeEigenvectors);
    Eigen::Matrix3f eigenVectorsPCA = eigen_solver.eigenvectors();
    eigenVectorsPCA.col(2) = eigenVectorsPCA.col(0).cross(eigenVectorsPCA.col(1));

    // Transform the original cloud to the origin where the principal components correspond to the axes.
    Eigen::Matrix4f projectionTransform(Eigen::Matrix4f::Identity());
    projectionTransform.block<3, 3>(0, 0) = eigenVectorsPCA.transpose();
    projectionTransform.block<3, 1>(0, 3) = -1.f * (projectionTransform.block<3, 3>(0, 0) * pcaCentroid.head<3>());
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloudPointsProjected(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::transformPointCloud(*cloudSegmented, *cloudPointsProjected, projectionTransform);
    // Get the minimum and maximum points of the transformed cloud.
    pcl::PointXYZ minPoint, maxPoint;
    pcl::getMinMax3D(*cloudPointsProjected, minPoint, maxPoint);
    const Eigen::Vector3f meanDiagonal = 0.5f * (maxPoint.getVector3fMap() + minPoint.getVector3fMap());

    // Final transform
    const Eigen::Quaternionf bboxQuaternion(eigenVectorsPCA);
    const Eigen::Vector3f bboxTransform = eigenVectorsPCA * meanDiagonal + pcaCentroid.head<3>();

    std::cout << bboxQuaternion.x() << ", " << bboxQuaternion.y() << ", "
              << bboxQuaternion.z() << ", " << bboxQuaternion.w() << std::endl;

    std::cout << bboxTransform << std::endl;
}

void ransacLineDetection(pcl::PointCloud<pcl::PointXYZ>::Ptr &cloud_in, pcl::ModelCoefficients &line)
{

    pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
    pcl::SACSegmentation<pcl::PointXYZ> seg;

    seg.setOptimizeCoefficients(true);
    seg.setModelType(pcl::SACMODEL_LINE);
    seg.setMethodType(pcl::SAC_RANSAC);
    seg.setDistanceThreshold(0.02);

    seg.setInputCloud(cloud_in);
    seg.segment(*inliers, line);

    if (inliers->indices.size() == 0)
    {
        std::cerr << "Could not estimate a linear model for the given dataset." << std::endl;
    }
}

void getCloud(std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> &clouds, pcl::PointCloud<pcl::PointXYZ>::Ptr &result)
{
    result->width = clouds[0]->width;
    result->height = clouds[0]->height;
    result->resize(result->width * result->height);
    for (int i = 0; i < clouds[0]->points.size(); i++)
    {
        result->points[i].x = clouds[0]->points[i].x;
        result->points[i].y = clouds[0]->points[i].y;
        result->points[i].z = clouds[0]->points[i].z;
    }
}

//----------------------------------------------------------------------------- //
int main(int argc, char **argv)
{

    pcl::PointCloud<PointPico>::Ptr source(new pcl::PointCloud<PointPico>);
    if (pcl::io::loadPCDFile<PointPico>(argv[1], *source) == -1)
    {
        PCL_ERROR(" error opening file ");
        return (-1);
    }
    std::cout << "cloud orginal size: " << source->size() << std::endl;

    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
    converPointCloudXYZ(source, cloud);
    std::cout << cloud->size() << std::endl;

    removeNans(cloud);
    std::cout << cloud->size() << std::endl;

    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZ>);
    outliersRemoval(cloud, cloud_filtered);
    std::cout << cloud_filtered->size() << std::endl;

    std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> clouds_connected;
    connectedComponets(cloud_filtered, clouds_connected);

    pcl::PointCloud<pcl::PointXYZ>::Ptr result(new pcl::PointCloud<pcl::PointXYZ>);
    getCloud(clouds_connected, result);

    //
    pcl::PointCloud<pcl::PointNormal>::Ptr cloud_normals(new pcl::PointCloud<pcl::PointNormal>);
    computeNormals(result, cloud_normals);
    //

    // VISUALIZATION
    pcl::visualization::PCLVisualizer viz("PCL");
    viz.addCoordinateSystem(0.2, "coord");
    viz.setBackgroundColor(0.0, 0.0, 0.5);
    viz.addPointCloud<pcl::PointXYZ>(cloud, "cloud");
    viz.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 0.0f, 1.0f, 0.0f, "cloud");
    viz.addPointCloud<pcl::PointXYZ>(cloud_filtered, "cloud_filtered");
    viz.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 1.0f, 0.0f, 0.0f, "cloud_filtered");

    pcl::visualization::PCLVisualizer vizS("PCL Result");
    vizS.setBackgroundColor(1.0, 1.0, 1.0);
    vizS.addPointCloud<pcl::PointNormal>(cloud_normals, "cloud_normals");
    vizS.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, 0.0f, 0.0f, 1.0f, "cloud_normals");
    //vizS.addPointCloudNormals<pcl::PointNormal>(cloud_normals, 1, 0.01, "normals");

    viz.spin();

    //pcl::io::savePCDFileASCII("test_pcd.pcd", *cloud_normals);
    return 0;
}
