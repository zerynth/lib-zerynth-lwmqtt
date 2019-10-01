"""
.. module:: mqtt

************************
Lightweight MQTT Library
************************

This module contains an implementation of an MQTT client based on the `paho-project <https://eclipse.org/paho/>`_ `embedded c client <https://github.com/eclipse/paho.mqtt.embedded-c>`_.
It aims to be less memory consuming than the pure Python one.

The Client allows to connect to a broker (both via insecure and TLS channels) and start publishing messages/subscribing to topics with a simple interface.

Python callbacks can be easily set to handle incoming messages.

Reconnection can be manually handled by the user by means of several callbacks and methods (:meth:`reconnect`, :meth:`connected`, ``loop_failure``)
    """

import socket
import ssl
import timers

PORT = 1883

# loop failure handler return codes
BREAK_LOOP = 0
RECOVERED = 1

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
def _mqtt_init(activated_cbks, client_id, clean_session,select_loop_time):
    pass

@native_c("_mqtt_connect", [])
def _mqtt_connect(channel, keepalive):
    pass

@native_c("_mqtt_connected", [])
def _mqtt_connected():
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

@native_c("_mqtt_unsubscribe", [])
def _mqtt_unsubscribe(topic):
    pass

@native_c("_mqtt_disconnect", [])
def _mqtt_disconnect():
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

@native_c("_mqtt_topic_match", [])
def _mqtt_topic_match(topic,gen_topic):
    pass

class Client:

    def __init__(self, client_id, clean_session=True,select_loop_time=500):
        """
============
Client class
============

.. class:: Client(client_id, clean_session=True)

    :param client_id: unique ID of the MQTT Client (multiple clients connecting to the same broken with the same ID are not allowed), can be an empty string with :samp:`clean_session` set to true.
    :param clent_session: when ``True`` lets the broken assign a clean state to connecting client without remembering previous subscriptions or other configurations.

    Instantiates the MQTT Client.

        """
        self._activated_cbks = [None]*10
        self._cbks = {}

        _mqtt_init(self._activated_cbks, client_id, clean_session, select_loop_time)

    def connect(self, host, keepalive, port=PORT, ssl_ctx=None, sock_keepalive=None, breconnect_cb=None, aconnect_cb=None, loop_failure=None, start_loop=True):
        """
.. method:: connect(host, keepalive, port=1883, ssl_ctx=None, breconnect_cb=None, aconnect_cb=None, loop_failure=None, start_loop=True)

    :param host: hostname or IP address of the remote broker.
    :param port: network port of the server host to connect to, defaults to 1883.
    :param keepalive: maximum period in seconds between communications with the broker. \
                If no other messages are being exchanged, this controls the rate at which the client will send ping messages to the broker.
    :param ssl_ctx: optional ssl context (:ref:`Zerynth SSL module <stdlib.ssl>`) for secure mqtt channels.
    :param breconnect_cb: optional callback with actions to be performed when :meth:`reconnect` is called. \
                        The callback function will be called passing mqtt client instance.
    :param aconnect_cb: optional callback with actions to be performed after the client successfully connects. \
                        The callback function will be called passing mqtt client instance.
    :param loop_failure: optional callback with actions to be performed on failures happening during the MQTT read cycle. \
                    The user should try to implement client reconnection logic here. \
                    By default, or if ``loop_failure`` callback returns ``mqtt.BREAK_LOOP``, the read loop is terminated on failures. \
                    ``loop_failure`` callback must return ``mqtt.RECOVERED`` to keep the MQTT read cycle alive.
    :param start_loop: if ``True`` starts the MQTT read cycle after connection.
    
        Connects to a remote broker and start the MQTT reception thread.

        """
        # to allow defining custom connects for clients inheriting from this one
        self._connect(host, keepalive, port=port, ssl_ctx=ssl_ctx, sock_keepalive=sock_keepalive, breconnect_cb=breconnect_cb, aconnect_cb=aconnect_cb, loop_failure=loop_failure, start_loop=start_loop)

    def _connect(self, host, keepalive, port=PORT, ssl_ctx=None, sock_keepalive=None, breconnect_cb=None, aconnect_cb=None, loop_failure=None, start_loop=True):
        self._after_connect  = aconnect_cb
        self._before_reconnect = breconnect_cb
        self._loop_failure = loop_failure
        self._keepalive = keepalive

        self._host = host
        self._port = port
        self._ssl_ctx = ssl_ctx
        self._sock_keepalive = sock_keepalive
        self._disconnected = False

        self._ll_connect()
        self._loop_started=False

        if start_loop:
            self.loop()

    def _ll_connect(self):
        ip = __default_net["sock"][0].gethostbyname(self._host)

        if self._ssl_ctx is None:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        else:
            self._sock = ssl.sslsocket(ctx=self._ssl_ctx)

        if self._sock_keepalive and len(self._sock_keepalive) == 3:
            try:
                self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            except Exception as e:
                print(e)
                # no keepalive
                pass

            try:
                self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, self._sock_keepalive[0])
            except Exception as e:
                print(e)
                # no keepalive
                pass

            try:
                self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, self._sock_keepalive[1])
            except Exception as e:
                print(e)
                # no keepalive
                pass

            try:
                self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, self._sock_keepalive[2])
            except Exception as e:
                print(e)
                # no keepalive
                pass

        self._sock.connect((ip, self._port))
        try:
            _mqtt_connect(self._sock.channel, self._keepalive)
            self._disconnected = False
        except Exception as e:
            #close the socket, ignoring exc
            try:
                self._sock.close()
            except:
                pass
            raise e

        if self._after_connect is not None:
            self._after_connect(self)

    def reconnect(self):
        """
.. method:: reconnect()

    Tries to connect again with previously set connection parameters.

    If ``breconnect_cb`` was passed to :meth:`connect`, ``breconnect_cb`` is executed first.
        """
        if self._before_reconnect:
            self._before_reconnect(self)

        self._sock.close()
        self._ll_connect()

    def connected(self):
        """
.. method:: connected()

    Returns ``True`` if client is connected, ``False`` otherwise.
        """
        return _mqtt_connected()

    def loop(self):
        """
.. method:: loop()

    Starts MQTT background loop to handle incoming packets.

    Already called by :meth:`connect` if ``start_loop`` is ``True``.
        """
        if not self._loop_started:
            self._loop_started = True
            thread(self._loop)

    def set_username_pw(self, username, password=''):
        """
.. method:: set_username_pw(username, password='')

    :param username: connection username.
    :param password: connection password.

    Sets connection username and password.
        """
        _mqtt_set_username_pw(username, password)

    def publish(self, topic, payload='', qos=0, retain=False):
        """
.. method:: publish(topic, payload='', qos=0, retain=False)

    :param topic: topic the message should be published on.
    :param payload: actual message to send. If not given a zero length message will be used.
    :param qos: is the quality of service level to use.
    :param retain: if set to true, the message will be set as the "last known good"/retained message for the topic.

    Publishes a message on a topic.

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

    Subscribes to a topic and set a callback for processing messages published on it.

    The callback function is called passing three parameters: the MQTT client object, the payload of received message and the actual topic::

        def my_callback(mqtt_client, payload, topic):
            # do something with client, payload and topic
            ...

        """
        _mqtt_subscribe(topic, qos)
        self._cbks[topic] = function

    def unsubscribe(self, topic):
        """
.. method:: unsubscribe(topic)

    Unsubscribes the client from one topic.

    * *topic* is the string representing the subscribed topic to unsubscribe from.
        """
        _mqtt_unsubscribe(topic)
        self._cbks[topic] = None

    def disconnect(self,timeout=None):
        """
.. method:: reconnect()

    Sends a disconnect message.
        """
        _mqtt_disconnect()
        self._disconnected = True
        if timeout:
            sleep(timeout)
        else:
            while self._loop_started:
                sleep(500)


    def close(self):
        self._sock.close()

    def _loop(self):
        while True:
            try:
                _mqtt_cycle()
            except IOError as eio:
                print(eio)
                # user handles cycle failure
                rc = BREAK_LOOP
                if self._loop_failure and not self._disconnected:
                    rc = self._loop_failure(self)
                if rc == BREAK_LOOP:
                    self._loop_started = False
                    break
            except Exception as e:
                print(e)
                self._loop_started = False
                break

            _mqtt_activated_cbks_acquire()
            for i, activated_topic_payload in enumerate(self._activated_cbks):
                if not activated_topic_payload:
                    break
                try:
                    topic = activated_topic_payload[0]
                    # print("received",topic)
                    for tpx, cb in self._cbks.items():
                        # print("comparing to",tpx,_mqtt_topic_match(topic,tpx))
                        if _mqtt_topic_match(topic,tpx):
                            print(activated_topic_payload[1])
                            cb(self,activated_topic_payload[1],topic)
                except Exception as e:
                    # print(e)
                    _mqtt_activated_cbks_release()
                    # release and raise
                    raise e
                self._activated_cbks[i] = None
            _mqtt_activated_cbks_release()

## Some topic match tests
# _mqtt_topic_match("aaa/bbb/ccc","aaa/bbb/#")
# _mqtt_topic_match("aaa/bbb/ccc","aaa/bbb/+")
# _mqtt_topic_match("aaa/bbb/ccc","aaa/+/+")
# _mqtt_topic_match("aaa/bbb/ccc","aaa/+/#")
# _mqtt_topic_match("aaa/bbb/ccc","+/bbb/#")
# _mqtt_topic_match("aaa/bbb/ccc","+/+/+")
# _mqtt_topic_match("aaa","aaa")
# _mqtt_topic_match("aaa/bbb","aaa/bbb")
# _mqtt_topic_match("aaa/bbb","aaa/bbb/#")
# _mqtt_topic_match("aaa/bbb","aaa/bbb/ddd/#")
# _mqtt_topic_match("aaa/bbb/ccc","aaa/bbb")




