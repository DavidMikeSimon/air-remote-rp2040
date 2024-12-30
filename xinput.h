#ifndef XINPUT_H
#define XINPUT_H

// Based on https://github.com/fluffymadness/tinyusb-xinput

typedef struct {
	uint8_t rid;
	uint8_t rsize;
	uint8_t digital_buttons_1;
	uint8_t digital_buttons_2;
	uint8_t lt;
	uint8_t rt;
	int l_x;
	int l_y;
	int r_x;
	int r_y;
	uint8_t reserved_1[6];
} ReportDataXinput;
extern ReportDataXinput XboxButtonData;

tusb_desc_device_t const xinputDeviceDescriptor = {.bLength = sizeof(tusb_desc_device_t),
                                        .bDescriptorType = TUSB_DESC_DEVICE,
                                        .bcdUSB = 0x0200,
                                        .bDeviceClass = 0xFF,
                                        .bDeviceSubClass = 0xFF,
                                        .bDeviceProtocol = 0xFF,
                                        .bMaxPacketSize0 =
                                            CFG_TUD_ENDPOINT0_SIZE,

                                        .idVendor = 0x045E,
                                        .idProduct = 0x028E,
                                        .bcdDevice = 0x0572,

                                        .iManufacturer = 0x01,
                                        .iProduct = 0x02,
                                        .iSerialNumber = 0x03,

                                        .bNumConfigurations = 0x01};


const uint8_t xinputConfigurationDescriptor[] = {
//Configuration Descriptor:
0x09,	//bLength
0x02,	//bDescriptorType
0x30,0x00, 	//wTotalLength   (48 bytes)
0x01,	//bNumInterfaces
0x01,	//bConfigurationValue
0x00,	//iConfiguration
0x80,	//bmAttributes   (Bus-powered Device)
0xFA,	//bMaxPower      (500 mA)

//Interface Descriptor:
0x09,	//bLength
0x04,	//bDescriptorType
0x00,	//bInterfaceNumber
0x00,	//bAlternateSetting
0x02,	//bNumEndPoints
0xFF,	//bInterfaceClass      (Vendor specific)
0x5D,	//bInterfaceSubClass   
0x01,	//bInterfaceProtocol   
0x00,	//iInterface

//Unknown Descriptor:
0x10,
0x21, 
0x10, 
0x01, 
0x01, 
0x24, 
0x81, 
0x14, 
0x03, 
0x00, 
0x03,
0x13, 
0x02, 
0x00, 
0x03, 
0x00, 

//Endpoint Descriptor:
0x07,	//bLength
0x05,	//bDescriptorType
0x81,	//bEndpointAddress  (IN endpoint 1)
0x03,	//bmAttributes      (Transfer: Interrupt / Synch: None / Usage: Data)
0x20,0x00, 	//wMaxPacketSize    (1 x 32 bytes)
0x04,	//bInterval         (4 frames)

//Endpoint Descriptor:

0x07,	//bLength
0x05,	//bDescriptorType
0x02,	//bEndpointAddress  (OUT endpoint 2)
0x03,	//bmAttributes      (Transfer: Interrupt / Synch: None / Usage: Data)
0x20,0x00, 	//wMaxPacketSize    (1 x 32 bytes)
0x08,	//bInterval         (8 frames)
};


// string descriptor table
char const *string_desc_arr_xinput[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "GENERIC",                   // 1: Manufacturer
    "XINPUT CONTROLLER",         // 2: Product
    "1.0"                       // 3: Serials
};


uint8_t xinput_endpoint_in=0;
uint8_t xinput_endpoint_out=0;

static void xinput_init(void) {

}

static void xinput_reset(uint8_t __unused rhport) {
    
}


static uint16_t xinput_open(uint8_t __unused rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
  //+16 is for the unknown descriptor 
  uint16_t const drv_len = sizeof(tusb_desc_interface_t) + itf_desc->bNumEndpoints*sizeof(tusb_desc_endpoint_t) + 16;
  TU_VERIFY(max_len >= drv_len, 0);

  uint8_t const * p_desc = tu_desc_next(itf_desc);
  uint8_t found_endpoints = 0;
  while ( (found_endpoints < itf_desc->bNumEndpoints) && (drv_len <= max_len)  )
  {
    tusb_desc_endpoint_t const * desc_ep = (tusb_desc_endpoint_t const *) p_desc;
    if ( TUSB_DESC_ENDPOINT == tu_desc_type(desc_ep) )
    {
      TU_ASSERT(usbd_edpt_open(rhport, desc_ep));

      if ( tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN )
      {
        xinput_endpoint_in = desc_ep->bEndpointAddress;
      }else
      {
        xinput_endpoint_out = desc_ep->bEndpointAddress;
      }
      found_endpoints += 1;
    }
    p_desc = tu_desc_next(p_desc);
  }
  return drv_len;
}

static bool xinput_device_control_request(uint8_t __unused rhport, tusb_control_request_t const *request) {
  return true;
}

static bool xinput_control_complete_cb(uint8_t __unused rhport, tusb_control_request_t __unused const *request) {
    return true;
}
//callback after xfer_transfer
static bool xinput_xfer_cb(uint8_t __unused rhport, uint8_t __unused ep_addr, xfer_result_t __unused result, uint32_t __unused xferred_bytes) {
  return true;
}

static usbd_class_driver_t const xinput_driver ={
  #if CFG_TUSB_DEBUG >= 2
    .name = "XINPUT",
#endif
    .init             = xinput_init,
    .reset            = xinput_reset,
    .open             = xinput_open,
    .control_request  = xinput_device_control_request,
    .control_complete = xinput_control_complete_cb,
    .xfer_cb          = xinput_xfer_cb,
    .sof              = NULL
};

#endif