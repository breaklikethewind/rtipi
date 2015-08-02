/*
 * sump.c:
 *      This app manages the sump pump
 *
 *
 * Copyright (c) 2014 Eric Nelson
 ***********************************************************************
 * This file uses wiringPi:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with wiringPi.  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "transport.h"

#define PAIR_PERIOD 30

typedef struct transport
{
	int paired;
	int sequencenumber;
	int exit;
	int push_period;
} transport_t;    

void *thread_data_push(void *ptr);
void *thread_request_handler(void *ptr);
void data_push(pushlist_t* pushlist);
int pair(char* request, char* response);
int sendupdate(char* request, char* response);

struct sockaddr_in servaddr, cliaddr, alladdr;
static pthread_t request_thread, push_thread;
static transport_t transport;
static pushlist_t* pushlist;
static pthread_mutex_t req_lock; 
static pthread_mutex_t push_lock;

commandlist_t commandlist[100]; // keep simple, statically allocate 100 possible commands
int req_err = 0;
int push_err = 0;

commandlist_t sequence_number = 
{ "",        "SEQUENCENUMBER",       NULL, TYPE_INTEGER, &transport.sequencenumber};

#define PAIR_COMMAND 0 // this is needed to advertise the need to pair
commandlist_t transport_commands[] = { 
{ "SETPAIR",         "PAIR",         &pair, TYPE_INTEGER, NULL},
{ "SHUTDOWN",        "SHUTDOWN",     NULL, TYPE_INTEGER, &transport.exit},
{ "SETPUSHPERIOD",   "PUSHPERIOD",   NULL, TYPE_INTEGER, &transport.push_period},
{ "SENDUPDATE",      "UPDATE",       &sendupdate, TYPE_INTEGER, NULL},
{ "",                "",             NULL, TYPE_NULL,    NULL} 
};

int sendupdate(char* request, char* response)
{
	sprintf(response, "1");
	data_push(pushlist);
	
	return 0;
}

int pair(char* request, char* response)
{
	char* junk;

	transport.paired = strtol(request, &junk, 0);
	sprintf(response, "%u", transport.paired);
	
	if (transport.paired)
		printf("Paired with %s\r\n", inet_ntoa(cliaddr.sin_addr));
	else
		printf("Un-paired\r\n");
	
	return 0;
}

void tp_stop_handlers()
{
	int sockfd;
	char sendmesg[200] = {0};

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	sprintf(sendmesg, "PAIR=0\r\n");
	sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&alladdr, sizeof(alladdr));
	
	transport.exit = 1;
	pthread_join(request_thread, NULL);
	pthread_join(push_thread, NULL);
}

int tp_handle_requests(commandlist_t* device_commandlist, pthread_mutex_t* lock)
{
	int i;
	int err;

	// merge device command list, and transport command list
	i = 0;
	while(strlen(transport_commands[i].request) != 0)
	{
		memcpy((void*)&(commandlist[i]), (void*)&(transport_commands[i]), sizeof(commandlist_t));
		i++;
	}

	while(strlen(device_commandlist[i].request) != 0)
	{
		memcpy((void*)&(commandlist[i]), (void*)&(device_commandlist[i]), sizeof(commandlist_t));
		i++;
	}
    
	err = pthread_create( &request_thread, NULL, thread_request_handler, (void*)&commandlist);
	if(err)
	{
		printf("Error - pthread_create() fail\r\n");
		return -1;
	}
	else
	{
		printf("Launching thread request_handler\r\n");
		return 0;
	}
	
	req_lock = *lock;
	
	return req_err;
}

void *thread_request_handler(void *ptr) 
{
	commandlist_t* command_list;
	int sockfd, n, i;
	char* junk;
	socklen_t len;
	char mesg[100];
	char sendmesg[200] = {0};
	char commandfuncdata[100];
	
	command_list = (commandlist_t*)ptr;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(32000);
	bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

	while (!transport.exit)
	{
		len = sizeof(cliaddr);
		n = recvfrom(sockfd, mesg, 1000, 0, (struct sockaddr *)&cliaddr, &len);
		mesg[n] = 0;
		printf("-------------------------------------------------------\r\n");
		printf("Received: %s\r\n\r\n", mesg);
		
		i = 0;
		strcpy(sendmesg, "");
		while ( (strlen(command_list[i].request) != 0) &&
		        (strncmp(command_list[i].request, mesg, strlen(command_list[i].request)) != 0) )
			i++;

		if (strlen(command_list[i].request) != 0)
		{
			if (command_list[i].commandfunc != NULL)
			{
			    // There is a function defined, call the function to get the data string
			    command_list[i].commandfunc(&mesg[strlen(command_list[i].request) + 1], commandfuncdata);
			    sprintf(sendmesg, "%s=%s\r\n", command_list[i].tag, commandfuncdata);
			}
			else if (command_list[i].data != NULL)
			{
			   // There is no function defined, lets 'stringize' the given variable & respond with that
			    pthread_mutex_lock(&req_lock);
			    switch (command_list[i].data_type)
			    {
				case TYPE_INTEGER:
				    *(int*)command_list[i].data = strtol(&mesg[strlen(command_list[i].request) + 1], &junk, 0);
				    sprintf(sendmesg, "%s=%u\r\n", command_list[i].tag, *(int*)command_list[i].data);
				    break;
				case TYPE_FLOAT:
				    *(float*)command_list[i].data = atof(&mesg[strlen(command_list[i].request) + 1]);
				    sprintf(sendmesg, "%s=%.1f\r\n", command_list[i].tag, *(float*)command_list[i].data);
				    break;
				case TYPE_STRING:
				    strcpy(*(char**)command_list[i].data, &mesg[strlen(command_list[i].request) + 1]);
				    sprintf(sendmesg, "%s=%s\r\n", command_list[i].tag, *(char**)command_list[i].data);
				    break;
				case TYPE_NULL:
				    break;
			    }
			    pthread_mutex_unlock(&req_lock);
			}
			
			sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));				
			printf("\r\nResponded: %s", sendmesg);
			printf("-------------------------------------------------------\r\n");
		}
		else			
			printf("INVALID COMMAND\r\n");
	}
	
	req_err = 0;
	return NULL;
}

int tp_handle_data_push(pushlist_t* pushlist, pthread_mutex_t* lock)
{
	int iret; 
	
	iret = pthread_create( &push_thread, NULL, thread_data_push, (void*)pushlist);
	if(iret)
	{
		printf("Error - pthread_create() fail\r\n");
		return -1;
	}
	else
	{
		printf("Launching thread request_handler\r\n");
		return 0;
	}
	
	push_lock = *lock;
	
	return push_err;
}

void *thread_data_push(void *ptr) 
{
	int sockfd;
	char sendmesg[100] = {0};
		
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	alladdr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	
	pushlist = (pushlist_t*)ptr;
	
	while (!transport.exit)
	{
		if (!transport.paired)
		{
			sprintf(sendmesg, "%s=0\r\n", commandlist[PAIR_COMMAND].tag);
			sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&alladdr, sizeof(alladdr));
			printf("Broadcasting 'PAIR=0', to establish pairing\r\n");
			sleep(PAIR_PERIOD);
		}
		else
		{
			data_push((pushlist_t*)ptr);
			sleep(transport.push_period);
		}
	}
	
	push_err = 0;
	return NULL;
}

void data_push(pushlist_t* pushlist)
{
	int i;
	int sockfd;
	char sendmesg[100] = {0};
	
	printf("Pushing data...\r\n");
	
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	
	// Send sensor data to host
	i = 0;
	while (strlen(pushlist[i].tag) != 0)
	{
		if (pushlist[i].data_type == TYPE_INTEGER)
		{
		    pthread_mutex_lock(&push_lock);
		    sprintf(sendmesg, "%s=%u\r\n", pushlist[i].tag, *(unsigned int*)pushlist[i].data);
		    pthread_mutex_unlock(&push_lock);
		    sendto(sockfd,sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		else if (pushlist[i].data_type == TYPE_FLOAT)
		{
		    pthread_mutex_lock(&push_lock);
		    sprintf(sendmesg, "%s=%.1f\r\n", pushlist[i].tag, *(float*)pushlist[i].data);
		    pthread_mutex_unlock(&push_lock);
		    sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		else if (pushlist[i].data_type == TYPE_STRING)
		{
		    pthread_mutex_lock(&push_lock);
		    sprintf(sendmesg, "%s=%s\r\n", pushlist[i].tag, (char*)pushlist[i].data);
		    pthread_mutex_unlock(&push_lock);
		    sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}

		printf("%s", sendmesg);
		
		i++;
	}
    
	sprintf(sendmesg, "%s=%u\r\n", sequence_number.tag, *(unsigned int*)sequence_number.data);
	sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr,sizeof(cliaddr));
	printf("%s", sendmesg);
	
	transport.sequencenumber++;
}


