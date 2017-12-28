#include "Arduino.h"
#include "DSC.h"
#include "DSC_Constants.h"
#include "DSC_Globals.h"
#include <TextBuffer.h>

/// ----- GLOBAL VARIABLES -----
/*
 * The existence of global variables are "declared" in DSC_Global.h, so that each 
 * source file that includes the header knows about them. The variables must then be
 * “defined” once in one of the source files (this one).

 * The following structure contains all of the global variables used by the ISR to 
 * communicate with the DSC.clkCalled() object. You cannot pass parameters to an 
 * ISR so these values must be global. The fields are defined in DSC_Globals.h
 */
dscGlobal_t dscGlobal;

// ----- Input/Output Pins (Global) -----
byte CLK;         // Keybus Yellow (Clock Line)
byte DTA_IN;      // Keybus Green (Data Line via V divider)
byte DTA_OUT;     // Keybus Green Output (Data Line through driver)
byte LED;         // LED pin on the arduino

void clkCalled_Handler(); // Prototype for interrupt handler, called on clock line change

TextBuffer tempByte(12);        // Initialize TextBuffer.h for temp byte buffer 
TextBuffer pInfo(WORD_BITS);    // Initialize TextBuffer.h for panel info
TextBuffer kInfo(WORD_BITS);    // Initialize TextBuffer.h for keypad info

/// --- END GLOBAL VARIABLES ---

DSC::DSC(void)
  {
    // ----- Time Variables -----
    // Volatile variables, modified within ISR, based on micros()
    dscGlobal.intervalTimer = 0;   
    dscGlobal.clockChange = 0;
    dscGlobal.lastChange = 0;      
    dscGlobal.lastRise = 0;         // NOT USED YET
    dscGlobal.lastFall = 0;         // NOT USED YET
    dscGlobal.newWord = false;      // NOT USED YET
    
    // Time variables, based on millis()
    dscGlobal.lastStatus = 0;
    dscGlobal.lastData = 0;

    // Class level variables to hold time elements
    int yy = 0, mm = 0, dd = 0, HH = 0, MM = 0, SS = 0;
    bool timeAvailable = false;     // Changes to true when kCmd == 0xa5 to 
                                    // indicate that the time elements are valid

    // ----- Input/Output Pins (DEFAULTS) ------
    //   These can be changed prior to DSC.begin() using functions below
    CLK      = 3;    // Keybus Yellow (Clock Line)
    DTA_IN   = 4;    // Keybus Green (Data Line via V divider)
    DTA_OUT  = 12;    // Keybus Green Output (Data Line through driver)
    LED      = 13;   // LED pin on the arduino

    // ----- Keybus Word String Vars -----
    dscGlobal.pBuild="", dscGlobal.pWord="";
    dscGlobal.oldPWord="", dscGlobal.pMsg="";
    dscGlobal.kBuild="", dscGlobal.kWord="";
    dscGlobal.oldKWord="", dscGlobal.kMsg="";
    dscGlobal.pCmd = 0, dscGlobal.kCmd = 0;

    // ----- Byte Array Variables -----
    //dscGlobal.pBytes[ARR_SIZE] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0};    // NOT USED
    //dscGlobal.kBytes[ARR_SIZE] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0};    // NOT USED
  }

int DSC::addSerial(void)
  {
  }

void DSC::begin(void)
  {
    pinMode(CLK, INPUT);
    pinMode(DTA_IN, INPUT);
    pinMode(DTA_OUT, OUTPUT);
    pinMode(LED, OUTPUT);

    tempByte.begin();         // Begin the tempByte buffer, allocate memory
    pInfo.begin();            // Begin the panel info buffer, allocate memory
    kInfo.begin();            // Begin the keypad info buffer, allocate memory

    // Set the interrupt pin
    intrNum = digitalPinToInterrupt(CLK);

    // Attach interrupt on the CLK pin
    attachInterrupt(intrNum, clkCalled_Handler, CHANGE);  
    //   Changed from RISING to CHANGE to read both panel and keypad data
  }

/* This is the interrupt handler used by this class. It is called every time the input
 * pin changes from high to low or from low to high.
 *
 * The function is not a member of the DSC class, it must be in the global scope in order 
 * to be called by attachInterrupt() from within the DSC class.
 */
void clkCalled_Handler()
  {
    dscGlobal.clockChange = micros();                   // Save the current clock change time
    dscGlobal.intervalTimer = 
        (dscGlobal.clockChange - dscGlobal.lastChange); // Determine interval since last clock change

    // If the interval is longer than the required amount (NEW_WORD_INTV - 200 us)
    if (dscGlobal.intervalTimer > (NEW_WORD_INTV - 200)) {
      dscGlobal.kWord = dscGlobal.kBuild;               // Save the complete keypad raw data bytes sentence
      dscGlobal.kBuild = "";                            // Reset the raw data bytes keypad word being built
    }
    dscGlobal.lastChange = dscGlobal.clockChange;       // Re-save the current change time as last change time

    // If clock line is going HIGH, this is PANEL data
    if (digitalRead(CLK)) {                
      dscGlobal.lastRise = dscGlobal.lastChange;        // Set the lastRise time
      if (dscGlobal.pBuild.length() <= MAX_BITS) {      // Limit the string size to something manageable
        //delayMicroseconds(120);           // Delay for 120 us to get a valid data line read
        if (digitalRead(DTA_IN)) dscGlobal.pBuild += "1"; 
        else dscGlobal.pBuild += "0";
      }
    }
    // Otherwise, it's going LOW, this is KEYPAD data
    else {                                  
      dscGlobal.lastFall = dscGlobal.lastChange;          // Set the lastFall time
      if (dscGlobal.kBuild.length() <= MAX_BITS) {        // Limit the string size to something manageable 
        //delayMicroseconds(200);           // Delay for 300 us to get a valid data line read
        if (digitalRead(DTA_IN)) dscGlobal.kBuild += "1"; 
        else dscGlobal.kBuild += "0";
      }
    }
  }

int DSC::process(void)
  {
    // ------------ Get/process incoming data -------------
    dscGlobal.pCmd = 0, 
    dscGlobal.kCmd = 0; 
    timeAvailable = false;      // Set the time element status to invalid
    
    // ----------------- Turn on/off LED ------------------
    if ((millis() - dscGlobal.lastChange) > 500)
      digitalWrite(LED, 0);     // Turn LED OFF (no recent status command [0x05])
    else
      digitalWrite(LED, 1);     // Turn LED ON  (recent status command [0x05])
    
    /*
     * The normal clock frequency is 1 Hz or one cycle every ms (1000 us) 
     * The new word marker is clock high for about 15 ms (15000 us)
     * If the interval is longer than the required amount (NEW_WORD_INTV + 200 us), 
     * and the panel word in progress (pBuild) is more than 8 characters long,
     * process the panel and keypad words, otherwise return failure (0).
     */
    if ((dscGlobal.intervalTimer < (NEW_WORD_INTV + 200)) || 
            (dscGlobal.pBuild.length() < 8)) return 0;  // Return failure

    dscGlobal.pWord = dscGlobal.pBuild;   // Save the complete panel raw data bytes sentence
    dscGlobal.pBuild = "";                // Reset the raw data panel word being built
    dscGlobal.pMsg = "";                  // Initialize panel message for output
    //dscGlobal.pCmd = 0;
    
    dscGlobal.kMsg = "";                  // Initialize keypad message for output 
    //dscGlobal.kCmd = 0;
    
    dscGlobal.pCmd = decodePanel();       // Decode the panel binary, return command byte, or 0
    dscGlobal.kCmd = decodeKeypad();      // Decode the keypad binary, return command byte, or 0
    
    if (dscGlobal.pCmd && dscGlobal.kCmd) return 3;  // Return 3 if both were decoded
    else if (dscGlobal.kCmd) return 2;    // Return 2 if keypad word was decoded
    else if (dscGlobal.pCmd) return 1;    // Return 1 if panel word was decoded
    else return 0;                        // Return failure if none were decoded
  }

byte DSC::decodePanel(void) 
  {
    // ------------- Process the Panel Data Word ---------------
    byte cmd = binToInt(dscGlobal.pWord,0,8);   // Get the panel pCmd (data word type/command)
    
    if (dscGlobal.pWord == dscGlobal.oldPWord || cmd == 0x00) {
      // Skip this word if the data hasn't changed, or pCmd is empty (0x00)
      return 0;     // Return failure
    }
    else {     
      // This seems to be a valid word, try to process it  
      dscGlobal.lastData = millis();            // Record the time (last data word was received)
      dscGlobal.oldPWord = dscGlobal.pWord;     // This is a new/good word, save it
     
      // Interpret the data
      if (cmd == 0x05) 
      {
        dscGlobal.lastStatus = millis();        // Record the time for LED logic
        dscGlobal.pMsg += F("{\"Status\":[");
        if (binToInt(dscGlobal.pWord,16,1)) {
          dscGlobal.pMsg += F("\"Ready\"");
        }
        else {
          dscGlobal.pMsg += F("\"Not Ready\"");
        }
        if (binToInt(dscGlobal.pWord,12,1)) dscGlobal.pMsg += F(",\"Error\"");
        if (binToInt(dscGlobal.pWord,13,1)) dscGlobal.pMsg += F(",\"Bypass\"");
        if (binToInt(dscGlobal.pWord,14,1)) dscGlobal.pMsg += F(",\"Memory\"");
        if (binToInt(dscGlobal.pWord,15,1)) dscGlobal.pMsg += F(",\"Armed\"");
        if (binToInt(dscGlobal.pWord,17,1)) dscGlobal.pMsg += F(",\"Program\"");
        if (binToInt(dscGlobal.pWord,29,1)) dscGlobal.pMsg += F(",\"Power Fail\"");   // ??? - maybe 28 or 20?
        dscGlobal.pMsg += F("]}");
      }    
      else if (cmd == 0xa5)
      {
        dscGlobal.pMsg += F("{\"PanelDateTime\":\"");
        int y3 = binToInt(dscGlobal.pWord,9,4);
        int y4 = binToInt(dscGlobal.pWord,13,4);
        yy = (String(y3) + String(y4)).toInt();
        mm = binToInt(dscGlobal.pWord,19,4);
        dd = binToInt(dscGlobal.pWord,23,5);
        HH = binToInt(dscGlobal.pWord,28,5);
        MM = binToInt(dscGlobal.pWord,33,6);     

        timeAvailable = true;      // Set the time element status to valid
        dscGlobal.pMsg += "20" + String(yy) + "/" + String(mm) + "/" + String(dd) + 
                          " " + String(HH) + ":" + String(MM) + "\"";

        dscGlobal.pMsg += ",\"Armed\":";
        byte arm = binToInt(dscGlobal.pWord,41,2);
        byte master = binToInt(dscGlobal.pWord,43,1);
        byte user = binToInt(dscGlobal.pWord,43,6); // 0-36
        if (arm == 0x02) {
          dscGlobal.pMsg += F("1");
          user = user - 0x19;
        }
        if ((arm == 0x03) || (arm == 0)) { //MC: Assuming 0 is also disarmed
          dscGlobal.pMsg += F("0");
        }
        if (arm > 0) {
          if (master) dscGlobal.pMsg += F(",\"MasterCode\":"); 
          else dscGlobal.pMsg += F(",\"UserCode\":");
          user += 1; // shift to 1-32, 33, 34
          if (user > 34) user += 5; // convert to system code 40, 41, 42
          dscGlobal.pMsg += "\"" + String(user) + "\"";
        }
        dscGlobal.pMsg += "}";
      }      
      else if (cmd == 0x27)
      {
        dscGlobal.pMsg += F("{\"ZonesA\":[");
        int zones = binToInt(dscGlobal.pWord,8+1+8+8+8+8,8);
        dscGlobal.pMsg += String((zones & 1) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 2) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 4) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 8) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 16) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 32) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 64) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 128) != 0) + F("]}");
        //if (zones == 0) dscGlobal.pMsg += "Ready ";
      }
      
      if (cmd == 0x2d)
      {
        dscGlobal.pMsg += F("{\"ZonesB\":[");
        int zones = binToInt(dscGlobal.pWord,8+1+8+8+8+8,8);
        dscGlobal.pMsg += String((zones & 1) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 2) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 4) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 8) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 16) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 32) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 64) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 128) != 0) + F("]}");
        //if (zones == 0) dscGlobal.pMsg += "Ready ";
      }
      
      if (cmd == 0x34)
      {
        dscGlobal.pMsg += F("{\"ZonesC\":[");
        int zones = binToInt(dscGlobal.pWord,8+1+8+8+8+8,8);
        dscGlobal.pMsg += String((zones & 1) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 2) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 4) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 8) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 16) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 32) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 64) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 128) != 0) + F("]}");
        //if (zones == 0) dscGlobal.pMsg += "Ready ";
      }
      
      if (cmd == 0x3e)
      {
        dscGlobal.pMsg += F("{\"ZonesD\":[");
        int zones = binToInt(dscGlobal.pWord,8+1+8+8+8+8,8);
        dscGlobal.pMsg += String((zones & 1) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 2) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 4) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 8) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 16) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 32) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 64) != 0) + F(",");
        dscGlobal.pMsg += String((zones & 128) != 0) + F("]}");
        //if (zones == 0) dscGlobal.pMsg += "Ready ";
      }
      // --- The other 32 zones for a 1864 panel need to be added after this ---
      else if (cmd == 0x11) {
        dscGlobal.pMsg += F("{\"KeypadQuery\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      }
      else if (cmd == 0x0a) {
        dscGlobal.pMsg += F("{\"PanelProgramMode\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      } 
      else if (cmd == 0x5d) {
        dscGlobal.pMsg += F("{\"AlarmMemoryGroup1\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      } 
      else if (cmd == 0x63) {
        dscGlobal.pMsg += F("{\"AlarmMemoryGroup2\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      } 
      else if (cmd == 0x64) {
        dscGlobal.pMsg += F("{\"BeepCommandGroup1\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      } 
      else if (cmd == 0x69) {
        dscGlobal.pMsg += F("{\"BeepCommandGroup2\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      } 
      else if (cmd == 0x39) {
        dscGlobal.pMsg += F("{\"Undefined\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      } 
      else if (cmd == 0xb1) {
        dscGlobal.pMsg += F("{\"ZoneConfiguration\":\"");
        dscGlobal.pMsg += String(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));
        dscGlobal.pMsg += F("\"}");
      }
    return cmd;     // Return success
    }
  }

byte DSC::decodeKeypad(void) 
  {
    // ------------- Process the Keypad Data Word ---------------
    byte cmd = binToInt(dscGlobal.kWord,0,8);     // Get the keypad pCmd (data word type/command)
    String btnStr = F("[Button] ");

    if (dscGlobal.kWord.indexOf("0") == -1) {  
      // Skip this word if kWord is all 1's
      return 0;     // Return failure
    }
    else { 
      // This seems to be a valid word, try to process it
      dscGlobal.lastData = millis();              // Record the time (last data word was received)
      dscGlobal.oldKWord = dscGlobal.kWord;                 // This is a new/good word, save it

      byte kByte2 = binToInt(dscGlobal.kWord,8,8); 
     
      // Interpret the data
      if (cmd == kOut) {
        if (kByte2 == one)
          dscGlobal.kMsg += btnStr + "1";
        else if (kByte2 == two)
          dscGlobal.kMsg += btnStr + "2";
        else if (kByte2 == three)
          dscGlobal.kMsg += btnStr + "3";
        else if (kByte2 == four)
          dscGlobal.kMsg += btnStr + "4";
        else if (kByte2 == five)
          dscGlobal.kMsg += btnStr + "5";
        else if (kByte2 == six)
          dscGlobal.kMsg += btnStr + "6";
        else if (kByte2 == seven)
          dscGlobal.kMsg += btnStr + "7";
        else if (kByte2 == eight)
          dscGlobal.kMsg += btnStr + "8";
        else if (kByte2 == nine)
          dscGlobal.kMsg += btnStr + "9";
        else if (kByte2 == aster)
          dscGlobal.kMsg += btnStr + "*";
        else if (kByte2 == zero)
          dscGlobal.kMsg += btnStr + "0";
        else if (kByte2 == pound)
          dscGlobal.kMsg += btnStr + "#";
        else if (kByte2 == stay)
          dscGlobal.kMsg += btnStr + F("Stay");
        else if (kByte2 == away)
          dscGlobal.kMsg += btnStr + F("Away");
        else if (kByte2 == chime)
          dscGlobal.kMsg += btnStr + F("Chime");
        else if (kByte2 == reset)
          dscGlobal.kMsg += btnStr + F("Reset");
        else if (kByte2 == kExit)
          dscGlobal.kMsg += btnStr + F("Exit");
        else if (kByte2 == lArrow)  // These arrow commands don't work every time
          dscGlobal.kMsg += btnStr + F("<");
        else if (kByte2 == rArrow)  // They are often reverse for unknown reasons
          dscGlobal.kMsg += btnStr + F(">");
        else if (kByte2 == kOut)
          dscGlobal.kMsg += F("[Keypad Response]");
        else {
          dscGlobal.kMsg += "[Keypad] 0x" + String(kByte2, HEX) + " (Unknown)";
        }
      }

      if (cmd == fire)
        dscGlobal.kMsg += btnStr + F("Fire");
      if (cmd == aux)
        dscGlobal.kMsg += btnStr + F("Auxillary");
      if (cmd == panic)
        dscGlobal.kMsg += btnStr + F("Panic");
      
      return cmd;     // Return success
    }
  }

const char* DSC::pnlFormat(void)
  {
    if (!dscGlobal.pCmd) return NULL;       // return failure
    // Formats the panel binary string into bytes of binary data in the form:
    // 8 1 8 8 8 8 8 etc, and returns a pointer to the buffer 
    pInfo.clear();
    pInfo.print("[Panel]  ");

    if (dscGlobal.pWord.length() > 8) {
      pInfo.print(binToChar(dscGlobal.pWord, 0, 8));
      pInfo.print(" ");
      pInfo.print(binToChar(dscGlobal.pWord, 8, 9));
      pInfo.print(" ");
      int grps = (dscGlobal.pWord.length() - 9) / 8;
      for(int i=0;i<grps;i++) {
        pInfo.print(binToChar(dscGlobal.pWord, 9+(i*8),9+(i+1)*8));
        pInfo.print(" ");
      }
      if (dscGlobal.pWord.length() > ((grps*8)+9))
        pInfo.print(binToChar(dscGlobal.pWord, (grps*8)+9, dscGlobal.pWord.length()));
    }
    else
      pInfo.print(binToChar(dscGlobal.pWord, 0, dscGlobal.pWord.length()));

    if (pnlChkSum(dscGlobal.pWord)) pInfo.print(" (OK)");

    return pInfo.getBuffer();               // return the pointer
  }

const char* DSC::pnlRaw(void)
  {
    if (!dscGlobal.pCmd) return NULL;       // return failure
    // Puts the raw word into a buffer and returns a pointer to the buffer
    pInfo.clear();
    pInfo.print("");
    
    for(int i=0;i<dscGlobal.pWord.length();i++) {
      pInfo.print(dscGlobal.pWord[i]);
    }
    
    if (pnlChkSum(dscGlobal.pWord)) pInfo.print(" (OK)");
    
    return pInfo.getBuffer();               // return the pointer
  }

const char* DSC::kpdRaw(void)
  {
    if (!dscGlobal.kCmd) return NULL;       // return failure
    // Puts the raw word into a buffer and returns a pointer to the buffer
    kInfo.clear();
    kInfo.print("");
    
    for(int i=0;i<dscGlobal.kWord.length();i++) {
      kInfo.print(dscGlobal.kWord[i]);
    }
    
    return kInfo.getBuffer();               // return the pointer
  }

const char* DSC::kpdFormat(void)
  {
    if (!dscGlobal.kCmd) return NULL;       // return failure
    // Formats the referenced string into bytes of binary data in the form:
    // 8 8 8 8 8 8 etc, and returns a pointer to the buffer 
    kInfo.clear();
    kInfo.print("[Keypad] ");
    
    if (dscGlobal.kWord.length() > 8) {
      int grps = dscGlobal.kWord.length() / 8;
      for(int i=0;i<grps;i++) {
        kInfo.print(binToChar(dscGlobal.kWord, i*8,(i+1)*8));
        kInfo.print(" ");
      }
      if (dscGlobal.kWord.length() > (grps*8))
        kInfo.print(binToChar(dscGlobal.kWord, (grps*8),dscGlobal.kWord.length()));
    }
    else
      kInfo.print(binToChar(dscGlobal.kWord, 0, dscGlobal.kWord.length()));

    return kInfo.getBuffer();               // return the pointer
  }

int DSC::pnlChkSum(String &dataStr)
  {
    // Sums all but the last full byte (minus padding) and compares to last byte
    // returns 0 if not valid, and 1 if checksum valid
    int cSum = 0;
    if (dataStr.length() > 8) {
      cSum += binToInt(dataStr,0,8);
      int grps = (dataStr.length() - 9) / 8;
      for(int i=0;i<grps;i++) {
        if (i<(grps-1)) 
          cSum += binToInt(dataStr,9+(i*8),8);
        else {
          byte cSumMod = cSum % 256;
          //String cSumStr = String(chkSum, HEX);
          //int cSumLen = cSumStr.length();
          byte lastByte = binToInt(dataStr,9+(i*8),8);
          //byte cSumByte = binToInt(cSumStr,(cSumLen-2),2);
          //if (cSumSub == lastByte) return true;
          //Serial.println(cSum);
          //Serial.println(cSumMod);
          //Serial.println(lastByte);
          if (cSumMod == lastByte) return 1;
        }
      }
    }
    return 0;
  }

unsigned int DSC::binToInt(String &dataStr, int offset, int dataLen)
  {
    // Returns the value of the binary data in the String from "offset" to "dataLen" as an int
    int iBuf = 0;
    for(int j=0;j<dataLen;j++) {
      iBuf <<= 1;
      if (dataStr[offset+j] == '1') iBuf |= 1;
    }
    return iBuf;
  }

const char* DSC::binToChar(String &dataStr, int offset, int endData)
  {   
    tempByte.clear();
    // Returns a char array of the binary data in the String from "offset" to "endData"
    tempByte.print(dataStr[offset]);
    for(int j=1;j<(endData-offset);j++) {
      tempByte.print(dataStr[offset+j]);
    }
    return tempByte.getBuffer();
  }

String DSC::byteToBin(byte b)
  {
    // Returns the 8 bit binary representation of byte "b" with leading zeros as a String
    int zeros = 8-String(b, BIN).length();
    String zStr = "";
    for (int i=0;i<zeros;i++) zStr += "0";
    return zStr + String(b, BIN);
  }

void DSC::zeroArr(byte byteArr[])
  {
    // Zeros an array of ARR_SIZE  bytes
    for (int i=0;i<ARR_SIZE;i++) byteArr[i] = 0;
  }

void DSC::setCLK(int p)
  {
    // Sets the clock pin, must be called prior to begin()
    CLK = p;
  }

void DSC::setDTA_IN(int p)
  {
    // Sets the data in pin, must be called prior to begin()
    DTA_IN = p;
  }
  
void DSC::setDTA_OUT(int p)
  {
    // Sets the data out pin, must be called prior to begin()
    DTA_OUT = p;
  }
void DSC::setLED(int p)
  {
    // Sets the LED pin, must be called prior to begin()
    LED = p;
  }

size_t DSC::write(uint8_t character) 
  { 
    // Code to display letter when given the ASCII code for it
  }

size_t DSC::write(const char *str) 
  { 
    // Code to display string when given a pointer to the beginning -- 
    // remember, the last character will be null, so you can use a while(*str). 
    // You can increment str (str++) to get the next letter
  }
  
size_t DSC::write(const uint8_t *buffer, size_t size) 
  { 
    // Code to display array of chars when given a pointer to the beginning 
    // of the array and a size -- this will not end with the null character
  }

///////// OLD //////////
/*
int TextBuffer::clear() 
  {
    if (!buffer) return 0;        // return failure
    buffer[0] = (char)0;
    position = 0;
    length = 0;
    return 1;                     // return success
  } 

int TextBuffer::end()
  {
    if (!buffer) return 0;        // return failure
    free(buffer);
    return 1;                     // return success
  }

const char* TextBuffer::getBuffer() 
  {
    if (!buffer) return (char)0;  // return null terminator
    return (const char*)buffer;   // return const char array buffer pointer
  }
  
char* TextBuffer::getBufPointer() 
  {
    if (!buffer) return 0;        // return failure
    clear();                      // clear the buffer
    return (char*)buffer;         // return char array buffer pointer
  }
  
int TextBuffer::getSize()
  {
    if (!buffer) return 0;        // return failure
      if (strlen((const char*)buffer) != length)
        // if the length is not correct, probably due to an external
        // write to the buffer, reset it to the correct length
        {
          length = strlen((const char*)buffer);
        }
    return length;
  }

int TextBuffer::getCapacity()
  {
    if (!buffer) return 0;        // return failure
    return capacity;
  }
  
String TextBuffer::getCheckSum()
  {
    // Create Checksum
    char checkSum = 0;
    int csCount = 1;
    while (buffer[csCount + 1] != 0)
    {
      checkSum ^= buffer[csCount];
      csCount++;
    }
    // Change the checksum to a string, in HEX form, convert to upper case, and print
    String checkSumStr = String(checkSum, HEX);
    checkSumStr.toUpperCase();
    
    return checkSumStr;
  }
  
*/
