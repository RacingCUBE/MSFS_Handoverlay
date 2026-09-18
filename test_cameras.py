"""
Quick script to test which camera is which index.
Requires Python with opencv-python installed: pip install opencv-python
"""

import cv2

print("=== Camera Index Tester ===\n")

for i in range(5):  # Test indices 0-4
    print(f"Testing camera index {i}...", end=" ")
    cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)

    if cap.isOpened():
        ret, frame = cap.read()
        if ret:
            print("✓ WORKING")
            print(f"  Resolution: {int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))}x{int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))}")
            print(f"  FPS: {int(cap.get(cv2.CAP_PROP_FPS))}")

            # Show the camera feed
            cv2.imshow(f"Camera {i} - Press any key to continue", frame)
            print(f"  Showing preview... press any key to test next camera")
            cv2.waitKey(0)
            cv2.destroyAllWindows()
        else:
            print("✗ Opened but can't read frames")
        cap.release()
    else:
        print("✗ Not available")

print("\n=== Test Complete ===")
print("Now you know which camera is which index!")
print("Edit config/settings.ini accordingly.")
