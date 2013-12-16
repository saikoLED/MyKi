/*

 *********************************
 * Created 23 August 2012        *
 * Copyright 2012, Brian Neltner *
 *           2013, Daniel Taub   *
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
 The purpose of this sketch is to combine six established
 programs on the saikoduino so that they are automatically
 rotated between.
 
 DMX_MODE_ADDRESS channel value:
     0-126 inclusive   - Normal DMX
     127-255 inclusive - Audio Mode

 DMX_START_ADDRESS   =>  Hue
 DMX_START_ADDRESS+1 =>  Saturation
 DMX_START_ADDRESS+2 =>  Intensity
 */

// Start address for light.
#define DMX_START_ADDRESS 97

// address to switch all lights into audio mode
#define DMX_MODE_ADDRESS 96

// Smoothing rate for light (1 is immediate, small is slow).
#define propgain 0.02

/**********************************************************/

#include "math.h"
#define DEG_TO_RAD(X) (M_PI*(X)/180)

#define RED OCR1A
#define GREEN OCR1B
#define BLUE OCR1C
#define WHITE OCR4A

#define audioPin 0 // Audio connected to AI0
#define strobePin 4 // MSGEQ7 Strobe Pin
#define resetPin 7 // MSGEQ7 Reset Pin  
#define mode_delaytime 142 // Seconds between mode changes.

// Definitions for DMX mode.
#define SIZE 512 // max frame size, if you want to do it this way
volatile byte dmx_buffer[SIZE]; // allocate buffer space
volatile unsigned int dmx_ptr = 0;
volatile byte dmx_state = 0; // state tracker
volatile byte address[4]; // received information

// Definitions for audio responsive mode.
#define audiothresholdvoltage 5 // This voltage above the lowest seen will show up in the scaled value.
#define audiotypicalrange 5 // This is the typical voltage difference between quiet and loud.
#define audioalphashift 0.1 // This is the time constant for shifting the high limit.
#define audioalphareturn 0.001 // This is the time constant for returning to typical values.
#define audioalpharead 0.1 // This is the time constant for filtering incoming audio magnitudes.
#define audioalphalow 0.001 // This is the time constant for moving the low limit.
#define audiodelaytime 1 // Milliseconds between sequential MSGEQ7 readings.

float audiocurrentvalue[7] = {
  45, 50, 40, 45, 45, 90, 100};
float audioscaledvalue[7];
float audiolimitedvalue[7];

unsigned int audiotypicallow[7] = {
  45, 50, 40, 45, 45, 90, 100}; // Typical noise floors.
unsigned int audiotypicalhigh[7]; // Approximate max value.

float audiocurrentlow[7] = {
  45, 50, 40, 45, 45, 90, 100}; // Initial noise floor.
float audiocurrenthigh[7]; // Initial max value.

unsigned int audioband[7] = {
  63, 160, 400, 1000, 2500, 6250, 16000};

struct HSI {
  float h;
  float s;
  float i;
  float htarget;
  float starget;
  float itarget;
} 
color;

enum mode {
  audio,
  dmx
} 
mode;

enum dmx_data_state {
  dataready,
  datawaiting
} 
dmx_data_state;

enum audio_state {
  audioinitialize,
  audioanalyze,
  audiohold
} 
audio_state;

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
  WHITE = rgbw[3]; // Low 8 bits of white PWM.

  //TC4H = rgbw[3] >> 8; // High 2 bits of white PWM.
  //WHITE = 0xFF & rgbw[3]; // Low 8 bits of white PWM.
}

// Fading functionality based on an integrator with propgain.
void updatehue() {
  color.h += propgain*(color.htarget-color.h);
}

void updatesaturation() {
  color.s += propgain*(color.starget-color.s);
}

void updateintensity() {
  color.i += propgain*(color.itarget-color.i);
}

void hsi2rgbw(float H, float S, float I, int* rgbw) {
  float r, g, b, w;
  float cos_h, cos_1047_h;
  H = fmod(H,360); // cycle H around to 0-360 degrees
  H = 3.14159*H/(float)180; // Convert to radians.
  S = S>0?(S<1?S:1):
  0; // clamp S and I to interval [0,1]
  I = I>0?(I<1?I:1):
  0;

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
  } 
  else if(H < 4.188787) {
    H = H - 2.09439;
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    g = S*I/3*(1+cos_h/cos_1047_h);
    b = S*I/3*(1+(1-cos_h/cos_1047_h));
    r = 0;
    w = (1-S)*I;
  } 
  else {
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

  rgbw[0]=0xFF*r;
  rgbw[1]=0xFF*g;
  rgbw[2]=0xFF*b;
  rgbw[3]=0xFF*w;

}

// Time-storing Global Variables
unsigned long mode_starttime;
unsigned long solidcolor_starttime;
unsigned long pastelfade_starttime;
unsigned long rainbowfade_starttime;
unsigned long fastfade_starttime;
unsigned long audio_starttime;

void setup() {
  // Configure LED pins.

  DDRB |= (1<<7)|(1<<6)|(1<<5);
  TCCR1A = 0b10101010; 
  TCCR1B = 0b00011001;
  ICR1 = 0xFF;
  RED = 0x00;
  GREEN = 0x00;
  BLUE = 0x00;
  DDRC |= (1<<7); // White LED PWM Port.
  //TC4H = 0x03;
  OCR4C = 0xFF;
  TCCR4A = 0b10000010;
  TCCR4B = 0b00000001;
  //TC4H = 0x00; // High 2 bits of white PWM.
  WHITE = 0x00; // Low 8 bits of white PWM.

  pinMode(audioPin, INPUT);
  pinMode(strobePin, OUTPUT);
  pinMode(resetPin, OUTPUT);
  digitalWrite(resetPin, LOW);
  digitalWrite(strobePin, HIGH);
  analogReference(DEFAULT);

  // Setup for DMX
  DDRD |= 0x03;
  PORTD &= 0xfc;

  UBRR1H = ((F_CPU/250000/16) - 1) >> 8;
  UBRR1L = ((F_CPU/250000/16) - 1);
  UCSR1A = 1<<UDRE1;
  UCSR1C = 1<<USBS1 | 1<<UCSZ11 | 1<<UCSZ10; // 2 stop bits,8 data bitss
  UCSR1B = 1<<RXCIE1 | 1<<RXEN1; // turn it on, set RX complete intrrupt on
  sei();
  dmx_data_state = datawaiting;

  for (int i=0; i<7; i++) {
    audiocurrentlow[i] += audiothresholdvoltage;
    audiotypicallow[i] += audiothresholdvoltage;
    audiocurrenthigh[i] = audiocurrentlow[i] + audiotypicalrange;
    audiotypicalhigh[i] = audiotypicallow[i] + audiotypicalrange;
  }

  // Set initial mode.
  mode = dmx;
}

void loop() {
  float audiointensity = 0;
  float audiofrequencyred = 0;
  float audiofrequencygreen = 0;
  float audiofrequencyblue = 0;
  float audiototalpowerred = 0;
  float audiototalpowergreen = 0;
  float audiototalpowerblue = 0;
  float audiomaxpowerred = 0;
  float audiomaxpowergreen = 0;
  float audiomaxpowerblue = 0;
  // Always checking for a new DMX packet.

  color.htarget = ((float)address[1]/0xFF)*360;
  color.starget = (float)address[2]/0xFF;
  color.itarget = (float)address[3]/0xFF;
  if (address[0]<127) {
    if (mode == audio) {
      // ICR1 = 0xFFFF; // ensure 16-bit PWM mode.
    }
    mode = dmx;
  }
  // If DMX value is >= 127, then audio mode.
  else {
    // Set mode.
    // If it was just changed audio needs to initialize.
    if (mode == dmx) {
      audio_state = audioinitialize;
      //      ICR1 = 0xFF;  // I see no reason to set 8-bit mode for audio...
    }
    mode = audio;
  }

  //  DMX Packet format:
  //  Byte 0 is command (0-127 is normal, 128-255 is audio)
  //  Byte 1 is hue (0-255 is color)
  //  Byte 2 is saturation
  //  Byte 3 is intensity (normal or minimum in audio mode)

  switch(mode) {
    // Data is already received and placed in target,
    // so now this switch statement just uses the specified
    // mode to set how the light responds to the data
    // provided.

    // For "normal" mode, just update the colors as normal.
  case dmx:
    // Regardless of whether or not there is new data, shift current
    // color towards target according to propgain.
    updatehue();
    updatesaturation();
    updateintensity();

    // After updating the real color values, send out to the LED.
    sendcolor();
    delay(1);
    break;

    // For audio mode, set hue and saturation, intensity is
    // calculated by summing all frequency bins of data.
  case audio:
    color.s = color.starget;
    color.h = color.htarget;
    if (audio_state == audioinitialize) {
      audio_state = audioanalyze;
    }
    else if (audio_state == audioanalyze) {

      digitalWrite(resetPin, HIGH);
      digitalWrite(resetPin, LOW);
      delayMicroseconds(72);

      // This asks for data from the audio analysis chip.
      for(int i=0; i<7; i++) {
        digitalWrite(strobePin, LOW);
        delayMicroseconds(35);
        unsigned int audiomomentaryvalue = analogRead(audioPin); // Get ADC Value.

        // Filter input audio signal.
        audiocurrentvalue[i] = audioalpharead*(float)audiomomentaryvalue + (1-audioalpharead)*(float)audiocurrentvalue[i];

        // Guarantee that the currentlow is not going to cause negative values in comparisons.
        if (audiocurrentlow[i]<audiothresholdvoltage) audiocurrentlow[i]=audiothresholdvoltage;

        // Set default limited value.
        audiolimitedvalue[i] = audiocurrentvalue[i];

        // Automatically generate an expected "current low" value as being the filtered current value plus a threshold.
        audiocurrentlow[i] = audioalphalow*((float)audiocurrentvalue[i]+audiothresholdvoltage) + (1-audioalphalow)*(float)audiocurrentlow[i];

        // If the value is higher than anything seen yet, it is the new high and limit the value.
        if (audiocurrentvalue[i] > audiocurrenthigh[i]) {
          audiocurrenthigh[i] = audioalphashift*(float)audiocurrentvalue[i] + (1-audioalphashift)*(float)audiocurrenthigh[i];
          audiolimitedvalue[i] = audiocurrenthigh[i];
        }

        // Limit the value based on the current low.
        if (audiocurrentvalue[i] < audiocurrentlow[i]) audiolimitedvalue[i] = audiocurrentlow[i];

        // Map [currentlow, currenthigh] to [0, 1].

        audioscaledvalue[i] = (float)(audiolimitedvalue[i]-audiocurrentlow[i])/(float)(audiocurrenthigh[i]-audiocurrentlow[i]);

        if (i>1 && i<5) audiointensity += audioscaledvalue[i]/3;

        audiocurrenthigh[i] = (1-audioalphareturn)*audiocurrenthigh[i]+audioalphareturn*(audiocurrentlow[i]+audiotypicalrange);

        digitalWrite(strobePin, HIGH);
        delayMicroseconds(72);
      }


      // Here is where the intensity changes either towards full or towards off, depending on how bright it is already
      if (color.itarget > 0.5) 
        color.i = color.itarget - (color.itarget*audiointensity);
      else
        color.i = color.itarget + (1.0-color.itarget)*(audiointensity);

      //      color.i = audiointensity;
      //      color.i = color.itarget;
      //      color.i = (audiointensity * (0xFF - color.itarget)) + color.itarget;
      sendcolor(); // try 32   
      //        RED = 0xFFFF * (audiointensity);
      break;
    }
  }  
}

ISR(USART1_RX_vect) {

  // handle data transfers
  // check if BREAK byte
  //if ((UCSR1A & 0x10) == 0x10){
  if (UCSR1A &(1<<FE1)) {
    byte temp = UDR1; // get data 
    if (dmx_state == 2) {
      address[0] = dmx_buffer[DMX_MODE_ADDRESS];
      address[1] = dmx_buffer[DMX_START_ADDRESS];
      address[2] = dmx_buffer[DMX_START_ADDRESS+1];
      address[3] = dmx_buffer[DMX_START_ADDRESS+2];
    }
    dmx_state = 1; // set to slot0 byte waiting
    //PORTB |= 0x04;

  }
  // check for state 2 first as its most probable
  else if (dmx_state == 2) {
    dmx_buffer[dmx_ptr] = UDR1;
    dmx_ptr++;
    if (dmx_ptr >= SIZE) {
      dmx_state = 0; // last byte recieved or error
      address[0] = dmx_buffer[DMX_MODE_ADDRESS];
      address[1] = dmx_buffer[DMX_START_ADDRESS];
      address[2] = dmx_buffer[DMX_START_ADDRESS+1];
      address[3] = dmx_buffer[DMX_START_ADDRESS+2];
    }
  }
  else if (dmx_state == 1) {
    // check if slot0 = 0
    if (UDR1) {
      dmx_state = 0; // error - reset reciever
    }
    else {
      dmx_ptr = 0; // reset buffer pointer to beginning
      dmx_state = 2; // set to data waiting
    }
  }
  else {
    byte temp = UDR1;
    dmx_state = 0; // reset - bad condition or done
  }
  //PORTB &= 0xFB;

}

