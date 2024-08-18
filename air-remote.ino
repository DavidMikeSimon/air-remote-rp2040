#include <Wire.h>
#include <CircularBuffer.h>

#include "usb.h"

// Some keys that aren't defined in hid.h
#define HID_USAGE_CONSUMER_MENU_ESCAPE 0x46
#define HID_USAGE_CONSUMER_CHANNEL 0x86
#define HID_USAGE_CONSUMER_MEDIA_SELECT_HOME 0x9A
#define HID_USAGE_CONSUMER_VOLUME_DOWN 0xEA
#define HID_USAGE_CONSUMER_VOLUME_UP 0xE9

enum
{
  RID_KEYBOARD = 1,
  RID_MOUSE,
  RID_CONSUMER_CONTROL,
};

uint8_t const desc_hid_report[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
  TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(RID_MOUSE) ),
  TUD_HID_REPORT_DESC_CONSUMER( HID_REPORT_ID(RID_CONSUMER_CONTROL) ),
};

Adafruit_USBD_HID usb_hid;

bool passthru = true;

// Disable passthru while the TV input menu is presumably open
unsigned long inputs_menu_start = 0;

// Disable passthru right after we open the TV home menu
unsigned long home_menu_start = 0;

struct InputEvent {
  uint8_t kind;
  uint8_t data;
};

CircularBuffer<InputEvent, 16> input_events;

bool isPassthru() {
  if (inputs_menu_start != 0) {
    if (millis() - inputs_menu_start < 4000) {
      return false;
    } else {
      inputs_menu_start = 0;
    }
  }

  if (home_menu_start != 0) {
    if (millis() - home_menu_start < 5000) {
      return false;
    } else {
      home_menu_start = 0;
    }
  }

  return passthru && usb_hid.ready();
}

void setup() {
  pinMode(PIN_LED, OUTPUT);

  Serial.begin(115200);

  Wire.begin(0x05);
  Wire.onReceive(dataReceived);
	Wire.onRequest(dataRequested);

  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();
}

void dataReceived(int numBytes) {
	while(Wire.available()) {
    uint8_t byte = Wire.read();

    if (byte == 'p') {
      // Lowercase p: passthru off
      passthru = false;
    } else if (byte == 'P') {
      // Uppercase P: passthru on
      passthru = true;
    }
	}
}

void dataRequested() {
  if (!input_events.isEmpty()) {
    InputEvent event = input_events.pop();
    Wire.write(event.kind);
    Wire.write(event.data);
  } else {
    Wire.write(0);
    Wire.write(0);
  }
}

uint8_t fifo_buf[64];

void loop() {
  digitalWrite(PIN_LED, isPassthru() ? HIGH : LOW);

  if (rp2040.fifo.available() >= 2) {
    uint32_t itf_protocol = rp2040.fifo.pop();
    uint32_t msg_len = rp2040.fifo.pop();
    for (size_t pos = 0; pos < msg_len; ++pos) {
      fifo_buf[pos] = rp2040.fifo.pop();
    }
    handle_usb_input(itf_protocol, msg_len, fifo_buf);
  } else {
    handle_usb_input(0, 0, NULL);
  }

  delay(10);
}

const char* HID_CHAR_MAP_SHIFT_OFF = "abcdefghijklmnopqrstuvwxyz1234567890" "\x0A\x1B\x08\x09" " -=[]\\" "X" ";'"  "`,./";
const char* HID_CHAR_MAP_SHIFT_ON  =  "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()" "\x0A\x1B\x08\x09" " _+{}|"  "X" ":\"" "~<>?";

void xfer_key_press(bool shift, uint8_t scan_code) {
  if (scan_code >= HID_KEY_A && scan_code <= HID_KEY_SLASH ) {
    char c = (shift ? HID_CHAR_MAP_SHIFT_ON : HID_CHAR_MAP_SHIFT_OFF)[scan_code - HID_KEY_A];
    input_events.unshift(InputEvent{ .kind = 'A', .data = c});
  } else {
    input_events.unshift(InputEvent{ .kind = 'K', .data = scan_code});
  }
}

bool ok_button_pressed = false;
bool key_press_active = false;
unsigned long mouse_button_last = 0;
unsigned long lower_left_button_last = 0;
unsigned long lower_right_button_last = 0;
unsigned long volume_down_last = 0;
unsigned long volume_down_holding_sent_last = 0;
unsigned long volume_up_last = 0;
unsigned long volume_up_holding_sent_last = 0;

const int BUTTON_HOLD_THRESHOLD = 400;
const int MOUSE_MOVE_THRESHOLD = 180;
const int VOLUME_SEND_INTERVAL = 500;
const int SINGLE_PRESS_DELAY = 15;

void handle_usb_input(uint32_t itf_protocol, uint32_t len, uint8_t* report) {
  if (len == 0) {
    // Periodic tick check, not actually input
    if (lower_left_button_last > 0 && millis() - lower_left_button_last >= BUTTON_HOLD_THRESHOLD) {
      input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_MENU_ESCAPE});
      lower_left_button_last = 0;
    }

    if (lower_right_button_last > 0 && millis() - lower_right_button_last >= BUTTON_HOLD_THRESHOLD) {
      input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_MEDIA_SELECT_HOME});
      lower_right_button_last = 0;
      home_menu_start = millis();
    }

    if (volume_up_last > 0) {
      unsigned long mark = volume_up_holding_sent_last > 0 ? volume_up_holding_sent_last : volume_up_last;
      if (millis() - mark > VOLUME_SEND_INTERVAL) {
        Serial.println("HOLD VOLUME UP");
        input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_VOLUME_UP});
        volume_up_holding_sent_last = millis();
      }
    }

    if (volume_down_last > 0) {
      unsigned long mark = volume_down_holding_sent_last > 0 ? volume_down_holding_sent_last : volume_down_last;
      if (millis() - mark > VOLUME_SEND_INTERVAL) {
        Serial.println("HOLD VOLUME DOWN");
        input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_VOLUME_DOWN});
        volume_down_holding_sent_last = millis();
      }
    }


    return;
  }

  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    bool shift = (report[0] & 2) != 0;
    
    // Check for the first non-zero scan code
    uint8_t scan_code = report[2];
    if (scan_code == 0) { scan_code = report[3]; }
    if (scan_code == 0) { scan_code = report[4]; }

    if (scan_code == HID_KEY_APPLICATION) {
      // Lower left key
      lower_left_button_last = millis();
    } else if (scan_code == HID_KEY_NONE) {
      // Key release
      key_press_active = false;

      if (lower_left_button_last > 0) {
        if (millis() - lower_left_button_last < BUTTON_HOLD_THRESHOLD) {
          input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_PLAY_PAUSE});
          if (isPassthru()) {
            usb_hid.sendReport16(RID_CONSUMER_CONTROL, HID_USAGE_CONSUMER_PLAY_PAUSE);
            delay(SINGLE_PRESS_DELAY);
            usb_hid.sendReport16(RID_CONSUMER_CONTROL, 0);
          }
        }
        lower_left_button_last = 0;
      }

      // Don't check passthru; always allow a key release report through, to prevent stuck keys
      if (usb_hid.ready()) {
        usb_hid.sendReport(RID_KEYBOARD, report, sizeof(hid_keyboard_report_t));
      }
    } else {
      // Key press
      if (!key_press_active) {
        key_press_active = true;
        xfer_key_press(shift, scan_code);

        if (inputs_menu_start > 0) {
          if (scan_code == HID_KEY_ARROW_DOWN || scan_code == HID_KEY_ARROW_UP) {
            // Up and down keys reset the inputs menu timeout
            inputs_menu_start = millis();
          } else if (scan_code == HID_KEY_ARROW_LEFT || scan_code == HID_KEY_ARROW_RIGHT) {
            // Left and right keys close the input menu
            inputs_menu_start = 0;
          }
        }
      }

      if (isPassthru()) {
        usb_hid.sendReport(RID_KEYBOARD, report, sizeof(hid_keyboard_report_t));
      }
    }
  } else if (report[0] == 0x04) {
    // Mouse events
    // 0x04 (1 if OK pressed) (X) (Y) 0x00
    if (mouse_button_last > 0 && millis() - mouse_button_last > MOUSE_MOVE_THRESHOLD) {
      // Mouse movement
      if (isPassthru()) {
        hid_mouse_report_t new_report;
        new_report.buttons = report[1];
        new_report.x = report[2];
        new_report.y = report[3];
        new_report.wheel = 0;
        new_report.pan = 0;
        usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
      }
    } else if (report[1]) {
      // OK button pressed
      if (!ok_button_pressed) {
        input_events.unshift(InputEvent{ .kind = 'O', .data = 1});
        inputs_menu_start = 0; // Pressing OK selects an input
      }
    }
    ok_button_pressed = report[1] > 0;
  } else if (report[0] == 0x01) {
    // Consumer events
    if (report[1] == 0x00) {
      // Consumer release
      if (mouse_button_last > 0) {
        if (millis() - mouse_button_last < MOUSE_MOVE_THRESHOLD) {
          // Mouse click
          if (isPassthru()) {
            hid_mouse_report_t new_report;
            new_report.buttons = 1;
            new_report.x = 0;
            new_report.y = 0;
            new_report.wheel = 0;
            new_report.pan = 0;

            usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
            delay(SINGLE_PRESS_DELAY);
            new_report.buttons = 0;
            usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
          }
        }
        mouse_button_last = 0;
        lower_right_button_last = 0;
      } else {
        if (lower_right_button_last > 0) {
          if (millis() - lower_right_button_last < BUTTON_HOLD_THRESHOLD) {
            input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_CHANNEL});
            if (inputs_menu_start == 0) {
              inputs_menu_start = millis();
            } else {
              inputs_menu_start = 0; // Pressing input again closes the input menu
            }

          }
          lower_right_button_last = 0;
        }
      }

      if (volume_up_last > 0) {
        volume_up_last = 0;
        if (volume_up_holding_sent_last == 0) {
          Serial.println("SINGLE VOLUME UP");
          input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_VOLUME_UP});
        }
      }

      if (volume_down_last > 0) {
        volume_down_last = 0;
        if (volume_down_holding_sent_last == 0) {
          Serial.println("SINGLE VOLUME DOWN");
          input_events.unshift(InputEvent{ .kind = 'C', .data = HID_USAGE_CONSUMER_VOLUME_DOWN});
        }
      }
    } else if (report[1] == 0x23) {
      // Upper right button
      mouse_button_last = millis();
    } else if (report[1] == 0x24) {
      // Lower right button
      if (mouse_button_last > 0) {
        // Mouse right-click
        if (isPassthru()) {
          hid_mouse_report_t new_report;
          new_report.buttons = 2;
          new_report.x = 0;
          new_report.y = 0;
          new_report.wheel = 0;
          new_report.pan = 0;

          usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
          delay(SINGLE_PRESS_DELAY);
          new_report.buttons = 0;
          usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
        }
      } else {
        lower_right_button_last = millis();
      }
    } else if (report[1] == HID_USAGE_CONSUMER_VOLUME_DOWN) {
      volume_down_last = millis();
    } else if (report[1] == HID_USAGE_CONSUMER_VOLUME_UP) {
      volume_up_last = millis();
    }
  }
}

//------------- Core1 -------------//
void setup1() {
  // configure pio-usb: defined in usbh_helper.h
  rp2040_configure_pio_usb();

  // run host stack on controller (rhport) 1
  // Note: For rp2040 pico-pio-usb, calling USBHost.begin() on core1 will have most of the
  // host bit-banging processing works done in core1 to free up core0 for other works
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+
extern "C"
{

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  //Serial.printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  rp2040.fifo.push(itf_protocol);
  rp2040.fifo.push(len);
  for (size_t pos = 0; pos < len; ++pos) {
    rp2040.fifo.push(report[pos]);
  }

  // continue to request to receive report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.println("Error: cannot request to receive report");
  }
}

}