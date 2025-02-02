/* INCLUDES FOR THIS PROJECT */
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <limits>
#include <opencv2/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/xfeatures2d.hpp>
#include <opencv2/xfeatures2d/nonfree.hpp>

#include "dataStructures.h"
#include "matching2D.hpp"
#include "ringbuffer.h"

using namespace std;

/* MAIN PROGRAM */
int main(int argc, const char* argv[])
{
    /* INIT VARIABLES AND DATA STRUCTURES */

    // data location
    string dataPath = "../";

    // camera
    string imgBasePath = dataPath + "images/";
    string imgPrefix = "KITTI/2011_09_26/image_00/data/000000"; // left camera, color
    string imgFileType = ".png";
    int imgStartIndex = 0; // first file index to load (assumes Lidar and camera names have identical naming convention)
    int imgEndIndex = 9;   // last file index to load
    int imgFillWidth = 4;  // no. of digits which make up the file index (e.g. img-0001.png)

    // misc
    const int dataBufferSize = 2;       // no. of images which are held in memory (ring buffer) at the same time
    Ringbuffer<DataFrame, dataBufferSize> dataBuffer; // list of data frames which are held in memory at the same time
    bool bVis = false;            // visualize results

    /* MAIN LOOP OVER ALL IMAGES */
    std::stringstream ss;
    double averageKeypointTime = 0.0;
    double averageKeypoints = 0.0;
    double averageDescriptorTime = 0.0;
    double averageMatches = 0.0;
    double averageMatchRatio = 0.0;
    int validImageCount = 0;
    for (size_t imgIndex = 0; imgIndex <= imgEndIndex - imgStartIndex; imgIndex++)
    {
        /* LOAD IMAGE INTO BUFFER */

        // assemble filenames for current index
        ostringstream imgNumber;
        imgNumber << setfill('0') << setw(imgFillWidth) << imgStartIndex + imgIndex;
        string imgFullFilename = imgBasePath + imgPrefix + imgNumber.str() + imgFileType;

        // load image from file and convert to grayscale
        cv::Mat img, imgGray;
        img = cv::imread(imgFullFilename);
        cv::cvtColor(img, imgGray, cv::COLOR_BGR2GRAY);

        //// STUDENT ASSIGNMENT
        //// TASK MP.1 -> replace the following code with ring buffer of size dataBufferSize

        // push image into data frame buffer
        DataFrame frame;
        frame.cameraImg = imgGray;
        dataBuffer.push_back(frame);

        //// EOF STUDENT ASSIGNMENT
        cout << "#1 : LOAD IMAGE INTO BUFFER done" << endl;

        /* DETECT IMAGE KEYPOINTS */

        // extract 2D keypoints from current image
        vector<cv::KeyPoint> keypoints; // create empty feature list for current image
        string detectorType = "SHITOMASI";

        //// STUDENT ASSIGNMENT
        //// TASK MP.2 -> add the following keypoint detectors in file matching2D.cpp and enable string-based selection based on detectorType
        //// -> SHITOMASI, HARRIS, FAST, BRISK, ORB, AKAZE, SIFT

        bool visualizeKeypoints = false;
        auto timeKeypointsStart = std::chrono::high_resolution_clock::now();
        if (detectorType.compare("SHITOMASI") == 0)
        {
            detKeypointsShiTomasi(keypoints, imgGray, visualizeKeypoints);
        }
        else if (detectorType.compare("HARRIS") == 0)
        {
            detKeypointsHarris(keypoints, imgGray, visualizeKeypoints);
        }
        else
        {
            detKeypointsModern(keypoints, imgGray, detectorType, visualizeKeypoints);
        }
        double timeKeypointDetection = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - timeKeypointsStart).count() / 1000.0;
        averageKeypointTime += timeKeypointDetection;
        //// EOF STUDENT ASSIGNMENT

        //// STUDENT ASSIGNMENT
        //// TASK MP.3 -> only keep keypoints on the preceding vehicle

        // only keep keypoints on the preceding vehicle
        bool bFocusOnVehicle = true;
        cv::Rect vehicleRect(535, 180, 180, 150);
        if (bFocusOnVehicle)
        {
            auto keypointIter = keypoints.begin();
            while (keypointIter != keypoints.end())
            {
                if (!vehicleRect.contains(keypointIter->pt))
                {
                    keypoints.erase(keypointIter);
                }
                else
                {
                    ++keypointIter;
                }
            }
        }
        ss << keypoints.size() << " " << std::fixed << std::setprecision(2) << timeKeypointDetection;
        averageKeypoints += keypoints.size();
        //// EOF STUDENT ASSIGNMENT

        // optional : limit number of keypoints (helpful for debugging and learning)
        bool bLimitKpts = false;
        if (bLimitKpts)
        {
            int maxKeypoints = 50;

            if (detectorType.compare("SHITOMASI") == 0)
            { // there is no response info, so keep the first 50 as they are sorted in descending quality order
                keypoints.erase(keypoints.begin() + maxKeypoints, keypoints.end());
            }
            cv::KeyPointsFilter::retainBest(keypoints, maxKeypoints);
            cout << " NOTE: Keypoints have been limited!" << endl;
        }

        // push keypoints and descriptor for current frame to end of data buffer
        dataBuffer.at(0).keypoints = keypoints;
        cout << "#2 : DETECT KEYPOINTS done" << endl;

        /* EXTRACT KEYPOINT DESCRIPTORS */

        //// STUDENT ASSIGNMENT
        //// TASK MP.4 -> add the following descriptors in file matching2D.cpp and enable string-based selection based on descriptorType
        //// -> BRIEF, ORB, FREAK, AKAZE, SIFT

        cv::Mat descriptors;
        string descriptorType = "BRISK"; // BRISK, BRIEF, ORB, FREAK, AKAZE, SIFT
        auto timeDescKeypointsStart = std::chrono::high_resolution_clock::now();
        descKeypoints(dataBuffer.at(0).keypoints, dataBuffer.at(0).cameraImg, descriptors,
            descriptorType);
        double timeDescriptors = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - timeDescKeypointsStart).count() / 1000.0;
        ss << " " << timeDescriptors;
        averageDescriptorTime += timeDescriptors;
        //// EOF STUDENT ASSIGNMENT

        // push descriptors for current frame to end of data buffer
        dataBuffer.at(0).descriptors = descriptors;

        cout << "#3 : EXTRACT DESCRIPTORS done" << endl;

        if (dataBuffer.size() > 1) // wait until at least two images have been processed
        {

            /* MATCH KEYPOINT DESCRIPTORS */

            vector<cv::DMatch> matches;
            string matcherType = "MAT_BF";        // MAT_BF, MAT_FLANN
            string descriptorType = "DES_BINARY"; // DES_BINARY, DES_HOG
            string selectorType = "SEL_KNN";       // SEL_NN, SEL_KNN

            //// STUDENT ASSIGNMENT
            //// TASK MP.5 -> add FLANN matching in file matching2D.cpp
            //// TASK MP.6 -> add KNN match selection and perform descriptor distance ratio filtering with t=0.8 in file matching2D.cpp

            matchDescriptors(dataBuffer.at(1).keypoints, dataBuffer.at(0).keypoints,
                dataBuffer.at(1).descriptors, dataBuffer.at(0).descriptors,
                matches, descriptorType, matcherType, selectorType);

            ss << " " << matches.size();
            if (keypoints.size() > 0)
            {
                double matchRatio = matches.size() / static_cast<double>(keypoints.size());
                ss << " " << matchRatio;
                ++validImageCount;
                averageMatchRatio += matchRatio;
            }
            ss << std::endl;
            averageMatches += matches.size();

            //// EOF STUDENT ASSIGNMENT

            // store matches in current data frame
            dataBuffer.at(0).kptMatches = matches;

            cout << "#4 : MATCH KEYPOINT DESCRIPTORS done" << endl;

            // visualize matches between current and previous image
            bVis = true;
            if (bVis)
            {
                cv::Mat matchImg = (dataBuffer.at(0).cameraImg).clone();
                cv::drawMatches(dataBuffer.at(1).cameraImg, dataBuffer.at(1).keypoints,
                    dataBuffer.at(0).cameraImg, dataBuffer.at(0).keypoints,
                    matches, matchImg,
                    cv::Scalar::all(-1), cv::Scalar::all(-1),
                    vector<char>(), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);

                string windowName = "Matching keypoints between two camera images";
                cv::namedWindow(windowName, 7);
                cv::imshow(windowName, matchImg);
                cout << "Press key to continue to next image" << endl;
                cv::waitKey(0); // wait for key to be pressed
            }
            bVis = false;
        }
        else
        {
            ss << std::endl;
        }

    } // eof loop over all images

    // evaluation
    std::cout << std::endl << "Evaluation:" << std::endl << ss.str();
    std::cout << std::string(30, '-') << std::endl;
    std::cout << std::fixed << std::setprecision(2) <<
        averageKeypoints / 10.0 << " " <<
        averageKeypointTime / 10.0 << " " <<
        averageDescriptorTime / 10.0 << " " <<
        averageMatches / 9.0 << " " << averageMatchRatio / validImageCount << std::endl;


    return 0;
}
