.. module:: mqtt

====
MQTT
====

This module contains an implementation of the MQTT protocol (client-side) based on the work
of Roger Light <roger@atchoo.org> from the `paho-project <https://eclipse.org/paho/>`_.

"*MQTT is a publish-subscribe based lightweight messaging protocol for use on top of the TCP/IP protocol.
[...] The publish-subscribe messaging pattern requires a message broker. The broker is responsible for
distributing messages to interested clients based on the topic of a message.*" (from Wikipedia)

So a client must connect to a broker in order to:

    * **publish** messages specifying a topic so that other clients that have subscribed to that topic
      will be able to receive those messages;
    * receive messages **subscribing** to a specific topic.

To clarify this behaviour imagine a home network of temperature sensors and controllers where
each temperature sensor publishes sampled data on 'home/nameOfRoom' channel and the home
temperature controller subscribes to all channels to achieve a smart heating.

When publishing and subscribing a client is able to specify a quality of service (QoS) level
for messages which activates procedures to assure a message to be actually delivered or
received, available levels are:

    * 0 - at most once
    * 1 - at least once
    * 2 - exactly once

Resource clearly explaining the QoS feature:
`mqtt-quality-of-service-levels <http://www.hivemq.com/blog/mqtt-essentials-part-6-mqtt-quality-of-service-levels>`_

Back to the implementation Viper mqtt.Client class provides methods to:

    * connect to a broker
    * publish
    * subscribe
    * unsubscribe
    * disconnect from the broker


and a system of **callbacks** to handle incoming packets.

    
=================
MQTTMessage class
=================

.. class:: MQTTMessage

    This is a class that describes an incoming message. It is passed to
    callbacks as *message* field in the *data* dictionary.

    Members:

    * topic : String. topic that the message was published on.
    * payload : String. the message payload.
    * qos : Integer. The message Quality of Service 0, 1 or 2.
    * retain : Boolean. If true, the message is a retained message and not fresh.
    * mid : Integer. The message id.
    
============
Client class
============

.. class:: Client

    This is the main module class.
    After connecting to a broker it is suggested to subscribe to some channels
    and configure callbacks.
    Then, a non-blocking loop function, starts a separate thread to handle incoming
    packets.

    Example::

        my_client = mqtt.Client("myId",True)
        for retry in range(10):
            try:
                client.connect("test.mosquitto.org", 60)
                break
            except Exception as e:
                print("connecting...")
        my_client.subscribe([["cool/channel",1]])
        my_client.on("PUBLISH",print_cool_stuff,is_cool)
        my_client.loop()

        # do something else...

    Details about the callback system under :func:`~mqtt.Client.on` method.
    
.. method:: __init__(client_id, clean_session = True)

    * *client_id* is the unique client id string used when connecting to the
      broker.

    * *clean_session* is a boolean that determines the client type. If True,
      the broker will remove all information about this client when it
      disconnects. If False, the client is a persistent client and
      subscription information and queued messages will be retained when the
      client disconnects.
      Note that a client will never discard its own outgoing messages if
      accidentally disconnected: calling reconnect() will cause the messages to
      be resent.
        
.. method:: on(command, function, condition = None, priority = 0)

    * *command* is a string referring to which MQTT command call the callback on.

    * *function* is the function to execute if *condition* is respected.
      It takes both the client itself and a *data* dictionary as parameters.
      The *data* dictionary may contain the following fields:

        * *message*: MQTTMessage present only on PUBLISH packets for messages
          with qos equal to 0 or 1, or on PUBREL packets for messages with
          qos equal to 2

    * *condition* is a function taking the same *data* dictionary as parameter
      and returning True or False if the packet respects a certain condition.
      *condition* parameter is optional because a generic callback can be set without
      specifying a condition, only in response to a command.
      A callback of this type is called a 'low priority' callback meaning that it
      is called only if all the more specific callbacks (the ones with condition)
      get a False condition response.

        Example::

            def is_cool(data):
                if ('message' in data):
                    return (data['message'].topic == "cool")
                # N.B. not checking if 'message' is in data could lead to Exception
                # on PUBLISH packets for messages with qos equal to 2
                return False

            def print_cool_stuff(client, data):
                print("cool: ", data['message'].payload)

            def print_generic_stuff(client, data):
                if ('message' in data):
                    print("not cool: ", data['message'].payload)

            my_client.on("PUBLISH", print_cool_stuff, is_cool)
            my_client.on("PUBLISH", print_generic_stuff)


        In the above example for every PUBLISH packet it is checked if the topic
        is *cool*, only if this condition fails, *print_generic_stuff* gets executed.
        
.. method:: connect(host, keepalive, port = 1883)

    Connect to a remote broker.

    * *host* is the hostname or IP address of the remote broker.
    * *port* is the network port of the server host to connect to. Defaults to
      1883.
    * *keepalive* is the maximum period in seconds between communications with the
      broker. If no other messages are being exchanged, this controls the
      rate at which the client will send ping messages to the broker.
        
.. method:: reconnect()

    Reconnect the client if accidentally disconnected.
        
.. method:: subscribe(topics)

    Subscribe to one or more topics.

    * *topis* a list structured this way::

      [[topic1,qos1],[topic2,qos2],...]

      where topic1,topic2,... are strings and qos1,qos2,... are integers for
      the maximum quality of service for each topic
        
.. method:: unsubscribe(topics)

    Unsubscribe the client from one or more topics.

    * *topics* is list of strings that are subscribed topics to unsubscribe from.
        
.. method:: publish(topic, payload = None, qos = 0, retain = False)

    Publish a message on a topic.

    This causes a message to be sent to the broker and subsequently from
    the broker to any clients subscribing to matching topics.

    * *topic* is the topic that the message should be published on.
    * *payload* is the actual message to send. If not given, or set to None a
      zero length message will be used.
    * *qos* is the quality of service level to use.
    * *retain*: if set to true, the message will be set as the "last known
      good"/retained message for the topic.

    It returns the mid generated for the message to give the possibility to
    set a callback precisely for that message.
    
.. method:: reconnect()

    Send a disconnect message.
        
.. method:: loop(on_message = None)

    Non blocking loop method that starts a thread to handle incoming packets.

    * *on_message* is an optional argument to set a generic callback on messages
      with qos equal to 0, 1 or 2
        
