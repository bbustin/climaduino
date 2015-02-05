import json, socket
import time

# get the Climaduino's hostname
hostname = socket.gethostname()
climaduino_path = "climaduino/{}/".format(hostname)

# way to get around bridge slowness
# from: http://forum.arduino.cc/index.php?topic=188998.0
###
import sys    
sys.path.insert(0, '/usr/lib/python2.7/bridge/')

from bridgeclient import BridgeClient as bridgeclient
from tcp import TCPJSONClient
yun_bridge = TCPJSONClient('127.0.0.1', 5700)
###

def send_readings(data):
	# Connect to the server
	s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	s.connect('/tmp/climaduino_mqtt_bridge')
	s.send(json.dumps(data))
	s.close()

yun_bridge.send({'command': 'get'})
timeout = 10
while timeout >= 0:
  r = yun_bridge.recv()
  if not r is None:
    timeout = -1
  timeout -= 0.1
  time.sleep(0.1)
yun_bridge.close()

if r:
	readings = {}
	for (key, value) in r['value'].items():
		if any(path in key for path in ['readings/', 'status/']):
			readings["{}{}".format(climaduino_path, key)] = value
	send_readings(readings)