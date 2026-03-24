#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from ackermann_msgs.msg import AckermannDrive
from sensor_msgs.msg import Imu

import math
import serial
import struct
from socket import *
import threading

PKT_FMT = '<fffB'
PKT_SIZE = 13


class UartBridge(Node):
    def __init__(self):
        super().__init__('uart_bridge')

        self.declare_parameter('port', '/dev/ttyAMA0')
        self.declare_parameter('baud', 115200)

        port = self.get_parameter('port').value
        baud = self.get_parameter('baud').value

        udp_port = 5001

        self.sock = socket.socket(socket.AF_PACKET, socket.SOCK_DGRAM)
        self.sock.bind("",udp_port) #need to setup the ip binding


        self.ser = serial.Serial(port, baud, timeout=0.01)

        self.declare_parameter('wheelbase', 1.57)  # [m]
        self.declare_parameter('delta_max', 0.55)  # [rad] max steering angle
        self.declare_parameter('motor_ratio', 0.000019)  # [rad/m] actuator geometry (1.11 deg/cm)
        self.declare_parameter('kp', 0.5)  # [1/(rad/s)]
        self.declare_parameter('control_rate', 100.0)  # [Hz]
        self.declare_parameter('use_feedforward', True)

        self.L = self.get_parameter('wheelbase').value
        self.delta_max = self.get_parameter('delta_max').value
        self.motor_ratio = self.get_parameter('motor_ratio').value
        self.kp = self.get_parameter('kp').value
        self.rate = self.get_parameter('control_rate').value
        self.use_ff = self.get_parameter('use_feedforward').value

        # STATE VARIABLES
        self.v = 0.0
        self.delta_desired = 0.0
        self.omega_measured = 0.0

        self.dt = 1.0 / self.rate

        self.create_subscription(Imu, '/imu/data', self.imu_callback, 10)

        self.pub = self.create_publisher(AckermannDrive, '/driveStatus', 10)
        #self.sub = self.create_subscription(AckermannDrive, '/driveData', self.drive_cb, 10)
        self.listener = threading.Thread(target=self.drive_cb)
        self.listener.daemon = True
        self.listener.start()

        self.timer = self.create_timer(0.02, self.read_status)

        self.rx_buf = bytearray()
        self.get_logger().info(f'Bridge: {port}@{baud} | /driveData→ESP32→/driveStatus')

    def drive_cb(self, msg):
        data, addr = self.sock.recvfrom(16)
        self.v = data [8:]
        self.delta_desired = data[:8]
        
        #self.v = msg.drive.speed  # [m/s]
        #self.delta_desired = msg.drive.steering_angle  # [rad]

        if abs(self.v) < 0.01:
            return

        # Desired yaw rate from bicycle model
        omega_desired = (self.v / self.L) * math.tan(self.delta_desired)

        # Error
        error = omega_desired - self.omega_measured  # [rad/s]

        # Proportional output in radians
        u_pid_rad = self.kp * error

        # Feedforward
        if self.use_ff:
            u_pid_rad += self.delta_desired

        data = struct.pack('<fff', u_pid_rad, msg.speed, msg.acceleration)
        chk = sum(data) & 0xFF
        pkt = data + bytes([chk])
        self.ser.write(pkt)
        self.get_logger().info(f'→ESP32: s={msg.steering_angle:.2f} v={msg.speed:.2f}')

    def imu_callback(self, msg):
        self.omega_measured = msg.angular_velocity.z  # [rad/s]

    def read_status(self):
        data = self.ser.read(self.ser.in_waiting or 0)
        if data:
            self.rx_buf.extend(data)

        while len(self.rx_buf) >= PKT_SIZE:
            pkt = self.rx_buf[:PKT_SIZE]
            self.rx_buf = self.rx_buf[PKT_SIZE:]

            s, v, a, chk = struct.unpack(PKT_FMT, pkt)
            if sum(pkt[:-1]) & 0xFF == chk:
                msg = AckermannDrive()
                msg.steering_angle = float(s)
                msg.speed = float(v)
                msg.acceleration = float(a)
                self.pub.publish(msg)
                self.get_logger().info(f'←ESP32: s={s:.2f} v={v:.2f}')


def main():
    rclpy.init()
    node = UartBridge()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
