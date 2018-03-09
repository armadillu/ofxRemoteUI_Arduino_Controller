#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <bluefruit.h>
#include <MIDI.h>
#include "DisplaySetup.h"

#define NUM_KNOBS 					8
#define NUM_BUTTONS 				7 
#define NUM_CFG_BUTTONS				2

#define MAX_KNOB_VALUE 				940 /*1023*/
#define MAX_KNOB_MIDI_VALUE 		127
#define MIDI_CHANNEL_OUT			(4 + currentPage)
#define KNOB_CC_STARTING_INDEX		20 //for no good reason
#define BUTTON_NOTE_STARTING_INDEX	60 //for no good reason

#define KNOB_DIFF_VAL_THRESHOLD 	9

#define NUM_PAGES					5	/*by the right button, you can cycle through midi channels to send midi at*/ 
										/*we start at channel 4, up to 8*/
								
#define CUR_DATA 					data[currentPage] 	/*shortcut to access current page*/

////////////////////////////////////// CONFIG //////////////////////////////////////////////

#define ENABLE_BLE_MIDI			true
#define DEBUG_OVER_SERIAL 		true
#define ENABLE_DISPLAY			false

/////////////////////////////////////////////////////////////////////////////////////////////

// Create a new instance of the Arduino MIDI Library,
// and attach BluefruitLE MIDI as the transport.
#if ENABLE_BLE_MIDI
	BLEDis bledis;
	BLEMidi blemidi;
	MIDI_CREATE_BLE_INSTANCE(blemidi);
#endif

//Display instance
#if ENABLE_DISPLAY			
Adafruit_SSD1306 display = Adafruit_SSD1306();
#endif

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

struct InputData{
	KnobData knobs[NUM_KNOBS];
	ButtonData buttons[NUM_BUTTONS];
};


//////  PIN CONFIGS ////////////////////////////////////////////////////////////////////////////////////
//                               b0  b1  b2 b3  b4  b5  b6 
unsigned char buttonPins[] = 	{16, 15, 7, 11, 27, 12, 13};
unsigned char knobPins[] = 		{2,  3,  4, 5,  28, 29, 30, 31};
unsigned char cfgButtonPins[] = {26, 25}; //nextPage, sync 
unsigned char powerLedPin = 	14;

unsigned char nextButtonID = 	0;
unsigned char syncButtonID = 	1;

////// GLOBALS  ////////////////////////////////////////////////////////////////////////////////////////

unsigned int frameCount = 0; //count frames over time
InputData data[NUM_PAGES]; 	//all the state for all the buttons & knobs across all "pages"
ButtonData cfgButtons[NUM_CFG_BUTTONS]; //state for cfg buttons (side buttons)
unsigned int currentPage = 0; 		//what "page" are we currently at


void setup(){

	frameCount = 0;
  
	Serial.begin(115200);
	Serial.println("ofxRemoteUI Remote Controller");
	Serial.println("----------------------------------------------\n");
	
	#if (ENABLE_DISPLAY)
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);	// initialize with the I2C addr 0x3C (for the 128x32)
	Serial.println("OLED setup");
	#endif

	for(int k = 0; k < NUM_PAGES; k++){
		for(int i = 0; i < NUM_BUTTONS; i++){
			data[k].buttons[i].pin = buttonPins[i];
			pinMode(data[k].buttons[i].pin, INPUT_PULLUP);
		}
	
		for(int i = 0; i < NUM_KNOBS; i++){
			data[k].knobs[i].pin = knobPins[i];
		}	
	}

	for(int i = 0; i < NUM_CFG_BUTTONS; i++){
		cfgButtons[i].pin = cfgButtonPins[i];
		pinMode(cfgButtons[i].pin, INPUT_PULLUP);
	}

	
  	pinMode(powerLedPin, OUTPUT);

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
		if (!digitalRead(CUR_DATA.buttons[i].pin)){ //button pressed
			if(!CUR_DATA.buttons[i].value){
				CUR_DATA.buttons[i].value = true;
				#if(ENABLE_BLE_MIDI)
					MIDI.sendNoteOn(BUTTON_NOTE_STARTING_INDEX + i, 127 , MIDI_CHANNEL_OUT); //start at note BUTTON_NOTE_STARTING_INDEX
				#endif	
				#if (DEBUG_OVER_SERIAL)
					Serial.printf("button %d ON", i); Serial.println();
				#endif
				screenNeedsUpdate = true;
			}
		}else{ //button depressed
			if(CUR_DATA.buttons[i].value){
				CUR_DATA.buttons[i].value = false;
				#if(ENABLE_BLE_MIDI)
				MIDI.sendNoteOff(BUTTON_NOTE_STARTING_INDEX + i, 0, MIDI_CHANNEL_OUT);
				#endif
				#if (DEBUG_OVER_SERIAL)
					Serial.printf("button %d OFF", i); Serial.println();
				#endif
				screenNeedsUpdate = true;
			}
		}		
	}

	//handle special buttons with these flags 
	bool shouldSendFullState = false;	
	bool shouldIncrementPage = false;

	//handle CONFIG BUTTON UPDATES
	for(int i = 0; i < NUM_CFG_BUTTONS; i++){
		if (!digitalRead(cfgButtons[i].pin)){ //button pressed
			if(!cfgButtons[i].value){
				cfgButtons[i].value = true;
				#if (DEBUG_OVER_SERIAL)
					Serial.printf("cfg button %d ON", i); Serial.println();
				#endif
				if(i == nextButtonID) shouldIncrementPage = true;
				if(i == syncButtonID) shouldSendFullState = true;
			}
		}else{ //button depressed
			if(cfgButtons[i].value){
				cfgButtons[i].value = false;
				#if (DEBUG_OVER_SERIAL)
					Serial.printf("button %d OFF", i); Serial.println();
				#endif
			}
		}		
	}


	//handle KNOB UPDATES
	for(int i = 0; i < NUM_KNOBS; i++){
		int analogVal = analogRead(CUR_DATA.knobs[i].pin);
		if(analogVal > MAX_KNOB_VALUE) analogVal = MAX_KNOB_VALUE;
		int newVal = MAX_KNOB_VALUE - analogVal;
		if(abs(CUR_DATA.knobs[i].value - newVal) > KNOB_DIFF_VAL_THRESHOLD){
			CUR_DATA.knobs[i].value = newVal; //flip
			if(CUR_DATA.knobs[i].value < 0) CUR_DATA.knobs[i].value = 0;
			if(CUR_DATA.knobs[i].value > MAX_KNOB_VALUE ) CUR_DATA.knobs[i].value = MAX_KNOB_VALUE;
			CUR_DATA.knobs[i].midiValue = (int)( float(MAX_KNOB_MIDI_VALUE * CUR_DATA.knobs[i].value) / float(MAX_KNOB_VALUE) );
			if(CUR_DATA.knobs[i].midiValue > MAX_KNOB_MIDI_VALUE ) CUR_DATA.knobs[i].midiValue = MAX_KNOB_MIDI_VALUE;
			#if(DEBUG_OVER_SERIAL)
				Serial.printf("Knob %d : %d", i, CUR_DATA.knobs[i].midiValue); Serial.println();
			#endif
			#if(ENABLE_BLE_MIDI)
				MIDI.sendControlChange(KNOB_CC_STARTING_INDEX + i, CUR_DATA.knobs[i].midiValue, MIDI_CHANNEL_OUT);	//20 as undefined
			#endif
			screenNeedsUpdate = true;
		}
	}

	if(shouldIncrementPage){
		currentPage++;
		if(currentPage >= NUM_PAGES) currentPage = 0;	
		#if(DEBUG_OVER_SERIAL)
			Serial.printf("Switching to Page: %d", currentPage); Serial.println();
		#endif
	}

	if(shouldSendFullState){ //send state of all knobs through midi (sync)
		#if(ENABLE_BLE_MIDI)
		for(int i = 0; i < NUM_KNOBS; i++){
			MIDI.sendControlChange(KNOB_CC_STARTING_INDEX + i, CUR_DATA.knobs[i].midiValue, MIDI_CHANNEL_OUT);	
		}
		#endif
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
	#if ENABLE_DISPLAY
	display.clearDisplay();
	static char aux[4][40];
	display.setTextSize(1);
	display.setTextColor(WHITE);
	sprintf(aux[0], "Knob %03d %03d %03d %03d", CUR_DATA.knobs[0].midiValue, CUR_DATA.knobs[1].midiValue, CUR_DATA.knobs[2].midiValue, CUR_DATA.knobs[3].midiValue);
	sprintf(aux[1], "Knob %03d %03d %03d %03d", CUR_DATA.knobs[4].midiValue, CUR_DATA.knobs[5].midiValue, CUR_DATA.knobs[6].midiValue, CUR_DATA.knobs[7].midiValue);
	sprintf(aux[2], "btns  %d   %d   %d   %d ", CUR_DATA.buttons[0].value, CUR_DATA.buttons[1].value, CUR_DATA.buttons[2].value, CUR_DATA.buttons[3].value);
	sprintf(aux[3], "btns  %d   %d   %d   %d ", CUR_DATA.buttons[4].value, CUR_DATA.buttons[5].value, CUR_DATA.buttons[6].value, CUR_DATA.buttons[7].value);
	int y = 0;
	//display.setCursor(0,y); display.print("#### ofxRemoteUI ####"); y += 9;
	display.setCursor(0,y); display.print(aux[0]); y += 8;
	display.setCursor(0,y); display.print(aux[1]); y += 8;
	display.setCursor(0,y); display.print(aux[2]); y += 8;
	display.setCursor(0,y); display.print(aux[3]); y += 8;
	display.display(); // actually display all of the above
	#endif
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

void handleStatusLight(){
	int val = 0;
	
	if(Bluefruit.connected()){ //device connected!	
  		if(blemidi.notifyEnabled()){ //ready to rx msgs

  			//here we handle the blink-codes; the idea is that the status light shows the "page number" in which you are on
  			
  			int baseTime = 25;
  			//val = 255; //steady on if connected
  			int interval = (currentPage + 1) * baseTime + baseTime;
  			int time = frameCount%interval;
  			if(time < (currentPage + 1) * baseTime){ //> is silence
  				int pageTime = time % baseTime;
  				if(pageTime < baseTime/2){
  					val = 0;
  				}else{
  					val = 255;
  				}
  			}else{
  				val = 0; //silence
  			}
  			
  		}else{ //not ready - never seems to happen?
  			val = (frameCount%60 > 30) ? 255 : 0; //slow blink if device connected and not ready
  		}
	}else{ //no bluetooth connection - slow pulse
		val = 127 + 127 * sin(frameCount * 0.05); //slow pulse if no connection
	}
	analogWrite(powerLedPin, val); 
}
