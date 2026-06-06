"""
annotate.py — 专用元器件标注工具（替代 labelImg）

使用方法：
    python annotate.py --dir dataset/images/train --save dataset/labels/train
    python annotate.py --dir dataset/images/val --save dataset/labels/val

操作：
    鼠标拖拽    画框（框住元器件）
    1 / 2 / 3   选择类别（1=电阻  2=LED  3=电容）
    Z           撤销最后一个框
    S           保存当前图片的标注
    D / 空格    下一张图
    A           上一张图
    ESC         退出

当前类别显示在画面左上角，画框前先按 1/2/3 选择。
"""

import argparse
import cv2
import os
import numpy as np

CLASSES = ["resistor", "led", "capacitor"]
CLASS_COLORS = [
    (0, 165, 255),   # 电阻 — 橙色
    (255, 0, 255),   # LED — 紫色
    (0, 255, 0),     # 电容 — 绿色
]

# 全局状态
drawing = False
start_point = None
end_point = None
current_class = 0  # 默认选电阻
boxes = []  # [(x1,y1,x2,y2, class_id), ...]


def mouse_callback(event, x, y, flags, param):
    global drawing, start_point, end_point, boxes

    if event == cv2.EVENT_LBUTTONDOWN:
        drawing = True
        start_point = (x, y)
        end_point = (x, y)

    elif event == cv2.EVENT_MOUSEMOVE:
        if drawing:
            end_point = (x, y)

    elif event == cv2.EVENT_LBUTTONUP:
        drawing = False
        end_point = (x, y)
        if start_point and end_point:
            x1, y1 = start_point
            x2, y2 = end_point
            # 确保左上角在右下角前面
            x1, x2 = min(x1, x2), max(x1, x2)
            y1, y2 = min(y1, y2), max(y1, y2)
            # 最小框 10x10 像素
            if abs(x2 - x1) > 10 and abs(y2 - y1) > 10:
                boxes.append((x1, y1, x2, y2, current_class))
                print(f"  + {CLASSES[current_class]} ({x1},{y1})-({x2},{y2})")


def draw_boxes(img, boxes, temp_box=None):
    """在图片上画所有标注框"""
    display = img.copy()
    for (x1, y1, x2, y2, cls) in boxes:
        color = CLASS_COLORS[cls]
        cv2.rectangle(display, (x1, y1), (x2, y2), color, 2)
        label = CLASSES[cls]
        cv2.putText(display, label, (x1, y1 - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)
    # 画临时框（正在拖拽的）
    if temp_box:
        x1, y1, x2, y2 = temp_box
        cv2.rectangle(display, (x1, y1), (x2, y2), (255, 255, 255), 1)
    return display


def load_labels(label_path, img_w, img_h):
    """加载已有的 YOLO 标注文件"""
    boxes = []
    if not os.path.exists(label_path):
        return boxes
    with open(label_path, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 5:
                cls = int(parts[0])
                cx, cy, w, h = float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])
                x1 = int((cx - w/2) * img_w)
                y1 = int((cy - h/2) * img_h)
                x2 = int((cx + w/2) * img_w)
                y2 = int((cy + h/2) * img_h)
                boxes.append((x1, y1, x2, y2, cls))
    return boxes


def save_labels(label_path, boxes, img_w, img_h):
    """保存标注为 YOLO 格式"""
    with open(label_path, 'w') as f:
        for (x1, y1, x2, y2, cls) in boxes:
            cx = ((x1 + x2) / 2) / img_w
            cy = ((y1 + y2) / 2) / img_h
            w = (x2 - x1) / img_w
            h = (y2 - y1) / img_h
            f.write(f"{cls} {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}\n")
    print(f"  [SAVED] {label_path} ({len(boxes)} boxes)")


def draw_hud(display, idx, total, filename):
    """画 HUD 信息"""
    # 顶部：类别选择
    hud_y = 25
    for i, cls_name in enumerate(CLASSES):
        color = CLASS_COLORS[i] if i == current_class else (128, 128, 128)
        prefix = "→" if i == current_class else " "
        text = f"{prefix} [{i+1}] {cls_name}"
        cv2.putText(display, text, (10, hud_y + i * 25),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
    # 底部：操作提示 + 进度
    h = display.shape[0]
    cv2.putText(display, f"[{idx+1}/{total}] {filename}",
                (10, h - 45), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)
    cv2.putText(display, "D=next  A=prev  Z=undo  S=save  ESC=quit",
                (10, h - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (200, 200, 200), 1)
    return display


def main():
    global boxes, current_class, drawing, start_point, end_point

    parser = argparse.ArgumentParser(description="元器件标注工具")
    parser.add_argument("--dir", default="dataset/images/train", help="图片目录")
    parser.add_argument("--save", default="dataset/labels/train", help="标注保存目录")
    args = parser.parse_args()

    img_dir = args.dir
    save_dir = args.save
    os.makedirs(save_dir, exist_ok=True)

    images = sorted([f for f in os.listdir(img_dir) if f.endswith(('.jpg', '.png'))])
    if not images:
        print(f"[ERROR] {img_dir} 中没有图片")
        return

    print(f"[OK] 标注工具 | 图片: {img_dir} ({len(images)} 张) | 保存: {save_dir}")
    print("[操作] 1/2/3=选类别 | 拖拽=画框 | Z=撤销 | S=保存 | D=下一张 | A=上一张 | ESC=退出")

    win_name = "Annotate"
    cv2.namedWindow(win_name, cv2.WINDOW_NORMAL)
    cv2.setMouseCallback(win_name, mouse_callback)

    idx = 0
    while 0 <= idx < len(images):
        filename = images[idx]
        img_path = os.path.join(img_dir, filename)
        img = cv2.imread(img_path)
        if img is None:
            print(f"[WARN] 无法读取 {filename}，跳过")
            idx += 1
            continue

        h, w = img.shape[:2]

        # 加载已有标注（如果存在）
        label_name = os.path.splitext(filename)[0] + ".txt"
        label_path = os.path.join(save_dir, label_name)
        boxes = load_labels(label_path, w, h)

        if boxes:
            print(f"[{idx+1}/{len(images)}] {filename} (已有 {len(boxes)} 个框)")

        while True:
            # 构建临时框
            temp_box = None
            if drawing and start_point and end_point:
                temp_box = (start_point[0], start_point[1], end_point[0], end_point[1])

            display = draw_boxes(img, boxes, temp_box)
            display = draw_hud(display, idx, len(images), filename)
            cv2.imshow(win_name, display)

            key = cv2.waitKey(16) & 0xFF  # ~60fps

            if key == 27:  # ESC — 退出前自动保存
                if boxes:
                    save_labels(label_path, boxes, w, h)
                print("[OK] 退出")
                cv2.destroyAllWindows()
                return

            elif key == ord('1'):
                current_class = 0
                print("  → 类别: resistor (电阻)")
            elif key == ord('2'):
                current_class = 1
                print("  → 类别: led (LED)")
            elif key == ord('3'):
                current_class = 2
                print("  → 类别: capacitor (电容)")

            elif key == ord('z'):  # 撤销
                if boxes:
                    removed = boxes.pop()
                    print(f"  - 撤销: {CLASSES[removed[4]]}")

            elif key == ord('s'):  # 保存
                save_labels(label_path, boxes, w, h)

            elif key == ord('d') or key == 32:  # 下一张（自动保存）
                save_labels(label_path, boxes, w, h)
                idx += 1
                break

            elif key == ord('a'):  # 上一张
                if boxes:
                    save_labels(label_path, boxes, w, h)
                idx = max(0, idx - 1)
                break

    print(f"[OK] 全部 {len(images)} 张标注完成！")
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
