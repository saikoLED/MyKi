/*

*********************************
* Created 23 August 2012          *
* Copyright 2012, Brian Neltner *
* http://saikoled.com           *
* Licensed under GPL3           *
*********************************

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

A copy of the GNU General Public License can be found at
http://www.gnu.org/licenses/gpl.html

Description:
This sketch implements psychadelic mode, as implemented in Fnord aka Seizure Dome.
It has some legacy code that's not in use that was in my default template for 
working with the saikoduino.

*/

#include "math.h"
#define DEG_TO_RAD(X) (M_PI*(X)/180)

#define WHITE OCR1A
#define BLUE OCR1B
#define GREEN OCR1C
#define RED OCR3A

int whitevalue;

long int delaytime;  // Seconds between mode changes.
long int modifier;
int color1;
int color2;

struct HSI {
  float h;
  float s;
  float i;
  float htarget;
  float starget;
} color;

void sendcolor() {
  int rgbw[4];
  while (color.h >=360) color.h = color.h - 360;
  while (color.h < 0) color.h = color.h + 360;
  if (color.i > 1) color.i = 1;
  if (color.i < 0) color.i = 0;
  if (color.s > 1) color.s = 1;
  if (color.s < 0) color.s = 0;
  // Fix ranges (somewhat redundantly).
  hsi2rgbw(color.h, color.s, color.i, rgbw);
  RED = rgbw[0];
  GREEN = rgbw[1];
  BLUE = rgbw[2];
  WHITE = rgbw[3];
}

void hsi2rgbw(float H, float S, float I, int* rgbw) {
  float r, g, b, w;
  float cos_h, cos_1047_h;
  H = fmod(H,360); // cycle H around to 0-360 degrees
  H = 3.14159*H/(float)180; // Convert to radians.
  S = S>0?(S<1?S:1):0; // clamp S and I to interval [0,1]
  I = I>0?(I<1?I:1):0;
  
  // This section is modified by the addition of white so that it assumes 
  // fully saturated colors, and then scales with white to lower saturation.
  //
  // Next, scale appropriately the pure color by mixing with the white channel.
  // Saturation is defined as "the ratio of colorfulness to brightness" so we will
  // do this by a simple ratio wherein the color values are scaled down by (1-S)
  // while the white LED is placed at S.
  
  // This will maintain constant brightness because in HSI, R+B+G = I. Thus, 
  // S*(R+B+G) = S*I. If we add to this (1-S)*I, where I is the total intensity,
  // the sum intensity stays constant while the ratio of colorfulness to brightness
  // goes down by S linearly relative to total Intensity, which is constant.

  if(H < 2.09439) {
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    r = S*I/3*(1+cos_h/cos_1047_h);
    g = S*I/3*(1+(1-cos_h/cos_1047_h));
    b = 0;
    w = (1-S)*I;
  } else if(H < 4.188787) {
    H = H - 2.09439;
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    g = S*I/3*(1+cos_h/cos_1047_h);
    b = S*I/3*(1+(1-cos_h/cos_1047_h));
    r = 0;
    w = (1-S)*I;
  } else {
    H = H - 4.188787;
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    b = S*I/3*(1+cos_h/cos_1047_h);
    r = S*I/3*(1+(1-cos_h/cos_1047_h));
    g = 0;
    w = (1-S)*I;
  }
  
  // Mapping Function from rgbw = [0:1] onto their respective ranges.
  // For standard use, this would be [0:1]->[0:0xFFFF] for instance.

  // Here instead I am going to try a parabolic map followed by scaling.  
  rgbw[0]=0xFFFF*r*r;
  rgbw[1]=0xFFFF*g*g;
  rgbw[2]=0xFFFF*b*b;
  rgbw[3]=0xFFFF*w*w;
}

void setup() {
  // Configure LED pins.
  DDRB |= (1<<7)|(1<<6)|(1<<5);
  TCCR1A = 0b10101010; 
  TCCR1B = 0b00011001;
  ICR1 = 0xFFFF;
  RED = 0x0000;
  GREEN = 0x0000;
  BLUE = 0x0000;
  DDRC |= (1<<6); // Red LED PWM Port.
  TCCR3A = 0b10000010;
  TCCR3B = 0b00011001;
  ICR3 = 0xFFFF;
  
  // Set prescalers to 1 for all timers; note that this means that millis() et all will be 64x too short.
  
  color.i = 1;
  color.s = 1;
  
  color1 = 0;
  color2 = 120;
  
  delaytime = 150;
  modifier = 5;
}

void loop() {
  color.h = color1;
  sendcolor();
  delay(delaytime/10);
  color.h = color2;
  sendcolor();
  delay(delaytime/10);
  delaytime = delaytime + modifier;
  if (delaytime > 500 or delaytime < 150) modifier = -modifier;
  color1 += 1;
  color2 += 1;
  while (color1 >= 360) color1 = color1 - 360;
  while (color2 >= 360) color2 = color2 - 360;
}
