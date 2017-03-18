#pragma once
#include <math.h>
#include <iostream>
#include <opencv2\opencv.hpp>
#include <opencv2\core\core.hpp>
#include <opencv2\highgui\highgui.hpp>
#include <assert.h>
#include <string.h>
#include "MyLog.h"

#define PI 3.14159265358979323846
#define RESOURCE_PATH ".\\Resources\\"
#define OUTPUT_PATH ".\\Outputs\\"
#define TEMP_PATH ".\\Temp\\"



#define OPENCV_3
#ifndef OPENCV_3
	#define OPENCV_2
#else
	//may define sth
	#define OPENCV3_CONTRIB
#endif

#define RUN_MAIN
#ifndef RUN_MAIN
	#define RUN_TEST
#endif
//#define SHOW_IMAGE

using namespace cv;

const double M_PI = PI;
const double ERR = 1e-7;

inline double round(const double a) {return cvRound(a);}
inline double square(const double a) {return pow(a,2);}
#define GET_STR(msg,s)	\
	{\
		std::stringstream ss;\
		ss << msg;\
		s=ss.str();\
	}

template<typename T> 
std::string vec2str(std::vector<T> & v) {
	std::stringstream ss;
	ss << "{";
	for (auto i:v) {ss << i << ", ";}
	ss << "}";
	return ss.str();
}

inline void _resize_(InputArray src, OutputArray dst, Size dsize, double fx = 0, double fy = 0) {
	src.size().area() > dsize.area()
		? cv::resize(src, dst, dsize, fx, fy, CV_INTER_AREA)
		: cv::resize(src, dst, dsize, fx, fy, CV_INTER_CUBIC);

}