# TODO: Detect when connection to broker lost and reconnect!
import paho.mqtt.client as mqtt
import SocketServer, threading, socket, time, os, json

## Global variables
previous_readings = set()
client = mqtt.Client()

def open_SocketServer(socket_address, BaseRequestHandler):
	# clean up stale socket if there is one
	try:
		os.remove(socket_address)
	except OSError:
		pass

	connected = False
	tries = 0
	while not connected and tries <= 20:
		tries += 1
		try:
			server = SocketServer.UnixStreamServer(socket_address, handler)
			t = threading.Thread(target=server.serve_forever)
			t.setDaemon(True) # don't hang on exit
			t.start()
		except socket.error as error:
			self.stdout.write("Error starting server. Will retry shortly.\n\t{}".format(error))
			time.sleep(5)
		else:
			connected = True

	if not connected:
		raise Exception("Unable to listen for setting changes")

	print("Listening for setting changes on socket {}".format(socket_address))

def mqtt_connect(host, port=1883, keep_alive=60):
	client.on_connect = on_connect
	client.on_message = on_message
	client.on_disconnect = on_disconnect
	client.connect(host, port, keep_alive)

	# Blocking call that processes network traffic, dispatches callbacks and
	# handles reconnecting.
	# Other loop*() functions are available that give a threaded interface and a
	# manual interface.
	client.loop_forever()

def yun_connect(hostname):
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
	return(TCPJSONClient('127.0.0.1', 5700))
	###

## MQTT handlers
# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
	# Subscribing in on_connect() means that if we lose the connection and
	# reconnect then subscriptions will be renewed.
	client.subscribe("{}settings/#".format(climaduino_path))
	print("Connected to MQTT broker")

# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
	print("setting: {}, {}".format(msg.topic, msg.payload))
	yun_bridge.send({'command':'put', 'key':'{}'.format(msg.topic.replace(climaduino_path, "")), 'value':'{}'.format(msg.payload)})

def on_disconnect(client, userdata, rc):
	if rc != 0:
		print("Connection to MQTT broker unexpectedly lost")
		connected = False
		while not connected:
			try:
				client.reconnect()
			except (socket.gaierror, socket.error):
				print("Reconnection failed. Will try again shortly.")
				time.sleep(30)
			else:
				connected = True
				print("Reconnected")
	else:
		print("Disconnected from MQTT broker")

## SocketServer handlers
class ReceiveReadingsHandler(SocketServer.BaseRequestHandler):
	def handle(self):
		global previous_readings

		data = json.loads(self.request.recv(1024))

		if len(data) == 0:
			return
		try:
			#compare with previous readings and only send what changed
			new_readings = set(data.items())
		except TypeError:
			print("Invalid data received: {}".format(data))

		changes = new_readings.difference(previous_readings)

		for (key, value) in changes:
			# Convert boolean values to 0 and 1
			if isinstance(value, bool):
				value = int(value)
			print("changed: {}, {}".format(key, value))
			client.publish(key, value)
			time.sleep(0.1) # does not seem to work reliably without a slight pause

		previous_readings = new_readings
		return

if __name__ == "__main__":
	hostname = socket.gethostname()
	climaduino_path = "climaduino/{}/".format(hostname)

	open_SocketServer('/tmp/climaduino_mqtt_bridge', ReceiveReadingsHandler)
	yun_bridge = yun_connect(hostname)
	mqtt_connect("test.mosquitto.org")




