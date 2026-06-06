"""
collect_data.py — 数据集采集脚本

功能：
    从摄像头连续拍摄元器件照片，保存到 dataset/images/train/
    用于后续标注和训练 YOLO 模型

使用方法：
    python collect_data.py --cam 1                # 手机摄像头
    python collect_data.py --cam 0                # 笔记本摄像头
    python collect_data.py --cam 1 --save-dir dataset/images/val  # 保存到验证集

操作：
    按 空格  拍照保存（会自动连拍 5 张，间隔 0.5s）
    按 S     单张拍照
    按 ESC   退出

拍照技巧：
    1. 把元器件放在白色/浅色背景上（纸、鼠标垫）
    2. 从不同角度、距离各拍 20~30 张
    3. 变换光线条件（开灯/关灯/自然光）
    4. 三类元器件各拍至少 50 张
    5. 每张画面里可以放多个同类元器件
"""

import argparse
import cv2
import os
import time


def main():
    parser = argparse.ArgumentParser(description="元器件数据集采集")
    parser.add_argument("--cam", type=int, default=1, help="摄像头编号")
    parser.add_argument("--save-dir", default="dataset/images/train",
                        help="保存目录（默认训练集）")
    args = parser.parse_args()

    save_dir = args.save_dir
    os.makedirs(save_dir, exist_ok=True)

    # 统计已有图片数量，从这个编号继续
    existing = [f for f in os.listdir(save_dir) if f.endswith(('.jpg', '.png'))]
    count = len(existing)

    cap = cv2.VideoCapture(args.cam)
    if not cap.isOpened():
        print(f"[ERROR] 无法打开摄像头 (编号: {args.cam})")
        return

    print(f"[OK] 数据采集模式 | 保存到: {save_dir} | 已有: {count} 张")
    print("[操作] 空格=连拍5张 | S=单拍 | ESC=退出")

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                continue

            # 显示信息
            display = frame.copy()
            cv2.putText(display, f"Saved: {count} | SPACE=burst | S=single | ESC=quit",
                        (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
            cv2.imshow("Collect Data", display)

            key = cv2.waitKey(1) & 0xFF

            if key == 27:  # ESC
                break

            elif key == ord('s'):  # 单拍
                filename = f"{count:04d}.jpg"
                filepath = os.path.join(save_dir, filename)
                cv2.imwrite(filepath, frame)
                count += 1
                print(f"[SAVE] {filepath}")

            elif key == 32:  # 空格 — 连拍 5 张
                for i in range(5):
                    ret2, frame2 = cap.read()
                    if ret2:
                        filename = f"{count:04d}.jpg"
                        filepath = os.path.join(save_dir, filename)
                        cv2.imwrite(filepath, frame2)
                        count += 1
                        print(f"[SAVE] {filepath}")
                    time.sleep(0.5)

    except KeyboardInterrupt:
        print("\n[INFO] Ctrl+C 退出")

    finally:
        cap.release()
        cv2.destroyAllWindows()
        print(f"[OK] 采集完成，共 {count} 张图片")


if __name__ == "__main__":
    main()
