/**
   This code converts the Palm Portable Keyboard's output into bluetooth.
   See https://github.com/pymo/ppk_bluetooth for details.

   Additional changes by David R. Van Wagner to adapt to producing Commodore 64/128 scancodes
   and serve as a custom BLE service instead as expected by a custom Commodore Emulator
   https://github.com/davervw/c-simple-emu6502-cbm
**/

#include <Arduino.h>
#include <driver/uart.h>
#include <esp_sleep.h>

#include "ble_keyboard.h"
BleKeyboard bleKeyboard;

// Uncomment to enable debug output through the arduino console at speed 115200n1;
// Comment out to disable debug output.
// #define PPK_DEBUG

// Uncomment to compile firmware for Handspring keyboard;
// Comment out to compile firmware for Palm III, V or M500 keyboard.
// #define HANDSPRING

// Uncomment to put the firmware in battery test mode. It prints
// the raw battery reading to the bluetooth host. It also disables the
// sleep function.
// #define BATTERY_TEST_MODE

// #define GERMAN_LAYOUT

#ifdef HANDSPRING
// Handspring pinout
#define VCC_PIN 4
#define GND_PIN 6
#define RX_PIN 10
#define TX_PIN 19
#define HEX_ID0 0xF9
#define HEX_ID1 0xFB
#define INVERT_TTL false
#else
// Palm III, V or m500 pinout
#define VCC_PIN 4
#define RTS_PIN 6
#define DCD_PIN 7
#define GND_PIN 10
#define RX_PIN 5
#define TX_PIN 19
#define HEX_ID0 0xFA
#define HEX_ID1 0xFD
#define INVERT_TTL true
#endif

#define BATTERY_ADC_PIN 2
#define BATTERY_LED 8
#define FUNC_LED 3

void CheckBatteryWithInterval();

void ledOff(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}
void ledOn(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
}

// wait this many milliseconds before making sure keyboard is still awake
#define KEEPALIVE_TIMEOUT 500000

// if the keyboard has been idled for 30 min, enter light sleep.
#define IDLE_TIMEOUT 1800000

// if the keyboard can not initialize after 4 seconds, reboot the board.
#define KEYBOARD_INIT_TIMEOUT 4000

// Conversion factor for micro seconds to mili seconds
#define uS_TO_mS_FACTOR 1000

short key_map[128] = {0};
short shift_key_map[128] = {0};
short fn_key_map[128] = {0};
bool numlock = false;
bool shiftlock = false;

char last_byte = 0;

// The timestamp of the last time the keyboard has communicated with the board.
unsigned long last_comm = 0;
// The timestamp of the last time there is a key been pressed.
unsigned long last_pressed = 0;

// Palm Portable Keyboard scancodes
// got PPK matrix codes by running this program in diagnostic mode, typing all keys, see README.md for diagram
//
//    __0___1___2___3___4___5___6___7___8___9__
// 00 | 1 | 2 | 3 | Z | 4 | 5 | 6 | 7 |Cmd| Q |
// 10 | W | E | R | T | Y | ` | X | A | S | D |
// 20 | F | G | H |Spc|Cap|Tab|Ctr|
// 30 |               | Fn|Alt|
// 40 |               | C | V | B | N | - | = |
// 50 | Bs|Dat| 8 | 9 | 0 |Sp2| [ | ] | \ |Pho|
// 60 | U | I | O | P | ' |Ent|ToD|   | J | K |
// 70 | L | ; | / | Up|Mem|   | M | , | . |Don|
// 80 |Del| Lt| Dn| Rt|               |LSh|RSh|

// Commodore 64 (0..63) and 128 (0..87) scancodes
// c-simple-emu6502-cbm ref: https://github.com/davervw/c-simple-emu6502-cbm/blob/unified/src/c-simple-emu6502-cbm/C128ScanCode.h
// Also see ref: https://github.com/davervw/c-keymaps
//    __0___1___2___3___4___5___6___7___8___9__
// 00 |Del|Ret|LRt|F7 |F1 |F3 |F5 |UDn| 3 | W |
// 10 | A | 4 | Z | S | E |LSh| 5 | R | D | 6 |
// 20 | C | F | T | X | 7 | Y | G | 8 | B | H |
// 30 | U | V | 9 | I | J | 0 | M | K | O | N |
// 40 | + | P | L | - | . | : | @ | , | £ | * |
// 50 | ; |Hom|RSh| = |UpA| / | 1 |LtA|Ctr| 2 |
// 60 |Spc|Cbm| Q |Sto|Hel|#8#|#5#|Tab|#2#|#4#|
// 70 |#7#|#1#|Esc|#+#|#-#|LF |Ent|#6#|#9#|#3#|
// 80 |Alt|#0#|#.#|Up |Dn |Lt |Rt |NS |

// C64 scancodes indexed by Palm Portable Keyboard scancodes above
const short c128_keymap[128] = {
/*  0: */ 56, 59, 8, 12, 11, 16, 19, 24, 61, 62,
/* 10: */ 9, 14, 17, 22, 25, DOCBM+17, 23, 10, 13, 18,
/* 20: */ 21, 26, 29, 60, CAPS, 67, 58, NOKEY, NOKEY, NOKEY,
/* 30: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 80, NOKEY, NOKEY, NOKEY, NOKEY,
/* 40: */ NOKEY, NOKEY, NOKEY, NOKEY, 20, 31, 28, 39, 43, 53,
/* 50: */ 0, 4, 27, 32, 35, RESTORE, DOSHIFT+45, DOSHIFT+50, 48, 5,
/* 60: */ 30, 33, 38, 41, DOSHIFT+24, 1, 6, NOKEY, 34, 37,
/* 70: */ 42, 50, 55, DOSHIFT+7, 3, NOKEY, 36, 47, 44, 63,
/* 80: */ 0, DOSHIFT+2, 7, 2, NOKEY, NOKEY, NOKEY, NOKEY, 15, 52,
};
//    1   2   3   4   5   6   7   8   9   0   -   =  Back     F1
// Tab q   w   e   r   t   y   u   i   o   p   [   ]    £     F3
// Cap  a   s   d   f   g   h   j   k   l   ;   '   Retrn     F5
// LShf  z   x   c   v   b   n   m   ,   .   /   RShft Up     F7
// Ctr Fn Alt Cbm {Space  Bar}Rest ` Stop{Delete}Lt Dn Rt

// when shift is pressed, these can override the normal keymap
const short shift_c128_keymap[128] = {
/*  0: */ NOKEY, NOSHIFT+46, NOKEY, NOKEY, NOKEY, NOKEY, NOSHIFT+54, 19, NOKEY, NOKEY,
/* 10: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, DOCBM+NOSHIFT+14, NOKEY, NOKEY, NOKEY, NOKEY,
/* 20: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 30: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 40: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOSHIFT+57, NOSHIFT+40,
/* 50: */ NOKEY, NOKEY, NOSHIFT+49, 27, 32, NOKEY, DOCBM+NOSHIFT+62, DOCBM+NOSHIFT+9, DOCBM+NOSHIFT+43, NOKEY,
/* 60: */ NOKEY, NOKEY, NOKEY, NOKEY, 59, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 70: */ NOKEY, NOSHIFT+45, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 80: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 15, 52,
};
//    !   @   #   $   %   ↑   &   *   (   )   ←   +   Ins     F2
// Tab Q   W   E   R   T   Y   U   I   O   P   {   }    |     F4
// Cap  A   S   D   F   G   H   J   K   L   :   "   Retrn     F6
// LShf  Z   X   C   V   B   N   M   <   >   ?   RShft Up     F8
// Ctr Fn Alt Cbm {Space  Bar}Rest ~ Stop{Delete}Lt Dn Rt

// when fn is pressed
const short fn_c128_keymap[128] = {
/*  0: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 70, NOKEY, NOKEY,
/* 10: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 20: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 30: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 40: */ NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
/* 50: */ 51, 64, 65, 78, 73, NOKEY, NOKEY, NOKEY, NOKEY, 75,
/* 60: */ 69, 66, 77, 74, NOKEY, 76, DISPLAY4080, NOKEY, 71, 68,
/* 70: */ 79, 76, 76, 83, 87, NOKEY, 81, 81, 44, 72,
/* 80: */ 51, 85, 84, 86, NOKEY, NOKEY, NOKEY, NOKEY, 15, 52,
};
//                            7   8   9   +       =  Home     Help
//                             4   5   6   -                  LineFeed
//                              1   2   3   En      Enter     40/80Display
// LShf                          0   0   .   En  RShft Up     NoScroll
//                                   Esc {Home } Lt Dn Rt

// numeric keypad mask for toggling definitions between normal and keypad(fn)
const bool numlock_c128_keymap[128] = {
/*  0: */ 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
/* 10: */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 20: */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 30: */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 40: */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 50: */ 0, 0, 1, 1, 1, 0, 0, 0, 0, 0,
/* 60: */ 1, 1, 1, 1, 0, 1, 0, 0, 1, 1,
/* 70: */ 1, 1, 1, 1, 0, 0, 1, 1, 1, 0,
/* 80: */ 0, 1, 1, 1, 0, 0, 0, 0, 0, 0,
};

// PPK CBM Caps handled by Commodore 128 ROM
//    1   2   3   4   5   6   7   8   9   0   -   =  Back     F1
// Tab Q   W   E   R   T   Y   U   I   O   P   [   ]    £     F3
// Cap  A   S   D   F   G   H   J   K   L   ;   '   Retrn     F5
// LShf  Z   X   C   V   B   N   M   ,   .   /   RShft Up     F7
// Ctr Fn Alt Cbm {Space  Bar}Rest ` Stop{Delete}Lt Dn Rt

void config_keymap() {
  if (sizeof(c128_keymap) != sizeof(key_map))
    return;
  memcpy(key_map, c128_keymap, sizeof(key_map));
}

void config_shift_keymap() {
  if (sizeof(shift_c128_keymap) != sizeof(shift_key_map))
    return;
  memcpy(shift_key_map, shift_c128_keymap, sizeof(shift_key_map));
}

void config_fnkeymap() {
  if (sizeof(fn_c128_keymap) != sizeof(fn_key_map))
    return;
  memcpy(fn_key_map, fn_c128_keymap, sizeof(fn_key_map));
}

void boot_keyboard() {
#ifdef PPK_DEBUG
  Serial.println("beginning keyboard boot sequence");
#endif

#ifdef HANDSPRING
  pinMode(RX_PIN, INPUT_PULLUP);
#else
  pinMode(DCD_PIN, INPUT);
  pinMode(RTS_PIN, INPUT);
  pinMode(RX_PIN, INPUT_PULLDOWN);
#endif

  pinMode(VCC_PIN, OUTPUT);
  digitalWrite(VCC_PIN, LOW);
  pinMode(GND_PIN, OUTPUT);
  digitalWrite(GND_PIN, LOW);
  delay(100);
  digitalWrite(VCC_PIN, HIGH);

  int start_time = millis();
#ifndef HANDSPRING
  while (digitalRead(DCD_PIN) != HIGH) {
    if (millis() - start_time > KEYBOARD_INIT_TIMEOUT) {
#ifdef PPK_DEBUG
      Serial.print(
          "Keyboard not pulling up DCD, bad keyboard? Rebooting...");
#endif
      ESP.restart();
    }
    delay(1);
  };

#ifdef PPK_DEBUG
  Serial.println("DCD_PIN response done");
#endif

  if (digitalRead(RTS_PIN) == LOW) {
    delay(10);
    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, HIGH);
  } else {
    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, HIGH);
    digitalWrite(RTS_PIN, LOW);
    delay(10);
    digitalWrite(RTS_PIN, HIGH);
  }
#endif  // #ifndef HANDSPRING

#ifdef PPK_DEBUG
  Serial.print("waiting for keyboard serial ID... ");
#endif

  // Read the Hardware Serial1 until we get the expected hex ID. Due to the
  // timing of plugging in the adapter, there could be garbage chars before the
  // ID, the following code will ignore them.
  int byte1, byte2 = 0;
  while (true) {
    delay(10);
    if (Serial1.available()) {
      byte1 = byte2;
      byte2 = Serial1.read();
#ifdef PPK_DEBUG
      Serial.print(byte2);
      Serial.print(" ");
#endif
    }
    if ((byte1 == HEX_ID0) && (byte2 == HEX_ID1)) break;
    if (millis() - start_time > KEYBOARD_INIT_TIMEOUT) {
#ifdef PPK_DEBUG
      Serial.print(
          "No keyboard ID received, something is wrong, rebooting the board.");
#endif
      ESP.restart();
    }
  }

#ifdef PPK_DEBUG
  Serial.println(" done");
#endif
}

void setup() {
  setCpuFrequencyMhz(80);
#ifdef PPK_DEBUG
  Serial.begin(115200);
#endif
  ledOn(BATTERY_LED);
  ledOn(FUNC_LED);
  CheckBatteryWithInterval();
  // The adapter may have just been plugged in and is not fully contacting
  // with the keyboard's golden fingers yet. Wait for 0.5s before doing anything.
  delay(500);

  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN, /*invert=*/INVERT_TTL);

  config_keymap();
  config_shift_keymap();
  config_fnkeymap();
  boot_keyboard();
  ledOff(BATTERY_LED);
  bleKeyboard.begin();
#ifdef PPK_DEBUG
  Serial.println("setup completed");
#endif
}

bool fn_key_down = false;
bool lshift_key_down = false;
bool rshift_key_down = false;

void ReportKeyUpDownEvent(short key_code, bool key_up)
{
  if (key_up)
  {
    bleKeyboard.release(key_code);
  }
  else
  {
    bleKeyboard.press(key_code);
  }
}

void ResetKeyUpDownEvent() 
{ 
  bleKeyboard.releaseAll();
  if (shiftlock)
    bleKeyboard.press(15);
}

// convenience masks
#define UPDOWN_MASK 0b10000000
#define MAP_MASK 0b01111111

void HandleKeyEvent(uint8_t key_byte) {
  bool key_up = ((key_byte & UPDOWN_MASK) != 0);
  short masked_key_byte = key_byte & MAP_MASK;
  short key_code = NOKEY;

  if (fn_key_down && (!numlock || !numlock_c128_keymap[masked_key_byte])) {
    if (fn_key_map[masked_key_byte] != NOKEY)
        key_code = fn_key_map[masked_key_byte];
    // otherwise eat the key as if didn't happen
  }
  else if ((shiftlock || lshift_key_down || rshift_key_down) && shift_key_map[masked_key_byte] != NOKEY)
    key_code = shift_key_map[masked_key_byte];
  else if (numlock && !fn_key_down && numlock_c128_keymap[masked_key_byte])
    key_code = fn_key_map[masked_key_byte];
  else
    key_code = key_map[masked_key_byte];

  // keyboard duplicates the final key-up byte
  if (key_byte == last_byte) {
    ResetKeyUpDownEvent();
  } else {
    if (key_code != NOKEY) {
      if (fn_key_down && !key_up
          && (lshift_key_down && masked_key_byte == 89
            ||rshift_key_down && masked_key_byte == 88)
         ) // avoids interference with any other FN+SHIFT mappings if require both
      {
        shiftlock = !shiftlock;
        ReportKeyUpDownEvent(15, !shiftlock);
      }

      ReportKeyUpDownEvent(key_code, key_up);

      if ((masked_key_byte) == 88)
        lshift_key_down = !key_up;
      else if ((masked_key_byte) == 89)
        rshift_key_down = !key_up;
    } else {
      // special case the Fn key
      if ((masked_key_byte) == 34)
        fn_key_down = !key_up;
      if (fn_key_down && masked_key_byte == 49 && !key_up)
        numlock = !numlock;
    }
  }

  // Handles the Volume and Brightness keys in the end, they are using consumer
  // report.
  // if (fn_key_down) {
  //   MediaKeyReport media_key_report = {0, 0};
  //   if (masked_key_byte == 0b01010001)
  //     memcpy(media_key_report, KEY_MEDIA_BRIGHTNESS_DOWN,
  //            sizeof(media_key_report));  // left arrow
  //   else if (masked_key_byte == 0b01010010)
  //     memcpy(media_key_report, KEY_MEDIA_VOLUME_DOWN,
  //            sizeof(media_key_report));  // down arrow
  //   else if (masked_key_byte == 0b01010011)
  //     memcpy(media_key_report, KEY_MEDIA_BRIGHTNESS_UP,
  //            sizeof(media_key_report));  // right arrow
  //   else if (masked_key_byte == 0b01001001)
  //     memcpy(media_key_report, KEY_MEDIA_VOLUME_UP,
  //            sizeof(media_key_report));  // up arrow
  //   else if (masked_key_byte == 0b00001000)
  //     memcpy(media_key_report, KEY_MEDIA_WWW_HOME,
  //            sizeof(media_key_report));  // Fn+CMD=iOS Home button
  //   if (media_key_report[0] != 0 || media_key_report[1] != 0) {
  //     // if (key_up)
  //     //   bleKeyboard.release(media_key_report);
  //     // else
  //     //   bleKeyboard.press(media_key_report);
  //   }
  // }
  last_byte = key_byte;
#ifdef PPK_DEBUG
  Serial.printf("%s %u\n", key_up ? "released" : "pressed",
                (unsigned int)masked_key_byte);
#endif
  last_comm = millis();
  last_pressed = millis();
}

/*  Battery function
    The current consumption of the entire board measured from the battery
   connector:
   79mA 160MHz normal operation
   73.6mA 80MHz normal operation
   15.6mA Bluetooth off
   1.78mA Light sleep
   19.6uA power switch off
*/
#define BAT_CHECK_INTERVAL 60000  // 60 seconds
unsigned long last_battery_check_time = 0;

typedef enum LedBlinkPattern {
  LED_NO_BLINK,
  LED_BLINK_ONCE,
  LED_BLINK_TWICE,
  LED_BLINK_THREE_TIMES,
  LED_BLINK_QUICK,
  LED_ALWAYS_ON
} LedBlinkPattern;

// The ADC of ESP32-C3 is not linear to the voltage... so we don't bother
// converting the raw value to volts. We just measure the raw value across the
// entire run time, then we can calculate the raw reading at 0%, 5%, ..., 100%
// battery life. It is a rough estimation anyway.
//
// If you are using a different ESP32 board, you may need to change this table.
// This lookup table must have 21 entries. Each entry must be different,
// otherwise we will have divide-by-zero error below.
int bat_percent_lookup[] = {2000, 2340, 2390, 2420, 2450, 2475, 2490,
                            2505, 2520, 2530, 2550, 2570, 2600, 2645,
                            2675, 2705, 2740, 2770, 2805, 2845, 2905};
LedBlinkPattern battery_led_pattern = LED_NO_BLINK;

void CheckBatteryWithInterval() {
  int current_time = millis();
  if ((last_battery_check_time != 0) &&
      (current_time - last_battery_check_time < BAT_CHECK_INTERVAL))
    return;

  int raw_bat_data = analogRead(BATTERY_ADC_PIN);
  int battery_level = -1;  // percentage of the battery
  int n;
  for (n = 0; n <= 20; n++) {
    if (raw_bat_data < bat_percent_lookup[n]) {
      if (n == 0) {
        battery_level = 0;
        break;
      } else {
        // raw_bat_data falls between bat_percent_lookup[n-1] and
        // bat_percent_lookup[n]. Do interpolation.
        battery_level = 5 * (n - 1) +
                        5 * (raw_bat_data - bat_percent_lookup[n - 1]) /
                            (bat_percent_lookup[n] - bat_percent_lookup[n - 1]);
        break;
      }
    }
  }
  if (n > 20) {
    battery_level = 100;
  }
#ifdef PPK_DEBUG
  Serial.printf("Battery percentage %d, raw_data = %d\n", battery_level,
                raw_bat_data);
#endif
  //bleKeyboard.setBatteryLevel(battery_level);
  if (battery_level >= 20)
    battery_led_pattern = LED_NO_BLINK;
  else if (battery_level >= 15 && battery_level < 20)
    battery_led_pattern = LED_BLINK_ONCE;
  else if (battery_level >= 10 && battery_level < 15)
    battery_led_pattern = LED_BLINK_TWICE;
  else if (battery_level < 10)
    battery_led_pattern = LED_BLINK_THREE_TIMES;
  last_battery_check_time = current_time;
}

LedBlinkPattern func_led_pattern = LED_ALWAYS_ON;
unsigned long current_time = 0;

void CheckConnectionStatus() {
   if (bleKeyboard.isConnected())
     func_led_pattern = LED_BLINK_ONCE;
   else
    func_led_pattern = LED_BLINK_QUICK;
}

void HandleFuncLedBlink(uint8_t pin) {
  current_time = millis();

  switch (func_led_pattern) {
    case LED_NO_BLINK:
      ledOff(pin);
      break;
    case LED_ALWAYS_ON:
      ledOn(pin);
      break;
    case LED_BLINK_QUICK:
      if (current_time % 140 < 50)
        ledOn(pin);
      else
        ledOff(pin);
      break;
    case LED_BLINK_ONCE:
      if (current_time % 5000 < 50)
        ledOn(pin);
      else
        ledOff(pin);
      break;
    default:
      break;
  }
}

#define BLINK_CYCLE 3000  // 3 seconds
#define BLINK_LENGTH 50   // milliseconds
unsigned long last_cycle_start_timestamp = 0;

void HandleBatteryLedBlink(uint8_t pin) {
  current_time = millis();
  //  | On | Off | On | Off | On | Off |
  if (battery_led_pattern >= LED_BLINK_ONCE &&
      (current_time - last_cycle_start_timestamp) >
          BLINK_CYCLE) {  // Overdue for the next cycle, turn on the LED
    ledOn(pin);
    last_cycle_start_timestamp = current_time;
  } else if ((current_time - last_cycle_start_timestamp) > BLINK_LENGTH &&
             (current_time - last_cycle_start_timestamp) <= BLINK_LENGTH * 4) {
    ledOff(pin);
  } else if (battery_led_pattern >= LED_BLINK_TWICE &&
             (current_time - last_cycle_start_timestamp) > BLINK_LENGTH * 4 &&
             (current_time - last_cycle_start_timestamp) <= BLINK_LENGTH * 5) {
    ledOn(pin);
  } else if ((current_time - last_cycle_start_timestamp) > BLINK_LENGTH * 5 &&
             (current_time - last_cycle_start_timestamp) <= BLINK_LENGTH * 8) {
    ledOff(pin);
  } else if (battery_led_pattern >= LED_BLINK_THREE_TIMES &&
             (current_time - last_cycle_start_timestamp) > BLINK_LENGTH * 8 &&
             (current_time - last_cycle_start_timestamp) <= BLINK_LENGTH * 9) {
    ledOn(pin);
  } else if ((current_time - last_cycle_start_timestamp) > BLINK_LENGTH * 9) {
    ledOff(pin);
  }
}

esp_sleep_wakeup_cause_t SleepAndWaitForWakeUp() {
#ifdef PPK_DEBUG
  Serial.println("Entering light sleep.");
  /* To make sure the complete line is printed before entering sleep mode,
     need to wait until UART TX FIFO is empty:
  */
  uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
  ledOff(BATTERY_LED);
  ledOff(FUNC_LED);
  // Holds the state of VCC, GND and RTS line so the PPK is still alive.
  // Otherwise we won't be able to wake up by key press.
  gpio_hold_en((gpio_num_t)VCC_PIN);
  gpio_hold_en((gpio_num_t)GND_PIN);
#ifndef HANDSPRING
  gpio_hold_en((gpio_num_t)RTS_PIN);
#endif
  gpio_hold_en((gpio_num_t)BATTERY_LED);
  gpio_wakeup_enable((gpio_num_t)RX_PIN,
                     INVERT_TTL ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  // We need to wake up periodically to keep the PPK alive.
  // Otherwise the PPK goes to sleep and we won't be able to wake up by key
  // press.
  esp_sleep_enable_timer_wakeup(KEEPALIVE_TIMEOUT * uS_TO_mS_FACTOR);
  // Sleep starts, execution will continue once RX line is active, or the timer
  // is up.
  esp_light_sleep_start();
  gpio_hold_dis((gpio_num_t)VCC_PIN);
  gpio_hold_dis((gpio_num_t)GND_PIN);
#ifndef HANDSPRING
  gpio_hold_dis((gpio_num_t)RTS_PIN);
#endif
  gpio_hold_dis((gpio_num_t)BATTERY_LED);
  return esp_sleep_get_wakeup_cause();
}

void loop() {
  if (Serial1.available()) {
    for (int i = Serial1.available(); i > 0; i--) {
      HandleKeyEvent(Serial1.read());
    }
  }
  // Sleep the board if the keyboard has been idling for a long time
  bool keyboard_rebooted_in_this_cycle;
  keyboard_rebooted_in_this_cycle = false;

#ifndef BATTERY_TEST_MODE
  if ((millis() - last_comm) > IDLE_TIMEOUT) {
    esp_sleep_wakeup_cause_t wakeup_reason = SleepAndWaitForWakeUp();
    switch (wakeup_reason) {
      case ESP_SLEEP_WAKEUP_TIMER:
#ifdef PPK_DEBUG
        Serial.println(
            "Wakeup caused by timer, toogle the keyboard and sleep again.");
#endif
        boot_keyboard();
        keyboard_rebooted_in_this_cycle = true;
        // We don't update last_comm here, so in the next loop, the board will
        // enter sleep again.
        break;
      case ESP_SLEEP_WAKEUP_GPIO:
#ifdef PPK_DEBUG
        Serial.println("Wakeup caused by key press, don't go to sleep again.");
#endif
        boot_keyboard();
        last_comm = millis();
        keyboard_rebooted_in_this_cycle = true;
        break;
      default:
#ifdef PPK_DEBUG
        Serial.printf(
            "It should not happen. Wakeup was not caused by timer nor "
            "keypress: %d\n",
            wakeup_reason);
#endif
        break;
    }
  }
#endif

  // reboot if no recent keypress, otherwise keyboard falls asleep
  if (!keyboard_rebooted_in_this_cycle &&
      (millis() - last_pressed) > KEEPALIVE_TIMEOUT) {
#ifdef PPK_DEBUG
    Serial.println("rebooting keyboard for timeout");
#endif
    last_pressed = millis();
    boot_keyboard();
  }
  CheckBatteryWithInterval();
  HandleBatteryLedBlink(BATTERY_LED);
  CheckConnectionStatus();
  HandleFuncLedBlink(FUNC_LED);
  // Delay a bit after a loop to save power. We don't need too frequent key
  // detection. The hardware reference of PPK says "Filters Key Bounce: 15 - 25
  // milliseconds".
  delay(25);
}
