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

// Orientation angle of the hand contour's major axis, in radians, using an ellipse fit -
// a stand-in for "which way the hand/wrist is twisted", used to track dial rotation by
// hand twist rather than by the fingertip's position. A twisting hand doesn't necessarily
// sweep its fingertip through a wide arc, especially over a small knob, so tracking the
// fingertip's angle around a fixed pivot misses a lot of real turns - the wrist's own
// rotation is the more reliable signal. Returns NAN if no contour large enough to
// plausibly be a hand was found, or if the contour has too few points to fit an ellipse.
//
// IMPORTANT: an ellipse's major axis is a LINE, not a direction - a hand oriented at 10
// degrees looks identical to one at 190 degrees to this fit. The angle returned here is
// therefore only meaningful modulo 180 degrees (pi radians); track it frame-to-frame with
// angleDeltaAxis(), not a plain subtraction, since a naive difference would see a spurious
// ~180 degree jump every time the true angle crosses that wrap boundary mid-rotation.
float computeHandOrientationAngle(const cv::Mat& alphaMask, int alphaThreshold = 32,
                                   double minContourArea = 200.0);

// Signed angular difference from fromAngle to toAngle for a 180-degree-periodic axis angle
// (see computeHandOrientationAngle), wrapped to (-pi/2, pi/2]. Uses the standard "double
// the angle, difference, halve it" trick so a rotation crossing the axis's own wrap
// boundary doesn't look like a sudden reversal. Sign convention (which direction comes out
// positive) hasn't been verified against real footage yet - confirm it once hardware
// exists, the same way the VR overlay's parallax direction was confirmed with real
// instrumented data rather than assumed (see project notes on that fix).
float angleDeltaAxis(float fromAngle, float toAngle);

// Direction angle (radians, standard atan2 range (-pi, pi]) from the hand contour's
// centroid toward its fingertip - a genuine 360-degree direction, unlike
// computeHandOrientationAngle()'s 180-degree-periodic axis. Meant as a live, intuitive
// "which way is the hand pointing" readout (naturally reads across a full +-180 degree
// range as a hand turns, with no wraparound ambiguity to explain). NOT used for the actual
// dial-rotation tracking in main.cpp, which uses the axis angle instead - a
// centroid-to-fingertip vector can jump around more if segmentation flickers between which
// contour point gets picked as "the fingertip", especially with multiple fingers visible.
// NAN if no contour large enough to be a hand was found.
float computeHandDirectionAngle(const cv::Mat& alphaMask, HandEntryEdge entryEdge,
                                 int alphaThreshold = 32, double minContourArea = 200.0);

// Wrap-aware exponential smoothing filter for a 180-degree-periodic axis angle (see
// computeHandOrientationAngle). Filters via the same "double the angle" vector trick used
// by angleDeltaAxis(), averaging on the unit circle in doubled-angle space rather than the
// raw angle directly - a plain average breaks near the axis's own wrap boundary (e.g.
// naively averaging 179 and 1 degrees should read ~0/180, not ~90).
class AxisAngleFilter {
public:
    // Discards accumulated state - call whenever tracking (re)starts (e.g. a new dial drag
    // begins, or the hand just re-entered frame after being absent) so a stale angle from
    // an unrelated moment doesn't bias the first few filtered samples.
    void reset() { m_initialized = false; }

    // Feeds one new raw angle sample (radians) and returns the filtered angle (radians).
    // alpha in (0, 1]: weight given to the newest sample - 1.0 is no smoothing at all,
    // smaller values smooth more but add more lag. Passed per-call (not fixed at
    // construction) so it can be a live-tunable UI setting.
    float update(float rawAngle, float alpha);

private:
    bool m_initialized = false;
    float m_x = 1.0f;
    float m_y = 0.0f;
};

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
