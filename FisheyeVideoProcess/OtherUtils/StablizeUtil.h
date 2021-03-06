﻿/**
	REF from http://nghiaho.com/uploads/code/videostab.cpp
**/


#pragma once
#include "..\Config.h"
#include "ImageUtil.h"
#include "FileUtil.h"
#include <fstream>


// This video stablisation smooths the global trajectory using a sliding average window

const int SMOOTHING_RADIUS = 30; // In frames. The larger the more stable the video, but less reactive to sudden panning
const int HORIZONTAL_BORDER_CROP = 20; // In pixels. Crops the border to reduce the black borders from stabilisation being too noticeable.

// 1. Get previous to current frame transformation (dx, dy, da) for all frames
// 2. Accumulate the transformations to get the image trajectory
// 3. Smooth out the trajectory using an averaging window
// 4. Generate new set of previous to current transform, such that the trajectory ends up being the same as the smoothed trajectory
// 5. Apply the new transformation to the video



struct TransformParam {
	TransformParam() {}
	TransformParam(double _dx, double _dy, double _da) {
		dx = _dx;
		dy = _dy;
		da = _da;
	}

	double dx;
	double dy;
	double da; // angle
};

struct Trajectory {
	Trajectory() {}
	Trajectory(double _x, double _y, double _a) {
		x = _x;
		y = _y;
		a = _a;
	}

	double x;
	double y;
	double a; // angle
};

class StablizeUtil {
public:
	static std::string getCorrectedVideoName(std::string origName, int fps, Size sz) {
		int seeder = 0;
		hash_combine(seeder, origName);
		hash_combine(seeder, fps);
		hash_combine(seeder,sz.height);
		hash_combine(seeder,sz.width);
		std::string ret;
		GET_STR(TEMP_PATH << "TEMP" << seeder << ".avi",ret);
		return ret;
	};

	static std::string getStabCorrectedVideoName(std::string origName, int fps, Size sz) {
		return getCorrectedVideoName(origName,fps,sz)+".stab.avi";
	};


	static void Stablize(const char * fn, const char * fnDst) {
		VideoCapture cap(fn);
		LOG_MESS("Stablize " << fn << " to " << fnDst << " ...");

		Mat cur, cur_grey;
		Mat prev, prev_grey;

		cap >> prev;
		cvtColor(prev, prev_grey, COLOR_BGR2GRAY);

		auto vWriter = VideoWriter(fnDst, CV_FOURCC('D', 'I', 'V', 'X'),cap.get(CV_CAP_PROP_FPS), prev.size());

		// Step 1 - Get previous to current frame transformation (dx, dy, da) for all frames
		std::vector <TransformParam> prev_to_cur_transform; // previous to current

		//int k=1;
		int max_frames = cap.get(CV_CAP_PROP_FRAME_COUNT);
		Mat last_T;

		while(true) {
			cap >> cur;

			if(cur.data == NULL) {
				break;
			}

			cvtColor(cur, cur_grey, COLOR_BGR2GRAY);

			// vector from prev to cur
			std::vector <Point2f> prev_corner, cur_corner;
			std::vector <Point2f> prev_corner2, cur_corner2;
			std::vector <uchar> status;
			std::vector <float> err;

			goodFeaturesToTrack(prev_grey, prev_corner, 500, 0.01, 30);
			calcOpticalFlowPyrLK(prev_grey, cur_grey, prev_corner, cur_corner, status, err);

			// weed out bad matches
			for(size_t i=0; i < status.size(); i++) {
				if(status[i]) {
					prev_corner2.push_back(prev_corner[i]);
					cur_corner2.push_back(cur_corner[i]);
				}
			}

			// translation + rotation only
			Mat T = estimateRigidTransform(prev_corner2, cur_corner2, false); // false = rigid transform, no scaling/shearing

			// in rare cases no transform is found. We'll just use the last known good transform.
			if(T.data == NULL) {
				last_T.copyTo(T);
			}

			T.copyTo(last_T);

			// decompose T
			double dx = T.at<double>(0,2);
			double dy = T.at<double>(1,2);
			double da = atan2(T.at<double>(1,0), T.at<double>(0,0));

			prev_to_cur_transform.push_back(TransformParam(dx, dy, da));

			cur.copyTo(prev);
			cur_grey.copyTo(prev_grey);

		}

		// Step 2 - Accumulate the transformations to get the image trajectory

		// Accumulated frame to frame transform
		double a = 0;
		double x = 0;
		double y = 0;

		std::vector <Trajectory> trajectory; // trajectory at all frames

		for(size_t i=0; i < prev_to_cur_transform.size(); i++) {
			x += prev_to_cur_transform[i].dx;
			y += prev_to_cur_transform[i].dy;
			a += prev_to_cur_transform[i].da;

			trajectory.push_back(Trajectory(x,y,a));

		}

		// Step 3 - Smooth out the trajectory using an averaging window
		std::vector <Trajectory> smoothed_trajectory; // trajectory at all frames

		for(size_t i=0; i < trajectory.size(); i++) {
			double sum_x = 0;
			double sum_y = 0;
			double sum_a = 0;
			int count = 0;

			for(int j=-SMOOTHING_RADIUS; j <= SMOOTHING_RADIUS; j++) {
				if(i+j >= 0 && i+j < trajectory.size()) {
					sum_x += trajectory[i+j].x;
					sum_y += trajectory[i+j].y;
					sum_a += trajectory[i+j].a;

					count++;
				}
			}

			double avg_a = sum_a / count;
			double avg_x = sum_x / count;
			double avg_y = sum_y / count;

			smoothed_trajectory.push_back(Trajectory(avg_x, avg_y, avg_a));

		}

		// Step 4 - Generate new set of previous to current transform, such that the trajectory ends up being the same as the smoothed trajectory
		std::vector <TransformParam> new_prev_to_cur_transform;

		// Accumulated frame to frame transform
		a = 0;
		x = 0;
		y = 0;

		for(size_t i=0; i < prev_to_cur_transform.size(); i++) {
			x += prev_to_cur_transform[i].dx;
			y += prev_to_cur_transform[i].dy;
			a += prev_to_cur_transform[i].da;

			// target - current
			double diff_x = smoothed_trajectory[i].x - x;
			double diff_y = smoothed_trajectory[i].y - y;
			double diff_a = smoothed_trajectory[i].a - a;

			double dx = prev_to_cur_transform[i].dx + diff_x;
			double dy = prev_to_cur_transform[i].dy + diff_y;
			double da = prev_to_cur_transform[i].da + diff_a;

			new_prev_to_cur_transform.push_back(TransformParam(dx, dy, da));

		}

		// Step 5 - Apply the new transformation to the video
		cap.set(CV_CAP_PROP_POS_FRAMES, 0);
		Mat T(2,3,CV_64F);

		int vert_border = HORIZONTAL_BORDER_CROP * prev.rows / prev.cols; // get the aspect ratio correct

		int k=0;
		Rect finalRec;
		while(k < max_frames-1) { // don't process the very last frame, no valid transform
			cap >> cur;

			if(cur.data == NULL) {
				break;
			}

			T.at<double>(0,0) = cos(new_prev_to_cur_transform[k].da);
			T.at<double>(0,1) = -sin(new_prev_to_cur_transform[k].da);
			T.at<double>(1,0) = sin(new_prev_to_cur_transform[k].da);
			T.at<double>(1,1) = cos(new_prev_to_cur_transform[k].da);

			T.at<double>(0,2) = new_prev_to_cur_transform[k].dx;
			T.at<double>(1,2) = new_prev_to_cur_transform[k].dy;

			Mat cur1, cur2;

			warpAffine(cur, cur2, T, cur.size());

			cur2 = cur2(Range(vert_border, cur2.rows-vert_border), Range(HORIZONTAL_BORDER_CROP, cur2.cols-HORIZONTAL_BORDER_CROP));

			Rect rec;
			ImageUtil::removeBlackPixelByContourBound(cur2, cur1, rec);
			if (finalRec.area() == 0) {
				finalRec = rec;
			} else {
				Point2i tl = Point2i(max(finalRec.tl().x, rec.tl().x),max(finalRec.tl().y, rec.tl().y));
				Point2i br = Point2i( min(finalRec.br().x, rec.br().x), min(finalRec.br().y, rec.br().y));
				finalRec = Rect(tl,br);
			}


			k++;
		}


		cap.set(CV_CAP_PROP_POS_FRAMES, 0);


		k=0;
		while(k < max_frames-1) { // don't process the very last frame, no valid transform
			cap >> cur;

			if(cur.data == NULL) {
				break;
			}

			T.at<double>(0,0) = cos(new_prev_to_cur_transform[k].da);
			T.at<double>(0,1) = -sin(new_prev_to_cur_transform[k].da);
			T.at<double>(1,0) = sin(new_prev_to_cur_transform[k].da);
			T.at<double>(1,1) = cos(new_prev_to_cur_transform[k].da);

			T.at<double>(0,2) = new_prev_to_cur_transform[k].dx;
			T.at<double>(1,2) = new_prev_to_cur_transform[k].dy;

			Mat cur1, cur2;

			warpAffine(cur, cur2, T, cur.size());

			cur2 = cur2(Range(vert_border, cur2.rows-vert_border), Range(HORIZONTAL_BORDER_CROP, cur2.cols-HORIZONTAL_BORDER_CROP));
			cur2 = cur2(finalRec);
			ImageUtil::resize(cur2,cur2,cur.size());
			vWriter << cur2;
			k++;
		}

		return;
	}

};
