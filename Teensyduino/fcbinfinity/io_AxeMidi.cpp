#include <Wprogram.h>
#include "io_AxeMidi.h"
#include "fcbinfinity.h"

#include "io_MIDI.h"

#include "utils_FCBEffectManager.h"

/* Main instance the class comes pre-instantiated, just like the Midi class does. */
AxeMidi_Class AxeMidi;

/**
 * Constructor
 */
AxeMidi_Class::AxeMidi_Class() {
  m_iAxeModel = 3;
  m_bFirmwareVersionReceived = false;
  m_bTunerOn = false;
  m_fpRawSysExCallback = NULL;
  m_fpAxeFxSysExCallback = NULL;
  m_fpAxeFxConnectedCallback = NULL;
  m_fpAxeFxDisconnectedCallback = NULL;
  m_iAxeChannel = 1;
}

// Initialize the static constant that holds the note names
const char *AxeMidi_Class::notes[] = {
  "A ", "A#", "B ", "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#"
};


/**
 * Updates the library and checks for new midi messages, call this once
 * every loop, or only after all the modules have had a chance to process
 * the midi message or you might lose messages.
 */
elapsedMillis l_eTimeSinceLastAxeFxSysEx;
elapsedMillis l_eTimeSinceLastTunerMessage;
boolean AxeMidi_Class::handleMidi() {

  // This function gets called every arduino loop. We want to call the disconnection callback
  // after the 3 seconds of silence on the AxeFx. This only works if the AxeFx actually sends something
  // periodically so it expects TapTempo realtime messages to be sent.
  if (m_bFirmwareVersionReceived && l_eTimeSinceLastAxeFxSysEx>3000) {
    m_bFirmwareVersionReceived = false;
    if (m_fpAxeFxDisconnectedCallback != NULL)
      (m_fpAxeFxDisconnectedCallback)();
  }

  // Check if the tuner is still on.
  if (m_bTunerOn && l_eTimeSinceLastTunerMessage>500) {
    m_bTunerOn = false;
  }

  // Look for new messages and return if there are no new messages.
  if (!MIDINEW.read(MIDI_CHANNEL_OMNI)) {
    m_bHasMessage = false;
    return false;
  }

  // We've got a message!
  m_bHasMessage = true;

  // Lets see if the message is a sysex and call the appropriate callbacks
  if (MIDINEW.getType() == SystemExclusive) {
    // Get the byte array SysEx message and the length of the message
    byte * sysex = (byte *)MIDINEW.getSysExArray();
    int length = MIDINEW.getData1();

    if (sysex[5] == SYSEX_LOOBACK_CHECK_DATA) {
      // Oof, midi thru is enabled on the AxeFx, send a message to the user that they
      // need to disable it.
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("#WARNING# Disable   Midi Thru on AxeFx");
      delay(5000);
      lcd.clear();
      lcd.setCursor(0,0);
    }

    // In case the length > 4 and the AXE_MANUFACTURER bytes match, this
    // sysex is coming from the AxeFx unit. Lets do some basic handling and
    // if there are any callbacks registered, call them now
    if (length>4 &&
        sysex[1] == AXE_MANUFACTURER_B1 &&
        sysex[2] == AXE_MANUFACTURER_B2 &&
        sysex[3] == AXE_MANUFACTURER_B3) {

      // Reset the AxeFx disconnection timer
      l_eTimeSinceLastAxeFxSysEx = 0;

      // If we havn't already received the AxeFx model, lets
      // update and call the initialization callback
      if (!m_bFirmwareVersionReceived) {
        m_bFirmwareVersionReceived=true;
        m_iAxeModel = sysex[4];
        if (m_fpAxeFxConnectedCallback != NULL)
          (m_fpAxeFxConnectedCallback)();
      }

      // If tuner message, set tuner to on and reset the timer
      if (sysex[5] == SYSEX_AXEFX_REALTIME_TUNER) {
        m_bTunerOn = true;
        l_eTimeSinceLastTunerMessage = 0;
      }

      if (sysex[5] == SYSEX_AXEFX_LOOPER_STATUS) {
        FCBLooperEffect.updateStatus(sysex[6]);
      }

      // If set parameter response
      if (sysex[5] == SYSEX_AXEFX_SET_PARAMETER) {
        int effectID = 0, paramID = 0, value = 0, msg = 0;
        if (m_iAxeModel>=3) {
          // AxeFx2
          effectID = sysex[6] + (128 * sysex[7]);
          paramID = sysex[8] + (128 * sysex[9]);
          value = sysex[10] + (128 * sysex[11]) + (16384 * sysex[12]);
          msg = 13;
        } else {
          // Older models
          effectID = sysex[6] + (16 * sysex[7]); // extract block num, param num, and param value
          paramID = sysex[8] + (16 * sysex[9]);
          value = sysex[10] + (16 * sysex[11]);
          msg = 12;
        }
        Serial.print("Param Value: effect");
        Serial.print(effectID);
        Serial.print(", paramID: ");
        Serial.print(paramID);
        Serial.print(", value: ");
        Serial.print(value);
        Serial.print(", bytes: ");
        for(int b=msg;b<length-2;b++) {
          if (sysex[b]==0) break;
          Serial.print(sysex[b], BYTE);
        }
        Serial.println(".");
      }

      // Lets see if we have a valid callback function for AxeFx sysex messages
      if (m_fpAxeFxSysExCallback != NULL)
        (m_fpAxeFxSysExCallback)(sysex, length);
    }
    else {
      // Not an axefx sysex, call the rawsysex callback
      if (m_fpRawSysExCallback != NULL)
        (m_fpRawSysExCallback)(sysex, length);
    }

    // Some debugging code to just dump the sysex data on the serial line.
    Serial.print("Sysex ");
    bytesHexDump(sysex, length);
    Serial.println(" ");

    return true;
  }

  // If it's not a SysEx, output some useful info
  Serial.print("Type: ");
  Serial.print(MIDINEW.getType());
  Serial.print(", data1: ");
  Serial.print(MIDINEW.getData1());
  Serial.print(", data2: ");
  Serial.print(MIDINEW.getData2());
  Serial.print(", Channel: ");
  Serial.print(MIDINEW.getChannel());
  Serial.println(", MIDI OK!");

  return true;
}

/**
 * This returns whether or not we have received a midi message.
 */
boolean AxeMidi_Class::hasMessage() {
  return m_bHasMessage;
}

/**
 * This function tells the AxeFx to switch to a new preset
 * @TODO calculate the AxeFx bank and send the correct CC#0 value
 * if iAxeFxPresetNumber > 127
 */
void AxeMidi_Class::sendPresetChange(int iAxeFxPresetNumber) {
  // CC 0: 0 sets AxeFx bank to A, it might be a solution for the
  // problem I had when sending PC#2 and the AxeFx jumping to #384 (Bypass)
  // I'm not quite sure we're required to send this every PC, but we'll test
  // that later.
  // Thanks to Slickstring/Reincaster for the hint! :P

  // After some testing it seemed that AxeFx > IO > Midi > Mapping Mode was set to
  // Custom, setting back to None fixed the above problem.

  // It seems the Axe wont do subsequent preset changes
  // unless we send it some other midi message. Lets just keep this
  // bank mode switcher code in place for now

  iAxeFxPresetNumber--;
  sendControlChange(0, iAxeFxPresetNumber / 128);

  // Send the PC message
  sendProgramChange(iAxeFxPresetNumber);
}

/**
 * Tells the AxeFx to switch to either the X or Y mode for
 * all it's effects.
 * See the link below for more info about the CC numbers
 * http://wiki.fractalaudio.com/axefx2/index.php?title=MIDI_CCs_list
 */
void AxeMidi_Class::sendToggleXY(boolean bYModeOn) {
  // CC 100 to 119 control all the x/y for all the effects, just toggle them all.
  // If bYModeOn is true, send 127, otherwise send 0
  //for (int cc=100; cc<=119; ++cc)
  sendControlChange(AXEFX_DEFAULTCC_Amp_1_XY, bYModeOn?127:0);
  //requestBypassStates();
}

/**
 * Sends a CC command to the AxeFx
 * See the link below for more info about the CC numbers
 * http://wiki.fractalaudio.com/axefx2/index.php?title=MIDI_CCs_list
 */
void AxeMidi_Class::sendControlChange(int cc, int value) {
  //MIDI.sendControlChange(0, 0, m_iAxeChannel);
  MIDINEW.sendControlChange(cc, value, m_iAxeChannel);
}

/**
 * Sends a PC command to the AxeFx
 */
void AxeMidi_Class::sendProgramChange(int pc) {
  MIDINEW.sendProgramChange(pc, m_iAxeChannel);
}

/**
 * Overrides the sendSysEx in MIDI.cpp.
 * Just send a sysex message via MIDI, but track the checksum in the sysexChecksum variable
 * if m_iAxeModel>=3
 */
void AxeMidi_Class::sendSysEx(byte length, byte * sysexData) {
  if (!m_bFirmwareVersionReceived &&
      sysexData[4] != SYSEX_AXEFX_FIRMWARE_VERSION &&
      sysexData[4] != SYSEX_LOOBACK_CHECK_DATA) {
    // If we don't have the firmware version yet, please request it from the
    // axefx, but dont send it if this is the actual firmware request
    sendLoopbackAndVersionCheck();
  }

  // More info on checksumming see:
  // http://wiki.fractalaudio.com/axefx2/index.php?title=MIDI_SysEx
  if (m_iAxeModel>=3) {
    byte sum = 0xF0;
    for (int i=0; i<length-1; ++i)
      sum ^= sysexData[i];
    sysexData[length-1] = (sum & 0x7F);
    //Serial.print("Sending checksummed sysex: ");
    //bytesHexDump(sysexData, length);
    //Serial.println();
  } else {
    length--;
    //Serial.print("Sending unchecksummed sysex: ");
    //bytesHexDump(sysexData, length);
    //Serial.println();
  }

  MIDINEW.sendSysEx(length, sysexData);
}

/**
 * This just sends a bogus message to test if the AxeFx is echoing our messages back
 * if so, the user should disable thru...
 */
void AxeMidi_Class::sendLoopbackAndVersionCheck() {
  // Send the bogus loopback check data
  byte msgBogusLoopbackData[] = {
    SYSEX_LOOBACK_CHECK_DATA,
    SYSEX_LOOBACK_CHECK_DATA,
    SYSEX_LOOBACK_CHECK_DATA,
    SYSEX_LOOBACK_CHECK_DATA,
    SYSEX_LOOBACK_CHECK_DATA,
    SYSEX_EMPTY_BYTE
  };
  sendSysEx(6, msgBogusLoopbackData);

  // Send the firmware version data
  static const byte msgRequestFirmwareVersion[] = {
    AXE_MANUFACTURER,
    1,
    SYSEX_AXEFX_FIRMWARE_VERSION,
    0,
    0,
    SYSEX_EMPTY_BYTE
  };
  sendSysEx(8, (byte*)msgRequestFirmwareVersion);

}

/**
 * Tell the AxeFx to send the PresetName over Midi
 */
void AxeMidi_Class::requestPresetName() {
  static const byte msgRequestPresetName[] = {
    AXE_MANUFACTURER,
    m_iAxeModel,
    SYSEX_AXEFX_PRESET_NAME,
    SYSEX_EMPTY_BYTE
  };
  sendSysEx(6, (byte*)msgRequestPresetName);
}

/**
 * Tell the AxeFx to send the PresetName over Midi
 * @TODO This is just a test, no clue if it actually works.
 * but we really want to know the current presetnumber on startup
 * so lets just try.
 */
void AxeMidi_Class::requestPresetNumber() {
  static const byte msgRequestPresetNumber[] = {
    AXE_MANUFACTURER,
    m_iAxeModel,
    SYSEX_AXEFX_PRESET_CHANGE,
    SYSEX_EMPTY_BYTE
  };
  sendSysEx(6, (byte*)msgRequestPresetNumber);
}

/**
 * Tell the AxeFx to send the bypass states for the current preset's effects
 * http://forum.fractalaudio.com/other-midi-controllers/39161-using-sysex-recall-present-effect-bypass-status-info-available.html
 */
void AxeMidi_Class::requestBypassStates() {
  static const byte msgRequestBypassStates[] = {
    AXE_MANUFACTURER,
    m_iAxeModel,
    SYSEX_AXEFX_GET_PRESET_EFFECT_BLOCKS_AND_CC_AND_BYPASS_STATE,
    SYSEX_EMPTY_BYTE
  };
  sendSysEx(6, (byte*)msgRequestBypassStates);
}

/**
 * Tell the AxeFx to start sending us looper updates
 */
void AxeMidi_Class::requestLooperUpdates(bool enable) {
  static byte msgRequestLooperUpdates[] = {
    AXE_MANUFACTURER,
    m_iAxeModel,
    SYSEX_AXEFX_LOOPER_STATUS,
    0,
    SYSEX_EMPTY_BYTE
  };
  msgRequestLooperUpdates[5] = (enable)?1:0;
  sendSysEx(7, (byte*)msgRequestLooperUpdates);

  //F0 00 01 74 03 23 01 24 F7
}
void AxeMidi_Class::requestLooperUpdates() {
  requestLooperUpdates(true);
}

/**
 * Ask the Axe To Send us some information about a effect's parameters
 * http://wiki.fractalaudio.com/index.php?title=Axe-Fx_SysEx_Documentation#MIDI_GET_PATCH
 */
void AxeMidi_Class::requestEffectParameter(int effectID, int paramID, int value=0, int query=0) {
  static byte msgRequestEffectParameter[] = {
    AXE_MANUFACTURER,
    m_iAxeModel,
    SYSEX_AXEFX_SET_PARAMETER,
    0,0,
    0,0,
    0,0,0, // Query
    SYSEX_EMPTY_BYTE,
    SYSEX_EMPTY_BYTE // Extra byte for AxeFx2 message
  };

  if (m_iAxeModel>=3) {
    // AxeFx2
    msgRequestEffectParameter[5] = effectID & 0x7F;
    msgRequestEffectParameter[6] = (effectID >> 7) & 0x7F;
    msgRequestEffectParameter[7] = paramID & 0x7F;
    msgRequestEffectParameter[8] = (paramID >> 7) & 0x7F;
    msgRequestEffectParameter[12] = query;
    sendSysEx(14, (byte*)msgRequestEffectParameter);
  } else {
    // Older models
    msgRequestEffectParameter[5] = effectID & 0xF;
    msgRequestEffectParameter[6] = (effectID >> 4) & 0xF;
    msgRequestEffectParameter[7] = paramID & 0xF;
    msgRequestEffectParameter[8] = (paramID >> 4) & 0xF;
    msgRequestEffectParameter[11] = query;
    sendSysEx(13, (byte*)msgRequestEffectParameter);
  }
}

/**
 * Tell the AxeFx to start the tuner and send us the realtime
 * midi messages
 *
 * @TODO implement this, for now just start the tuner manually on the AxeFx
 */
void AxeMidi_Class::startTuner() {
}

/**
 * The AxeFX-II requires us to send checksummed SysEx messages over midi and
 * it will also send checksummed messages. This boolean controls whether or not
 * to send the extra byte checksum and allows older AxeFX models to also use this
 * library. If you want to use this library with the AxeFX-II you should set this
 * variable to true after the initialization of the library.
 */
int AxeMidi_Class::getModel() {
  return m_iAxeModel;
}

/**
 * Registering of the callback functions
 */
void AxeMidi_Class::registerAxeSysExReceiveCallback( void (*func)(byte*,int) ) {
  m_fpAxeFxSysExCallback = func;
}
void AxeMidi_Class::registerRawSysExReceiveCallback( void (*func)(byte*,int) ) {
  m_fpRawSysExCallback = func;
}
void AxeMidi_Class::registerAxeFxConnectedCallback( void (*func)() ) {
  m_fpAxeFxConnectedCallback = func;
}
void AxeMidi_Class::registerAxeFxDisconnectedCallback( void (*func)() ) {
  m_fpAxeFxDisconnectedCallback = func;
}