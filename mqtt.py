"""
.. module:: mqtt

************************
Lightweight MQTT Library
************************

.. warning:: The following mqtt library is still experimental: reconnections are not handled and some minor features are missing. Moreover it is not guaranteed to work on all supported devices.

This module contains an implementation of an MQTT client based on the `paho-project <https://eclipse.org/paho/>`_ `embedded c client <https://github.com/eclipse/paho.mqtt.embedded-c>`_.
It aims to be less memory consuming than the pure Python one.

The Client allows to connect to a broker (both via insecure and TLS channels) and start publishing messages/subscribing to topics with a simple interface.

Python callbacks can be easily set to handle incoming messages.

    """

# TODO: 
# 
#   - handle reconnection
#   - 

import socket
import ssl

PORT = 1883

@native_c("_mqtt_init", 
    [
        "csrc/lwmqtt_ifc.c",
        "csrc/lwmqtt/MQTTClient-C/src/MQTTClient.c",
        "csrc/lwmqtt/MQTTClient-C/src/zerynth/MQTTZerynth.c",
        "csrc/lwmqtt/MQTTPacket/src/*",
        "#csrc/misc/snprintf.c",
        "#csrc/misc/zstdlib.c"
    ],
    [ 
        "MQTTCLIENT_PLATFORM_HEADER=MQTTZerynth.h",
        "MQTT_TASK=1"
    ],
    [
        "-I.../csrc/lwmqtt/MQTTClient-C/src/",
        "-I.../csrc/lwmqtt/MQTTClient-C/src/zerynth/",
        "-I.../csrc/lwmqtt/MQTTPacket/src",
        "-I#csrc/misc",
        "-I#csrc/zsockets"
    ]
)
def _mqtt_init(activated_cbks, client_id):
    pass

@native_c("_mqtt_connect", [])
def _mqtt_connect(channel):
    pass

@native_c("_mqtt_set_username_pw", [])
def _mqtt_set_username_pw(username, password):
    pass

@native_c("_mqtt_publish", [])
def _mqtt_publish(topic, payload, qos, retain):
    pass

@native_c("_mqtt_subscribe", [])
def _mqtt_subscribe(topic, qos):
    pass

@native_c("_mqtt_cycle", [])
def _mqtt_cycle():
    pass

@native_c("_mqtt_activated_cbks_acquire", [])
def _mqtt_activated_cbks_acquire():
    pass

@native_c("_mqtt_activated_cbks_release", [])
def _mqtt_activated_cbks_release():
    pass

class Client:

    def __init__(self, client_id, clean_session=False):
        """
============
Client class
============

.. class:: Client(client_id, clean_session=False)

    :param client_id: unique ID of the MQTT Client (multiple clients connecting to the same broken with the same ID are not allowed), can be an empty string with :samp:`clean_session` set to true.
    :param clent_session: when ``True`` lets the broken assign a clean state to connecting client without remembering previous subscriptions or other configurations.

    Instantiate the MQTT Client.

        """
        self._activated_cbks = [None]*10
        self._cbks = {}

        _mqtt_init(self._activated_cbks, client_id)

    def connect(self, host, keepalive, port=PORT, ssl_ctx=None, breconnect_cb=None, aconnect_cb=None):
        """
.. method:: connect(host, keepalive, port=1883)

    :param host: hostname or IP address of the remote broker.
    :param port: network port of the server host to connect to, defaults to 1883.
    :param keepalive: maximum period in seconds between communications with the broker. If no other messages are being exchanged, this controls the rate at which the client will send ping messages to the broker.
    :param ssl_ctx: optional ssl context (:ref:`Zerynth SSL module <stdlib.ssl>`) for secure mqtt channels.
    
        Connect to a remote broker and start the MQTT reception thread.

        """
        # to allow defining custom connects for clients inheriting from this one
        self._connect(host, keepalive, port=port, ssl_ctx=ssl_ctx, breconnect_cb=breconnect_cb, aconnect_cb=aconnect_cb)

    def _connect(self, host, keepalive, port=PORT, ssl_ctx=None, breconnect_cb=None, aconnect_cb=None):
        self._after_connect  = aconnect_cb

        self._host = host
        self._port = port
        ip = __default_net["sock"][0].gethostbyname(host)

        self._ssl_ctx = ssl_ctx
        if ssl_ctx is None:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        else:
            self._sock = ssl.sslsocket(ctx=ssl_ctx)

        self._sock.connect((ip,port))
        _mqtt_connect(self._sock.channel)

        if self._after_connect is not None:
            self._after_connect(self)

        thread(self._loop)


    def set_username_pw(self, username, password=''):
        """
.. method:: set_username_pw(username, password='')

    :param username: connection username.
    :param password: connection password.

    Set connection username and password.
        """
        _mqtt_set_username_pw(username, password)

    def publish(self, topic, payload='', qos=0, retain=False):
        """
.. method:: publish(topic, payload='', qos=0, retain=False)

    :param topic: topic the message should be published on.
    :param payload: actual message to send. If not given a zero length message will be used.
    :param qos: is the quality of service level to use.
    :param retain: if set to true, the message will be set as the "last known good"/retained message for the topic.

    Publish a message on a topic.

    This causes a message to be sent to the broker and subsequently from
    the broker to any clients subscribing to matching topics.

    """
        _mqtt_publish(topic, payload, qos, 1 if retain else 0)

    def subscribe(self, topic, function, qos=0):
        """
.. method:: subscribe(topic, function, qos=0)

    :param topic: topic to subscribe to.
    :param function: callback to be executed when a message published on chosen topic is received.
    :param qos: quality of service for the subscription.

    Subscribe to a topic and set a callback for processing messages published on it.

    The callback function is called passing two parameters: the MQTT client object and the payload of received message::

        def my_callback(mqtt_client, payload):
            # do something with client and payload
            ...

        """
        _mqtt_subscribe(topic, qos)
        self._cbks[topic] = function

    def _loop(self):
        while True:
            _mqtt_cycle()
            _mqtt_activated_cbks_acquire()
            for i, activated_topic_payload in enumerate(self._activated_cbks):
                if not activated_topic_payload:
                    break
                self._cbks[activated_topic_payload[0]](self, activated_topic_payload[1])
                self._activated_cbks[i] = None
            _mqtt_activated_cbks_release()






