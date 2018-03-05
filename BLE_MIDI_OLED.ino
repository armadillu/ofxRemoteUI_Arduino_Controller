#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <bluefruit.h>
#include <MIDI.h>
#include "DisplaySetup.h"

#define NUM_KNOBS 			8
#define NUM_BUTTONS 		9 /* 7 top-buttons + 2 side-buttons */

#define MAX_KNOB_VALUE 		940 /*1023*/
#define MAX_KNOB_MIDI_VALUE 127
#define MIDI_CHANNEL_OUT	4

#define KNOB_DIFF_VAL_THRESHOLD 	9

////////////////////////////////////// CONFIG //////////////////////////////////////////////

#define ENABLE_BLE_MIDI			true
#define DEBUG_OVER_SERIAL 		false
#define ENABLE_DISPLAY			false

/////////////////////////////////////////////////////////////////////////////////////////////

// Create a new instance of the Arduino MIDI Library,
// and attach BluefruitLE MIDI as the transport.
BLEDis bledis;
BLEMidi blemidi;
MIDI_CREATE_BLE_INSTANCE(blemidi);

//Display instance
Adafruit_SSD1306 display = Adafruit_SSD1306();

#include "BluetoothSetup.h"
#include "Utils.h"

struct ButtonData{
	unsigned char pin;
	bool value = false;
};

struct KnobData{
	int value = 0; //0..1023
	int midiValue;
	unsigned char pin;
};


KnobData knobs[NUM_KNOBS];
ButtonData buttons[NUM_BUTTONS];

//pin configs                 b0  b1  b2 b3  b4  b5  b6  Nex Sync
unsigned char buttonPins[] = {16, 15, 7, 11, 27, 12, 13, 26, 25}; //8 is sync midi; 14 is nextSlot
unsigned char knobPins[] = {2, 3, 4, 5, 28, 29, 30, 31};
unsigned char powerLedPin = 14; //power & bluetooth state LED
unsigned int frameCount = 0;


void setup(){

	frameCount = 0;
  
	Serial.begin(115200);
	Serial.println("ofxRemoteUI Remote Controller");
	Serial.println("----------------------------------------------\n");
	
	#if (ENABLE_DISPLAY)
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);	// initialize with the I2C addr 0x3C (for the 128x32)
	Serial.println("OLED setup");
	#endif
		
	for(int i = 0; i < NUM_BUTTONS; i++){
		buttons[i].pin = buttonPins[i];
		pinMode(buttons[i].pin, INPUT_PULLUP);
	}

  	pinMode(powerLedPin, OUTPUT);
	
	for(int i = 0; i < NUM_KNOBS; i++){
		knobs[i].pin = knobPins[i];
	}

	#if (ENABLE_DISPLAY)
	// Clear the buffer.
	display.display();
	delay(1000);
	display.clearDisplay();
	display.display();
	Serial.println("Display cleared");
	#endif

	/// MIDI /////////////////////////////////////////////////////////////////////////////
	// Config the peripheral connection with maximum bandwidth 
	// more SRAM required by SoftDevice
	// Note: All config***() function must be called before begin()
	#if(ENABLE_BLE_MIDI)	
		Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);	
		Bluefruit.begin();
		Bluefruit.setName("ofxRemoteUI BLE");
		Bluefruit.setTxPower(4); //max power
		Bluefruit.autoConnLed(true); // Setup the on board blue LED to be enabled on CONNECT
		Serial.println("BlueTooth setup");
	
		// Configure and Start Device Information Service
		bledis.setManufacturer("Adafruit Industries");
		bledis.setModel("Bluefruit Feather52");
		bledis.begin();
		Serial.println("Bledis setup");
	
		// Initialize MIDI, and listen to all MIDI channels
		// This will also call blemidi service's begin()
		MIDI.begin(MIDI_CHANNEL_OMNI);
		MIDI.setHandleNoteOn(handleNoteOn);
		MIDI.setHandleNoteOff(handleNoteOff);
		Serial.println("MIDI begin");
		
		startAdv(); // Set up and start advertising
		Serial.println("Started Advertising");
		Scheduler.startLoop(midiRead); // Start MIDI read loop
		Serial.println("Scheduler started");
	#endif

	#if (ENABLE_DISPLAY)
		display.setTextSize(1);
		display.setTextColor(WHITE);
		display.setCursor(0,0);
		display.println("ofxRemoteUI Ready!");
		display.println();
		display.println("Waiting For Bluetooth");
		display.println("Connection...");
		display.display(); // actually display all of the above
	#endif
}

void handleStatusLight(){
	int val = 0;
	if(Bluefruit.connected()){ //device connected!
  		if(blemidi.notifyEnabled()){ //ready to rx msgs
  			val = 255; //steady on if connected
  		}else{ //not ready
  			val = (frameCount%60 > 30) ? 255 : 0; //slow blink if device connected and not ready
  		}
	}else{ //no bluetooth connection
		val = 127 + 127 * sin(frameCount * 0.05); //slow pulse if no connection
	}
	analogWrite(powerLedPin, val); 
}

void loop() {

	delay(16);

	frameCount++;
    
	#if ENABLE_BLE_MIDI
	    handleStatusLight();
		if (!Bluefruit.connected()) { return; }  // Don't continue if we aren't connected.
		if (!blemidi.notifyEnabled()) { return; }   // Don't continue if the connected device isn't ready to receive messages.
	#else
		analogWrite(powerLedPin, 255); //led always on if not on bluetooth mode
	#endif

	bool screenNeedsUpdate = false;
	
	//handle BUTTON UPDATES
	for(int i = 0; i < NUM_BUTTONS; i++){
		if (!digitalRead(buttons[i].pin)){ //button pressed
			if(!buttons[i].value){
				buttons[i].value = true;
				#if(ENABLE_BLE_MIDI)
					MIDI.sendNoteOn(60 + i, 127 , MIDI_CHANNEL_OUT); //start at note 60
				#endif	
				#if (DEBUG_OVER_SERIAL)
					Serial.printf("button %d ON", i); Serial.println();
				#endif
				screenNeedsUpdate = true;
			}
		}else{ //button depressed
			if(buttons[i].value){
				buttons[i].value = false;
				#if(ENABLE_BLE_MIDI)
				MIDI.sendNoteOff(60 + i, 0, MIDI_CHANNEL_OUT);
				#endif
				#if (DEBUG_OVER_SERIAL)
					Serial.printf("button %d OFF", i); Serial.println();
				#endif
				screenNeedsUpdate = true;
			}
		}		
	}

	//handle KNOB UPDATES
	for(int i = 0; i < NUM_KNOBS; i++){
		int analogVal = analogRead(knobs[i].pin);
		if(analogVal > MAX_KNOB_VALUE) analogVal = MAX_KNOB_VALUE;
		int newVal = MAX_KNOB_VALUE - analogVal;
		if(abs(knobs[i].value - newVal) > KNOB_DIFF_VAL_THRESHOLD){
			knobs[i].value = newVal; //flip
			if(knobs[i].value < 0) knobs[i].value = 0;
			if(knobs[i].value > MAX_KNOB_VALUE ) knobs[i].value = MAX_KNOB_VALUE;
			knobs[i].midiValue = (int)( float(MAX_KNOB_MIDI_VALUE * knobs[i].value) / float(MAX_KNOB_VALUE) );
			if(knobs[i].midiValue > MAX_KNOB_MIDI_VALUE ) knobs[i].midiValue = MAX_KNOB_MIDI_VALUE;
			#if(DEBUG_OVER_SERIAL)
				Serial.printf("Knob %d : %d", i, knobs[i].midiValue); Serial.println();
			#endif
			#if(ENABLE_BLE_MIDI)
				MIDI.sendControlChange(20 + i, knobs[i].midiValue, MIDI_CHANNEL_OUT);	//20 as undefined
			#endif
			screenNeedsUpdate = true;
		}
	}

	#if (ENABLE_DISPLAY)
	if(screenNeedsUpdate){
		updateScreen();
	}
	#endif
}


void midiRead(){
	if (! Bluefruit.connected()) { return; }
	if (! blemidi.notifyEnabled()) { return; }
	MIDI.read(); // read any new MIDI messages
}

void updateScreen(){
	display.clearDisplay();
	static char aux[4][40];
	display.setTextSize(1);
	display.setTextColor(WHITE);
	sprintf(aux[0], "Knob %03d %03d %03d %03d", knobs[0].midiValue, knobs[1].midiValue, knobs[2].midiValue, knobs[3].midiValue);
	sprintf(aux[1], "Knob %03d %03d %03d %03d", knobs[4].midiValue, knobs[5].midiValue, knobs[6].midiValue, knobs[7].midiValue);
	sprintf(aux[2], "btns  %d   %d   %d   %d ", buttons[0].value, buttons[1].value, buttons[2].value, buttons[3].value);
	sprintf(aux[3], "btns  %d   %d   %d   %d ", buttons[4].value, buttons[5].value, buttons[6].value, buttons[7].value);
	int y = 0;
	//display.setCursor(0,y); display.print("#### ofxRemoteUI ####"); y += 9;
	display.setCursor(0,y); display.print(aux[0]); y += 8;
	display.setCursor(0,y); display.print(aux[1]); y += 8;
	display.setCursor(0,y); display.print(aux[2]); y += 8;
	display.setCursor(0,y); display.print(aux[3]); y += 8;
	display.display(); // actually display all of the above
}

void handleNoteOn(byte channel, byte pitch, byte velocity){
	Serial.printf("Note on: channel = %d, pitch = %d, velocity - %d", channel, pitch, velocity);
	Serial.println();
	printDisplayMsg("NOTE ON!");
}


void handleNoteOff(byte channel, byte pitch, byte velocity){
	// Log when a note is released.
	Serial.printf("Note off: channel = %d, pitch = %d, velocity - %d", channel, pitch, velocity);
	Serial.println();
	printDisplayMsg("NOTE OFF!");
}
