/*
 * morse.c:
 *      This app transmits morse code on a GPIO
 *
 *	How to test:
 *      Connect the GPIO to a sonalert 
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
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <wiringPi.h>
#include <sys/mman.h>
#include "beep.h"

#define DitLen 2
#define DahLen 5
#define DitDahSpaceLen 2
#define CharLen 5
#define SpaceLen 8
#define WPM 5
#define msPerTick 50

volatile unsigned *timer;
int mode_debug = 1;
int BeepPin;

char code[26][5] = {
//      a     b       c       d      e    f       g      h       i     j             
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", 
//	k      l       m     n     o      p       q       r      s      t         
	"-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",  
//	u      v       w      x       y       z
	"..-", "...-", ".--", "-..-", "-.--", "--.."
};

char num[10][6] = {
//      1        2        3        4        5                 
	".----", "..---", "...--", "....-", ".....", 
//      6        7        8        9        0                 
	"-....", "--...", "---..", "----.", "-----", 
};

char punc[0][0] = {
};

/*
 *********************************************************************************
 * support functions
 *********************************************************************************
 */

int SendChar(char ch)
{
	int index, i;
	char ditdahchar;
	
	if ( (ch > 96) && (ch < 123) ) // lower case
		index = ch - 97; 
	else if ( (ch > 64) && (ch < 91) ) // capitals
		index = ch - 65;
	else if ( (ch > 48) && (ch < 58) ) // numbers
		index = ch - 49;
	else if (ch == 48)  // 0
		index = 10;
	else
		return -1;
		
	for (ditdahchar = code[index][0], i = 0; ditdahchar != '\0'; i++, ditdahchar = code[index][i])
	{
		if (mode_debug)
			printf("%c", ditdahchar);

		digitalWrite(BeepPin, HIGH);
		if (ditdahchar == '.')
			usleep(msPerTick * DitLen * 1000);
		else // assume "-"
			usleep(msPerTick * DahLen * 1000);
		digitalWrite(BeepPin, LOW);

		usleep(msPerTick * DitDahSpaceLen * 1000);
	}
	
	if (mode_debug)
		printf("|");

	return 0;
}

/*
 *********************************************************************************
 * access functions
 *********************************************************************************
 */

int BeepInit (int beeppin, int debug)
{
	int err = 0;
	
	BeepPin = beeppin;
	mode_debug = debug;
	
	digitalWrite(BeepPin, LOW);
	pinMode(BeepPin, OUTPUT);

	return err;
}

int BeepMorse(int wpm, char* message)
{
	char* msg = message;
	
	while (*msg != '\0')
	{
		if (*msg == ' ')
		{
			usleep(msPerTick * SpaceLen * 1000);
			if (mode_debug)
				printf(" ");
		}
		else
		{
			usleep(msPerTick * CharLen * 1000);
			SendChar(*msg);
		}
		msg++;
	}
	
	if (mode_debug)
		printf("\n");
	
	return 0;
}

