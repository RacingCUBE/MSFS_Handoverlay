#include "HandTouchTracker.h"
#include <cmath>
#include <limits>

cv::Point2f findFingertipNormalized(const cv::Mat& alphaMask, HandEntryEdge entryEdge,
                                     int alphaThreshold, double minContourArea) {
    if (alphaMask.empty()) return cv::Point2f(-1.0f, -1.0f);

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
    if (contours.empty()) return cv::Point2f(-1.0f, -1.0f);

    // Largest contour = the hand (assumes no other large moving object in frame, true for
    // this rig: fixed overhead cameras over a static instrument panel).
    size_t bestIdx = 0;
    double bestArea = 0.0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double area = cv::contourArea(contours[i]);
        if (area > bestArea) {
            bestArea = area;
            bestIdx = i;
        }
    }
    if (bestArea < minContourArea) return cv::Point2f(-1.0f, -1.0f);

    const auto& contour = contours[bestIdx];
    cv::Point best = contour[0];
    for (const auto& pt : contour) {
        switch (entryEdge) {
            case HandEntryEdge::Bottom: if (pt.y < best.y) best = pt; break;  // topmost point
            case HandEntryEdge::Top:    if (pt.y > best.y) best = pt; break;  // bottommost point
            case HandEntryEdge::Left:   if (pt.x > best.x) best = pt; break;  // rightmost point
            case HandEntryEdge::Right:  if (pt.x < best.x) best = pt; break;  // leftmost point
        }
    }

    return cv::Point2f(
        static_cast<float>(best.x) / static_cast<float>(mask8u.cols),
        static_cast<float>(best.y) / static_cast<float>(mask8u.rows));
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
