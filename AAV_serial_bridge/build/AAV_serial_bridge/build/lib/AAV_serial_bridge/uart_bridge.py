#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from ackermann_msgs.msg import AckermannDrive
import serial
import struct

PKT_FMT = '<fffB'
PKT_SIZE = 13

class UartBridge(Node):
    def __init__(self):
        super().__init__('uart_bridge')
        
        self.declare_parameter('port', '/dev/ttyAMA0')
        self.declare_parameter('baud', 115200)
        
        port = self.get_parameter('port').value
        baud = self.get_parameter('baud').value
        
        self.ser = serial.Serial(port, baud, timeout=0.01)
        
        self.pub = self.create_publisher(AckermannDrive, '/driveStatus', 10)
        self.sub = self.create_subscription(AckermannDrive, '/driveData', self.drive_cb, 10)
        self.timer = self.create_timer(0.02, self.read_status)
        
        self.rx_buf = bytearray()
        self.get_logger().info(f'Bridge: {port}@{baud} | /driveData→ESP32→/driveStatus')
    
    def drive_cb(self, msg):
        data = struct.pack('<fff', msg.steering_angle, msg.speed, msg.acceleration)
        chk = sum(data) & 0xFF
        pkt = data + bytes([chk])
        self.ser.write(pkt)
        self.get_logger().info(f'→ESP32: s={msg.steering_angle:.2f} v={msg.speed:.2f}')
    
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
