import paho.mqtt.client as mqtt
import socket
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
json = TCPJSONClient('127.0.0.1', 5700)
###

client = mqtt.Client()

client.connect("test.mosquitto.org", 1883, 60)

json.send({'command': 'get'})
timeout = 10
while timeout >= 0:
  r = json.recv()
  if not r is None:
    timeout = -1
  timeout -= 0.1
  time.sleep(0.1)
json.close()

for (key, value) in r['value'].items():
	if any(path in key for path in ['readings/', 'status/']):
		print(client.publish("{}{}".format(climaduino_path, key), value))
		time.sleep(0.1) # does not seem to work reliably without a slight pause
		print("{}{} => {}".format(climaduino_path, key, value))

client.disconnect()