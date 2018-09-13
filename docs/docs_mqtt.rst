.. module:: mqtt

************************
Lightweight MQTT Library
************************

.. warning:: The following mqtt library is still experimental: reconnections are not handled and some minor features are missing. Moreover it is not guaranteed to work on all supported devices.

This module contains an implementation of an MQTT client based on the `paho-project <https://eclipse.org/paho/>`_ `embedded c client <https://github.com/eclipse/paho.mqtt.embedded-c>`_.
It aims to be less memory consuming than the pure Python one.

The Client allows to connect to a broker (both via insecure and TLS channels) and start publishing messages/subscribing to topics with a simple interface.

Python callbacks can be easily set to handle incoming messages.

    
============
Client class
============

.. class:: Client(client_id, clean_session=False)

    :param client_id: unique ID of the MQTT Client (multiple clients connecting to the same broken with the same ID are not allowed), can be an empty string with :samp:`clean_session` set to true.
    :param clent_session: when ``True`` lets the broken assign a clean state to connecting client without remembering previous subscriptions or other configurations.

    Instantiate the MQTT Client.

        
.. method:: connect(host, keepalive, port=1883)

    :param host: hostname or IP address of the remote broker.
    :param port: network port of the server host to connect to, defaults to 1883.
    :param keepalive: maximum period in seconds between communications with the broker. If no other messages are being exchanged, this controls the rate at which the client will send ping messages to the broker.
    :param ssl_ctx: optional ssl context (:ref:`Zerynth SSL module <stdlib.ssl>`) for secure mqtt channels.
    
        Connect to a remote broker and start the MQTT reception thread.

        
.. method:: set_username_pw(username, password='')

    :param username: connection username.
    :param password: connection password.

    Set connection username and password.
        
.. method:: publish(topic, payload='', qos=0, retain=False)

    :param topic: topic the message should be published on.
    :param payload: actual message to send. If not given a zero length message will be used.
    :param qos: is the quality of service level to use.
    :param retain: if set to true, the message will be set as the "last known good"/retained message for the topic.

    Publish a message on a topic.

    This causes a message to be sent to the broker and subsequently from
    the broker to any clients subscribing to matching topics.

    
.. method:: subscribe(topic, function, qos=0)

    :param topic: topic to subscribe to.
    :param function: callback to be executed when a message published on chosen topic is received.
    :param qos: quality of service for the subscription.

    Subscribe to a topic and set a callback for processing messages published on it.

    The callback function is called passing two parameters: the MQTT client object and the payload of received message::

        def my_callback(mqtt_client, payload):
            # do something with client and payload
            ...

        
