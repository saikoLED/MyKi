/*

*********************************
* Created 23 November 2013      *
* Copyright 2013, Brian Neltner *
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

This software implements 4x 16-bit PWM with a fully saturated and
color corrected fading algorithm in HSI color space, and 
HSI -> RGBW conversion to allow for better pastel color if desired.

It also implements an parabolic mapping for LED desired
brightness to actual output RGB value.

*/
  
#include "math.h"
#define DEG_TO_RAD(X) (M_PI*(X)/180)

#define steptime 1
#define saturation 1
#define WHITE OCR1A
#define BLUE OCR1B
#define GREEN OCR1C
#define RED OCR3A
#define hue_increment 0.01

int whitevalue;

struct HSI {
  float h;
  float s;
  float i;
} color;

void setup()  {
  // This section of setup ignores Arduino conventions because
  // they are too confusing to follow. Instead, I am figuring
  // this out directly from the datasheet for the ATmega32u4.
  
  // Pin Setup
  // Change the data direction on B5, B6, and B7 to 
  // output without overwriting the directions of B0-4.
  // B5/OC1A is WHITE, B6/OC1B is BLUE, and B7/OC1C is GREEN.
  // These correspond to pins 9, 10, and 11 on an Arduino Leonardo.

  DDRB |= (1<<7)|(1<<6)|(1<<5);

  // PWM Setup Stuff
  // TCCR1A is the Timer/Counter 1 Control Register A.
  // TCCR1A[7:6] = COM1A[1:0] (Compare Output Mode for Channel A)
  // TCCR1A[5:4] = COM1B[1:0] (Compare Output Mode for Channel B)
  // TCCR1A[3:2] = COM1C[1:0] (Compare Output Mode for Channel C)
  // COM1x[1:0] = 0b10 (Clear OC1x on compare match. Set at TOP)
  // TCCR1A[1:0] = WGM1[1:0] (Waveform Generation Mode for Timer 1 LSB)
  // TCCR1B[4:3] = WGM1[3:2] (Waveform Generation Mode for Timer 1 MSB)
  // WGM[3:0] = 0b1110 (Mode 14)
  // Mode 14 - Fast PWM, TOP=ICR1, Update OCR1x at TOP, TOVn Flag on TOP
  // So, ICR1 is the source for TOP.

  // Clock Stuff
  // TCCR1B[2:0] = CS1[2:0] (Clock Select for Timer 1)
  // CS1[2:0] = 001 (No Prescaler)

  // Unimportant Stuff
  // TCCR1B7 = ICNC1 (Input Capture Noise Canceler - Disable with 0)
  // TCCR1B6 = ICES1 (Input Capture Edge Select - Disable with 0)
  // TCCR1B5 = Reserved

  // And put it all together and... tada!
  TCCR1A = 0b10101010; 
  TCCR1B = 0b00011001;

  // Writes 0xFFFF to the ICR1 register. THIS IS THE SOURCE FOR TOP.
  // This defines our maximum count before resetting the output pins
  // and resetting the timer. 0xFFFF means 16-bit resolution.

  ICR1 = 0xFFFF;
  
  // Similarly for Red, set the OC3A/PC6 pin to output.
  // This corresponds to Pin 5 on an Arduino Leonardo.
  
  DDRC |= (1<<6);
  
  // In this case, TCCR3A is our control setup register, and it matches
  // TCCR1A in layout. We're only using Channel A (the others aren't 
  // accessible anyway), so we can leave Channel B and C to 00. The
  // logic of WGM3[3:0] is identical, so we can leave that unchanged.
  // Similarly, TCCR3B has the same logic, so can be left unchanged.
  
  TCCR3A = 0b10000010;
  TCCR3B = 0b00011001;
  
  // And finally, we need to set ICR3 as the source for TOP in the Fast PWM.
  
  ICR3 = 0xFFFF;
  
  // Example of setting 16-bit PWM value.

  // Initial RGB values.
  RED = 0x0000;
  GREEN = 0x0000;
  BLUE = 0x0000;
  WHITE = 0x0000;
  
  color.h = 0;
  color.s = saturation;
  color.i = 0;
  
  // Initial color = off, hue of red fully saturated.
  while (color.i < 1) {
    sendcolor();
    color.i = color.i + 0.001; // Increase Intensity
    updatehue();
    updatesaturation();
    delay (steptime);
  }
}

void updatehue() {
  color.h = color.h + hue_increment;
}

void updatesaturation() {
  color.s = saturation;
}

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
  
void loop()  {
  sendcolor();
  updatehue();
  updatesaturation();
  delay (steptime);
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
