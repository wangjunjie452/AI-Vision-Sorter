"""
detect.py — AI 视觉分拣系统 PC 端主程序

功能概述：
    摄像头实时采集画面 → YOLOv8 目标检测 → 提取目标类别和坐标
    → 通过串口发送分拣指令给 STM32 → STM32 控制舵机完成分拣

状态机（与 STM32 同步）：
    IDLE → 发送 CMD → WAIT_ACK → 收到 ACK → WAIT_READY → 收到 READY → IDLE
    超时 5s 未收到 READY → 自动回到 IDLE（防止死锁）

使用方法：
    python detect.py                          # 默认 COM6，摄像头 1
    python detect.py --port COM3              # 指定串口
    python detect.py --port COM3 --cam 0      # 指定串口 + 笔记本摄像头
    python detect.py --port COM3 --no-show    # 不显示画面（无 GUI 环境）

按 ESC 或 Ctrl+C 退出

依赖：
    pip install ultralytics opencv-python pyserial

通信协议：
    PC → STM32:  "CMD:类别ID,X,Y\n"   "PING\n"（心跳，每5s）
    STM32 → PC:  "ACK:类别ID\n"  →  舵机动 作  →  "READY\n"
"""

import argparse
import cv2
import os
from ultralytics import YOLO
from serial_comm import SerialComm

# 获取脚本所在目录，确保模型文件路径正确
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "custom_electronics.pt")

# ======================== 分拣类别映射表（核心配置） ========================
# 键 = 自定义 YOLO 模型识别的物体名称（resistor/led/capacitor）
# 值 = 对应的分拣类别（0=A盒, 1=B盒, 2=C盒）
# 不在表里的物体会被忽略（category_id = -1，不发送指令）
CATEGORY_MAP = {
    "resistor": 0,     # 电阻 → A类
    "led": 1,          # LED  → B类
    "capacitor": 2,    # 电容 → C类
}


class Detector:
    """
    YOLO 检测器 + 串口通信封装

    状态机确保 PC 与 STM32 同步：
        IDLE        → 可以检测 + 发送 CMD
        WAIT_ACK    → 等待 STM32 回复 ACK（已收到指令）
        WAIT_READY  → 等待 STM32 回复 READY（舵机动作完成）
    """

    # 状态常量
    STATE_IDLE = "IDLE"
    STATE_WAIT_ACK = "WAIT_ACK"
    STATE_WAIT_READY = "WAIT_READY"

    # ACK 超时（秒）— STM32 收到 CMD 后应立即回复 ACK
    ACK_TIMEOUT = 2.0
    # READY 超时（秒）— 最长舵机序列为 C 类 3.3s，留余量
    READY_TIMEOUT = 5.0

    # 同一类别分拣后的冷却时间（秒）
    # 冷却期内同一类别的检测会被跳过，防止同一物体反复触发
    SORT_COOLDOWN = 5.0

    def __init__(self, model_path=MODEL_PATH, conf=0.3, port="COM3"):
        """
        初始化检测器

        参数：
            model_path: YOLO 模型文件路径
            conf:       检测置信度阈值（0.3 = 较灵敏，适合小元器件）
            port:       串口号
        """
        self.model = YOLO(model_path)  # 加载 YOLO 模型
        self.conf = conf                # 置信度阈值
        self.serial = SerialComm(port)  # 串口通信对象

        # 状态机
        self.state = self.STATE_IDLE
        self._cmd_sent_time = 0.0       # 发送 CMD 的时间戳（用于超时检测）

        # 防重复：记录每个类别上次分拣的时间戳
        self._cooldown_until = {}       # {category_id: timestamp}

    def start(self):
        """打开串口连接，消耗 STM32 启动时发送的 READY"""
        self.serial.connect()
        self._flush_initial_ready()
        self.state = self.STATE_IDLE

    def stop(self):
        """关闭串口连接"""
        self.serial.disconnect()

    def _flush_initial_ready(self):
        """
        消耗 STM32 上电时发送的 "READY\n"（main.c 第 59 行）
        避免状态机被启动信号干扰
        """
        import time
        time.sleep(0.5)  # 等待 STM32 启动
        resp = self.serial.read_response()
        if resp == "READY":
            print(f"[INFO] STM32 就绪 (got: {resp})")

    def process_frame(self, frame):
        """
        处理一帧图像：YOLO 推理 + 提取检测结果

        参数：
            frame: OpenCV 图像（BGR 格式）
        返回：
            (annotated, detections)
            annotated:  画了检测框的图像
            detections: 检测结果列表，每个元素是 dict：
                {
                    "class": "bottle",        # 物体名称
                    "category_id": 0,         # 分拣类别（0/1/2/-1）
                    "confidence": 0.85,       # 置信度（0~1）
                    "center": (160, 120),     # 检测框中心坐标 (x, y)
                }
        """
        # YOLO 推理，verbose=False 不打印日志
        results = self.model(frame, conf=self.conf, verbose=False)
        annotated = results[0].plot()  # 在图像上画检测框

        detections = []
        for box in results[0].boxes:
            # 获取物体名称（如 "bottle"）
            cls_name = self.model.names[int(box.cls)]
            # 获取置信度
            conf = float(box.conf)
            # 获取检测框坐标 [x1, y1, x2, y2]，计算中心点
            x1, y1, x2, y2 = box.xyxy[0].tolist()
            cx = int((x1 + x2) / 2)
            cy = int((y1 + y2) / 2)

            detections.append({
                "class": cls_name,
                "category_id": CATEGORY_MAP.get(cls_name, -1),  # 查映射表，不在表中返回 -1
                "confidence": conf,
                "center": (cx, cy),
            })

        return annotated, detections

    def send_sort_command(self, detection):
        """
        将检测结果通过串口发送给 STM32，并进入等待+冷却状态

        发送格式："CMD:类别ID,X坐标,Y坐标\n"
        例：检测到电阻 → "CMD:0,160,120\n"
        """
        import time
        cat_id = detection["category_id"]
        cx, cy = detection["center"]
        cmd_str = f"CMD:{cat_id},{cx},{cy}"
        print(f"[TX] {cmd_str}  ({detection['class']} conf={detection['confidence']:.2f})")
        self.serial.send_command(cat_id, cx, cy)
        self.state = self.STATE_WAIT_ACK
        self._cmd_sent_time = time.time()
        # 记录冷却：该类别在 N 秒内不会再触发
        self._cooldown_until[cat_id] = time.time() + self.SORT_COOLDOWN

    def _check_timeout(self):
        """检查是否超时，超时则回到 IDLE"""
        import time
        now = time.time()
        if self.state == self.STATE_WAIT_ACK:
            if now - self._cmd_sent_time > self.ACK_TIMEOUT:
                print("[WARN] ACK 超时，STM32 可能未收到指令 → 回到 IDLE")
                self.state = self.STATE_IDLE
        elif self.state == self.STATE_WAIT_READY:
            if now - self._cmd_sent_time > self.READY_TIMEOUT:
                print("[WARN] READY 超时，舵机可能卡住 → 回到 IDLE")
                self.state = self.STATE_IDLE

    def _process_stm32_response(self):
        """读取并处理 STM32 响应，驱动状态机转换"""
        resp = self.serial.read_response()
        if not resp:
            return None

        print(f"[RX] {resp}")

        if resp.startswith("ACK"):
            # 收到 ACK → 确认 STM32 已接收指令
            if self.state == self.STATE_WAIT_ACK:
                self.state = self.STATE_WAIT_READY
            else:
                print(f"[INFO] 收到意外的 ACK（当前状态: {self.state}），忽略")

        elif resp == "READY":
            # 收到 READY → 舵机动作完成，可以发送下一条指令
            self.state = self.STATE_IDLE

        elif resp == "PONG":
            # 心跳回复，正常忽略
            pass

        elif resp == "ERR":
            print("[ERROR] STM32 报告协议解析错误")
            self.state = self.STATE_IDLE

        return resp

    def run(self, camera=1, show_window=True):
        """
        主循环：摄像头采集 → YOLO 检测 → 串口发送 → 显示画面

        流程：
            1. 打开摄像头和串口
            2. 每帧：读取 STM32 响应 → 更新状态 → 空闲时检测+发送
            3. 按 ESC 或 Ctrl+C 退出

        参数：
            camera:     摄像头编号（0=笔记本, 1=iVCam 手机）
            show_window: 是否显示 OpenCV 窗口
        """
        self.start()
        cap = cv2.VideoCapture(camera)
        if not cap.isOpened():
            print(f"[ERROR] 无法打开摄像头 (编号: {camera})")
            return

        if not self.serial.connected:
            print("[ERROR] 串口未连接，请检查端口号和连线")
            cap.release()
            return

        print(f"[OK] 系统运行中 | 串口: {self.serial.port} | 摄像头: {camera} | ESC 退出")

        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    print("[WARN] 摄像头读取失败，重试...")
                    continue

                # ① 读取 STM32 响应（无论什么状态都读）
                resp = self._process_stm32_response()
                if resp:
                    # 在画面上显示 STM32 状态
                    cv2.putText(frame, f"STM32: {resp}",
                                (10, 90), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 0, 0), 2)

                # ② 超时检测
                self._check_timeout()

                # ③ 空闲状态 → YOLO 检测 + 发送指令（带冷却检查）
                if self.state == self.STATE_IDLE:
                    annotated, detections = self.process_frame(frame)

                    # 从检测结果中找到第一个未在冷却期的有效目标
                    import time
                    now = time.time()
                    for d in detections:
                        if d["category_id"] < 0:
                            continue  # 不在映射表，跳过
                        # 检查该类别是否在冷却期
                        cooldown_end = self._cooldown_until.get(d["category_id"], 0)
                        if now < cooldown_end:
                            remaining = cooldown_end - now
                            print(f"[SKIP] {d['class']} 冷却中 ({remaining:.1f}s)")
                            continue  # 冷却中，跳过
                        # 可以发送
                        self.send_sort_command(d)
                        info = f"{d['class']}({d['category_id']}) conf={d['confidence']:.2f}"
                        cv2.putText(annotated, f"Sent: {info}",
                                    (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)
                        break
                else:
                    # 非空闲状态：仍在检测但不发送，保持画面可见
                    annotated, _ = self.process_frame(frame)

                # ④ 在画面上显示当前状态
                cv2.putText(annotated, f"State: {self.state}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                if show_window:
                    cv2.imshow("AI Vision Sorter", annotated)
                    if cv2.waitKey(1) & 0xFF == 27:  # ESC 退出
                        break

        except KeyboardInterrupt:
            print("\n[INFO] Ctrl+C 退出")

        finally:
            cap.release()
            if show_window:
                cv2.destroyAllWindows()
            self.stop()
            print("[OK] 系统已停止")


def main():
    parser = argparse.ArgumentParser(description="AI 视觉分拣系统 PC 端")
    parser.add_argument("--port", default="COM6",
                        help="串口号（默认 COM6，设备管理器中查看）")
    parser.add_argument("--cam", type=int, default=1,
                        help="摄像头编号（0=笔记本, 1=iVCam 手机，默认 1）")
    parser.add_argument("--no-show", action="store_true",
                        help="不显示 OpenCV 窗口（无 GUI 环境使用）")
    args = parser.parse_args()

    det = Detector(port=args.port)
    det.run(camera=args.cam, show_window=not args.no_show)


if __name__ == "__main__":
    main()
