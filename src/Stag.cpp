#include "Stag.h"
#include "Ellipse.h"
#include "utility.h"

#define HALF_PI 1.570796326794897

using cv::Mat;
using cv::Point2d;

Stag::Stag(int libraryHD, int inErrorCorrection)
{
	errorCorrection = inErrorCorrection;

	// check libraryHD
	std::set<int> possibleHDs = {11, 13, 15, 17, 19, 21, 23};
	if (possibleHDs.count(libraryHD) == 0) {
		throw std::invalid_argument("Invalid library HD " + std::to_string(libraryHD) + ". Possible values are: [11, 13, 15, 17, 19, 21, 23]");
	}

	// if errorCorrection set to -1, take max possible value for libraryHD
	if (errorCorrection == -1) {
		errorCorrection = (libraryHD-1)/2;
	}

	// check errorCorrection
	if (inErrorCorrection > (libraryHD -1) / 2 || errorCorrection < 0) {
		throw std::invalid_argument("Invalid error correction value " + std::to_string(errorCorrection) + " for library HD " + std::to_string(libraryHD) + ". Error correction needs to be in range 0 <= HD <= (HD-1)/2.");
	}

	quadDetector = QuadDetector();
	fillCodeLocations();
	decoder = Decoder(libraryHD);
}


void Stag::detectMarkers(const Mat& inImage)
{
	// convert image to grayscale
	if (inImage.channels() == 1) {
		image = inImage;
	} else if (inImage.channels() == 3) {
		cv::cvtColor(inImage, image, cv::COLOR_BGR2GRAY);
	} else if (inImage.channels() == 4) {
		cv::cvtColor(inImage, image, cv::COLOR_BGRA2GRAY);
	} else {
		throw std::invalid_argument("Invalid image color space. Supported color spaces are: [GRAYSCALE, BGR, BGRA].");
	}

	quadDetector.detectQuads(image, &edInterface);

	vector<Quad> quads = quadDetector.getQuads();

	for (auto & quad : quads)
	{
		quad.estimateHomography();
		Codeword c = readCode(quad);
		int shift;
		int id;
		if (decoder.decode(c, errorCorrection, id, shift))
		{
			Marker marker(quad, id);
			marker.shiftCorners2(shift);
			markers.push_back(marker);
		}
		else
			falseCandidates.push_back(quad);
	}

	for (auto & marker : markers)
		poseRefiner.refineMarkerPose(&edInterface, marker);
}


void Stag::logResults(const string& path)
{
	drawer.drawEdgeMap(path + "1_edges.png", image, edInterface.getEdgeMap());
	drawer.drawLines(path + "2_lines.png", image, edInterface.getEDLines());
	drawer.drawCorners(path + "3_corners.png", image, quadDetector.getCornerGroups());
	drawer.drawQuads(path + "4_quads.png", image, quadDetector.getQuads());
    drawer.drawQuads(path + "5_distorted_quads.png", image, quadDetector.getDistortedQuads());
	drawer.drawMarkers(path + "6_markers.png", image, markers);
    drawer.drawQuads(path + "7_false_quads.png", image, falseCandidates);
	drawer.drawEllipses(path + "8_ellipses.png", image, markers);
}


Codeword Stag::readCode(const Quad &q)
{
	// take readings from 48 code locations, 12 black border locations, and 12 white border locations
	vector<unsigned char> samples(72);

	// a better idea may be creating a list of points to be sampled and let the OpenCV's interpolation function handle the sampling
	for (int i = 0; i < 48; i++)
	{
		Mat projectedPoint = q.H * codeLocs[i];
		samples[i] = readPixelSafeBilinear(image, Point2d(projectedPoint.at<double>(0) / projectedPoint.at<double>(2), projectedPoint.at<double>(1) / projectedPoint.at<double>(2)));
	}
	for (int i = 0; i < 12; i++)
	{
		Mat projectedPoint = q.H * blackLocs[i];
		samples[i + 48] = readPixelSafeBilinear(image, Point2d(projectedPoint.at<double>(0) / projectedPoint.at<double>(2), projectedPoint.at<double>(1) / projectedPoint.at<double>(2)));
	}
	for (int i = 0; i < 12; i++)
	{
		Mat projectedPoint = q.H * whiteLocs[i];
		samples[i + 60] = readPixelSafeBilinear(image, Point2d(projectedPoint.at<double>(0) / projectedPoint.at<double>(2), projectedPoint.at<double>(1) / projectedPoint.at<double>(2)));
	}

	// threshold the readings using Otsu's method
	cv::threshold(samples, samples, 0, 255, cv::THRESH_OTSU + cv::THRESH_BINARY_INV);

	// create a codeword using the thresholded readings
	Codeword c;
	for (int i = 0; i < 48; i++)
		c[i] = samples[i] / 255;

	return c;
}

void Stag::fillCodeLocations()
{
	// fill coordinates to be sampled
	codeLocs = vector<Mat>(48);

	// code circles are located in a circle with radius outerCircleRadius
	double outerCircleRadius = 0.4;
	double innerCircleRadius = outerCircleRadius * 0.9;

	// each quadrant is rotated by HALF_PI
	// these part is left as is for self-documenting purposes
	for (int i = 0; i < 4; i++)
	{
		codeLocs[0 + i * 12] = createMatFromPolarCoords(0.088363142525988, 0.785398163397448 + i * HALF_PI, innerCircleRadius);

		codeLocs[1 + i * 12] = createMatFromPolarCoords(0.206935928182607, 0.459275804122858 + i * HALF_PI, innerCircleRadius);
		codeLocs[2 + i * 12] = createMatFromPolarCoords(0.206935928182607, HALF_PI - 0.459275804122858 + i * HALF_PI, innerCircleRadius);

		codeLocs[3 + i * 12] = createMatFromPolarCoords(0.313672146827381, 0.200579720495241 + i * HALF_PI, innerCircleRadius);
		codeLocs[4 + i * 12] = createMatFromPolarCoords(0.327493143484516, 0.591687617505840 + i * HALF_PI, innerCircleRadius);
		codeLocs[5 + i * 12] = createMatFromPolarCoords(0.327493143484516, HALF_PI - 0.591687617505840 + i * HALF_PI, innerCircleRadius);
		codeLocs[6 + i * 12] = createMatFromPolarCoords(0.313672146827381, HALF_PI - 0.200579720495241 + i * HALF_PI, innerCircleRadius);

		codeLocs[7 + i * 12] = createMatFromPolarCoords(0.437421957035861, 0.145724938287167 + i * HALF_PI, innerCircleRadius);
		codeLocs[8 + i * 12] = createMatFromPolarCoords(0.437226762361658, 0.433363129825345 + i * HALF_PI, innerCircleRadius);
		codeLocs[9 + i * 12] = createMatFromPolarCoords(0.430628029742607, 0.785398163397448 + i * HALF_PI, innerCircleRadius);
		codeLocs[10 + i * 12] = createMatFromPolarCoords(0.437226762361658, HALF_PI - 0.433363129825345 + i * HALF_PI, innerCircleRadius);
		codeLocs[11 + i * 12] = createMatFromPolarCoords(0.437421957035861, HALF_PI - 0.145724938287167 + i * HALF_PI, innerCircleRadius);
	}

	double borderDist = 0.045;

	blackLocs = vector<Mat>(12);
	whiteLocs = vector<Mat>(12);

	for (int i = 0; i < 12; i++)
		blackLocs[i] = Mat(3, 1, CV_64FC1);
	for (int i = 0; i < 12; i++)
		whiteLocs[i] = Mat(3, 1, CV_64FC1);

	blackLocs[0].at<double>(0) = borderDist;
	blackLocs[0].at<double>(1) = borderDist * 3;
	blackLocs[0].at<double>(2) = 1;

	blackLocs[1].at<double>(0) = borderDist * 2;
	blackLocs[1].at<double>(1) = borderDist * 2;
	blackLocs[1].at<double>(2) = 1;

	blackLocs[2].at<double>(0) = borderDist * 3;
	blackLocs[2].at<double>(1) = borderDist;
	blackLocs[2].at<double>(2) = 1;

	blackLocs[3].at<double>(0) = 1 - 3 * borderDist;
	blackLocs[3].at<double>(1) = borderDist;
	blackLocs[3].at<double>(2) = 1;

	blackLocs[4].at<double>(0) = 1 - 2 * borderDist;
	blackLocs[4].at<double>(1) = borderDist * 2;
	blackLocs[4].at<double>(2) = 1;

	blackLocs[5].at<double>(0) = 1 - borderDist;
	blackLocs[5].at<double>(1) = borderDist * 3;
	blackLocs[5].at<double>(2) = 1;

	blackLocs[6].at<double>(0) = 1 - borderDist;
	blackLocs[6].at<double>(1) = 1 - 3 * borderDist;
	blackLocs[6].at<double>(2) = 1;

	blackLocs[7].at<double>(0) = 1 - 2 * borderDist;
	blackLocs[7].at<double>(1) = 1 - 2 * borderDist;
	blackLocs[7].at<double>(2) = 1;

	blackLocs[8].at<double>(0) = 1 - 3 * borderDist;
	blackLocs[8].at<double>(1) = 1 - borderDist;
	blackLocs[8].at<double>(2) = 1;

	blackLocs[9].at<double>(0) = borderDist * 3;
	blackLocs[9].at<double>(1) = 1 - borderDist;
	blackLocs[9].at<double>(2) = 1;

	blackLocs[10].at<double>(0) = borderDist * 2;
	blackLocs[10].at<double>(1) = 1 - 2 * borderDist;
	blackLocs[10].at<double>(2) = 1;

	blackLocs[11].at<double>(0) = borderDist;
	blackLocs[11].at<double>(1) = 1 - 3 * borderDist;
	blackLocs[11].at<double>(2) = 1;


	whiteLocs[0].at<double>(0) = 0.25;
	whiteLocs[0].at<double>(1) = -borderDist;
	whiteLocs[0].at<double>(2) = 1;

	whiteLocs[1].at<double>(0) = 0.5;
	whiteLocs[1].at<double>(1) = -borderDist;
	whiteLocs[1].at<double>(2) = 1;

	whiteLocs[2].at<double>(0) = 0.75;
	whiteLocs[2].at<double>(1) = -borderDist;
	whiteLocs[2].at<double>(2) = 1;

	whiteLocs[3].at<double>(0) = 1 + borderDist;
	whiteLocs[3].at<double>(1) = 0.25;
	whiteLocs[3].at<double>(2) = 1;

	whiteLocs[4].at<double>(0) = 1 + borderDist;
	whiteLocs[4].at<double>(1) = 0.5;
	whiteLocs[4].at<double>(2) = 1;

	whiteLocs[5].at<double>(0) = 1 + borderDist;
	whiteLocs[5].at<double>(1) = 0.75;
	whiteLocs[5].at<double>(2) = 1;

	whiteLocs[6].at<double>(0) = 0.75;
	whiteLocs[6].at<double>(1) = 1 + borderDist;
	whiteLocs[6].at<double>(2) = 1;

	whiteLocs[7].at<double>(0) = 0.5;
	whiteLocs[7].at<double>(1) = 1 + borderDist;
	whiteLocs[7].at<double>(2) = 1;

	whiteLocs[8].at<double>(0) = 0.25;
	whiteLocs[8].at<double>(1) = 1 + borderDist;
	whiteLocs[8].at<double>(2) = 1;

	whiteLocs[9].at<double>(0) = -borderDist;
	whiteLocs[9].at<double>(1) = 0.75;
	whiteLocs[9].at<double>(2) = 1;

	whiteLocs[10].at<double>(0) = -borderDist;
	whiteLocs[10].at<double>(1) = 0.5;
	whiteLocs[10].at<double>(2) = 1;

	whiteLocs[11].at<double>(0) = -borderDist;
	whiteLocs[11].at<double>(1) = 0.25;
	whiteLocs[11].at<double>(2) = 1;
}


Mat Stag::createMatFromPolarCoords(double radius, double radians, double circleRadius)
{
	Mat point(3, 1, CV_64FC1);
	point.at<double>(0) = 0.5 + cos(radians) * radius * (circleRadius / 0.5);
	point.at<double>(1) = 0.5 - sin(radians) * radius * (circleRadius / 0.5);
	point.at<double>(2) = 1;
	return point;
}