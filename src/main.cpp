/*
 * main.cpp
 *
 *  Created on: 27-Aug-2020
 *	Last updated: 28-Aug-2020
 *      Author: rik
 */


#include "main.hpp"


// #define LUI_MEM_DEFRAG_EN					1		// Enable memory defragment (1: enable, 0: disable) 


String g_file_names[4] = {"/image_1.jpg", "/image_2.jpg", "/image_3.jpg", "/image_4.jpg"};

static volatile uint32_t g_btn_press_timestamp = 0;

uint8_t g_mode = MODE_PREVIEW;
uint8_t g_current_file_index = 0;

lui_obj_t *g_scene_main;
lui_obj_t *g_swtch_wifi;
lui_obj_t *g_lbl_status;
lui_obj_t *g_lbl_web_server;
lui_obj_t *g_lbl_wifi_on;
lui_obj_t *g_panel;

// For 1.14", 1.3", 1.54", and 2.0" TFT with ST7789:
TFT_eSPI tft = TFT_eSPI(); 
ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80
File fs_upload_file;              // a File object to temporarily store the received file


void setup(void) 
{
	Serial.begin(74880);

	// Set GPIO0 (D3) as input pullup
	pinMode(0, INPUT_PULLUP);

	Serial.println("[ OK ] GPIO0 as input pullup");
	/* IMPORTANT: if using Adafruit library, SPI mode must be MODE2 for this specific st7789 to work */
	tft.begin();
	Serial.println("[ OK ] Display driver begin");
	tft.setRotation(2);
	tft.fillScreen(TFT_BLACK);

	// Start SPIFFS Filse System
	// TODO: Migrate to LittleFS
	SPIFFS.begin();
	Serial.println("[ OK ] SPIFFS file system begin");
	String str = "";
	Dir dir = SPIFFS.openDir("/");
	while (dir.next()) {
		str += dir.fileName();
		str += " / ";
		str += dir.fileSize();
		str += "\r\n";
	}
	Serial.println(str); 

	// Initialize LameUI library
	lui_init();
	Serial.println("[ OK ] LameUI library begin");

	prepare_gui();
	Serial.println("[ OK ] GUI prepared");

	// Showing the welcome screen for 2 seconds
	lui_update();
	delay(2000);
	lui_scene_set_active(g_scene_main);



	server.on("/upload", HTTP_GET, []() {                			 	// if the client requests the upload page
		server_handle_file_read("/upload.html");                	// send it if it exists
	});

	server.on("/upload", HTTP_POST, [](){ 
		server.send(200); },                          				// Send status 200 (OK) to tell the client we are ready to receive
		server_handle_file_upload                                    	// Receive and save the file
	);                    												// Start the SPI Flash Files System
	server.onNotFound([](){server_handle_file_read(server.uri());});

	Serial.println("[ OK ] Server request handler register");


	// The decoder must be given the exact name of the rendering function above
	TJpgDec.setCallback(tjpg_draw_pixels);
	Serial.println("[ OK ] TJpgDec callback register");
	draw_next_jpg_file();
	Serial.println("[ OK ] Draw first image");

	Serial.println("[DONE] Setup finished");

	Serial.println("[NOTE] press and hold the button at GPIO0 for 3seconds to turn on WiFi. \
	Then go to 192.168.4.1. To turn off wifi, again press and hold the button.");	

	// Start sleeping to save power
	g_btn_press_timestamp = millis();
	esp_enter_light_sleep();
}


void loop() 
{
	uint8_t btn_event = read_btn_press();
	if (g_mode == MODE_PREVIEW)
	{
		if (btn_event == BTN_EVENT_PRESSED)
		{
			draw_next_jpg_file();
			g_btn_press_timestamp = millis();
			// start sleeping as soon as image drawing is done. Wakes up on button press again
			esp_enter_light_sleep();
		}
		else if (btn_event == BTN_EVENT_LONG_PRESSED)
		{
			g_mode = MODE_UPLOAD;
			lui_object_set_visibility(1, g_panel);
			turn_on_wifi_ap();	
			lui_update();
			lui_switch_set_on(g_swtch_wifi);
			delay(700);
			lui_update();
		}
	}
	else if (g_mode == MODE_UPLOAD)
	{
		if (btn_event == BTN_EVENT_LONG_PRESSED)
		{
			g_mode = MODE_PREVIEW;
			turn_off_wifi_ap();
			lui_label_set_text("", g_lbl_status);
			lui_object_set_bg_color(LUI_STYLE_LABEL_BG_COLOR, g_lbl_status);
			lui_update();
			delay(2000);
			lui_object_set_visibility(0, g_panel);

			draw_next_jpg_file();

			g_btn_press_timestamp = millis();
		}
		else
		{
			server.handleClient();
		}
		lui_update();
	}
}

void esp_wakeup_cb()
{
	Serial.println("[NOTE] Wakeup");
}

void esp_enter_light_sleep()
{
	Serial.println("\n[NOTE] Entering light sleep");
	Serial.flush();
	// If already connected to station, disconnect
	//wifi_station_disconnect();
	// IMPORTANT: This is mandatory, else esp won't enter sleep	
	wifi_set_opmode_current(NULL_MODE);
	// set sleep type
	wifi_fpm_set_sleep_type(LIGHT_SLEEP_T); 
	// Enables force sleep
	wifi_fpm_open(); 
	// Set GPIO0 as wake up pin. Wakes up on LOW LEVEL in GPIO0
	gpio_pin_wakeup_enable(0, GPIO_PIN_INTR_LOLEVEL);
	// Set the callback function on wakeup (optional)
	wifi_fpm_set_wakeup_cb(esp_wakeup_cb);
	// Sleep for longest possible time
	wifi_fpm_do_sleep(0xFFFFFFF); 
	delay(10); // Sleep starts during inactivity, so introduce this delay
}

uint8_t read_btn_press()
{
	if (digitalRead(0) == 0)
	{	
		uint32_t start = millis();
		// Debounce time 500ms
		while (millis() - start < 500)
		{	
			// if mode is UPLOAD mode, keep the server handler running
			if (g_mode == MODE_UPLOAD)
			{
				server.handleClient();
			}
		}

		// Check if button is still low after debouncing period
		if (digitalRead(0) == 0)
		{
			start = millis();
			// Within next 1.5sec if button never goes high, consider it as a long press
			// Else if it goe high, cancel the press and return as event_none
			while (millis() - start < 1500)
			{	
				if (digitalRead(0) == 1)
				{
					return BTN_EVENT_NONE;
				}
				// if mode is UPLOAD mode, keep the server handler running
				if (g_mode == MODE_UPLOAD)
				{
					server.handleClient();
				}
				// feed the damn dog
				yield();
			}
			return BTN_EVENT_LONG_PRESSED;
		}

		// If button is high after the first 500ms debounce period, consider it as a single press
		else
		{
			return BTN_EVENT_PRESSED;
		}
	}

	// Button never became low, so event = event_none
	else
	{
		return BTN_EVENT_NONE;
	}
}


void draw_next_jpg_file()
{
	uint8_t attempt_count = 0;
	++g_current_file_index;
	++attempt_count;

	while (!SPIFFS.exists(g_file_names[g_current_file_index]))
	{
		++g_current_file_index;
		++attempt_count;
		if (g_current_file_index > 3)
		{
			g_current_file_index = 0;
		}

		// When all files are searched for but none are found, try to draw the default one
		if (attempt_count > 4)
		{
			g_current_file_index = 0;
			TJpgDec.drawFsJpg(0, 0, "/default.jpg");
			return;
		}

	}
	Serial.println(g_file_names[g_current_file_index]);
	uint16_t w = 0, h = 0;
	uint16_t x = 0, y = 0;
	TJpgDec.getFsJpgSize(&w, &h, g_file_names[g_current_file_index]); // Note name preceded with "/"
	Serial.print("Width = "); Serial.print(w); Serial.print(", height = "); Serial.println(h);

	uint8_t scale = get_best_jpg_scaling_factor(w, h, HOR_RES, VERT_RES);
	// scale 0 means iamge is too big to fit even after applying biggest scale
	if (scale == 0 || w == 0 || h == 0)
	{
		// draw a small red X
		tft.fillScreen(0);
		tft.drawLine(100, 100, 140, 140, TFT_RED);
		tft.drawLine(140, 100, 100, 140, TFT_RED);
	}
	else
	{
		TJpgDec.setJpgScale(scale);
		uint16_t scaled_w = (w / scale);
		uint16_t scaled_h = (h / scale);
		Serial.printf("scale: %d,  sw: %d   sh: %d", scale, scaled_w, scaled_h);
		// calculating x and y to draw the image in the center
		x = (scaled_w < HOR_RES) ? ((HOR_RES - scaled_w) / 2) : 0;
		y = (scaled_h < VERT_RES) ? ((VERT_RES - scaled_h) / 2) : 0;
		if (scaled_w < HOR_RES || scaled_h < VERT_RES)
		{
			tft.fillScreen(0x0000);
		}
		TJpgDec.drawFsJpg(x, y, g_file_names[g_current_file_index]);
	}

}


void turn_on_wifi_ap()
{
	//WiFi.onSoftAPModeStationConnected(staion_connected_event_handler);
	WiFi.mode(WIFI_AP);
	WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
	IPAddress myIP = WiFi.softAPIP();
	static char ip_buf[50];
	sprintf(ip_buf, "Go to:  http://%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
	lui_label_set_text(ip_buf, g_lbl_web_server);
	Serial.println(ip_buf);
	server.begin();		
	Serial.println("HTTP server started");
}


void turn_off_wifi_ap()
{
	server.close();
	WiFi.softAPdisconnect(true);
	lui_switch_set_off(g_swtch_wifi);
	lui_label_set_text("Server closed...", g_lbl_web_server);
}


void draw_pixels_area_cb(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
	uint32_t wh = width * height;
	const uint32_t mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));

    tft.setAddrWindow(x, y, width, height);
	// Set 16bit mode (see Adafruit_SPITFT.cpp)
	SPI1U1 = ((SPI1U1 & mask) | ((15 << SPILMOSI) | (15 << SPILMISO)));
	while(SPI1CMD & SPIBUSY) {}
    while (wh--) 
	{
		//tft.pushColor(color);  // <-- slow
		SPI1W0 = (color >> 8) | (color << 8);
		SPI1CMD |= SPIBUSY;
		while(SPI1CMD & SPIBUSY) {}
	}
}


bool tjpg_draw_pixels(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
	uint32_t wh = w * h;
	const uint32_t mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));

    tft.setAddrWindow(x, y, w, h);
	// Set 16bit mode (see Adafruit_SPITFT.cpp)
	SPI1U1 = ((SPI1U1 & mask) | ((15 << SPILMOSI) | (15 << SPILMISO)));
	while(SPI1CMD & SPIBUSY) {}
    while (wh--) 
	{
		//tft.pushColor(color);  // <-- slow
		SPI1W0 = ((*bitmap) >> 8) | ((*bitmap) << 8);
		SPI1CMD |= SPIBUSY;
		while(SPI1CMD & SPIBUSY) {}
		bitmap++;
	}

	return true;
}


void server_handle_file_upload()
{ // upload a new file to the SPIFFS
	HTTPUpload& upload = server.upload();

	if (!upload.filename.endsWith(".jpg"))
	{
		Serial.println(F("File type is not jpg. Error"));
		server.send(500, "text/plain", "500: couldn't create file");
		lui_label_set_text("File is not jpg", g_lbl_status);
		lui_object_set_bg_color(lui_rgb(0xff, 0, 0), g_lbl_status);
		return;
	}

	else
	{
		if(upload.status == UPLOAD_FILE_START)
		{
			Serial.println(upload.totalSize);

			String filename = upload.filename;
			if(!filename.startsWith("/")) 
			{
				filename = "/"+filename;
			}
			Serial.print("file Name: "); Serial.println(filename);
			lui_object_set_bg_color(lui_rgb(0, 0x80, 0), g_lbl_status);
			lui_label_set_text("Uploading Started", g_lbl_status);
			lui_update(); // If we don't call here, rendering is not happening from void loop(). Why?

			fs_upload_file = SPIFFS.open(filename, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
			filename = String();
		} 
		else if(upload.status == UPLOAD_FILE_WRITE)
		{
			if(fs_upload_file)
			{
				static char status_buff[50];
				
				fs_upload_file.write(upload.buf, upload.currentSize); // Write the received bytes to the file
				
				sprintf(status_buff, "Uploading...        %0.2fkB", (float)upload.totalSize / 1000.0);
				lui_label_set_text(status_buff, g_lbl_status);
				lui_update();
				//Serial.println(status_buff);
			}
		} 
		else if(upload.status == UPLOAD_FILE_END)
		{
			if(fs_upload_file) 
			{                                    // If the file was successfully created
				fs_upload_file.close();                               // Close the file again
				Serial.print("server_handle_file_upload Size: "); 
				Serial.println(upload.totalSize);

				static char status_buff[50];
				String status = "[" + upload.filename + "]" + " uploaded.";
				status.toCharArray(status_buff, 50);
				lui_label_set_text(status_buff, g_lbl_status);
				lui_object_set_bg_color(lui_rgb(0, 0x80, 0), g_lbl_status);
				server.send(200);
			} 
			else 
			{
				server.send(500, "text/plain", "500: couldn't create file");
			}
		}
	}

	
}


String server_get_content_type(String filename) 
{ // convert the file extension to the MIME type
	if (filename.endsWith(".html")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".gz")) return "application/x-gzip";
	return "text/plain";
}


void server_handle_file_read(String path) 
{ // send the right file to the client (if it exists)
	Serial.println("server_handle_file_read: " + path);

	if (path.endsWith("/")) 
	{
		path += "upload.html";          							// If a folder is requested, send the index file
	}
	String contentType = server_get_content_type(path);             // Get the MIME type
	String pathWithGz = path + ".gz";

	if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) 
	{ // If the file exists, either as a compressed archive, or normal
		if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
			path += ".gz";                                         // Use the compressed verion
		File file = SPIFFS.open(path, "r");                    // Open the file
		size_t sent = server.streamFile(file, contentType);    // Send it to the client
		file.close();                                          // Close the file again
		Serial.println(String("\tSent file: ") + path);
	}
	else
	{
		server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
		Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
	}
}


uint8_t get_best_jpg_scaling_factor(uint16_t img_w, uint16_t img_h, uint16_t new_w, uint16_t new_h)
{
	uint8_t all_factors[4] = {1, 2, 4, 8};
	uint8_t scaling_factor = 0;
	uint8_t best_factor = 0;

	uint8_t w_scale = ceil((float)img_w / (float)new_w);
	uint8_t h_scale = ceil((float)img_h / (float)new_h);
	scaling_factor =  w_scale > h_scale ? w_scale : h_scale;

	if (scaling_factor > all_factors[3])
	{
		return 0;
	}

	// now find the best factor from all the available ones
	
	uint8_t i = 0;
	while (!best_factor)
	{
		while (i < 4)
		{
			if (scaling_factor == all_factors[i])
			{
				best_factor = all_factors[i];
				break;
			}
			else
			{
				++i;
			}
		}
		++scaling_factor;
		i = 0;
	}

	return best_factor;

}


void prepare_gui()
{
	//----------------------------------------------------------
	//creating display driver variable for lame_ui
	lui_dispdrv_t *my_display_driver = lui_dispdrv_create();
	lui_dispdrv_register(my_display_driver);
	lui_dispdrv_set_resolution(HOR_RES, VERT_RES, my_display_driver);
	lui_dispdrv_set_draw_pixels_area_cb(draw_pixels_area_cb, my_display_driver);
	lui_dispdrv_set_render_complete_cb(NULL, my_display_driver);

	g_scene_main = lui_scene_create();
	lui_scene_set_font(&font_ubuntu_16, g_scene_main);

	lui_obj_t *scene_welcome_screen = lui_scene_create();
	lui_scene_set_font(&font_ubuntu_48, scene_welcome_screen);

	//set the active scene. This scene will be rendered iby the lui_update()
	lui_scene_set_active(scene_welcome_screen);

	lui_obj_t *lbl_welcome = lui_label_create();
	lui_object_add_to_parent(lbl_welcome, scene_welcome_screen);
	lui_object_set_position(50, 10, lbl_welcome);
	lui_label_set_text("Lame\nGallery\n0.1", lbl_welcome);
	lui_object_set_area(235, 170, lbl_welcome);

	lui_obj_t *lbl_info = lui_label_create();
	lui_object_add_to_parent(lbl_info, scene_welcome_screen);
	lui_object_set_position(5, 185, lbl_info);
	lui_label_set_text("Developed by Avra Mitra \n[ github.com/abhra0897/ ]", lbl_info);
	lui_object_set_area(235, 50, lbl_info);
	lui_label_set_font(&font_ubuntu_16, lbl_info);


	g_panel = lui_panel_create();
	lui_object_add_to_parent(g_panel, g_scene_main);
	lui_object_set_position(0, 0, g_panel);
	lui_object_set_area(HOR_RES, VERT_RES, g_panel);
	lui_object_set_visibility(0, g_panel);

	lui_obj_t *heading_panel = lui_panel_create();
	lui_object_add_to_parent(heading_panel, g_panel);
	lui_object_set_bg_color(LUI_STYLE_LABEL_BORDER_COLOR, heading_panel);
	lui_object_set_position(0, 10, heading_panel);
	lui_object_set_area(HOR_RES, 20, heading_panel);

	lui_obj_t *lbl_heading = lui_label_create();
	lui_object_add_to_parent(lbl_heading, heading_panel);
	lui_label_set_text("LameGallery 0.1", lbl_heading);
	lui_label_set_text_color(0xffff, lbl_heading);
	lui_object_set_bg_color(LUI_STYLE_LABEL_BORDER_COLOR, lbl_heading);
	lui_object_set_border_visibility(1, lbl_heading);
	lui_object_set_position(60, 0, lbl_heading);
	lui_object_set_area(HOR_RES, 20, lbl_heading);

	lui_obj_t *lbl_wifi_creds = lui_label_create();
	lui_object_add_to_parent(lbl_wifi_creds, g_panel);
	static char wifi_cred_buff[50];
	sprintf(wifi_cred_buff, "SSID: %s \nPASSWORD: %s", WIFI_AP_SSID, WIFI_AP_PASSWORD);
	lui_label_set_text(wifi_cred_buff, lbl_wifi_creds);
	lui_object_set_border_visibility(1, lbl_wifi_creds);
	lui_object_set_position(20, 40, lbl_wifi_creds);
	lui_object_set_area(200, 40, lbl_wifi_creds);

	g_lbl_web_server = lui_label_create();
	lui_object_add_to_parent(g_lbl_web_server, g_panel);
	lui_object_set_border_visibility(1, g_lbl_web_server);
	lui_object_set_position(20, 90, g_lbl_web_server);
	lui_object_set_area(200, 20, g_lbl_web_server);

	g_lbl_status = lui_label_create();
	lui_object_add_to_parent(g_lbl_status, g_panel);
	lui_label_set_text_color(lui_rgb(0xff, 0xff, 0xff), g_lbl_status);
	lui_object_set_position(20, 140, g_lbl_status);
	lui_object_set_area(200, 20, g_lbl_status);

	g_lbl_wifi_on = lui_label_create();
	lui_object_add_to_parent(g_lbl_wifi_on, g_panel);
	lui_label_set_text("WiFi:", g_lbl_wifi_on);
	lui_object_set_position(20, 170, g_lbl_wifi_on);
	lui_label_set_font(&font_ubuntu_48, g_lbl_wifi_on); // using this font eats lots of RAM
	lui_object_set_area(118, 50, g_lbl_wifi_on);


	g_swtch_wifi = lui_switch_create();
	lui_object_add_to_parent(g_swtch_wifi, g_panel);
	lui_object_set_position(140, 180, g_swtch_wifi);
	lui_object_set_area(85, 40, g_swtch_wifi);
}