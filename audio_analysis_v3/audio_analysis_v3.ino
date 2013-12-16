#include "math.h"
#define DEG_TO_RAD(X) (M_PI*(X)/180)

#define steptime 1
#define propgain 0.0005 // "Small Constant"
#define minsaturation 0.7
#define maxsaturation 1
#define redPin 9    // Red LED connected to digital pin 9
#define greenPin 10 // Green LED connected to digital pin 10
#define bluePin 11 // Blue LED connected to digital pin 11
#define whitePin 13 // White LED connected to digital pin 13
#define audioPin 0 // Audio connected to AI0
#define strobePin 4 // MSGEQ7 Strobe Pin
#define resetPin 7 // MSGEQ7 Reset Pin
#define thresholdvoltage 5 // This voltage above the lowest seen will show up in the scaled value.
#define typicalrange 5 // This is the typical voltage difference between quiet and loud.
#define alphashift 0.1 // This is the time constant for shifting the high limit.
#define alphareturn 0.001 // This is the time constant for returning to typical values.
#define alpharead 0.1 // This is the time constant for filtering incoming audio magnitudes.
#define alphalow 0.001 // This is the time constant for moving the low limit.
#define delaytime 1 // Milliseconds between sequential MSGEQ7 readings.

float currentvalue[7] = {45, 50, 40, 45, 45, 90, 100};
float scaledvalue[7];
float limitedvalue[7];

unsigned int typicallow[7] = {45, 50, 40, 45, 45, 90, 100}; // Typical noise floors.
unsigned int typicalhigh[7]; // Approximate max value.

float currentlow[7] = {45, 50, 40, 45, 45, 90, 100}; // Initial noise floor.
float currenthigh[7]; // Initial max value.

unsigned int band[7] = {63, 160, 400, 1000, 2500, 6250, 16000};

void setup() {
  
  // Configure LED pins.
  pinMode(whitePin, OUTPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  TCCR0B = _BV(CS00);
  TCCR1B = _BV(CS10);
  TCCR3B = _BV(CS30);
  TCCR4B = _BV(CS40);
  digitalWrite(whitePin, 0);
  digitalWrite(redPin, 0);
  digitalWrite(greenPin, 0);
  digitalWrite(bluePin, 0);
  
  // Setup Serial Communication
  Serial.begin(57600);
  Serial.println("Booting up Audio Analysis -- Saikoduino v7");
  
  // Configure Audio Pins
  pinMode(audioPin, INPUT);
  pinMode(strobePin, OUTPUT);
  pinMode(resetPin, OUTPUT);
  digitalWrite(resetPin, LOW);
  digitalWrite(strobePin, HIGH);
  analogReference(DEFAULT);
  
  for (int i=0; i<7; i++) {
    currentlow[i] += thresholdvoltage;
    typicallow[i] += thresholdvoltage;
    currenthigh[i] = currentlow[i] + typicalrange;
    typicalhigh[i] = typicallow[i] + typicalrange;
  }
}

void loop() {
  float intensityred = 0;
  float intensitygreen = 0;
  float intensityblue = 0;
  float frequencyred = 0;
  float frequencygreen = 0;
  float frequencyblue = 0;
  float totalpowerred = 0;
  float totalpowergreen = 0;
  float totalpowerblue = 0;
  float maxpowerred = 0;
  float maxpowergreen = 0;
  float maxpowerblue = 0;
  
  
  digitalWrite(resetPin, HIGH);
  digitalWrite(resetPin, LOW);
  delayMicroseconds(72);

  for(int i=0; i<7; i++) {
    digitalWrite(strobePin, LOW);
    delayMicroseconds(35);
    unsigned int momentaryvalue = analogRead(audioPin); // Get ADC Value.
    
    // Filter input audio signal.
    currentvalue[i] = alpharead*(float)momentaryvalue + (1-alpharead)*(float)currentvalue[i];
    
    // Guarantee that the currentlow is not going to cause negative values in comparisons.
    if (currentlow[i]<thresholdvoltage) currentlow[i]=thresholdvoltage;
    
    // Set default limited value.
    limitedvalue[i] = currentvalue[i];

    // Automatically generate an expected "current low" value as being the filtered current value plus a threshold.
    currentlow[i] = alphalow*((float)currentvalue[i]+thresholdvoltage) + (1-alphalow)*(float)currentlow[i];
    
    // If the value is higher than anything seen yet, it is the new high and limit the value.
    if (currentvalue[i] > currenthigh[i]) {
      currenthigh[i] = alphashift*(float)currentvalue[i] + (1-alphashift)*(float)currenthigh[i];
      limitedvalue[i] = currenthigh[i];
    }
    
    // Limit the value based on the current low.
    if (currentvalue[i] < currentlow[i]) limitedvalue[i] = currentlow[i];
    
    // Map [currentlow, currenthigh] to [0, 0xFF].
  
    scaledvalue[i] = (float)(limitedvalue[i]-currentlow[i])/(float)(currenthigh[i]-currentlow[i])*0xFF;

    if (i<2) {
      intensityred += scaledvalue[i]/2;
      totalpowerred += (limitedvalue[i]-currentlow[i]);
      maxpowerred += (currenthigh[i]-currentlow[i]);
      frequencyred += scaledvalue[i]/0xFF*i;
    }
    else if (i<4) {
      intensitygreen += scaledvalue[i]/2;
      totalpowergreen += (limitedvalue[i]-currentlow[i]);
      maxpowergreen += (currenthigh[i]-currentlow[i]);
      frequencygreen += scaledvalue[i]/0xFF*i;
    }
    else if (i<5) {
      intensityblue += scaledvalue[i];
      totalpowerblue += (limitedvalue[i]-currentlow[i]);
      maxpowerblue += (currenthigh[i]-currentlow[i]);
      frequencyblue += scaledvalue[i]/0xFF*i;
    }
          
    currenthigh[i] = (1-alphareturn)*currenthigh[i]+alphareturn*(currentlow[i]+typicalrange);
    
    digitalWrite(strobePin, HIGH);
    delayMicroseconds(72);
  }

  analogWrite(redPin, intensityred);
  analogWrite(greenPin, intensitygreen);
  analogWrite(bluePin, intensityblue);
  
  delay(delaytime);
}
