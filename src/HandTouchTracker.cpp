#include "HandTouchTracker.h"
#include <cmath>
#include <limits>

namespace {

// Shared by findFingertipNormalized() and computeHandOrientationAngle(): thresholds the
// mask and returns the largest external contour (assumed to be the hand - true for this
// rig, fixed overhead cameras over a static instrument panel with nothing else large
// enough to compete). Returns an empty contour if none is found or it's too small.
std::vector<cv::Point> findHandContour(const cv::Mat& alphaMask, int alphaThreshold,
                                        double minContourArea) {
    if (alphaMask.empty()) return {};

    cv::Mat mask8u;
    if (alphaMask.type() != CV_8UC1) {
        alphaMask.convertTo(mask8u, CV_8UC1);
    } else {
        mask8u = alphaMask;
    }

    cv::Mat binary;
    cv::threshold(mask8u, binary, alphaThreshold, 255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return {};

    size_t bestIdx = 0;
    double bestArea = 0.0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double area = cv::contourArea(contours[i]);
        if (area > bestArea) {
            bestArea = area;
            bestIdx = i;
        }
    }
    if (bestArea < minContourArea) return {};
    return contours[bestIdx];
}

// Shared by findFingertipNormalized() and computeHandDirectionAngle(): the contour point
// farthest from the edge the arm enters from (see HandEntryEdge).
cv::Point findExtremePoint(const std::vector<cv::Point>& contour, HandEntryEdge entryEdge) {
    cv::Point best = contour[0];
    for (const auto& pt : contour) {
        switch (entryEdge) {
            case HandEntryEdge::Bottom: if (pt.y < best.y) best = pt; break;  // topmost point
            case HandEntryEdge::Top:    if (pt.y > best.y) best = pt; break;  // bottommost point
            case HandEntryEdge::Left:   if (pt.x > best.x) best = pt; break;  // rightmost point
            case HandEntryEdge::Right:  if (pt.x < best.x) best = pt; break;  // leftmost point
        }
    }
    return best;
}

}  // namespace

cv::Point2f findFingertipNormalized(const cv::Mat& alphaMask, HandEntryEdge entryEdge,
                                     int alphaThreshold, double minContourArea) {
    if (alphaMask.empty()) return cv::Point2f(-1.0f, -1.0f);

    std::vector<cv::Point> contour = findHandContour(alphaMask, alphaThreshold, minContourArea);
    if (contour.empty()) return cv::Point2f(-1.0f, -1.0f);

    cv::Point best = findExtremePoint(contour, entryEdge);
    return cv::Point2f(
        static_cast<float>(best.x) / static_cast<float>(alphaMask.cols),
        static_cast<float>(best.y) / static_cast<float>(alphaMask.rows));
}

float computeHandDirectionAngle(const cv::Mat& alphaMask, HandEntryEdge entryEdge,
                                 int alphaThreshold, double minContourArea) {
    if (alphaMask.empty()) return std::numeric_limits<float>::quiet_NaN();

    std::vector<cv::Point> contour = findHandContour(alphaMask, alphaThreshold, minContourArea);
    if (contour.empty()) return std::numeric_limits<float>::quiet_NaN();

    cv::Moments m = cv::moments(contour);
    if (m.m00 <= 0.0) return std::numeric_limits<float>::quiet_NaN();
    cv::Point2f centroid(static_cast<float>(m.m10 / m.m00), static_cast<float>(m.m01 / m.m00));

    cv::Point fingertip = findExtremePoint(contour, entryEdge);
    return std::atan2(static_cast<float>(fingertip.y) - centroid.y,
                       static_cast<float>(fingertip.x) - centroid.x);
}

float computeHandOrientationAngle(const cv::Mat& alphaMask, int alphaThreshold,
                                   double minContourArea) {
    if (alphaMask.empty()) return std::numeric_limits<float>::quiet_NaN();

    std::vector<cv::Point> contour = findHandContour(alphaMask, alphaThreshold, minContourArea);
    // fitEllipse requires at least 5 points.
    if (contour.size() < 5) return std::numeric_limits<float>::quiet_NaN();

    cv::RotatedRect ellipse = cv::fitEllipse(contour);
    // OpenCV's RotatedRect::angle is in degrees, range [0, 180) - convert to radians for
    // consistency with the rest of this file.
    return ellipse.angle * static_cast<float>(CV_PI) / 180.0f;
}

float angleDeltaAxis(float fromAngle, float toAngle) {
    // Double both angles so the 180-degree-periodic axis becomes a normal 360-degree-
    // periodic direction, difference them with ordinary wraparound-safe subtraction, then
    // halve the result back into axis space - the standard trick for tracking an
    // undirected line's orientation continuously across its own wrap boundary.
    float delta = (toAngle - fromAngle) * 2.0f;
    while (delta > static_cast<float>(CV_PI)) delta -= 2.0f * static_cast<float>(CV_PI);
    while (delta <= -static_cast<float>(CV_PI)) delta += 2.0f * static_cast<float>(CV_PI);
    return delta * 0.5f;
}

float AxisAngleFilter::update(float rawAngle, float alpha) {
    float x = std::cos(2.0f * rawAngle);
    float y = std::sin(2.0f * rawAngle);
    if (!m_initialized) {
        m_x = x;
        m_y = y;
        m_initialized = true;
    } else {
        m_x = alpha * x + (1.0f - alpha) * m_x;
        m_y = alpha * y + (1.0f - alpha) * m_y;
    }
    return std::atan2(m_y, m_x) * 0.5f;
}

TouchMatchResult matchButton(const cv::Point2f& fingertipNorm, bool isLeftEye, int zone,
                              const std::vector<TouchButtonCalibration>& buttons,
                              float maxDistNorm) {
    TouchMatchResult result;
    result.fingertipXNorm = fingertipNorm.x;
    result.fingertipYNorm = fingertipNorm.y;
    result.isLeftEye = isLeftEye;
    if (fingertipNorm.x < 0.0f || fingertipNorm.y < 0.0f) return result;  // no fingertip found

    auto tryFind = [&](bool requireZoneMatch) -> int {
        int bestIdx = -1;
        float bestDist = std::numeric_limits<float>::max();
        for (size_t i = 0; i < buttons.size(); ++i) {
            const auto& b = buttons[i];
            if (b.isLeftEye != isLeftEye) continue;
            if (requireZoneMatch && zone >= 0 && b.zone >= 0 && b.zone != zone) continue;
            float dx = b.xNorm - fingertipNorm.x;
            float dy = b.yNorm - fingertipNorm.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = static_cast<int>(i);
            }
        }
        if (bestIdx >= 0 && bestDist <= maxDistNorm) return bestIdx;
        return -1;
    };

    int idx = tryFind(true);
    if (idx < 0) idx = tryFind(false);  // fall back to ignoring zone entirely
    if (idx < 0) return result;

    result.matched = true;
    result.buttonIndex = idx;
    result.buttonName = buttons[idx].name;
    return result;
}
