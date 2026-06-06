"""
serial_comm.py — PC 端串口通信模块

功能：
    1. 与 STM32 建立串口连接（115200, 8N1）
    2. 发送分拣指令："CMD:类别ID,X,Y\n"
    3. 发送心跳："PING\n"（每 5 秒一次，STM32 回复 "PONG\n"）
    4. 接收 STM32 响应："ACK:类别ID\n" / "READY\n"

使用方法：
    comm = SerialComm(port="COM3")   # 创建对象，指定串口号
    comm.connect()                    # 打开串口
    comm.send_command(0, 160, 120)    # 发送分拣指令：类别0，坐标(160,120)
    resp = comm.read_response()       # 读取响应
    comm.disconnect()                 # 关闭串口

注意：
    - 串口号在 Windows 设备管理器查看（COM3、COM4 等）
    - 心跳线程在后台自动运行，不需要手动管理
    - 线程安全：发送操作用锁保护，避免多线程同时写串口
"""

import serial
import threading
import time


class SerialComm:
    """串口通信类，封装了连接、发送、接收、心跳功能"""

    def __init__(self, port="COM3", baudrate=115200, timeout=1):
        """
        初始化串口参数（此时还未真正打开串口）

        参数：
            port:     串口号（如 "COM3"）
            baudrate: 波特率（默认 115200，与 STM32 一致）
            timeout:  读超时（秒）
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None              # pyserial 串口对象
        self.connected = False       # 连接状态标志
        self._lock = threading.Lock()  # 线程锁，保护串口写操作
        self._heartbeat_thread = None  # 心跳线程
        self._running = False          # 控制心跳线程退出

    def connect(self):
        """
        打开串口连接

        参数：8N1（8 数据位、无校验、1 停止位），与 STM32 配置一致
        成功后自动启动心跳线程（每 5 秒发一次 PING）
        """
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,      # 8 数据位
                parity=serial.PARITY_NONE,       # 无校验
                stopbits=serial.STOPBITS_ONE,    # 1 停止位
                timeout=self.timeout,            # 读超时 1 秒
            )
            self.connected = True
            self._running = True
            # 启动后台心跳线程（daemon=True，主线程退出时自动结束）
            self._heartbeat_thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
            self._heartbeat_thread.start()
            print(f"[OK] 串口已连接: {self.port} @ {self.baudrate}")
        except serial.SerialException as e:
            print(f"[ERROR] 串口连接失败: {e}")
            self.connected = False

    def disconnect(self):
        """关闭串口，停止心跳线程"""
        self._running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connected = False
        print("[OK] 串口已断开")

    def send_command(self, class_id, x, y):
        """
        发送分拣指令给 STM32

        参数：
            class_id: 物体类别（0=A, 1=B, 2=C）
            x:        检测框中心 X 坐标（像素，当前未使用，预留）
            y:        检测框中心 Y 坐标（像素，当前未使用，预留）

        发送格式："CMD:0,160,120\n"
        返回：True=成功，False=失败
        """
        if not self.connected:
            return False
        cmd = f"CMD:{class_id},{x},{y}\n"
        with self._lock:  # 加锁，避免心跳线程同时写串口
            try:
                self.ser.write(cmd.encode("ascii"))
                return True
            except serial.SerialException as e:
                print(f"[ERROR] 发送失败: {e}")
                return False

    def send_ping(self):
        """
        发送心跳帧："PING\n"

        STM32 收到后回复 "PONG\n"
        如果 10 秒没收到 PING，STM32 的 LED 会灭（进入待机）
        """
        if not self.connected:
            return False
        with self._lock:
            try:
                self.ser.write(b"PING\n")
                return True
            except serial.SerialException:
                return False

    def read_response(self):
        """
        读取 STM32 的回复（非阻塞）

        返回：
            字符串（如 "ACK:0"、"READY"、"PONG"）
            None = 没有新数据
        """
        if not self.connected:
            return None
        try:
            if self.ser.in_waiting > 0:  # 缓冲区有数据才读
                line = self.ser.readline().decode("ascii", errors="ignore").strip()
                return line if line else None
        except serial.SerialException:
            self.connected = False
        return None

    def _heartbeat_loop(self):
        """
        心跳线程：每 5 秒发送一次 PING

        目的：让 STM32 知道 PC 还在线
        如果 PC 崩溃或串口断开，STM32 10 秒后自动进入待机（LED 灭）
        """
        while self._running:
            time.sleep(5)
            if self.connected:
                self.send_ping()


# ======================== 单独运行时做简单测试 ========================
if __name__ == "__main__":
    """
    测试方法：
        1. 连接 USB 转 TTL 模块到 STM32（PA9/PA10/GND）
        2. 运行：python serial_comm.py
        3. 应该看到：发送 PING → 收到 PONG
    """
    comm = SerialComm(port="COM3")
    comm.connect()
    if comm.connected:
        print("[TEST] 发送 PING...")
        comm.send_ping()
        time.sleep(0.5)
        resp = comm.read_response()
        print(f"[TEST] 响应: {resp}")
        comm.disconnect()
