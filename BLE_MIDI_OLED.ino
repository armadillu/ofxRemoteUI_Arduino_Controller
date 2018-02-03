#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <bluefruit.h>
#include <MIDI.h>
#include "DisplaySetup.h"
#include "RotaryEncoder.h"

#define NUM_KNOBS 			8
#define NUM_BUTTONS 		4

#define MAX_KNOB_VALUE 		200	
#define MIDI_CHANNEL_OUT	4

#define ENABLE_BLE_MIDI		true
#define DEBUG_OVER_SERIAL 	true

// Create a new instance of the Arduino MIDI Library,
// and attach BluefruitLE MIDI as the transport.
BLEDis bledis;
BLEMidi blemidi;
MIDI_CREATE_BLE_INSTANCE(blemidi);

//Display instance
Adafruit_SSD1306 display = Adafruit_SSD1306();

#include "BluetoothSetup.h"
#include "Utils.h"

struct KnobData{
	bool needsUpdate = true;
	int value = 0;
	int displayValue;
};

struct ButtonData{
	unsigned char pin;
	bool value = false;
};

KnobData knobs[NUM_KNOBS];
ButtonData buttons[NUM_BUTTONS];
unsigned char buttonPins[] = {16,15,7,11};

void setup(){
	
	Serial.begin(115200);
	Serial.println("ofxRemoteUI Remote Controller");
	Serial.println("----------------------------------------------\n");

	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);	// initialize with the I2C addr 0x3C (for the 128x32)
	Serial.println("OLED setup");
	
	RotaryEncoder.begin(A1, A0);
	RotaryEncoder.setCallback(encoder_callback);
	//RotaryEncoder.start();
	Serial.println("Rotary Encoder setup");

	for(int i = 0; i < NUM_BUTTONS; i++){
		buttons[i].pin = buttonPins[i];
		pinMode(buttons[i].pin, INPUT_PULLUP);
	}
	
	// Clear the buffer.
	display.display();
	delay(1000);
	display.clearDisplay();
	display.display();
	Serial.println("Display cleared");

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

	//display ready
	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(0,0);
	display.println("ofxRemoteUI Ready!");
	display.println();
	display.println("Waiting For Bluetooth");
	display.println("Connection...");
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


void encoder_callback(int step){
	knobs[0].value += step;
	if(knobs[0].value < 0) knobs[0].value = 0;
	if(knobs[0].value >= MAX_KNOB_VALUE ) knobs[0].value = MAX_KNOB_VALUE - 1;
	knobs[0].needsUpdate = true;
	knobs[0].displayValue = (int)(float(100.0f) * float(knobs[0].value) / float(MAX_KNOB_VALUE - 1));
	#if (DEBUG_OVER_SERIAL)
	Serial.printf("knob %d val: %d", 0, knobs[0].value); Serial.println();
	#endif
}


void loop() {

	delay(50);
	
	#if ENABLE_BLE_MIDI
		if (! Bluefruit.connected()) { return; }
		if (! blemidi.notifyEnabled()) { return; }
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
	for(int i = 0; i < 1; i++){
		if(knobs[i].needsUpdate){
			knobs[i].needsUpdate = false;
			#if(ENABLE_BLE_MIDI)
			MIDI.sendControlChange(20 + i, knobs[i].value, MIDI_CHANNEL_OUT);	//20 as undefined
			#endif
			screenNeedsUpdate = true;
		}
	}

	if(screenNeedsUpdate){
		updateScreen();
	}
}


void midiRead(){
	if (! Bluefruit.connected()) { return; }
	if (! blemidi.notifyEnabled()) { return; }
	MIDI.read(); // read any new MIDI messages
}

void updateScreen(){
	display.clearDisplay();
	static char aux[4][256];
	display.setTextSize(1);
	display.setTextColor(WHITE);
	sprintf(aux[0], "Knob %03d %03d %03d %03d", knobs[0].displayValue, knobs[1].displayValue, knobs[2].displayValue, knobs[3].displayValue);
	sprintf(aux[1], "Knob %03d %03d %03d %03d", knobs[4].displayValue, knobs[5].displayValue, knobs[6].displayValue, knobs[7].displayValue);
	sprintf(aux[2], "btns  %d   %d   %d   %d ", buttons[0].value, buttons[1].value, buttons[2].value, buttons[3].value);
	int y = 0;
	display.setCursor(0,y); display.print("#### ofxRemoteUI ####"); y += 9;
	display.setCursor(0,y); display.print(aux[0]); y += 8;
	display.setCursor(0,y); display.print(aux[1]); y += 8;
	display.setCursor(0,y); display.print(aux[2]); y += 8;
	display.display(); // actually display all of the above
}
