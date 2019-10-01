/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander - initial API and implementation and/or initial documentation
 *    Ian Craggs - convert to FreeRTOS
 *******************************************************************************/

#include "MQTTZerynth.h"


#define ZERYNTH_MQTT_THREAD_STACK 768

int ThreadStart(Thread* thread, void (*fn)(void*), void* arg)
{
    VThread mqttth_handle = vosThCreate(ZERYNTH_MQTT_THREAD_STACK, VOS_PRIO_NORMAL, fn, arg, NULL);
    vosThResume(mqttth_handle);

	return 0;
}


void MutexInit(Mutex* mutex)
{
    mutex->sem = vosSemCreate(1);
}

// client mutex or active callback mutex and GIL could cause deadlock, 
// release GIL whenever blocking on a mutex

int MutexLock(Mutex* mutex)
{
    RELEASE_GIL();
	vosSemWait(mutex->sem);
    ACQUIRE_GIL();
	return 0;
}

int MutexUnlock(Mutex* mutex)
{
    RELEASE_GIL();
	vosSemSignalCap(mutex->sem, 1);
    ACQUIRE_GIL();
	return 0;
}


void TimerCountdownMS(Timer* timer, unsigned int timeout_ms)
{
	timer->millis_to_wait = timeout_ms;
	timer->start_millis   = vosMillis();
}


void TimerCountdown(Timer* timer, unsigned int timeout) 
{
	TimerCountdownMS(timer, timeout * 1000);
}


int TimerLeftMS(Timer* timer) 
{
	uint32_t delta_t = (uint32_t)(vosMillis() - timer->start_millis);
	if (delta_t > timer->millis_to_wait) return 0;
	return timer->millis_to_wait - delta_t;
}


char TimerIsExpired(Timer* timer)
{
    if (timer->millis_to_wait == 0) return 0; // not expired if not started

	uint32_t delta_t = (uint32_t)(vosMillis() - timer->start_millis);
	if (delta_t > timer->millis_to_wait) return 1;
	return 0;
}


void TimerInit(Timer* timer)
{
	timer->millis_to_wait = 0;
	timer->start_millis   = 0;
}


int Zerynth_read(Network* n, unsigned char* buffer, int len, int timeout_ms)
{
    int rc;
    struct timeval tv;
    fd_set read_fds;

    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = ( timeout_ms % 1000 ) * 1000;

    RELEASE_GIL();

    FD_ZERO( &read_fds );
    if (n->my_socket < FD_SETSIZE) {
        // FD_SET only for allowed sock_ids ( < FD_SETSIZE)
        // if the id is not valid, the zsock_select is implemented in a custom way to 
        // handle such ids 
        // (e.g. for esp32net TLS sockets with id > 300, select is not available, but
        // is implemented checking if data is available to read for only one socket whose id is derived
        // from maxfdp1)
        FD_SET(n->my_socket, &read_fds );
    }

    rc = zsock_select(n->my_socket + 1, &read_fds, NULL, NULL, timeout_ms == 0 ? NULL : &tv );

    /* Zero fds ready means we timed out */
    if ( rc <= 0 ) {
        ACQUIRE_GIL();
        return rc;
    }

    rc = zsock_recv(n->my_socket, buffer, len, 0);
    ACQUIRE_GIL();
    return rc;

#if 0
    uint32_t start_millis = _systime_millis;
    int recvLen = 0;
    zsock_setsockopt(n->my_socket, 0, SO_RCVTIMEO, &tv, sizeof(struct timeval));
    do
    {
        rc = 0;

        rc = zsock_recv(n->my_socket, buffer + recvLen, len - recvLen, 0);
        if (rc > 0)
            recvLen += rc;
        else if (rc < 0)
        {
            recvLen = rc;
            break;
        }

    } while (recvLen < len && (_systime_millis - start_millis) < timeout_ms);
    ACQUIRE_GIL();


	return recvLen;
#endif
}


int Zerynth_write(Network* n, unsigned char* buffer, int len, int timeout_ms)
{
	int sentLen = 0;
	uint64_t start_millis = vosMillis();//_systime_millis;

    RELEASE_GIL();
    do
    {
        int rc = 0;

		rc = zsock_send(n->my_socket, buffer + sentLen, len - sentLen, 0);

        if (rc > 0)
            sentLen += rc;
        else if (rc < 0)
        {
            sentLen = rc;
            break;
        }
    } while (sentLen < len && ((vosMillis() - start_millis) < timeout_ms));
    ACQUIRE_GIL();


	return sentLen;
}


void Zerynth_disconnect(Network* n)
{
	zsock_close(n->my_socket);
}


void NetworkInit(Network* n)
{
	n->my_socket = 0;
	n->mqttread = Zerynth_read;
	n->mqttwrite = Zerynth_write;
	n->disconnect = Zerynth_disconnect;
}


#if 0
// Network Connect is implemented in Python to handle both TLS and non TLS more easily

int NetworkConnect(Network* n, char* addr, int port)
{
	struct sockaddr sAddr;
	int retVal = -1;
	uint32_t ipAddress;

    struct addrinfo *ai_res, *cur;

    RELEASE_GIL();

    zsock_getaddrinfo(addr, NULL, NULL, &ai_res);

    for ( cur = ai_res; cur != NULL; cur = cur->ai_next ) {

        int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if ( fd < 0 ) {
            retVal = -1;
            continue;
        }

		// should be filled by getaddrinfo, but would need to convert to string and back to int
        ((struct sockaddr_in*) cur->ai_addr)->sin_port = Zerynth_htons(port); 

        if ( zsock_connect( fd, cur->ai_addr, cur->ai_addrlen ) == 0 ) {
            n->my_socket = fd; // connected!
            retVal = 0;
            break;
        }

        zsock_close( fd );
        retVal = -1;
    }

    zsock_freeaddrinfo( ai_res );
    ACQUIRE_GIL();

    return ( retVal );
}

int NetworkConnectTLS(Network *n, char* addr, int port, SlSockSecureFiles_t* certificates, unsigned char sec_method, unsigned int cipher, char server_verify)
{

}
#endif
