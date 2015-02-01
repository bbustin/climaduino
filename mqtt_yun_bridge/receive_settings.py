import paho.mqtt.client as mqtt
import socket

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

# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe("{}settings/#".format(climaduino_path))

# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))
    json.send({'command':'put', 'key':'{}'.format(msg.topic.replace(climaduino_path, "")), 'value':'{}'.format(msg.payload)})

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("test.mosquitto.org", 1883, 60)

# Blocking call that processes network traffic, dispatches callbacks and
# handles reconnecting.
# Other loop*() functions are available that give a threaded interface and a
# manual interface.
client.loop_forever()