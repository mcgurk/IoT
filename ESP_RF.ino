#include <arduino.h>
#include <EEPROM.h>

//RF-receiver (XY-MK-5V. Works with 3.3V?)
#define INPUT_PIN D2

//RF-transmitter (FS1000A works with 3.3V?)
#define OUTPUT_PIN D5

//misc
#define LED_PIN D4
//#define PIR_PIN 12
#define BUTTON_PIN D3 //flash-button

//TR-502MSV RF remote control (Lidl)
//buttons:
#define RF_BTN1   B11
#define RF_BTN2   B01
#define RF_BTN3   B10
#define RF_BTN4   B00
//opcodes:
#define RF_ON         B1011
#define RF_OFF        B1111
#define RF_DIM_UP     B1101
#define RF_DIM_DOWN   B1001
#define RF_ON_ALL     B0011
#define RF_OFF_ALL    B0111
//id/tag, changed from remote control reset button (random)

//variables for interrupt
uint8_t bitarray[64];
uint8_t rawbitarray[128];
volatile uint8_t intCodeAvailable = 0;

//misc
uint16_t RF_TAG;
//uint16_t RF_TAG = 0x0C09;
uint8_t toggle = 0xff;
uint8_t start = 0x00;


void pinInterrupt() {
  static uint32_t intLastInterruptTime;
  static uint8_t intBitCounter;
  static uint8_t intPreviousBit;
  static uint8_t intStartReading;
  static uint8_t intRawBitArray[64];

  uint16_t pulse_time = micros() - intLastInterruptTime;
  intLastInterruptTime = micros();
  if (pulse_time > 8000) { //actual readings for "header": 8208, 8212, 8216, 8220. lets use 8000 to be safe
    intBitCounter = 0;
    intPreviousBit = 0;
    intStartReading = 1;
  } else {
    if (intStartReading) {
      intPreviousBit = intPreviousBit^1;
      if ((pulse_time > 500) && (pulse_time < 800)) { //636
        intRawBitArray[intBitCounter++] = intPreviousBit;
      } else
      if ((pulse_time > 500*2) && (pulse_time < 800*2)) { //636
        intRawBitArray[intBitCounter++] = intPreviousBit;
        intRawBitArray[intBitCounter++] = intPreviousBit;
      } else {
        intStartReading = 0;
      }
      if (intBitCounter > 60) {
        intStartReading = 0;
        for (uint8_t i = 0; i < 20; i++) bitarray[i] = intRawBitArray[i*3+2];
        intCodeAvailable = 1;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(INPUT_PIN, INPUT);
  pinMode(OUTPUT_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  EEPROM.begin(512); uint8_t v1 = EEPROM.read(0); uint8_t v2 = EEPROM.read(1); RF_TAG = (v1 << 8) + v2; //ESP
  //eeprom_read_block((void*)&RF_TAG, 0, 2); //Arduino

  //pinMode(PIR_PIN, INPUT);

  Serial.println("--- TR-502MSV RF remote control (Lidl) ---");
  Serial.println("RF-message payload: random indentifier (12 bits) + button (2 bits) + opcode (4 bits) + checksum (2 bits)");
  Serial.println("RF-message starts with lot of zeros. After that comes 1 followed by 0 before first databit.");
  Serial.println("After that comes first databit and every databit is followed by 1 and 0.");
  Serial.println("After last 1 and 0 there is lot of zeros.");
  Serial.println(F("/ = help"));

  Serial.print("RF_TAG: "); Serial.println(RF_TAG, HEX);
  Serial.println(); Serial.flush();

  attachInterrupt(INPUT_PIN, pinInterrupt, CHANGE);
}


void loop() {

  //this works without interrupts:
  /*waitAndReadValidCode();
  debug(bitarray); //debug
  delay(500);*/
  
  checkInterrupt(); //!!!
  
  //Read and save TAG to eeprom
  if(!digitalRead(BUTTON_PIN)) {
    learnNewTag();
  }

  //if (digitalRead(PIR_PIN)) sendCode(RF_TAG, RF_BTN1, RF_ON);
  /*if (digitalRead(PIR_PIN)) {
    digitalWrite(LED_PIN, toggle);
    toggle ^= 0xff;
    start = 0x01;
  } else
    if (start) {
      digitalWrite(LED_PIN, LOW);
      start = 0x00;
    }*/

  if(Serial.available()) if(Serial.read() == '/') processInput();

}


void learnNewTag() {
    Serial.println("Read and save new TAG... (push reset to cancel)"); Serial.flush();
    waitAndReadValidCode();
    setNewTag(getTag(bitarray));
    delay(500);
}

void setNewTag(uint16_t newtag) {
    RF_TAG = newtag;
    //eeprom_write_block((const void*)&RF_TAG, 0, 2); //Arduino
    EEPROM.write(0, highByte(RF_TAG)); EEPROM.write(1, lowByte(RF_TAG)); EEPROM.commit(); //ESP
    Serial.print("New RF_TAG: "); Serial.println(RF_TAG, HEX);
}


//check interrupt
void checkInterrupt() {
  if (intCodeAvailable) {
    Serial.print("Received: ");
    printButtonAndOpcode(bitarray);
    debug(bitarray); //debug
    if (getBtn(bitarray) == RF_BTN4 && getOpcode(bitarray) == RF_ON) digitalWrite(LED_PIN, LOW);
    if (getBtn(bitarray) == RF_BTN4 && getOpcode(bitarray) == RF_OFF) digitalWrite(LED_PIN, HIGH);
    delay(400); //try to avoid unintended repeat (300ms is too short)
    intCodeAvailable = 0;
  }
}



//interrupt calls this! (very unstable; exeption(). Takes too long with ESP8266?)
uint8_t readCode() {
  //when we are here, "header" (lots of zeros) have been read
  delayMicroseconds(636+636+318); //try to skip first 1 and 0 and get to middle of first databit
  
  //read 20 databits separated with 1 and 0
  uint8_t in;
  for (uint8_t i=0; i < 20; i++) {
    if (digitalRead(INPUT_PIN)) in = 1; else in = 0;
    bitarray[i] = in;
    delayMicroseconds(636);
    if (!digitalRead(INPUT_PIN)) return 0; //failed, must be 1
    delayMicroseconds(636);
    if (digitalRead(INPUT_PIN)) return 0; //failed, must be 0
    delayMicroseconds(636);
  }

  yield();

  //debug(bitarray); //debug
  if(!validate(bitarray)) return 0;
  
  return 1; //success
}


void waitAndReadValidCode() {
  uint8_t prevbits[20];
  for (int i=0; i < 20; i++) prevbits[i] = 0xff;

  digitalWrite(LED_PIN, HIGH);
  uint8_t match_count = 0;
  while(true) {
    //detect "header" (lot of zeros) and try to read code
    while (true) {
      uint32_t t1 = micros();
      while(!digitalRead(INPUT_PIN)) {
        yield();
      }
      uint32_t t2 = micros();
      if ((t2 - t1) > 8000) {
        if(readCode()) {
          if(validate(bitarray)) {
            break;
          }
        }
      }
      yield();
    }
    
    bool match = true;
    for (int i = 0; i < 20; i++) 
      if (bitarray[i] != prevbits[i])
        match = false;

    if (match) {
      //Serial.print("code: "); debug(code); //debug
      break;
    } else {
      for (int i=0; i < 20; i++) prevbits[i] = bitarray[i];
    }
  }
  digitalWrite(LED_PIN, LOW);
}


uint16_t getTag(uint8_t* bits) {
    return bits[0] << 11 | bits[1] << 10 | bits[2] << 9 | bits[3] << 8 | bits[4] << 7 | bits[5] << 6 | bits[6] << 5 | bits[7] << 4 | bits[8] << 3 | bits[9] << 2 | bits[10] << 1 | bits[11];
}

uint8_t getBtn(uint8_t* bits) {
  return (bits[12] << 1) | bits[13];
}

uint8_t getOpcode(uint8_t* bits) {
  return (bits[14] << 3) | (bits[15] << 2) | (bits[16] << 1) | bits[17];
}

uint8_t checksum(uint8_t* bits) {
  uint8_t chksum = (bits[18] << 1) | bits[19];
  uint8_t btn = getBtn(bits);
  uint8_t opcode = getOpcode(bits);
  if ( (btn ^ (opcode >> 2) ^ (opcode & B11)) != chksum ) return 0; //failed
  return 1; //success
}


uint8_t validate(uint8_t* bits) {
    uint8_t btn = getBtn(bits);
    uint8_t opcode = getOpcode(bits);

    //if (!validateButtonAndOpcode(btn, opcode)) return 0; //failed
    switch (btn) {
      case RF_BTN1:
      case RF_BTN2:
      case RF_BTN3:
      case RF_BTN4:
        break;
      default:
        return 0; //failed (cannot happen; we have 2 bits and 4 different cases)
        break;
    }

    switch (opcode) {
      case RF_ON:
      case RF_OFF:
      case RF_DIM_UP:
      case RF_DIM_DOWN:
      case RF_ON_ALL:
      case RF_OFF_ALL:
        break;
      default:
        return 0; //failed
        break;
    }

    if (!checksum(bits)) return 0; //failed

    return 1; //success
}


void sendRawBitArray(uint8_t* rawbits, uint8_t rawbitlength, uint8_t pin, uint16_t codedelay) {
  noInterrupts();
  for (uint8_t i=0; i < rawbitlength; i++) {
    digitalWrite(pin, rawbits[i]);
    delayMicroseconds(codedelay);
  }
  interrupts();
  yield();
}

void sendCode(uint16_t rf_tag, uint8_t btn, uint8_t opcode) {

  uint8_t chksum = (btn ^ (opcode >> 2) ^ (opcode & B11));

  //build array of bits to send
  for (uint8_t i=0; i < 12; i++) {
    bitarray[i] = (rf_tag >> (11 - i)) & 0x01;
  }
  uint8_t i = 12;
  //button:
  bitarray[i++] = btn >> 1;
  bitarray[i++] = btn & 0x01;
  //opcode:
  bitarray[i++] = opcode >> 3;
  bitarray[i++] = (opcode >> 2) & 0x01;
  bitarray[i++] = (opcode >> 1) & 0x01;
  bitarray[i++] = opcode & 0x01;
  //chksum:
  bitarray[i++] = chksum >> 1;
  bitarray[i++] = chksum & 0x01;

  //build raw bit array (includes heading and trailing zeros and 1-0 separator
  uint8_t bitcounter = 0;
  for (uint8_t i=0; i < 10; i++) rawbitarray[bitcounter++] = 0;
  rawbitarray[bitcounter++] = 1;
  rawbitarray[bitcounter++] = 0;
  for (uint8_t i=0; i < 20; i++) {
    rawbitarray[bitcounter++] = bitarray[i];
    rawbitarray[bitcounter++] = 1;
    rawbitarray[bitcounter++] = 0;
  }
  for (uint8_t i=0; i < 10; i++) rawbitarray[bitcounter++] = 0;
  
  Serial.print("Send: ");
  printButtonAndOpcode(bitarray);
  Serial.print(" Debug: ");
  debug(bitarray); //debug

  sendRawBitArray(rawbitarray, bitcounter, OUTPUT_PIN, 636);

}



uint8_t printButtonAndOpcode(uint8_t* bits) {
  uint8_t btn = getBtn(bits);
  uint8_t opcode = getOpcode(bits);
  
  switch (btn) {
    case RF_BTN1:
      Serial.print("Button 1 - ");
      break;
    case RF_BTN2:
      Serial.print("Button 2 - ");
      break;
    case RF_BTN3:
      Serial.print("Button 3 - ");
      break;
    case RF_BTN4:
      Serial.print("Button 4 - ");
      break;
    default:
      Serial.print("Invalid button!");
      return 0; //failed (cannot happen; we have 2 bits and 4 different cases)
      break;
  }

  switch (opcode) {
    case RF_ON:
      Serial.print("ON");
      break;
    case RF_OFF:
      Serial.print("OFF");
      break;
    case RF_DIM_UP:
      Serial.print("Dim+");
      break;
    case RF_DIM_DOWN:
      Serial.print("Dim-");
      break;
    case RF_ON_ALL:
      Serial.print("All ON");
      break;
    case RF_OFF_ALL:
      Serial.print("All OFF");
      break;
    default:
      Serial.print("Invalid Opcode!");
      return 0; //failed
      break;
  }
  Serial.println();
  //Serial.flush();
  return 1;
}



void debug(uint8_t* bits) {
  //bit array to byte array
  uint8_t code[4];
  for (uint8_t i=0; i < 4; i++) {
    uint8_t p = i << 3;
    code[i] = bits[p] << 7 | bits[p+1] << 6 | bits[p+2] << 5 | bits[p+3] << 4 | bits[p+4] << 3 | bits[p+5] << 2 | bits[p+6] << 1 | bits[p+7];
  }
  //print bitarray as hex
  for (int i=0; i < 3; i++) {
    Serial.print(code[i], HEX);
  }
  Serial.print("  -  ");

  uint16_t tag = getTag(bits);
  Serial.print(tag, HEX);

  Serial.print(":  ");

  for (int i=0; i < 20; i++) {
    if (i == 12) Serial.print(" ");
    if (i == 14) Serial.print(" ");
    if (i == 18) Serial.print(" ");
    Serial.print(bits[i]);
  }  
  
  Serial.println();
  Serial.flush();
}  



//------------------
//SERIAL INPUT STUFF
//------------------

char readCharFromInput(char wait) {
  if (wait) while(Serial.available() == 0) {} //wait next char
  if(Serial.available()) return Serial.read();
  return 255;
}
#define readNextChar() readCharFromInput(0)
#define waitAndReadNextChar() readCharFromInput(1)

uint16_t t2h(uint8_t t1, uint8_t t2, uint8_t t3) {
  char *pEnd;
  char tmp[] = "000";
  tmp[0] = t1; tmp[1] = t2; tmp[2] = t3;
  return strtol(tmp, &pEnd, 16);
}

void processInput() {
  char token;
  token = waitAndReadNextChar();
  uint16_t tag;
  uint8_t t1, t2, t3, inbutton, button, incmd, cmd;
  tag = RF_TAG;
  switch(token) { 
    case 'C': 
      Serial.print(F("Serial  - "));
      t1 = waitAndReadNextChar();
      t2 = waitAndReadNextChar();
      t3 = waitAndReadNextChar();
      tag = t2h(t1, t2, t3);
      waitAndReadNextChar(); //wait separator
    case 'c': 
      inbutton = waitAndReadNextChar();
      waitAndReadNextChar(); //wait separator
      incmd = waitAndReadNextChar();
      switch(inbutton) {
        case '1':
          button = RF_BTN1;
          break;
        case '2':
          button = RF_BTN2;
          break;
        case '3':
          button = RF_BTN3;
          break;
        case '4':
          button = RF_BTN4;
          break;
      }
      switch(incmd) {
        case '0':
          cmd = RF_OFF;
          break;
        case '1':
          cmd = RF_ON;
          break;
      }
      sendCode(tag, button, cmd);
      break;
    case 'L':
      learnNewTag();
      break;
    case 'S':
      Serial.print("Set new tag - ");
      t1 = waitAndReadNextChar();
      t2 = waitAndReadNextChar();
      t3 = waitAndReadNextChar();
      tag = t2h(t1, t2, t3);
      setNewTag(tag);
      break;
    case '\n': case '\r': case 'H': case 'h':
      Serial.println(F("---HELP---\n / = this help \n /c = Send command \"/cb,c\" (button 1-4, 0=off/1=on) \n /C = Send command \"/Cxxx,b,c\" (tag in hex, button 1-4, 0=off/1=on) \n /L = Learn new tag \n /S = Set new tag \"/Sxxx\" (tag in hex) \n---------- \n"));
    break;
    default: break;
  }
}


