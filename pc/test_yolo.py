"""
test_yolo.py — YOLO 检测测试脚本

功能：
    打开摄像头，实时运行 YOLOv8 目标检测，画面上叠加检测框

使用方法：
    python test_yolo.py

操作：
    按 ESC 退出

首次运行：
    会自动下载 yolov8n.pt 模型文件（约 6MB）
    如果下载慢/失败，手动下载放到 pc/ 目录下：
    https://github.com/ultralytics/assets/releases/download/v8.3.0/yolov8n.pt

说明：
    - 检测结果会标注在画面上（框 + 类别名 + 置信度）
    - 左上角显示当前画面检测到的物体数量
    - 这个脚本只做检测，不串口通信，用于验证 YOLO 是否正常工作
"""

import cv2
import os
from ultralytics import YOLO

# 获取脚本所在目录，确保模型文件路径正确（避免从其他目录运行时找不到文件）
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "yolov8n.pt")

# 只显示这些类别的检测结果（与 detect.py 一致）
# 不在这个表里的物体不会被框出来，减少视觉干扰
# 想看全部 80 类就把这个表注释掉
CATEGORY_MAP = {
    "bottle": 0,       # 瓶子 → A盒
    "cup": 0,          # 杯子 → A盒
    "cell phone": 1,   # 手机 → B盒
    "remote": 1,       # 遥控器 → B盒
    "apple": 2,        # 苹果 → C盒
    "banana": 2,       # 香蕉 → C盒
}


def main():
    # 加载 YOLOv8n 模型（n = nano，最小最快，适合实时检测）
    print("[INFO] 加载 YOLOv8n 模型...")
    model = YOLO(MODEL_PATH)
    print("[OK] 模型加载完成")

    # 摄像头设备编号：0=笔记本自带，1=iVCam手机摄像头
    cap = cv2.VideoCapture(1)
    if not cap.isOpened():
        print("[ERROR] 无法打开摄像头")
        return

    print("[OK] 开始实时检测，按 ESC 退出")

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        # YOLO 推理
        # conf=0.7 表示只显示置信度 >= 70% 的检测结果（越高越严格，减少误检）
        # verbose=False 不在控制台打印每帧的检测日志
        results = model(frame, conf=0.7, verbose=False)

        # 过滤：只保留 CATEGORY_MAP 里的物体，忽略其他
        filtered_boxes = []
        for box in results[0].boxes:
            cls_name = model.names[int(box.cls)]
            if cls_name in CATEGORY_MAP:
                filtered_boxes.append(box)

        # 在原图上画检测框（只画过滤后的）
        annotated = results[0].plot()
        if len(filtered_boxes) != len(results[0].boxes):
            # 如果有被过滤掉的，重新画一帧（只画保留的）
            annotated = frame.copy()
            for box in filtered_boxes:
                x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
                cls_name = model.names[int(box.cls)]
                conf_val = float(box.conf)
                cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 255, 0), 2)
                label = f"{cls_name} {conf_val:.2f}"
                cv2.putText(annotated, label, (x1, y1 - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

        # 左上角显示检测到的物体数量
        num_objects = len(filtered_boxes)
        cv2.putText(annotated, f"Objects: {num_objects}",
                    (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)

        cv2.imshow("YOLO Detection Test", annotated)

        if cv2.waitKey(1) & 0xFF == 27:  # ESC 退出
            break

    # 释放资源
    cap.release()
    cv2.destroyAllWindows()
    print("[OK] 检测结束")


if __name__ == "__main__":
    main()
