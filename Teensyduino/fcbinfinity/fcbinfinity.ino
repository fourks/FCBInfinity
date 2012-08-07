/**
 * FCBInfinity, a Behringer FCB1010 modification.
 *
 * This is the main Arduino/Teensyduino file for the FCBInfinity project.
 * Please note that this is VERY much still a work in progress and you should
 * consider the code below still in Alpha/Testing phase. Use at own risk
 *
 * -Mackatack
 */


/**
 * ###########################################################
 * Main includes for the FCBInfinity project
 */

#include <Bounce.h>
#include <LedControl.h>
#include <LiquidCrystalFast.h>
#include <MIDI.h>
#include <EEPROM.h>

#include "utils_FCBSettings.h"
#include "utils_FCBTimer.h"
#include "utils_FCBEffectManager.h"

#include "io_ExpPedals.h"
#include "io_AxeMidi.h"

#include "fcbinfinity.h"


/**
 * ###########################################################
 * Initialization of the objects that are available throughout
 * the entire project. see fcbinfinity.h for more information
 * per object, this is also where these objects are externalized
 */

// Initialization of the LCD
LiquidCrystalFast lcd(19, 18, 38, 39, 40, 41, 42);  // pins: (RS, RW, Enable, D4, D5, D6, D7)

// Initialization of the MAX7219 chip, this chip controls all the leds on the system (except stompbank-rgb)
LedControl ledControl = LedControl(43, 20, 21, 1);  // pins: (DIN, CLK, LOAD, number_of_chips)

// The buttons in an array together with the x,y coordinates where the corresponding
// led can be found on the MAX7219 chip using the ledControl object.
// See the mkBounce() function for more information about the buttons.
_FCBInfButton btnRowUpper[] = {
  {2, 4, mkBounce(11), false},
  {2, 6, mkBounce(12), false},
  {2, 3, mkBounce(13), false},
  {2, 7, mkBounce(14), false},
  {2, 5, mkBounce(15), false}
};
_FCBInfButton btnRowLower[] = {
  {1, 4, mkBounce(4), false},
  {1, 6, mkBounce(5), false},
  {1, 3, mkBounce(7), false},
  {1, 7, mkBounce(8), false},
  {1, 5, mkBounce(9), false}
};
Bounce btnBankUp = mkBounce(16);
Bounce btnBankDown = mkBounce(10);
Bounce btnStompBank = mkBounce(25);
Bounce btnExpPedalRight = mkBounce(24);

// Initialization of the ExpressionPedals
ExpPedals_Class ExpPedal1(A6);  // Analog pin 6 (pin 44 on the Teensy)
ExpPedals_Class ExpPedal2(A7);  // Analog pin 7 (pin 45 on the Teensy)

// Some important variables we'll need later on and want globally, if we want to
// access them from any file, externalize them in fcbinfinity.h

int     g_iCurrentPreset = 0;       // Here we track the current preset, in case we want to use it
int     g_iPresetBank = 0;          // The bank controllable by the BankUp and BankDown buttons
int     g_iStompBoxMode = 0;        // The current stompboxmode, see the loop() function below
boolean g_bXYToggle = false;        // The X/Y toggler

// The defines for the stompbox modes, see the switch clause in the loop() below
#define STOMP_MODE_NORMAL     0
#define STOMP_MODE_10STOMPS   1
#define STOMP_MODE_LOOPER     2

// Some debugging timer callbacks
void effectUpdate(FCBTimer * t) {
  byte test[] = {
    AXE_MANUFACTURER,
    AxeMidi.getModel(),
    0x0E
  };
  Serial.println("CALLING FUNCTION 0x0E");
  AxeMidi.sendSysEx(5, test);
}
void timerCallbackRGBLedOff(FCBTimer*) {
  analogWrite(PIN_RGBLED_G, 0);
}

/**
 * This function will be called by the AxeMidi library because we registered
 * it as a callback in setup()
 * @param sysex holds the entire SysEx message including the opening and closing bytes
 * @param length holds the number bytes in the entire SysEx message
 */
void onAxeFxSysExMessage(byte * sysex, int length) {

  // Byte 5 of the SysEx message holds the function number, the function
  // ids are defined in AxeMidi.h
  switch (sysex[5])
  {
    case SYSEX_AXEFX_REALTIME_TEMPO:
      // Tempo, just flash a led or something
      // There's no additional data

      // Turn on the led
      analogWrite(PIN_RGBLED_G, 80);

      // Schedule a new timer that turns off the led again after 100ms
      FCBTimerManager::addTimeout(100, &timerCallbackRGBLedOff);
      return;

    case SYSEX_AXEFX_PRESET_MODIFIED:
      // This gets sent when you manually edit the preset on the axefx, it sends this
      // when adding or removing effect blocks, editing parameters, etc. etc. almost everything.
      // Everything but when you manually bypass a block. Total suckage. The AxeFx is completely silent
      // when you press the "FX BYP" button on the Axe.

      // I guess we should use this later on to update params from the AxeFx, but we might as well ask for state
      // updates manually every few seconds using a timer, that is, if we really want to keep the device completely
      // in sync with the AxeFx (which we probably want, no? :P)

    case SYSEX_AXEFX_REALTIME_TUNER: {
      // Tuner
      // Byte 6 Holds the note starting at A
      // Byte 7 Holds the octave
      // Byte 8 Holds the finetune data between 0x00 and 0x7F
      lcd.setCursor(0,1);
      lcd.print("                    ");

      // Translate the finetune (0-127) to a value between 2 and 20
      // so we know where to print the '|' character on the lcd
      // We can initialize the pos integer here because i've added curly
      // braces around this case block.
      int pos = map(sysex[8], 0, 127, 2, 20);
      lcd.setCursor(pos,1);
      lcd.print("|");

      // Show a > or < if we need to tune up or down.
      // This will show >|< if we're in tune
      if (sysex[8]>=62)
        lcd.print("<");
      if (sysex[8]<=65) {
        lcd.setCursor(pos-1,1);
        lcd.print(">");
      }

      // Some debug info, print the raw finetune data
      lcd.setCursor(18,1);
      lcd.print(sysex[8], HEX);

      // Jump to the first character on the second line of the lcd
      // Show the note name here
      lcd.setCursor(0,1);
      lcd.print(AxeMidi.notes[sysex[6]]);

      return;
    }
    case SYSEX_AXEFX_PRESET_NAME:
      // Preset name response, print the preset name on the lcd
      // Echo? If patch name length <= 8, just return
      if (length<=8) return;

      // just output the sysex bytes starting from position 6 to the lcd
      lcd.setCursor(3,0);
      lcd.print(" ");
      for(int i=6; i<length && i<20+6-4; ++i) {
        lcd.print((char)sysex[i]);
      }

      return;

    case SYSEX_AXEFX_PRESET_CHANGE:
      // Patch change event!

      // Byte 6 and 7 hold the new patch number (starting at 0)
      // however these values only count to 127, so values higher
      // than 0x7F need to be calculated differently
      Serial.print("Patch change: ");
      g_iCurrentPreset = sysex[6]*128 + sysex[7] + 1;
      g_iPresetBank = (g_iCurrentPreset-1)/4;
      setLedDigitValue(g_iPresetBank);

      Serial.print(g_iCurrentPreset);
      Serial.println("!");

      // Put the new number on the LCD, but prefix the value
      // with zeros just like the AxeFx does.
      lcd.setCursor(0,0);
      if (g_iCurrentPreset<100)
        lcd.print("0");
      if (g_iCurrentPreset<10)
        lcd.print("0");
      lcd.print(g_iCurrentPreset);
      lcd.print(" ");

      // Also ask the AxeFx to send us the effect bypass states
      AxeMidi.requestBypassStates();

      // We know the preset number and effect states, now ask the AxeFx to send us
      // the preset name as well.
      AxeMidi.requestPresetName();

      return;

    case SYSEX_AXEFX_GET_PRESET_EFFECT_BLOCKS_AND_CC_AND_BYPASS_STATE:
      // We received the current states of the effects.
      // The sysex will contain the following data
      // see: http://forum.fractalaudio.com/other-midi-controllers/39161-using-sysex-recall-present-effect-bypass-status-info-available.html

      // Some debug code until we figure out what all this means :P
      Serial.println("Preset effect states received");

      // declare some vars we'll use in the loop
      int state;
      int effectID;
      int cc;

      // Reset all the effectstates to 'not placed'
      FCBEffectManager.resetStates();

      for(int i=6; i<length-4; i+=5) {

        if (AxeMidi.getModel() < AXEMODEL_AXEFX2) {
          // Older models
          // byte+0 effect ID LS nibble
          // byte+1 effect ID MS nibble
          // byte+2 bypass CC# LS nibble
          // byte+3 bypass CC# MS nibble
          // byte+4 bypass state: 0=bypassed; 1=not bypassed
          effectID = (sysex[i+1] << 4) | sysex[i];
          cc = (sysex[i+3] << 4) | sysex[i+2];
          state = sysex[i+4];
        }
        else {
          // AxeFx3 and up style
          // for each effect there are 5 bytes:
          // byte+0, bit 1 = bypass, bit 2 is X/Y state
          // byte+1: 7 bits ccLs + 1 empty bit
          // byte+2: 6 empty bits + 1 bit for cc pedal or cc none + 1 bit ccMs
          // byte+3: 5 bits effectIdLs + 3 empty bits
          // byte+4: 4 empty bits + 4 bits effectIdMs
          effectID = ((sysex[i+3] & 0x78) >> 3) + ((sysex[i+4] & 0x0F) << 4); // block id
          cc = ((sysex[i+1] & 0x7E) >> 1) + ((sysex[i+2] & 3) << 6);  // cc number
          state = sysex[i] & 0x7F; // byp and XY state
        }

        // Update the state and cc of the effectblock to the new values.
        FCBEffectManager[effectID]->setStateAndCC(state, cc);
      }

      return;
  } // switch (sysex[5]) // FunctionID
}

/**
 * A seperate function to handle all the midi sysex messages
 * we might receive. Putting this in a separate function allows
 * us to use 'return' in case of errors etc.
 * If it detects a valid sysex for the AxeFx it will pass the message
 * to the handleAxeFxMidiSysEx() function
 */
void handleMidiSysEx() {

}

/**
 * Changes the current StompBoxMode and lights up the RGB-led
 * to set the current mode. See the loop() function for more info
 */
void setStompBoxMode(int iNewMode) {
  // set the new mode
  g_iStompBoxMode = iNewMode;

  // Change the RGB led to reflect the new mode.
  switch (g_iStompBoxMode) {
    case STOMP_MODE_NORMAL:
      setRGBLed(0, 0, 0);
      lcd.setCursor(0,1);
      lcd.print("Mode: Normal        ");
      break;
    case STOMP_MODE_10STOMPS:
      setRGBLed(0, 0, 50);
      lcd.setCursor(0,1);
      lcd.print("Mode: 10 stomps     ");
      break;
    case STOMP_MODE_LOOPER:
      setRGBLed(50, 0, 0);
      lcd.setCursor(0,1);
      lcd.print("Mode: Looper        ");
      break;
  }
}

/**
 * Sets the RGB-Led to a new color.
 */
inline void setRGBLed(int r, int g, int b) {
  analogWrite(PIN_RGBLED_R, r);
  analogWrite(PIN_RGBLED_G, g);
  analogWrite(PIN_RGBLED_B, b);
}


/**
 * updateIO
 * This method needs to be called first every loop and only once every loop
 * It checks for new data on all the IO, such as buttons, analog devices (ExpPedals) and Midi
 */
void updateIO() {
  // Check for new midi messages
  AxeMidi.handleMidi();

  // Update the states of the onboard expression pedals
  ExpPedal1.update();
  ExpPedal2.update();

  // Update the button states
  for(int i=0; i<5; ++i) {
    btnRowUpper[i].btn.update();
  }
  for(int i=0; i<5; ++i) {
    btnRowLower[i].btn.update();
  }
  btnBankUp.update();
  btnBankDown.update();
  btnStompBank.update();
  btnExpPedalRight.update();
}

/**
 * setLedDigitValue
 * sets a integer value on the led-digits
 */
void setLedDigitValue(int value) {
  boolean lastDP = false;
  boolean firstDP = false;
  if (value>999)
    lastDP = true;
  else if (value<0)
    firstDP = true;

  ledControl.setDigit(0, 2, value%10, lastDP);
  ledControl.setDigit(0, 1, value/10%10, false);
  ledControl.setDigit(0, 0, value/100%10, firstDP);
}

/**
 * doBootSplash()
 * My little FCBInfinity bootsplash animation,
 * This allows me to quickly see if the displays and leds are working
 * during startup.
 * This will probably get moved to an external file anytime soon.
 */
void doBootSplash() {
  // Set the led digits to read "InF."
  ledControl.setChar(0, 0, '1', false);
  ledControl.setChar(0, 1, ' ', false);
  ledControl.setLed(0, 1, 3, true);
  ledControl.setLed(0, 1, 5, true);
  ledControl.setLed(0, 1, 7, true);
  ledControl.setChar(0, 2, 'F', true);

   // Write something on the LCD
  lcd.begin(20, 2);
  lcd.print("  FCBInfinity v1.0");
  lcd.print("      by Mackatack");

  delay(800);
  lcd.clear();
  lcd.setCursor(0,0);
}

/**
 * Initialization function for all the buttons in this project;
 * it sets the corresponding pins to the INPUT_PULLUP state.
 * This state uses an internal pull-up resistor in the AT90USB1286 chip
 * This is why we didn't need to add a resistor per button to the custom
 * PCB. <3 Teensy/Arduino
 * @returns The Bounce object to check the button states
 */
inline Bounce mkBounce(int pin) {
  pinMode(pin, INPUT_PULLUP);
  return Bounce(pin, 30);
}

/**
 * This is the timer callback function that keeps looping until an AxeFx
 * has been connected via Midi. Once an Axe has been connected, the timer
 * will remove itself and call the initialization functions from the AxeFx.
 * For example, this will ask the AxeFx to send the current preset number,
 * preset name and effect states.
 * @todo, restart this timer if no message has been received from the AxeFx
 * for more than 5 seconds (disconnection)
 */
void timerAxeFxInitializationCheck(FCBTimer *timer) {
  if (AxeMidi.isInitialized()) {
    // The AxeMidi library has received the Firmware and model number from
    // the AxeFx, now we know whether or not we should add checksums to SysEx
    // messages and how to parse specific responses.
    // Lets ask the AxeFx to send us the current preset number, once
    // the AxeFx responds we we automatically request the PresetName and
    // effect states, see onAxeFxSysExMessage()
    AxeMidi.requestPresetNumber();

    // Since we're done, lets disable the timer.
    // That's really easy, just modify the timer struct
    // to set repetitions to 0, the FCBTimerManager will
    // clean everything up for us.
    timer->m_iNumRepeats = 0;
    return;
  }

  // The library has not been initialized yet, since the AxeFx might be
  // disconnected, keep sending the initialization SysEx message.
  AxeMidi.sendLoopbackAndVersionCheck();

  // That's all there's to it ;)
}

/**
 * ###########################################################
 * setup()
 * This function gets called once during the entire startup
 * of the device. Here we set all the pin modes and start the Midi
 * interface. This is also the place where we start our little
 * boot animation on the LCD.
 */
void setup() {
  // Light up the on-board teensy led
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  digitalWrite(PIN_ONBOARD_LED, HIGH);  // HIGH means on, LOW means off

  // Start the debugging information over serial
  Serial.begin(57600);
  Serial.println("FCBInfinity Startup");

  // Initialize MIDI, for now set midiThru off and channel to OMNI
  AxeMidi.begin(MIDI_CHANNEL_OMNI);
  AxeMidi.registerAxeSysExReceiveCallback(&onAxeFxSysExMessage);
  AxeMidi.turnThruOff();
  Serial.println("- midi setup done");

  // Turn all the leds on that are connected to the MAX chip.
  ledControl.shutdown(0, false);  // turns on display
  ledControl.setIntensity(0, 4);  // 15 = brightest
  for(int i=0; i<=7; ++i) {
    ledControl.setDigit(0, i, 8, true);
  }
  Serial.println("- all leds on done");

  // Set the pins for the Stompbox bank RGB-led to output (see fcbinfinity.h for pinout)
  // the PIN_RGBLED_x pins are PWM pins, so an AnalogWrite() will set the led
  // intensity for this led.
  pinMode(PIN_RGBLED_R, OUTPUT);
  pinMode(PIN_RGBLED_G, OUTPUT);
  pinMode(PIN_RGBLED_B, OUTPUT);
  Serial.println("- rgb leds on done");

  // Perform the bootsplash animation
  doBootSplash();
  Serial.println("- bootsplash done");

  // Turn off the on-board teensy led to indicate that
  // the setup has completed
  digitalWrite(PIN_ONBOARD_LED, LOW);
  Serial.println("- setup() done");

  // Set the bank to the initial presetbank, probably 0
  setLedDigitValue(g_iPresetBank);

  // Set the StompBoxMode to the normal mode, this just
  // sets the led to the correct color
  setStompBoxMode(STOMP_MODE_NORMAL);

  // Every 200 ms, check if an AxeFx has been connected
  lcd.setCursor(0,0);
  lcd.print(" Waiting for AxeFX! ");
  FCBTimerManager::addInterval(200, &timerAxeFxInitializationCheck);

} // setup()

/**
 * ###########################################################
 * loop()
 * This function gets called repeatedly while the device is active
 * this is where we check for button presses and midi data, this is
 * also where we update the displays, leds and send midi.
 * Never ever use delay or very slow functions in this loop because
 * it will slow down your input handling tremendously.
 */
void loop() {

  // Check all the connected inputs; buttons, expPedals, MIDI, etc. for new data or state changes
  updateIO();

  // Process all the timers in our FCBTimerManager, this checks if we
  // have registered a timeout anywhere in the project and calls the
  // callback function if the timeout has expired. This is a neat way
  // to flash leds and do some repeated actions, without all the hassle.
  static elapsedMicros timerUpdate;
  if (timerUpdate>1000) {
    // There's no need to run the timers every single loop though,
    // only call it every ms.
    timerUpdate = 0;
    FCBTimerManager::processTimers();
  }

  // Lets check if we received a MIDI message
  if (AxeMidi.hasMessage()) {
      // Yup we've got data, see AxeMidi.h and MIDI.h for more info
      // AxeMidi.getType()
      // AxeMidi.getData1()
      // AxeMidi.getData2();
      // AxeMidi.getSysExArray();

      // Since we registered the SysexCallback in the setup()
      // the AxeMidi will call that function in case there's a new sysex message
  }

  // Play around with the expression pedals a little
  // ExpPedal1 has a new value?
  // Send some debug data over the serial communications, set the value on the LED-digits and send a midi message
  if (ExpPedal1.hasChanged()) {
    // Send CC# 1 on channel 1, for debugging
    AxeMidi.sendControlChange(1, ExpPedal1.getValue());
  }

  // ExpPedal2 has a new value?
  // Just send the midi message
  if (ExpPedal2.hasChanged()) {
    // Send CC# 2 on channel 1, for debugging
    AxeMidi.sendControlChange(2, ExpPedal2.getValue());
  }

  // button 5 on the lower row toggles X/Y, it does that in any mode, so we can just
  // leave this outside of the StompBoxMode logic
  if (btnRowLower[4].btn.fallingEdge()) {
    g_bXYToggle = !g_bXYToggle;
    AxeMidi.sendToggleXY(g_bXYToggle);
    return;
  }

  // Right, since we're jumping into the stompbox-mode matter, let me quickly explain what
  // I'm trying to accomplish with this. Lets say you have a song where you're only using one preset
  // having 4 buttons to switch presets you wont even use is kind of useless, what if we could use those
  // to control effects as well. The code below allows us to do so.
  switch (g_iStompBoxMode) {

    case STOMP_MODE_NORMAL: {
      // This StompBoxMode is the normal one where the top 5 buttons toggle
      // some effects, the 1-4 bottom buttons control the preset changes and
      // the 5th button controls the X/Y switch. BankUp/Down control the bank
      // for the preset switching on the FCBInfinity

      // If the stompBox button is pressed, we want to switch to STOMP_MODE_10STOMPS
      // Just return, because all the remaining code below is useless now.
      // Later on, a long press will change us to looper mode
      if (btnStompBank.fallingEdge()) {
        setStompBoxMode(STOMP_MODE_10STOMPS);
        return;
      }

      // Button 1 to 4 on the bottom row send the ProgramChange message over midi to select new
      // presets on the AxeFx in this mode.
      // Please note that fallingEdge() detects when the button is pressed so this will react much
      // faster than some other devices, that only take action when a button is UNpressed, quite the difference.
      for(int i=0; i<4; ++i) {
        if (btnRowLower[i].btn.fallingEdge()) {
          // Send the PC message, i is the button number and add the current bank
          g_iCurrentPreset = i + 4*g_iPresetBank + 1;
          AxeMidi.sendPresetChange(g_iCurrentPreset);
          return;
        }
      }

      // If the bankUp/Down buttons are pressed update the presetBank variable.
      // Later on we might want to flash preset switching buttons or something and have a feature where you
      // cycle through banks faster by keeping the button pressed
      // For now, KISS :P
      if (btnBankUp.fallingEdge()) {
        ++g_iPresetBank;
        if (g_iPresetBank>999) g_iPresetBank = 999;
        setLedDigitValue(g_iPresetBank);
        return;
      }
      if (btnBankDown.fallingEdge()) {
        --g_iPresetBank;
        if (g_iPresetBank<0) g_iPresetBank=0;
        setLedDigitValue(g_iPresetBank);
        return;
      }

      // @TODO: implement the stompbox button logic

    } // end of STOMP_MODE_NORMAL
    break;

    case STOMP_MODE_10STOMPS: {
      // In this mode the RGB-led is colored blue and all the upper and lower buttons
      // toggle effect bypasses, the BankUp/Down now just select the next or previous
      // preset.

      // If the stompBox button is pressed, we want to switch back to STOMP_MODE_NORMAL
      // Just return, because all the remaining code below is useless now.
      // Later on, a long press will change us to looper mode
      if (btnStompBank.fallingEdge()) {
        setStompBoxMode(STOMP_MODE_NORMAL);
        return;
      }

      // In this mode the bankUp/Down buttons react a little different; instead of changing banks
      // they will just move to the next or previous preset on the AxeFx. We'll calculate the new
      // bank automatically when we receive the preset changed SysEx from the AxeFx.
      if (btnBankUp.fallingEdge()) {
        AxeMidi.sendPresetChange(g_iCurrentPreset+1);
        return;
      }
      if (btnBankDown.fallingEdge()) {
        AxeMidi.sendPresetChange(g_iCurrentPreset-1);
        return;
      }

      // @TODO: implement the stompbox button logic

    } // end of STOMP_MODE_10STOMPS
    break;

    case STOMP_MODE_LOOPER: {
      // This mode changes the top buttons into the controls for the looper in the AxeFx
      // I will implement this later...

    } // end of STOMP_MODE_LOOPER
    break;

  }


  // ##############################
  // Some additional io debugging functions below

  // Update the button states, when a button is pressed set
  // the LED-Digits to the button id, toggle the indicator led
  // that is associated with the button
  for(int i=0; i<5; ++i) {
    // Check the upper row of buttons
    if (btnRowUpper[i].btn.fallingEdge()) {
      btnRowUpper[i].ledStatus = !btnRowUpper[i].ledStatus;
      ledControl.setLed(0, btnRowUpper[i].x, btnRowUpper[i].y, btnRowUpper[i].ledStatus);
      return;
    }

    // Check the lower row of buttons
    if (btnRowLower[i].btn.fallingEdge()) {
      btnRowLower[i].ledStatus = !btnRowLower[i].ledStatus;
      ledControl.setLed(0, btnRowLower[i].x, btnRowLower[i].y, btnRowLower[i].ledStatus);
      return;
    }
  }

} // loop()
