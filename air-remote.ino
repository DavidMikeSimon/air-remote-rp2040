#include <Wire.h>
#include "usb.h"

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
  TUD_HID_REPORT_DESC_CONSUMER( HID_REPORT_ID(RID_CONSUMER_CONTROL) )
};

Adafruit_USBD_HID usb_hid;

void setup() {
  pinMode(PIN_LED, OUTPUT);

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin(0x05);
  Wire.onReceive(dataReceived);
	Wire.onRequest(dataRequested);

  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();
}

bool led = false;

void dataReceived(int numBytes) {
	while(Wire.available()) {
		char c = Wire.read();
	}
  led = !led;
  digitalWrite(PIN_LED, led ? HIGH : LOW);
}

unsigned long last_request = 0;

void dataRequested() {
  if (millis() - last_request > 3000) {
	  Wire.write("PONGILY PING");
    last_request = millis();
  } else {
    Wire.write("nope");
  }
}

uint8_t fifoBuf[64];

void loop() {
  uint32_t msgLen = rp2040.fifo.pop();
  for (size_t pos = 0; pos < msgLen; ++pos) {
    fifoBuf[pos] = rp2040.fifo.pop();
  }
  Serial.printf("GOT %u BYTES\n", msgLen);
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

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use.
// tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
// descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
// it will be skipped therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
  (void) desc_report;
  (void) desc_len;
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  Serial.printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  Serial.printf("VID = %04x, PID = %04x\r\n", vid, pid);
  if (tuh_hid_receive_report(dev_addr, instance)) {
    Serial.printf("Registered\r\n");
  } else {
    Serial.printf("Error: cannot request to receive report\r\n");
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  Serial.printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

unsigned long mouse_button_last = 0;
unsigned long lower_left_button_last = 0;
bool ok_button_pressed = false;

const int MOUSE_MOVE_THRESHOLD = 180;

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
  rp2040.fifo.push(len);
  for (size_t pos = 0; pos < len; ++pos) {
    rp2040.fifo.push(report[pos]);
  }

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    if (len != 8) {
      Serial.printf("report len = %u NOT 8, probably something wrong !!\r\n", len);
    } else if (report[2] == 0x65) {
      Serial.println("LOWER LEFT KEY");
      lower_left_button_last = millis();
    } else {
      if (report[2] == 0x00) {
        Serial.println("KB RELEASE");
        if (lower_left_button_last > 0) {
          // while (!usb_hid.ready()) {
          //   yield();
          // }
          // Serial.println("PLAY/PAUSE");
          // usb_hid.sendReport16(RID_CONSUMER_CONTROL, HID_USAGE_CONSUMER_PLAY_PAUSE);
          // delay(5);
          // usb_hid.sendReport16(RID_CONSUMER_CONTROL, 0);
          lower_left_button_last = 0;
        }
      } else {
        for (uint16_t i = 0; i < len; i++) {
          Serial.printf("0x%02X ", report[i]);
        }
        Serial.println("KB");
      }

      // NOTE: for better performance you should save/queue remapped report instead of
      // blocking wait for usb_hid ready here
      // while (!usb_hid.ready()) {
      //   yield();
      // }

      // usb_hid.sendReport(RID_KEYBOARD, report, sizeof(hid_keyboard_report_t));
    }
  } else {
    // Mouse events
    // 0x04 (1 if OK pressed) (X) (Y) 0x00
    if (report[0] == 0x04) {
      hid_mouse_report_t new_report;
      new_report.buttons = report[1];
      new_report.x = report[2];
      new_report.y = report[3];
      new_report.wheel = 0;
      new_report.pan = 0;

      if (mouse_button_last > 0 && millis() - mouse_button_last > MOUSE_MOVE_THRESHOLD) {
        // while (!usb_hid.ready()) {
        //   yield();
        // }
        // usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
      } else if (report[1]) {
        if (!ok_button_pressed) {
          Serial.println("OK BTN PRESS");
        }
      }

      ok_button_pressed = report[1] > 0;
    } else if (report[0] == 0x01) {
      if (report[1] == 0x00) {
        Serial.println("RELEASE");
        if (mouse_button_last > 0) {
          if (millis() - mouse_button_last < MOUSE_MOVE_THRESHOLD) {
            Serial.println("MOUSE BUTTON");
            hid_mouse_report_t new_report;
            new_report.buttons = 1;
            new_report.x = 0;
            new_report.y = 0;
            new_report.wheel = 0;
            new_report.pan = 0;

            // while (!usb_hid.ready()) {
            //   yield();
            // }

            // usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
            // delay(5);
            // new_report.buttons = 0;
            // usb_hid.sendReport(RID_MOUSE, &new_report, sizeof(new_report));
          }
          mouse_button_last = 0;
        }
      } else if (report[1] == 0x23) {
        Serial.println("UPPER RIGHT");
        mouse_button_last = millis();
      } else if (report[1] == 0x24) {
        Serial.println("LOWER RIGHT");
      } else if (report[1] == 0xEA) {
        Serial.println("VOL DOWN");
       } else if (report[1] == 0xE9) {
        Serial.println("VOL UP");
      } else {
        Serial.println("!!! UNKNOWN");
      }
    } 
  }

  // continue to request to receive report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.printf("Error: cannot request to receive report\r\n");
  }
}

}