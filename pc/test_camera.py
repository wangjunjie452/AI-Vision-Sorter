"""
test_camera.py — 摄像头测试脚本

功能：
    打开笔记本摄像头，实时显示画面，验证摄像头是否正常工作

使用方法：
    python test_camera.py

操作：
    按 ESC 退出
    按 S 截图保存为 screenshot.jpg

常见问题：
    - "无法打开摄像头" → 检查摄像头是否被其他程序占用
    - 画面黑屏 → 可能是摄像头编号问题，把 VideoCapture(0) 改成 VideoCapture(1)
"""

import cv2


def main():
    # 摄像头设备编号：
    #   0 = 笔记本内置摄像头
    #   1 = iVCam 手机摄像头（手机装 iVCam App，连同一个 WiFi/热点后自动出现）
    # 如果想切回笔记本摄像头，改成 VideoCapture(0)
    cap = cv2.VideoCapture(1)
    if not cap.isOpened():
        print("[ERROR] 无法打开摄像头，请检查设备连接")
        return

    print("[OK] 摄像头已打开，按 ESC 退出，按 S 截图")

    while True:
        ret, frame = cap.read()
        if not ret:
            print("[ERROR] 无法读取画面")
            break

        # 在画面上显示操作提示
        cv2.putText(frame, "Press ESC to quit | S to save",
                    (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        cv2.imshow("Camera Test", frame)

        # 按键检测（OpenCV 需要用 waitKey 获取按键）
        key = cv2.waitKey(1) & 0xFF
        if key == 27:   # 27 = ESC 键的 ASCII 码
            break
        elif key == ord('s'):
            cv2.imwrite("screenshot.jpg", frame)
            print("[OK] 截图已保存为 screenshot.jpg")

    # 释放资源
    cap.release()
    cv2.destroyAllWindows()
    print("[OK] 摄像头已关闭")


if __name__ == "__main__":
    main()
