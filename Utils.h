void printDisplayMsg(char * msg){
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(msg);
  yield();
  display.display(); // actually display all of the above
}
