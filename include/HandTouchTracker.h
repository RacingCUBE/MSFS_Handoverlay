#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "Config.h"  // TouchButtonCalibration

// Which side of the camera frame a hand/arm enters from - needed to tell "the fingertip"
// apart from "the wrist/forearm" using only a silhouette, since a plain contour centroid
// or bounding-box center can't distinguish them (that was the coarse approach flagged as
// insufficient during design - see findFingertipNormalized). Fixed per camera rig geometry,
// not something that changes per frame.
enum class HandEntryEdge {
    Bottom = 0,  // Arm enters from the bottom of the frame -> fingertip is the topmost contour point
    Top,         // -> fingertip is the bottommost point
    Left,        // -> fingertip is the rightmost point
    Right        // -> fingertip is the leftmost point
};

// Finds the fingertip position within one eye's segmentation alpha mask, taken as the point
// on the largest hand contour farthest from the edge the arm enters from. This is a coarse
// stand-in for real fingertip/keypoint detection, but it's cheap and works well enough here
// because the touch event already confirms contact happened - this only needs to say
// roughly *where*, not classify hand pose or find every fingertip on the hand.
//
// Returns a normalized (0-1, 0-1) position with (0,0) at the mask's top-left corner, or
// (-1,-1) if no contour large enough to plausibly be a hand was found (e.g. the hand hadn't
// entered frame yet when the touch event arrived, or segmentation lost track that frame).
cv::Point2f findFingertipNormalized(const cv::Mat& alphaMask, HandEntryEdge entryEdge,
                                     int alphaThreshold = 32, double minContourArea = 200.0);

// Result of matching a detected fingertip position against the calibrated button table.
struct TouchMatchResult {
    bool matched = false;
    std::string buttonName;
    int buttonIndex = -1;       // Index into the calibration vector (for UI highlighting)
    float fingertipXNorm = -1.0f;
    float fingertipYNorm = -1.0f;
    bool isLeftEye = true;
};

// Finds the calibrated button nearest the given normalized fingertip position, restricted to
// the same eye. If any calibrated buttons declare a zone >= 0, buttons matching the touch
// event's zone are preferred first; if none of those are within maxDistNorm, the search
// falls back to all buttons on that eye regardless of zone - a raw capacitive zone reading
// is coarse, and it's better to still attempt a match than refuse just because zone
// bookkeeping doesn't line up perfectly. Rejects matches farther than maxDistNorm (in
// normalized 0-1 units) since a fingertip nowhere near any known button is more likely
// mis-segmentation than a real new, uncalibrated button.
TouchMatchResult matchButton(const cv::Point2f& fingertipNorm, bool isLeftEye, int zone,
                              const std::vector<TouchButtonCalibration>& buttons,
                              float maxDistNorm = 0.15f);
