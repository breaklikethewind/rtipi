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

struct sockaddr_in servaddr, cliaddr, alladdr;
pthread_t request_thread, push_thread;
transport_t transport;
pthread_mutex_t lock; // sync between UDP thread and main
void *thread_data_push(void *ptr);
void *thread_request_handler(void *ptr);
void data_push(pushlist_t* pushlist);
commandlist_t commandlist[100]; // keep simple, statically allocate 100 possible commands

commandlist_t transport_commands = { \
{ "SHUTDOWN",        "SHUTDOWN",     NULL, TYPE_INTEGER, &transport.exit},
{ "SETPUSHPERIOD",   "PUSHPERIOD",   NULL, TYPE_INTEGER, &transport.push_period},
{ "SETPAIR",         "PAIR",         NULL, TYPE_INTEGER, &transport.paired},
{ "SETUPDATE",       "UPDATE",       NULL, TYPE_INTEGER, },
{ NULL,              NULL,           NULL, 0,            NULL} 
};

void thread_exit()
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

int handle_requests(commandlist_t* device_commandlist, pthread_mutex_t* lock)
{
	int i;

	// merge device command list, and transport command list
	i = 0;
	while(transport_commands[i].request != NULL)
	{
		memcpy(commandlist[i], transport_commands[i], sizeof(commandlist_t));
		i++;
	}

	while(device_commandlist[i].request != NULL)
	{
		memcpy(commandlist[i], device_commandlist[i], sizeof(commandlist_t));
		i++;
	}
    
	iret1 = pthread_create( &request_thread, NULL, thread_request_handler, (void*) command_list);
	if(iret1)
	{
		printf("Error - pthread_create() fail\r\n");
		return -1;
	}
	else
	{
		printf("Launching thread request_handler\r\n");
		return 0;
	}
}

void *thread_request_handler(void *ptr) 
{
	commandlist_t command_list;
	int sockfd, n, i;
	socklen_t len;
	char mesg[100];
	char sendmesg[200] = {0};
	char commandfuncdata[100];
	
	command_list = (commandlist_t)ptr;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(32000);
	bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

	while (!transport.exit)
	{
		len = sizeof(cliaddr);
		n = recvfrom(sockfd, mesg, 1000, 0, (struct sockaddr *)&cliaddr, &len);
		printf("-------------------------------------------------------\n");
		mesg[n] = 0;
		i = 0;
		printf("Received: %s\r\n",mesg);

		while (command_list[i]->request != NULL)
		{
			if (command_list[i]->commandfunc != NULL)
			{
			    // There is a function defined, call the function to get the data string
			    command_list[i]->commandfunc(command_list[i]->tag, commandfuncdata);
			    sprintf(sendmesg, "%s=%s\r\n", command_list[i]->tag, commandfuncdata);
			}
			else if (command_list[i]->data != NULL)
			{
			   // There is no function defined, lets 'stringize' the given variable & respond with that
			    pthread_mutex_lock(lock);
			    switch (command_list[i]->data_type)
			    {
				case TYPE_INTEGER:
				    sprintf(sendmesg, "%s=%u\r\n", command_list[i]->tag, *(int*)command_list[i]->data);
				    break;
				case TYPE_FLOAT:
				    sprintf(sendmesg, "%s=%.1f\r\n", command_list[i]->tag, *(float*)command_list[i]->data);
				    break;
				case TYPE_STRING:
				    sprintf(sendmesg, "%s=%s\r\n", command_list[i]->tag, *(char**)command_list[i]->data);
				    break;
			    }
			    pthread_mutex_unlock(lock);
			}
			else 
			    // We dont have a function or data we can respond with! Now what???
			    return -1;
			
			sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));				
			
			i++;
		}
		
		if (command_list[i]->request == NULL)
		{
			sprintf(sendmesg, "INVALID COMMAND\r\n");
			sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}			
			
		printf("Responded: %s", sendmesg);
		printf("-------------------------------------------------------\n");
	}
	
	return NULL;
}

int push_data(pushlist_t* pushlist, pthread_mutex_t* lock)
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
}

void *thread_data_push(void *ptr) 
{
	int sockfd;
	char sendmesg[1000] = {0};
		
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	alladdr.sin_addr.s_addr=htonl(INADDR_BROADCAST);
	
	while (!transport.exit)
	{
		if (!transport.paired)
		{
			sprintf(sendmesg, "PAIR=0\r\n");
			sendto(sockfd, sendmesg, sizeof(sendmesg), 0, (struct sockaddr *)&alladdr, sizeof(alladdr));
			printf("Broadcasting 'PAIR=0', to establish pairing\r\n");
			sleep(PAIR_PERIOD);
		}
		else
		{
			data_push((pushlist_t)ptr);
			sleep(transport.push_period);
		}
	}
	
	return NULL;
}

void data_push(pushlist_t* pushlist)
{
	int sockfd;
	pushlist_t pushlist;
	char sendmesg[100] = {0};
	
	printf("Pushing data...\r\n");
	
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	// Send sensor data to host
	i = 0;
	while (pushlist[i]->tag != NULL)
	{
		if (pushlist[i]->data_type == TYPE_INTEGER)
		{
		    pthread_mutex_lock(lock);
		    sprintf(sendmesg, "%s=%u\r\n", pushlist[i]->tag, (*unsinged int)pushlist[i]->data);
		    pthread_mutex_unlock(lock);
		    sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		    printf("%s", sendmesg);
		}
		else if (pushlist[i]->data_type == TYPE_FLOAT)
		{
		    pthread_mutex_lock(lock);
		    sprintf(sendmesg, "%s=%.1f\r\n", pushlist[i]->tag, (*float)pushlist[i]->data);
		    pthread_mutex_unlock(lock);
		    sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		    printf("%s", sendmesg);
		}
		else if (pushlist[i]->data_type == TYPE_STRING)
		{
		    pthread_mutex_lock(lock);
		    sprintf(sendmesg, "%s=%s\r\n", pushlist[i]->tag, *(char*)pushlist[i]->data);
		    pthread_mutex_unlock(lock);
		    sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		    printf("%s", sendmesg);
		}

		i++
	}
    
	sprintf(sendmesg, "SEQUENCENUMBER=%u\r\n", transport.sequencenumber);
	sendto(sockfd,sendmesg,sizeof(sendmesg),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
	printf("%s", sendmesg);
	
	transport.sequencenumber++;

    printf("\r\n");
}


