/*
 * range.c:
 *      This app measures distance using HC-S04 transducer module
 *
 *	How to test:
 *      Connect the echo pin of the HC-S04 to ExitPin, and the 
 *      trigger pin of the HC-S04 to TriggerPin. Be sure not to
 *      drive the input pins of the raspberry pi with 5v of the
 *      HC-S04. A simple voltage divider can cut the voltage to 3.3v. 
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

#ifndef RANGE_H
#define RANGE_H

int RangeInit(int echopin, int triggerpin, int debug);
double RangeMeasure(int average);

#endif


