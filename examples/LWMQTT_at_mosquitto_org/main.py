import streams
import json

from microchip.winc1500 import winc1500 as wifi_driver
# from espressif.esp32net import esp32wifi as wifi_driver

from wireless import wifi
from lwmqtt import mqtt

def hello_samples(client, payload):
    print('> received: ', payload)

streams.serial()

wifi_driver.auto_init(ext=1)

print('> wifi link')
wifi.link("SSID",wifi.WIFI_WPA2,"PSW")
print('> linked')

client = mqtt.Client("zerynth-mqtt",True)
# connect to "test.mosquitto.org"
for retry in range(10):
    try:
        print("connect")
        client.connect("test.mosquitto.org", 60)
        break
    except Exception as e:
        print("connecting...")
print("> connected.")
# subscribe to channels
client.subscribe("zerynth/samples", hello_samples)

while True:
    print("> publish.")
    client.publish("zerynth/samples", json.dumps({'rand': random(0,10)}))
    sleep(1000)
