/********* Trinket M0 Keypad *************/
/* Support for the DotStar LED was hacked
in pretty lazily so I'll probably end up
either replacing this code or adapting the
touchPad code to work for regular keypads
since it was written mostly from scratch,
is much cleaner, and uses the new remapper.

I've done my best to clean this up a bit
and make it more readable, but it would
take a bad amount of time to make it
perfect so this is the best I'm willing
to do for now.

-thnikk */

// M0 specific
#include <Arduino.h>
#ifdef TOUCH
#include "Adafruit_FreeTouch.h"
#endif
#include <FlashAsEEPROM.h>
#include <Adafruit_DotStar.h>
// Universal
#include <Bounce2.h>
#include <Keyboard.h>
//#include <Mouse.h>
#include <Adafruit_NeoPixel.h>
// RGB LED initialization
#define DATAPIN    7
#define CLOCKPIN   8
Adafruit_DotStar dotStar = Adafruit_DotStar( 1, DATAPIN, CLOCKPIN, DOTSTAR_BRG);
#if defined rgbw
  Adafruit_NeoPixel pixels = Adafruit_NeoPixel(numkeys, 1, NEO_GRBW + NEO_KHZ800);
#else
  Adafruit_NeoPixel pixels = Adafruit_NeoPixel(numkeys, 1, NEO_GRB + NEO_KHZ800);
#endif
// Trinket button pins
#ifdef TOUCH
const byte pins[] = { 0, 2, 20, 19 };
#else
const byte pins[] = { 0, 2, 3, 4, 19 };
#endif
char initMapping[] = {"zxcv"};
// Cycle LED Mode
unsigned long cycleMillis;
unsigned long cycleSpeed = 10;
byte cycleWheel;
// Reactive LED mode
byte colorState[numkeys+1];             // States between white, rainbow, fadeout, and off (per key)
byte reactiveWheel[numkeys+1];          // Reactive wheel counter
unsigned long reactSpeed = 0;
unsigned long reactMillis = 0;
// Custom LED mode
byte customWheel[numkeys];
unsigned long customMillis = 0;
unsigned long customSpeed = 5;
// BPS LED mode
unsigned long bpsMillis = 0;
unsigned long bpsMillis2 = 0;
unsigned long bpsSpeed = 0;
unsigned long bpsUpdate = 1000;
byte bpsCount = 0;
byte bpsBuffer = 170;
byte bpsFix = 170;
bool bpsPress[numkeys-1];
// Color change LED mode
bool pressedCC[numkeys]; // New pressed array for colorChange
byte changeColors[numkeys]; // Array for storing colors; auto rollover since it's a byte
byte changeVal = 17; // Amount to increment color on keypress
unsigned long changeMillis; // For millis timer
// Universal LED
byte ledMode = 0;
byte b = 127;  // Brightness
byte rgb[numkeys][3];
byte dsrgb[3];
byte numModes = 7;
// Side button
unsigned long s = 500;
unsigned long m = 1500;
unsigned long sideMillis = 0;
unsigned long brightMillis = 0;
byte hold = 0;
byte blink = 0;
// Universal
Bounce * bounce = new Bounce[5];
bool version = 0;
char mapping[numkeys][3];

// Remap code
byte specialLength = 31; // Number of "special keys"
String specialKeys[] = {
  "shift", "ctrl", "super",
  "alt", "f1", "f2", "f3",
  "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11",
  "f12", "insert",
  "delete", "backspace",
  "enter", "home", "end",
  "pgup", "pgdn", "up",
  "down", "left", "right",
  "tab", "escape", "altGr"
};
byte specialByte[] = {
  129, 128, 131, 130,
  194, 195, 196, 197,
  198, 199, 200, 201,
  202, 203, 204, 205,
  209, 212, 178, 176,
  210, 213, 211, 214,
  218, 217, 216, 215,
  179, 177, 134
};

#ifdef TOUCH
Adafruit_FreeTouch qt_1 = Adafruit_FreeTouch(4, OVERSAMPLE_8, RESISTOR_50K, FREQ_MODE_NONE);
#endif

byte inputBuffer; // Stores specialByte after conversion

unsigned long previousMillis;
bool set=0;
byte dsCycle;
bool dsPress;


/*
███████ ███████ ████████ ██    ██ ██████
██      ██         ██    ██    ██ ██   ██
███████ █████      ██    ██    ██ ██████
     ██ ██         ██    ██    ██ ██
███████ ███████    ██     ██████  ██
*/

void setup() {
	Serial.begin(9600);

  #ifdef TOUCH
	// Set pullups and debounce
	for (byte x=0; x<=numkeys; x++) {	pinMode(pins[x], INPUT_PULLUP);	bounce[x].attach(pins[x]); bounce[x].interval(8); }
  qt_1.begin();
  #else
  for (byte x=0; x<=4; x++) { pinMode(pins[x], INPUT_PULLUP); bounce[x].attach(pins[x]); bounce[x].interval(8); }
  #endif

	dotStar.begin(); // Initialize pins for output
	dotStar.show();  // Turn all LEDs off ASAP
	pixels.begin();

  // Initialize EEPROM
  if (EEPROM.read(0) != version) {
    EEPROM.write(0, version); EEPROM.write(20, ledMode); EEPROM.write(21, b);// Start brightness at half
    for (int x = 0; x < numkeys; x++) { // default custom RGB values
      EEPROM.write(30+x,50*x);
      for (int  y= 0; y < 3; y++) {	if (y == 0) EEPROM.write(40+(x*3)+y, int(initMapping[x])); if (y > 0) EEPROM.write(40+(x*3)+y, 0); }
    }	EEPROM.commit();
  }
  // Load values from EEPROM
  for (int x = 0; x < numkeys; x++) {	for (int  y= 0; y < 3; y++) mapping[x][y] = char(EEPROM.read(40+(x*3)+y)); customWheel[x] = EEPROM.read(30+x); }
  ledMode = EEPROM.read(20); b = EEPROM.read(21);

}

/*
██       ██████   ██████  ██████
██      ██    ██ ██    ██ ██   ██
██      ██    ██ ██    ██ ██████
██      ██    ██ ██    ██ ██
███████  ██████   ██████  ██
*/

#ifdef TOUCH
bool pressed;
int threshold = 400;
int qt;
#endif
void loop() {

  if ((millis() - previousMillis) > 1000) { // Check once a second to reduce overhead
    if (Serial && set == 0) { // Run once when serial monitor is opened to avoid flooding the serial monitor
      Serial.println("Please press 0 to enter the serial remapper.");
      set = 1;
    }
    // If 0 is received at any point, enter the remapper.
    if (Serial.available() > 0) if (Serial.read() == '0') remapSerial();
    // If the serial monitor is closed, reset so it can prompt the user to press 0 again.
    if (!Serial) set = 0;
    previousMillis = millis();
  }

  // Refresh bounce values
  for(byte x=0; x<=4; x++) bounce[x].update();

  #ifdef TOUCH
  qt = qt_1.measure();
  if (qt > threshold) pressed = 0;  else if (qt < threshold-50 ) pressed = 1;
  #endif
  sideButton();

  switch(ledMode){
    case 0: cycle(); break;
    case 1: reactive(0); break;
    case 2: reactive2(); break;
    case 3: custom(); break;
    case 4: BPS(); break;
    case 5: colorChange(); break;
    case 6: LEDoff(); break;
  }

  keyboard();

}

/*
██      ███████ ██████      ███    ███  ██████  ██████  ███████ ███████
██      ██      ██   ██     ████  ████ ██    ██ ██   ██ ██      ██
██      █████   ██   ██     ██ ████ ██ ██    ██ ██   ██ █████   ███████
██      ██      ██   ██     ██  ██  ██ ██    ██ ██   ██ ██           ██
███████ ███████ ██████      ██      ██  ██████  ██████  ███████ ███████
*/

void wheel(byte shortColor, byte key) { // Set RGB color with byte
  if (shortColor >= 0 && shortColor < 85) { rgb[key][0] = (shortColor * -3) +255; rgb[key][1] = shortColor * 3; rgb[key][2] = 0; }
  else if (shortColor >= 85 && shortColor < 170) { rgb[key][0] = 0; rgb[key][1] = ((shortColor - 85) * -3) +255; rgb[key][2] = (shortColor - 85) * 3; }
  else { rgb[key][0] = (shortColor - 170) * 3; rgb[key][1] = 0; rgb[key][2] = ((shortColor - 170) * -3) +255; }
}

void dsWheel(byte shortColor) { // Set RGB color with byte
  if (shortColor >= 0 && shortColor < 85) { dsrgb[0] = (shortColor * -3) +255; dsrgb[1] = shortColor * 3; dsrgb[2] = 0; }
  else if (shortColor >= 85 && shortColor < 170) { dsrgb[0] = 0; dsrgb[1] = ((shortColor - 85) * -3) +255; dsrgb[2] = (shortColor - 85) * 3; }
  else { dsrgb[0] = (shortColor - 170) * 3; dsrgb[1] = 0; dsrgb[2] = ((shortColor - 170) * -3) +255; }
}

void setLED(byte key) {
  pixels.setPixelColor(key, pixels.Color(b*rgb[key][0]/255, b*rgb[key][1]/255, b*rgb[key][2]/255));
  pixels.show();
  dotStar.setPixelColor(key, pixels.Color(dsrgb[1], dsrgb[0], dsrgb[2]));
  dotStar.show();
}
void setWhite(byte color, byte key) {
  pixels.setPixelColor(key, pixels.Color(b*color/255, b*color/255, b*color/255));
  pixels.show();
  dotStar.setPixelColor(0, pixels.Color(color, color, color));
  dotStar.show();
}
void dsSetWhite(byte color) {
  dotStar.setPixelColor(0, pixels.Color(color, color, color));
  dotStar.show();
}

void blinkLEDs(byte times) {
  for (int y = 0; y < times; y++) {
    for (int x = 0; x < numkeys; x++) setWhite(0, x);
    delay(20);
    for (int x = 0; x < numkeys; x++) setWhite(255, x);
    delay(50);
  }
}

// LED Modes
void cycle() {
  if ((millis() - cycleMillis) > cycleSpeed) {
    cycleWheel++; // No rollover needed since datatype is byte
    for(int x = 0; x < numkeys; x++) {
      wheel(cycleWheel, x);
      dsWheel(cycleWheel);
      setLED(x);
    }
    cycleMillis = millis();
  }
}

void reactive(byte flip) {
  if ((millis() - reactMillis) > reactSpeed) {
    for (int a=0; a <= numkeys; a++) {
      // Press action
      if ((!bounce[a].read() && !flip) || (bounce[a].read() && flip)) {
        for (int x = 0; x < 3; x++) { rgb[a][x] = 255; colorState[a] = 1; dsrgb[x] = 255; colorState[numkeys]=1; }
      }
      // Release action
      if ((bounce[a].read() && !flip) || (!bounce[a].read() && flip)) {
        if (colorState[a] == 1) { // Decrements white and increments red
          for (int x = 1; x < 3; x++) {
            byte buffer = rgb[a][x]; if (buffer > 0) buffer-=5; rgb[a][x] = buffer; buffer = dsrgb[x]; if (buffer > 0) buffer-=5; dsrgb[x] = buffer;
          }
          if ((rgb[a][2] == 0) && (rgb[a][1] == 0) && (rgb[a][0] == 255)) { colorState[a] = 2; reactiveWheel[a] = 0; }
          if ((dsrgb[2] == 0) && (dsrgb[1] == 0) && (dsrgb[0] == 255)) { colorState[numkeys] = 2; reactiveWheel[numkeys] = 0; }
        }

        if (colorState[a] == 2) {
          if(a<numkeys){ wheel(reactiveWheel[a], a); byte buffer = reactiveWheel[a]; buffer+=5; reactiveWheel[a] = buffer; }
          if(a==numkeys){ dsWheel(reactiveWheel[a]); byte buffer = reactiveWheel[a]; buffer+=5; reactiveWheel[a] = buffer; }
          if (reactiveWheel[a] == 170) colorState[a] = 3;
        }

        if (colorState[a] == 3) {
          if (rgb[a][2] > 0) { byte buffer = rgb[a][2]; buffer-=5; rgb[a][2] = buffer; }
          if (rgb[a][2] == 0) colorState[a] = 0;
          if (dsrgb[2] > 0) { byte buffer = dsrgb[2]; buffer-=5; dsrgb[2] = buffer; }
          if (dsrgb[2] == 0) colorState[numkeys] = 0;
        }

        if (a != numkeys) if (colorState[a] == 0) for (int x = 0; x < 3; x++) rgb[a][x] = 0;
        if (colorState[numkeys] == 0) for (int x = 0; x < 3; x++) dsrgb[x] = 0;

      } // End of release
      setLED(a);
    }
    reactMillis = millis();
  }
}

// Dirty workaround for getting dotstar working as intended on second reactive mode
void reactive2() {
  if ((millis() - reactMillis) > reactSpeed) {
    for (int a=0; a < numkeys; a++) {
      // Press action
      byte rCount = 0;
      for (byte g = 0; g < numkeys; g++) if (bounce[g].read()) rCount++;
      if (rCount == numkeys) {
        if (colorState[numkeys] != 1) { dsrgb[0] = 255; dsrgb[1] = 255; dsrgb[2] = 255; colorState[numkeys]=1;}
      }

      if (bounce[a].read()) { for (int x = 0; x < 3; x++) rgb[a][x] = 255; colorState[a] = 1; }

      // Release action
      if (!bounce[a].read()) {
        if (colorState[a] == 1) { // Decrements white and increments red
          for (int x = 1; x < 3; x++) {
            byte buffer = rgb[a][x]; if (buffer > 0) buffer-=5; rgb[a][x] = buffer;
          }
          if ((rgb[a][2] == 0) && (rgb[a][1] == 0) && (rgb[a][0] == 255)) { colorState[a] = 2; reactiveWheel[a] = 0; }
        }

        if (colorState[a] == 2) {
          if(a<numkeys){ wheel(reactiveWheel[a], a); byte buffer = reactiveWheel[a]; buffer+=5; reactiveWheel[a] = buffer; }
          if (reactiveWheel[a] == 170) colorState[a] = 3;
        }

        if (colorState[a] == 3) {
          if (rgb[a][2] > 0) { byte buffer = rgb[a][2]; buffer-=5; rgb[a][2] = buffer; }
          if (rgb[a][2] == 0) colorState[a] = 0;
        }

        if (a != numkeys) if (colorState[a] == 0) for (int x = 0; x < 3; x++) rgb[a][x] = 0;

      } // End of release

      // Release action
      if (rCount  < numkeys) {
        if (colorState[numkeys] == 1) { // Decrements white and increments red
          for (int x = 1; x < 3; x++) {
            byte buffer = dsrgb[x]; if (buffer > 0) buffer-=3; dsrgb[x] = buffer;
          }
          if ((dsrgb[2] == 0) && (dsrgb[1] == 0) && (dsrgb[0] == 255)) { colorState[numkeys] = 2; }
        }

        if (colorState[numkeys] == 2) {
          dsrgb[0]-=3;
          dsrgb[1]+=3;
          if ((dsrgb[0] == 0) && (dsrgb[2] == 0) && (dsrgb[1] == 255)) { colorState[numkeys] = 3; }
        }
        if (colorState[numkeys] == 3) {
          dsrgb[1]-=3;
          dsrgb[2]+=3;
          if ((dsrgb[0] == 0) && (dsrgb[1] == 0) && (dsrgb[2] == 255)) { colorState[numkeys] = 4; }
        }

        if (colorState[numkeys] == 4) {
          dsrgb[2]-=3;
          if (dsrgb[2] == 0) colorState[numkeys] = 0;
        }

        if (colorState[numkeys] == 0) for (int x = 0; x < 3; x++) dsrgb[x] = 0;

      } // End of release

      setLED(a);
    }
    reactMillis = millis();
  }
}

void custom() {
  if ((millis() - customMillis) > customSpeed) {
    for (int x = 0; x < numkeys; x++){
      // When side button is held
      if (hold == 3) { if (!bounce[x].read()) { byte buffer = customWheel[x]; buffer++; customWheel[x] = buffer; } }
      // When a button isn't being held or a color is being changed
      if (bounce[x].read() || hold == 3) { wheel(customWheel[x], x); for (byte z=0;z<3;z++) dsrgb[z] = rgb[0][z]; setLED(x); }
      // LEDs turn white on keypress
      if (!bounce[x].read() && hold != 3) { setWhite(255, x); dsSetWhite(255); }
    }
    customMillis = millis();
  }
}

void BPS() {

  for(byte x=0; x<numkeys-1; x++){
    if (!bounce[x].read() && hold == 0 && bpsPress[x] == 0) { bpsCount++; bpsPress[x] = 1; }
    if (bounce[x].read() && bpsPress[x] == 1) { bpsPress[x] = 0; }
  }
  // Check counter every second, apply multiplier to wheel, and wipe counter
  if ((millis() - bpsMillis) > bpsUpdate) {
    bpsFix = 170-(bpsCount*17);                     // Start at blue and move towards red
    if (bpsFix < 0) bpsFix = 0;                     // Cap color at red
    bpsCount = 0;                                   // Reset counter
    bpsMillis = millis();                           // Reset millis timer
  }
  if ((millis() - bpsMillis2) > 5) {                // Run once every five ms for smooth fades
    if (bpsBuffer < bpsFix) bpsBuffer++;            // Fade up if buffer value is lower
    if (bpsBuffer > bpsFix) bpsBuffer--;            // Fade down if buffer value is higher
    for (int x = 0; x < numkeys; x++) {
      wheel(bpsBuffer, x);                          // convert wheel value to rgb values in array
        for (byte z=0;z<3;z++) dsrgb[z] = rgb[0][z];// Copy color of left key to dotstar
        if (bounce[x].read()) setLED(x);
        if (!bounce[x].read()) {
          setWhite(255, x);                         // Set keys to white when pressed
          dsSetWhite(255);                          // Set dotstar to white when either key is pressed
        }
      }
    bpsMillis2 = millis();                          // Reset secondary millis timer
  }
}

byte dscc;
void colorChange(){
  for (byte a = 0; a < numkeys; a++) {
    if (!pressedCC[a]) {
      if (!bounce[a].read()) {
        dscc += changeVal;
        changeColors[a] = changeColors[a] += changeVal;
        pressedCC[a] = 1;
      }
    }
    if (pressedCC[a])  if (bounce[a].read())pressedCC[a] = 0;
    wheel(changeColors[a], a);
    dsWheel(dscc);
    // for (byte z=0;z<3;z++) dsrgb[z] = rgb[0][z];
  }

  if ((millis() - changeMillis) > 10) { // Limit changes to once ber 10 ms for reduced overhead
    for (byte x=0; x < numkeys; x++) setLED(x);
    changeMillis = millis();
  }

}

void LEDoff(){ // Optimize this in the future
  for (byte x=0;x<numkeys;x++) setWhite(0, x);
}

/*
██   ██ ███████ ██    ██ ██████   ██████   █████  ██████  ██████
██  ██  ██       ██  ██  ██   ██ ██    ██ ██   ██ ██   ██ ██   ██
█████   █████     ████   ██████  ██    ██ ███████ ██████  ██   ██
██  ██  ██         ██    ██   ██ ██    ██ ██   ██ ██   ██ ██   ██
██   ██ ███████    ██    ██████   ██████  ██   ██ ██   ██ ██████
*/

bool pressedLock[numkeys];
void keyboard(){

  for (byte x=0; x<numkeys; x++){
    if (!bounce[x].read() && pressedLock[x]) { for (byte y=0; y<3; y++) { Keyboard.press(mapping[x][y]); } pressedLock[x] = 0; }
    if (bounce[x].read() && !pressedLock[x]){ for (byte y=0; y<3; y++) { Keyboard.release(mapping[x][y]); } pressedLock[x] = 1; }
  }

}

void sideButton(){
  // Press action: Sets hold value depending on how long the side button is held
  #ifdef TOUCH
  if (!pressed) {
  #else
  if (!bounce[4].read()) {
  #endif
    if ((millis() - sideMillis) > 8 && (millis() - sideMillis) < s)  hold = 1;
    if ((millis() - sideMillis) > s && (millis() - sideMillis) < m)  hold = 2;
    if ((millis() - sideMillis) > m) hold = 3;
  }
  // Release action
  #ifdef TOUCH
  if (pressed) {
  #else
  if (bounce[4].read()) {
  #endif
    // Press and release escape
    if (hold == 1) { Keyboard.press(KEY_ESC); delay(12); Keyboard.release(KEY_ESC); }
    // Change LED mode
    if (hold == 2) {
      	ledMode++;
		for (int x = 0; x < numkeys; x++) for (int y = 0; y < 3; y++) { rgb[x][y] = 0; dsrgb[y] = 0; }// Clear colors
  		if (ledMode > (numModes-1)) ledMode = 0; // numModes -1 for 0 index
      	EEPROM.write(20, ledMode);
      	EEPROM.commit();
    }
    if (hold == 3) { // Save custom colors or brightness
    	if (ledMode == 3) for (int x = 0; x < numkeys; x++) EEPROM.write(30+x, customWheel[x]);
		if (ledMode != 3) for (int x = 0; x < numkeys; x++) EEPROM.write(21, b);
		EEPROM.commit();
    }
    hold = 0;
    sideMillis = millis();
  }

  // brightness changer (LED and RGB)
  if (hold == 3 && ledMode != 3) { if ((millis() - brightMillis) > 10) { if (!bounce[0].read()) if (b > 10) b--; if (!bounce[1].read()) if (b < 255) b++; brightMillis = millis(); } }

  // Blink code
  if (blink != hold) { if (hold == 2) blinkLEDs(1); if (hold == 3) blinkLEDs(2); blink = hold; if (ledMode == 2) {dsrgb[0]=255; dsrgb[1]=255; dsrgb[2]=255;}}

}

/*
██████  ███████ ███    ███  █████  ██████  ██████  ███████ ██████
██   ██ ██      ████  ████ ██   ██ ██   ██ ██   ██ ██      ██   ██
██████  █████   ██ ████ ██ ███████ ██████  ██████  █████   ██████
██   ██ ██      ██  ██  ██ ██   ██ ██      ██      ██      ██   ██
██   ██ ███████ ██      ██ ██   ██ ██      ██      ███████ ██   ██
*/

byte inputInterpreter(String input) { // Checks inputs for a preceding colon and converts said input to byte
  if (input[0] == ':') { // Check if user input special characters
    input.remove(0, 1); // Remove colon
    int inputInt = input.toInt(); // Convert to integer
    if (inputInt >= 0 && inputInt < specialLength) { // Checks to make sure length matches
      inputBuffer = specialByte[inputInt];
      Serial.print(specialKeys[inputInt]); // Print within function for easier access
      Serial.print(" "); // Space for padding
      return 1;
    }
    Serial.println();
    Serial.println("Invalid code added, please try again.");
    return 2;
  }
  else if (input[0] != ':' && input.length() > 3){
    Serial.println();
    Serial.println("Invalid, please try again.");
    return 2;
  }
  else return 0;
}

void remapSerial() {
  Serial.println("Welcome to the serial remapper!");
  // Buffer variables (puting these at the root of the relevant scope to reduce memory overhead)
  byte input = 0;

  // Print current EEPROM values
  Serial.print("Current values are: ");
  for (int x = 0; x < numkeys; x++) {
    for (int y = 0; y < 3; y++) {
      byte mapCheck = int(mapping[x][y]);
      if (mapCheck != 0){ // If not null...
        // Print if regular character (prints as a char)
        if (mapCheck > 33 && mapCheck < 126) Serial.print(mapping[x][y]);
        // Otherwise, check it through the byte array and print the text version of the key.
        else for (int z = 0; z < specialLength; z++) if (specialByte[z] == mapCheck){
          Serial.print(specialKeys[z]);
          Serial.print(" ");
        }
      }
    }
    // Print delineation
    if (x < (numkeys - 1)) Serial.print(", ");
  }
  Serial.println();
  // End of print

  // Take serial inputs
  Serial.println("Please input special keys first and then a printable character.");
  Serial.println();
  Serial.println("For special keys, please enter a colon and then the corresponding");
  Serial.println("number (example: ctrl = ':1')");
  // Print all special keys
  byte lineLength = 0;

  // Print table of special values
  for (int y = 0; y < 67; y++) Serial.print("-");
  Serial.println();
  for (int x = 0; x < specialLength; x++) {
    // Make every line wrap at 30 characters
    byte spLength = specialKeys[x].length(); // save as variable within for loop for repeated use
    lineLength += spLength + 6;
    Serial.print(specialKeys[x]);
    spLength = 9 - spLength;
    for (spLength; spLength > 0; spLength--) { // Print a space
      Serial.print(" ");
      lineLength++;
    }
    if (x > 9) lineLength++;
    Serial.print(" = ");
    if (x <= 9) {
      Serial.print(" ");
      lineLength+=2;
    }
    Serial.print(x);
    if (x != specialLength) Serial.print(" | ");
    if (lineLength > 55) {
      lineLength = 0;
      Serial.println();
    }
  }
  // Bottom line
  if ((specialLength % 4) != 0) Serial.println(); // Add a new line if table doesn't go to end
  for (int y = 0; y < 67; y++) Serial.print("-"); // Bottom line of table
  Serial.println();
  Serial.println("If you want two or fewer modifiers for a key and");
  Serial.println("no printable characters, finish by entering 'xx'");
  // End of table

  for (int x = 0; x < numkeys; x++) { // Main for loop for each key

    byte y = 0; // External loop counter for while loop
    byte z = 0; // quickfix for bug causing wrong input slots to be saved
    while (true) {
      while(!Serial.available()){}
      String serialInput = Serial.readString();
      byte loopV = inputInterpreter(serialInput);

      // If key isn't converted
      if (loopV == 0){ // Save to array and EEPROM and quit; do and break
        // If user finishes key
        if (serialInput[0] == 'x' && serialInput[1] == 'x') { // Break if they use the safe word
          for (y; y < 3; y++) { // Overwrite with null values (0 char = null)
            EEPROM.write((40+(x*3)+y), 0);
            mapping[x][y] = 0;
          }
          if (x < numkeys-1) Serial.print("(finished key,) ");
          if (x == numkeys-1) Serial.print("(finished key)");
          break;
        }
        // If user otherwise finishes inputs
        Serial.print(serialInput); // Print once
        if (x < 5) Serial.print(", ");
        for (y; y < 3; y++) { // Normal write/finish
          EEPROM.write((40+(x*3)+y), int(serialInput[y-z]));
          mapping[x][y] = serialInput[y-z];
        }
        break;
      }

      // If key is converted
      if (loopV == 1){ // save input buffer into slot and take another serial input; y++ and loop
        EEPROM.write((40+(x*3)+y), inputBuffer);
        mapping[x][y] = inputBuffer;
        y++;
        z++;
      }

      // If user input is invalid, print keys again.
      if (loopV == 2){
        for (int a = 0; a < x; a++) {
          for (int d = 0; d < 3; d++) {
            byte mapCheck = int(mapping[a][d]);
            if (mapCheck != 0){ // If not null...
              // Print if regular character (prints as a char)
              if (mapCheck > 33 && mapCheck < 126) Serial.print(mapping[a][d]);
              // Otherwise, check it through the byte array and print the text version of the key.
              else for (int c = 0; c < specialLength; c++) if (specialByte[c] == mapCheck){
                Serial.print(specialKeys[c]);
                // Serial.print(" ");
              }
            }
          }
          // Print delineation
          Serial.print(", ");
        }
        if (y > 0) { // Run through rest of current key if any inputs were already entered
          for (int d = 0; d < y; d++) {
            byte mapCheck = int(mapping[x][d]);
            if (mapCheck != 0){ // If not null...
              // Print if regular character (prints as a char)
              if (mapCheck > 33 && mapCheck < 126) Serial.print(mapping[x][d]);
              // Otherwise, check it through the byte array and print the text version of the key.
              else for (int c = 0; c < specialLength; c++) if (specialByte[c] == mapCheck){
                Serial.print(specialKeys[c]);
                Serial.print(" ");
              }
            }
          }
        }

      }
    } // Mapping loop
  } // Key for loop
  EEPROM.commit();
  Serial.println();
  Serial.println("Mapping saved, exiting. To re-enter the remapper, please enter 0.");

} // Remapper loop
