void printDisplayMsg(char * msg){
#if ENABLE_DISPLAY
	display.clearDisplay();
	display.setCursor(0,0);
	display.println(msg);
	yield();
	display.display(); // actually display all of the above
#endif
}
