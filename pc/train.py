"""
train.py — YOLOv8 自定义模型训练脚本

功能：
    基于 YOLOv8n 预训练模型，在标注好的元器件数据集上微调

使用方法：
    1. 确保已完成数据采集和标注（见下方流程）
    2. 运行：python train.py
    3. 训练完成后模型保存在 runs/detect/train/weights/best.pt

参数：
    --epochs    训练轮数（默认 50，数据少时用 100）
    --batch     批大小（默认 16，显存不够改 8）
    --img       输入图片尺寸（默认 640）
    --data      数据集配置文件（默认 data.yaml）
"""

import argparse
from ultralytics import YOLO


def main():
    parser = argparse.ArgumentParser(description="训练自定义 YOLOv8 模型")
    parser.add_argument("--epochs", type=int, default=50, help="训练轮数")
    parser.add_argument("--batch", type=int, default=16, help="批大小")
    parser.add_argument("--img", type=int, default=640, help="输入图片尺寸")
    parser.add_argument("--data", default="data.yaml", help="数据集配置文件")
    args = parser.parse_args()

    # 加载预训练模型（自动下载 yolov8n.pt）
    model = YOLO("yolov8n.pt")

    # 开始训练
    results = model.train(
        data=args.data,       # 数据集配置
        epochs=args.epochs,   # 训练轮数
        batch=args.batch,     # 批大小
        imgsz=args.img,       # 图片尺寸
        device="cpu",         # 用 CPU 训练（没有 GPU 也能跑，只是慢一些）
        project="runs/detect",
        name="train",
        exist_ok=True,
        pretrained=True,      # 使用预训练权重（微调）
        optimizer="auto",     # 自动选择优化器
        verbose=True,
    )

    print("\n" + "=" * 50)
    print("[OK] 训练完成！")
    print(f"  最佳模型: runs/detect/train/weights/best.pt")
    print(f"  复制到 pc/ 目录使用:")
    print(f"    copy runs\\detect\\train\\weights\\best.pt custom_electronics.pt")
    print("=" * 50)


if __name__ == "__main__":
    main()
