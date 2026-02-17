import sys
if sys.prefix == '/usr':
    sys.real_prefix = sys.prefix
    sys.prefix = sys.exec_prefix = '/home/sk/Desktop/AAV_serial_bridge/install/AAV_serial_bridge'
