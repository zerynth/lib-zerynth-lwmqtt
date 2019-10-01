/*
* @Author: lorenzo
* @Date:   2018-08-24 11:28:19
* @Last Modified by:   m.cipriani
* @Last Modified time: 2019-09-13 14:40:54
*/

// #define ZERYNTH_PRINTF
#include "zerynth.h"
#include "MQTTClient.h"


unsigned char mqtt_sendbuf[2048], mqtt_readbuf[2048];
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
uint32_t select_loop_time=500;


C_NATIVE(_mqtt_init) {
    NATIVE_UNWARN();

    uint8_t *clientid;
    uint32_t clientid_len, i;
    int cleansession;

    activated_callbacks = args[0];
    nargs--;
    args++;

    MutexInit(&activated_callbacks_mutex);

    if (parse_py_args("sii", nargs, args, &clientid, &clientid_len, &cleansession,&select_loop_time) != 3)
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
    mqtt_connectData.cleansession = cleansession;
    *res = MAKE_NONE();
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
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_connect) {
    NATIVE_UNWARN();

    if (parse_py_args("ii", nargs, args, &mqtt_network.my_socket, &mqtt_connectData.keepAliveInterval) != 2)
        return ERR_TYPE_EXC;

    mqtt_connectData.MQTTVersion = 4;

    if (MQTTConnect(&paho_mqtt_client, &mqtt_connectData) < 0) {
        return ERR_IOERROR_EXC;
    }
 
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_connected) {
    NATIVE_UNWARN();

    *res = (paho_mqtt_client.isconnected) ? PBOOL_TRUE() : PBOOL_FALSE();
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

    if (MQTTPublish(&paho_mqtt_client, cstring_topic, &message) != 0){
        return ERR_IOERROR_EXC;
    }

    gc_free(cstring_topic);
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_cycle) {
    NATIVE_UNWARN();

    int packet_handled;
    MutexLock(&paho_mqtt_client.mutex);
    TimerCountdownMS(&cycle_timer, select_loop_time); /* Don't wait too long if no traffic is incoming */
    packet_handled = cycle(&paho_mqtt_client, &cycle_timer);
    MutexUnlock(&paho_mqtt_client.mutex);

    if (packet_handled < 0) {
        // cycle returns packet_type or error code < 0
        return ERR_IOERROR_EXC;
    }
    *res = MAKE_NONE();
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
        subscribed_topics_cstrings[free_slot] = NULL;
        return ERR_IOERROR_EXC;
    }
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_unsubscribe) {
    NATIVE_UNWARN();

    uint32_t topic_len, i;
    uint8_t *topic;

    if (parse_py_args("s", nargs, args, &topic, &topic_len) != 1)
        return ERR_TYPE_EXC;

    int free_slot = -1;
    for (i = 0; i < MAX_MESSAGE_HANDLERS; i++) {
        if (memcmp(subscribed_topics_cstrings[i], topic, topic_len) == 0) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        // no more subscription slots
        return ERR_VALUE_EXC;
    }

    if (MQTTUnsubscribe(&paho_mqtt_client, subscribed_topics_cstrings[free_slot]) != 0) {
        return ERR_IOERROR_EXC;
    }

    gc_free(subscribed_topics_cstrings[free_slot]);
    subscribed_topics_cstrings[free_slot] = NULL;
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_disconnect) {
    NATIVE_UNWARN();

    if (MQTTDisconnect(&paho_mqtt_client) < 0) {
        return ERR_IOERROR_EXC;
    }
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_activated_cbks_acquire) {
    MutexLock(&activated_callbacks_mutex);
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_activated_cbks_release) {
    MutexUnlock(&activated_callbacks_mutex);
    *res = MAKE_NONE();
    return ERR_OK;
}

C_NATIVE(_mqtt_topic_match) {
    NATIVE_UNWARN();

    uint32_t topic_len, gen_topic_len,i,j,matching,state;
    uint8_t *topic;
    uint8_t *gen_topic;

    if (parse_py_args("ss", nargs, args, &topic, &topic_len, &gen_topic, &gen_topic_len) != 2)
        return ERR_TYPE_EXC;

    //gen_topic is a mqtt topic with regex + and #
    //topic is a topic without regex
    //return true if gen_topic matches topic
    //
    matching=1;
    j=0;
    state=0; //reading topic level
    uint8_t *lvl = topic;
    uint8_t *elvl = topic;
    uint8_t *glvl = gen_topic;
    uint8_t *gelvl = gen_topic;
    int nlvl;
    int nglvl;

    for(i=0;i<topic_len;i++){
        // printf("topic @%i %c\n",i,topic[i]);
        if(topic[i]=='/' || (i==topic_len-1)){
            // printf("found / or eos\n");
            //current level finished
            elvl=topic+i;
            if (*elvl=='/') elvl--; //ignore ending slashes
            //now check glevel
            for(;j<gen_topic_len;j++){
                // printf("gen_topic @%i %c\n",j,gen_topic[j]);
                if(gen_topic[j]=='/'|| (j==gen_topic_len-1)) {
                    gelvl=gen_topic+j;
                    if (*gelvl=='/') gelvl--; //ignore ending slashes
                    j++;
                    break;
                }
            }

            //let's compare levels
            if (glvl[0]=='+') {
                //level matches (assuming gen_topic is wellformed, /hhh/+fff/ must not be passed to this function
                // printf("matches due to +\n");
                matching=1;
            }else if (glvl[0]=='#') {
                //level matches and we can exit
                matching=1;
                // printf("matches due to #\n");
                break;
            } else {
                nlvl = elvl-lvl;
                nglvl = gelvl-glvl;
                if(nlvl!=nglvl) {
                    //can't match
                    // printf("different lvl length!\n");
                    matching=0;
                    break;
                } else {
                    if(memcmp(lvl,glvl,nlvl)==0) {
                        //they match, go on
                        // printf("same lvl match\n");
                        matching=1;
                    } else {
                        // printf("different lvl unmatch\n");
                        matching=0;
                        break;
                    }
                }
            }
            lvl=topic+i+1;
            glvl=gen_topic+j;
        } 
        //else ignore and go on
    }
    //it is possible for gen_topic to be longer than topic (i.e. zerynth/samples/# vs zerynth/samples)
    if (matching) {
        if (j<gen_topic_len) {
            //we need to check the remaining gen_topic is #
            if(gen_topic[j]!='#') {
                //oops, can't match
                // printf("not matching due to gen_topic\n");
                matching=0;
            }
        }
    }
    *res = PSMALLINT_NEW(matching);
    
    return ERR_OK;
}

