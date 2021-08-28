/*
 * main.hpp
 *
 *  Created on: 27-Aug-2020
 *	Last updated: 28-Aug-2020
 *      Author: rik
 */

#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>   // Include the SPIFFS library
#include <TJpg_Decoder.h>
#include "TFT_eSPI.h"
extern "C" 
{
	// Since LameUI is C based library, we need extern "C" to use it in C++
	// https://isocpp.org/wiki/faq/mixing-c-and-cpp#include-c-hdrs-nonsystem
	#include "lame_ui.h"
	#include "font_microsoft_16.h"
    #include "font_ubuntu_16.h"
	#include "font_ubuntu_48.h"
    #include "user_interface.h"
    #include "gpio.h"
}




// unused, but enforce the max size restriction in client (html/js)
#define MAX_FILE_SIZE			307200	//300kB max

#define MODE_PREVIEW			0
#define MODE_UPLOAD				1

#define BTN_EVENT_NONE			0
#define BTN_EVENT_PRESSED		1
#define BTN_EVENT_LONG_PRESSED	2

#define HOR_RES					240
#define VERT_RES				240

#if defined(ESP8266)
  #define TFT_CS         		-1
  #define TFT_RST        		5                                            
  #define TFT_DC         		4
#endif

#define WIFI_AP_SSID            "LameGallery0.1"
#define WIFI_AP_PASSWORD        "insurgency"

//TODO : 1. Commenting
//       2. ?

void draw_pixels_area_cb(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
bool tjpg_draw_pixels(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
String server_get_content_type(String filename); // convert the file extension to the MIME type
void server_handle_file_read(String path);       // send the right file to the client (if it exists)
void server_handle_file_upload();                // upload a new file to the SPIFFS
uint8_t read_btn_press();
void draw_next_jpg_file();
void turn_on_wifi_ap();
void turn_off_wifi_ap();
uint8_t get_best_jpg_scaling_factor(uint16_t img_w, uint16_t img_h, uint16_t new_w, uint16_t new_h);
void staion_connected_event_handler(const WiFiEventSoftAPModeStationConnected& evt);
void prepare_gui();
void esp_wakeup_cb();
void esp_enter_light_sleep();