"""
color_detect.py — 基于颜色的电子元器件检测（替代 YOLO）

原理：
    摄像头采集 → HSV 颜色分割 → 轮廓检测 → 按颜色分类元器件
    → 通过串口发送分拣指令给 STM32

颜色分类（默认值，可通过 --calibrate 模式现场校准）：
    A类 (0): 电阻 — 棕/橙色系（tan/brown body）
    B类 (1): LED  — 彩色高亮（红/绿/蓝/黄）
    C类 (2): 电容 — 银灰/深色（metallic/dark）

使用方法：
    python color_detect.py --port COM6 --cam 1             # 直接运行
    python color_detect.py --port COM6 --cam 1 --calibrate # 校准模式（显示HSV范围）
    python color_detect.py --port COM6 --cam 0             # 笔记本摄像头

按 ESC 或 Ctrl+C 退出
"""

import argparse
import cv2
import numpy as np
import os
import time
from serial_comm import SerialComm


# ======================== HSV 颜色范围配置 ========================
# 每个元器件类型对应多个 HSV 范围（因为同类元器件可能有多种颜色）
# 格式: {"name": ..., "category": 0/1/2, "ranges": [(H_min,S_min,V_min, H_max,S_max,V_max), ...]}
# H: 0-179, S: 0-255, V: 0-255 (OpenCV HSV)
#
# 使用 --calibrate 模式可以在现场调整这些值

DEFAULT_COMPONENTS = [
    {
        "name": "电阻",
        "category": 0,
        "color_label": (0, 165, 255),  # 橙色画框
        "ranges": [
            (5, 80, 80, 25, 255, 255),      # 棕/橙色
            (10, 50, 100, 30, 200, 255),     # 浅棕色
        ],
    },
    {
        "name": "LED",
        "category": 1,
        "color_label": (255, 0, 255),  # 紫色画框
        "ranges": [
            (0, 150, 150, 10, 255, 255),     # 红色 LED
            (170, 150, 150, 179, 255, 255),  # 红色 LED (H wrap)
            (35, 100, 100, 85, 255, 255),    # 绿色 LED
            (100, 100, 100, 130, 255, 255),  # 蓝色 LED
            (20, 100, 150, 35, 255, 255),    # 黄色 LED
        ],
    },
    {
        "name": "电容",
        "category": 2,
        "color_label": (255, 255, 0),  # 青色画框
        "ranges": [
            (0, 0, 100, 179, 40, 200),       # 银灰/浅色
            (0, 0, 30, 179, 80, 120),        # 深灰/黑色
        ],
    },
]


class ColorDetector:
    """
    HSV 颜色检测器 + 串口通信封装

    状态机与 detect.py 一致：
        IDLE → 发送 CMD → WAIT_ACK → 收到 ACK → WAIT_READY → 收到 READY → IDLE
    """

    STATE_IDLE = "IDLE"
    STATE_WAIT_ACK = "WAIT_ACK"
    STATE_WAIT_READY = "WAIT_READY"

    ACK_TIMEOUT = 2.0
    READY_TIMEOUT = 5.0
    # 最小轮廓面积（像素²），过滤噪声
    MIN_CONTOUR_AREA = 500
    # 同一类别的检测间隔（秒），避免同一物体重复触发
    COOLDOWN = 1.0

    def __init__(self, port="COM6", components=None):
        self.serial = SerialComm(port)
        self.components = components or DEFAULT_COMPONENTS
        self.state = self.STATE_IDLE
        self._cmd_sent_time = 0.0
        self._last_category = -1
        self._last_detect_time = 0.0

    def start(self):
        self.serial.connect()
        time.sleep(0.5)
        resp = self.serial.read_response()
        if resp == "READY":
            print(f"[INFO] STM32 就绪")
        self.state = self.STATE_IDLE

    def stop(self):
        self.serial.disconnect()

    def classify_contour(self, hsv_roi):
        """根据 HSV 直方图判断轮廓属于哪类元器件"""
        best_match = None
        best_score = 0

        for comp in self.components:
            total_pixels = 0
            mask_sum = np.zeros(hsv_roi.shape[:2], dtype=np.uint8)
            for (h_min, s_min, v_min, h_max, s_max, v_max) in comp["ranges"]:
                lower = np.array([h_min, s_min, v_min])
                upper = np.array([h_max, s_max, v_max])
                mask = cv2.inRange(hsv_roi, lower, upper)
                mask_sum = cv2.bitwise_or(mask_sum, mask)
            total_pixels = cv2.countNonZero(mask_sum)
            roi_area = hsv_roi.shape[0] * hsv_roi.shape[1]
            if roi_area > 0:
                ratio = total_pixels / roi_area
                if ratio > best_score:
                    best_score = ratio
                    best_match = comp

        # 至少 30% 的像素匹配才算
        if best_match and best_score > 0.3:
            return best_match, best_score
        return None, 0

    def detect_components(self, frame):
        """
        检测画面中的电子元器件

        返回:
            annotated: 画了检测框的图像
            detections: [{"class": str, "category": int, "center": (x,y), "area": int}, ...]
        """
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        # 高斯模糊减少噪声
        hsv_blur = cv2.GaussianBlur(hsv, (5, 5), 0)

        # 合并所有颜色范围的 mask
        combined_mask = np.zeros(hsv.shape[:2], dtype=np.uint8)
        for comp in self.components:
            for (h_min, s_min, v_min, h_max, s_max, v_max) in comp["ranges"]:
                lower = np.array([h_min, s_min, v_min])
                upper = np.array([h_max, s_max, v_max])
                mask = cv2.inRange(hsv_blur, lower, upper)
                combined_mask = cv2.bitwise_or(combined_mask, mask)

        # 形态学操作：去噪 + 填充空洞
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
        combined_mask = cv2.morphologyEx(combined_mask, cv2.MORPH_CLOSE, kernel)
        combined_mask = cv2.morphologyEx(combined_mask, cv2.MORPH_OPEN, kernel)

        # 查找轮廓
        contours, _ = cv2.findContours(combined_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        annotated = frame.copy()
        detections = []

        for cnt in contours:
            area = cv2.contourArea(cnt)
            if area < self.MIN_CONTOUR_AREA:
                continue

            x, y, w, h = cv2.boundingRect(cnt)
            # 提取 ROI 的 HSV 区域进行分类
            roi_hsv = hsv_blur[y:y+h, x:x+w]
            comp, score = self.classify_contour(roi_hsv)

            if comp:
                cx, cy = x + w // 2, y + h // 2
                color = comp["color_label"]
                cv2.rectangle(annotated, (x, y), (x + w, y + h), color, 2)
                label = f"{comp['name']} ({score:.0%})"
                cv2.putText(annotated, label, (x, y - 8),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
                detections.append({
                    "class": comp["name"],
                    "category": comp["category"],
                    "center": (cx, cy),
                    "area": area,
                    "score": score,
                })

        # 按面积从大到小排序（优先处理最近/最大的物体）
        detections.sort(key=lambda d: d["area"], reverse=True)
        return annotated, detections

    def _process_stm32_response(self):
        resp = self.serial.read_response()
        if not resp:
            return None
        print(f"[RX] {resp}")

        if resp.startswith("ACK"):
            if self.state == self.STATE_WAIT_ACK:
                self.state = self.STATE_WAIT_READY
        elif resp == "READY":
            self.state = self.STATE_IDLE
        elif resp == "PONG":
            pass
        elif resp == "ERR":
            print("[ERROR] STM32 报告协议解析错误")
            self.state = self.STATE_IDLE
        return resp

    def _check_timeout(self):
        now = time.time()
        if self.state == self.STATE_WAIT_ACK:
            if now - self._cmd_sent_time > self.ACK_TIMEOUT:
                print("[WARN] ACK 超时 → 回到 IDLE")
                self.state = self.STATE_IDLE
        elif self.state == self.STATE_WAIT_READY:
            if now - self._cmd_sent_time > self.READY_TIMEOUT:
                print("[WARN] READY 超时 → 回到 IDLE")
                self.state = self.STATE_IDLE

    def run(self, camera=1, show_window=True, calibrate=False):
        """
        主循环

        参数：
            camera:      摄像头编号
            show_window: 是否显示 OpenCV 窗口
            calibrate:   校准模式 — 显示 HSV 数值，不发送串口指令
        """
        self.start()
        cap = cv2.VideoCapture(camera)
        if not cap.isOpened():
            print(f"[ERROR] 无法打开摄像头 (编号: {camera})")
            return

        if not calibrate and not self.serial.connected:
            print("[ERROR] 串口未连接，请检查端口号和连线")
            cap.release()
            return

        mode_str = "校准模式（不会发送指令）" if calibrate else "运行模式"
        print(f"[OK] 系统运行中 | {mode_str} | 串口: {self.serial.port} | ESC 退出")

        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    continue

                # 检测元器件
                annotated, detections = self.detect_components(frame)

                if calibrate:
                    # 校准模式：显示 HSV 信息，不发送指令
                    cv2.putText(annotated, "CALIBRATE MODE - no commands sent",
                                (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
                    # 显示鼠标位置的 HSV 值（通过回调设置）
                    if detections:
                        d = detections[0]
                        info = f"{d['class']} cat={d['category']} area={d['area']} score={d['score']:.0%}"
                        cv2.putText(annotated, info,
                                    (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
                else:
                    # 正常模式：状态机驱动
                    resp = self._process_stm32_response()
                    self._check_timeout()

                    if self.state == self.STATE_IDLE and detections:
                        d = detections[0]
                        # 冷却：同类物体短时间内不重复发送
                        now = time.time()
                        if (d["category"] != self._last_category or
                                now - self._last_detect_time > self.COOLDOWN):
                            cmd_str = f"CMD:{d['category']},{d['center'][0]},{d['center'][1]}"
                            print(f"[TX] {cmd_str}  ({d['class']} score={d['score']:.0%})")
                            self.serial.send_command(d["category"],
                                                     d["center"][0], d["center"][1])
                            self.state = self.STATE_WAIT_ACK
                            self._cmd_sent_time = time.time()
                            self._last_category = d["category"]
                            self._last_detect_time = now
                            cv2.putText(annotated, f"Sent: {d['class']}({d['category']})",
                                        (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)

                    cv2.putText(annotated, f"State: {self.state}",
                                (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                if show_window:
                    cv2.imshow("Component Detector", annotated)
                    if cv2.waitKey(1) & 0xFF == 27:
                        break

        except KeyboardInterrupt:
            print("\n[INFO] Ctrl+C 退出")

        finally:
            cap.release()
            if show_window:
                cv2.destroyAllWindows()
            if not calibrate:
                self.stop()
            print("[OK] 系统已停止")


def main():
    parser = argparse.ArgumentParser(description="AI 视觉分拣 — 颜色检测模式")
    parser.add_argument("--port", default="COM6", help="串口号（默认 COM6）")
    parser.add_argument("--cam", type=int, default=1, help="摄像头编号（0=笔记本, 1=手机）")
    parser.add_argument("--no-show", action="store_true", help="不显示画面")
    parser.add_argument("--calibrate", action="store_true",
                        help="校准模式：只显示检测结果，不发送串口指令")
    args = parser.parse_args()

    detector = ColorDetector(port=args.port)
    detector.run(camera=args.cam, show_window=not args.no_show, calibrate=args.calibrate)


if __name__ == "__main__":
    main()
