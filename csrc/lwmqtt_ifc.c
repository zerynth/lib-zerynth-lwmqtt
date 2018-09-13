/*
* @Author: lorenzo
* @Date:   2018-08-24 11:28:19
* @Last Modified by:   lorenzo
* @Last Modified time: 2018-09-06 12:19:31
*/

#define ZERYNTH_PRINTF
#include "zerynth.h"
#include "MQTTClient.h"


unsigned char mqtt_sendbuf[512], mqtt_readbuf[256];
uint8_t *mqtt_client_username, *mqtt_client_password, *mqtt_clientid; 

MQTTPacket_connectData mqtt_connectData = MQTTPacket_connectData_initializer;
MQTTClient paho_mqtt_client;

Network mqtt_network;

Timer cycle_timer;

// it is not possible to know if subscription callbacks will be called in a cycle from the mqtt recv
// task (the Python one executing callback) or from the main (after a wait_for), so the need to protect
// the shared object via a mutex
Mutex activated_callbacks_mutex;
PObject *activated_callbacks;

uint8_t *subscribed_topics_cstrings[MAX_MESSAGE_HANDLERS];


C_NATIVE(_mqtt_init) {
    NATIVE_UNWARN();

    uint8_t *clientid;
    uint32_t clientid_len, i;

    activated_callbacks = args[0];
    nargs--;
    args++;

    MutexInit(&activated_callbacks_mutex);

    if (parse_py_args("s", nargs, args, &clientid, &clientid_len) != 1)
        return ERR_TYPE_EXC;


    for (i = 0; i < MAX_MESSAGE_HANDLERS; i++) {
        subscribed_topics_cstrings[i] = NULL;
    }

    NetworkInit(&mqtt_network);
    MQTTClientInit(&paho_mqtt_client, &mqtt_network, 30000, mqtt_sendbuf, sizeof(mqtt_sendbuf), 
                                                            mqtt_readbuf, sizeof(mqtt_readbuf));

    TimerInit(&cycle_timer);

    mqtt_clientid = gc_malloc(clientid_len + 1); // reserve 1 byte for c-string null byte
    mqtt_clientid[clientid_len] = 0;
    memcpy(mqtt_clientid, clientid, clientid_len);

    mqtt_connectData.clientID.cstring = mqtt_clientid;

    return ERR_OK;
}



C_NATIVE(_mqtt_set_username_pw) {

    uint32_t username_len, password_len;
    uint8_t *username, *password;

    if (parse_py_args("ss", nargs, args, &username, &username_len, &password, &password_len) != 2)
        return ERR_TYPE_EXC;

    mqtt_client_username = gc_malloc(username_len + 1);
    mqtt_client_password = gc_malloc(password_len + 1);

    mqtt_client_username[username_len] = 0;
    mqtt_client_password[password_len] = 0;

    memcpy(mqtt_client_username, username, username_len);
    memcpy(mqtt_client_password, password, password_len);

    mqtt_connectData.username.cstring = mqtt_client_username;
    mqtt_connectData.password.cstring = mqtt_client_password;

    return ERR_OK;
}

C_NATIVE(_mqtt_connect) {
    NATIVE_UNWARN();

    PARSE_PY_INT(mqtt_network.my_socket);

    mqtt_connectData.MQTTVersion = 4;
    mqtt_connectData.cleansession = 1;

    if (MQTTConnect(&paho_mqtt_client, &mqtt_connectData) != 0) {
        return ERR_IOERROR_EXC;
    }

    return ERR_OK;
}

C_NATIVE(_mqtt_publish) {
    NATIVE_UNWARN();

    MQTTMessage message;
    uint8_t *topic, *payload;
    uint32_t qos, retain, topic_len, payload_len;

    if (parse_py_args("ssii", nargs, args, &topic, &topic_len, &payload, &payload_len, &qos, &retain) != 4)
        return ERR_TYPE_EXC;

    message.qos = qos;
    message.retained = retain;
    message.payload = payload;
    message.payloadlen = payload_len;

    uint8_t *cstring_topic = gc_malloc(topic_len + 1); // convert topic from bytes sequence to cstring
    memcpy(cstring_topic, topic, topic_len);
    cstring_topic[topic_len] = 0;

    MQTTPublish(&paho_mqtt_client, cstring_topic, &message);

    gc_free(cstring_topic);

    return ERR_OK;
}

C_NATIVE(_mqtt_cycle) {
    NATIVE_UNWARN();

    MutexLock(&paho_mqtt_client.mutex);
    TimerCountdownMS(&cycle_timer, 500); /* Don't wait too long if no traffic is incoming */
    cycle(&paho_mqtt_client, &cycle_timer);
    MutexUnlock(&paho_mqtt_client.mutex);

    return ERR_OK;
}

void messages_handler(MessageData* data) {
    uint32_t i;

    MutexLock(&activated_callbacks_mutex);

    int free_slot = -1;
    for (i = 0; i < PSEQUENCE_ELEMENTS(activated_callbacks); i++) {
        if (PTYPE(PLIST_ITEM(activated_callbacks, i)) == PNONE) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        goto exit;
    }

    PObject *topic_payload[2];
    topic_payload[0] = pstring_new(data->topicName->lenstring.len, data->topicName->lenstring.data);
    topic_payload[1] = pstring_new(data->message->payloadlen, data->message->payload);
    PTuple *topic_payload_tuple = ptuple_new(2, topic_payload);
    PLIST_SET_ITEM(activated_callbacks, free_slot, topic_payload_tuple);

exit:
    MutexUnlock(&activated_callbacks_mutex);
}

C_NATIVE(_mqtt_subscribe) {
    NATIVE_UNWARN();

    uint32_t topic_len, i, qos;
    uint8_t *topic;

    if (parse_py_args("si", nargs, args, &topic, &topic_len, &qos) != 2)
        return ERR_TYPE_EXC;

    int free_slot = -1;
    for (i = 0; i < MAX_MESSAGE_HANDLERS; i++) {
        if (subscribed_topics_cstrings[i] == NULL) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        // no more subscription slots
        return ERR_VALUE_EXC;
    }

    subscribed_topics_cstrings[free_slot] = gc_malloc(topic_len);
    subscribed_topics_cstrings[free_slot][topic_len] = 0;
    memcpy(subscribed_topics_cstrings[free_slot], topic, topic_len);

    if (MQTTSubscribe(&paho_mqtt_client, subscribed_topics_cstrings[free_slot], qos, messages_handler) != 0) {
        gc_free(subscribed_topics_cstrings[free_slot]);
        return ERR_IOERROR_EXC;
    }

    return ERR_OK;
}

C_NATIVE(_mqtt_activated_cbks_acquire) {
    MutexLock(&activated_callbacks_mutex);
    return ERR_OK;
}

C_NATIVE(_mqtt_activated_cbks_release) {
    MutexUnlock(&activated_callbacks_mutex);
    return ERR_OK;
}