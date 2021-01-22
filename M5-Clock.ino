
#include <M5EPD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SPIFFS.h"
#include <nvs.h>


// Settings :-
int8_t global_timezone = 0;    // 0 = UK, put your own here
const char * wifi_ssid = "** SSID Here **";
const char * wifi_password = "** Wifi Password Here **";
const char * weather_api_key = "** API key here **";
const char * weather_api_location = "Buxton";

float tem;
float outside_temp;
float hum;

int16_t last_temp = 0, low_temp = 0, high_temp = 0, outdoor_temp = -2000;
int16_t last_weather = 0;
int16_t forced_shutdown = 0;
int nvr_save = 0;
int cleared = 0;

int count = 1;
rtc_time_t current_time;

char * ord[] = { "st", "nd", "rd", "th" };
char * days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
char * month[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

M5EPD_Canvas unused_canvas(&M5.EPD);


/* After M5.shutdown( secs ), the whole sketch restarts so it will reenter setup - just like a reboot.  
 * This means that all state (variables etc) are lost so you need to use NVS to store anything essential. 
 * M5.shutdown doesn't appear to do anything if you have the USB cable connected so we still need the loop with a delay in there. 
 * This platform needs some serious work.
 */



void load_persistent_data(void)
{
    // Load our 3 state variables from NVS
    nvs_handle nvs_arg;
    nvs_open("M5-Clock", NVS_READONLY, &nvs_arg);
    nvs_get_i16(nvs_arg, "last_temp", &last_temp);
    nvs_get_i16(nvs_arg, "low_temp", &low_temp);
    nvs_get_i16(nvs_arg, "high_temp", &high_temp);
    nvs_get_i16(nvs_arg, "outdoor_temp", &outdoor_temp);
    nvs_get_i16(nvs_arg, "last_weather", &last_weather);
    nvs_get_i16(nvs_arg, "forced_shutdown", &forced_shutdown);
    nvs_close(nvs_arg);
}

void setup()
{    
    M5.begin(false, false, true, true, false);

    // Serial is initialised by M5.begin to 115200 baud
    Serial.print("\nM5-Clock started.\n"); 
    
    M5.SHT30.Begin();
    M5.RTC.begin();

    M5.RTC.getTime(&current_time);

    M5.EPD.SetRotation(0);
    M5.TP.SetRotation(0);

    load_persistent_data();
    
    if ((current_time.min == 0) || forced_shutdown) // Clean the display once an hour 
    {
       // Clear the display
       M5.EPD.Clear(true); 
       cleared = 1;

       if (forced_shutdown)
       {
          forced_shutdown = 0;
          nvr_save = 1; 
       }
    }
    
    unused_canvas.createCanvas(960, 540);    
    
    if (!SPIFFS.begin(true))
    {
        log_e("SPIFFS Mount Failed");
        while(1);
    }

    unused_canvas.loadFont("/liquid-crystal.ttf", SPIFFS); // Load font files internal flash
    unused_canvas.createRender(120);
    unused_canvas.createRender(48);
    unused_canvas.createRender(32);
    
}

void save_persistent_data(void)
{
    // Save our 3 state variables - used sparingly to avoid wear on the flash
    nvs_handle nvs_arg;
    nvs_open("M5-Clock", NVS_READWRITE, &nvs_arg);
    nvs_set_i16(nvs_arg, "last_temp", last_temp);
    nvs_set_i16(nvs_arg, "low_temp", low_temp);
    nvs_set_i16(nvs_arg, "high_temp", high_temp);
    nvs_set_i16(nvs_arg, "outdoor_temp", outdoor_temp);
    nvs_set_i16(nvs_arg, "last_weather", last_weather);
    nvs_set_i16(nvs_arg, "forced_shutdown", forced_shutdown); 
    nvs_close(nvs_arg);
}

int dayofweek(int y, int m, int d) 
{
     static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
     if( m < 3 )
     {
        y -= 1;
     }
     return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

void render_text(int x, int y, int font_size, char * text)
{
   int width = strlen(text) * font_size / 2;  // Estimate width of canvas
   int height = font_size;

   M5EPD_Canvas canvas(&M5.EPD);
   canvas.createCanvas(width, height);

   canvas.setTextSize(font_size);
   canvas.setTextDatum(TC_DATUM);
   canvas.drawString(text, width / 2, 0);

   canvas.pushCanvas(x, y, UPDATE_MODE_GC16);
}

void render_time()
{
    // Display Time
    char timeString[16];
    char dateString[64];
    
    M5.RTC.getTime(&current_time);
    sprintf(timeString, " %02d:%02d ", current_time.hour, current_time.min);
    render_text(280, 160, 120, timeString);
    Serial.printf("Time rendered\n");


    // Display Date - once an hour
    if (cleared || (current_time.min == 0)) 
    {
       rtc_date_t current_date;
       M5.RTC.getDate(&current_date);

       int dow = dayofweek(current_date.year, current_date.mon, current_date.day);
   
       char * day = "Error";
       if ((dow >=0 ) && (dow <= 6)) day = days[dow];

       int o = current_date.day % 10;
       if (o > 4) o = 4;

       sprintf(dateString, "  %s %2d%s %s %4d  ", day, current_date.day, ord[o-1], month[current_date.mon-1], current_date.year);
       render_text(110, 310, 48, dateString);
       Serial.printf("Date rendered\n");
    }  
}


void sleep_for_a_minute()
{   
    // Calculate remaining minute and shutdown for that.
    M5.RTC.getTime(&current_time);
    int sleep_period = 60 - current_time.sec; 
    M5.shutdown(sleep_period);

    // In case we're plugged in and loop is still running
    delay(sleep_period * 1000);
}

// Stolen from the Factory Test example - should be useful
bool sync_ntp_time(void)
{
    const char *ntpServer = "time.cloudflare.com";
    configTime(global_timezone * 3600, 0, ntpServer);

    struct tm timeInfo;
    if (getLocalTime(&timeInfo))
    {
        rtc_time_t time_struct;
        time_struct.hour = timeInfo.tm_hour;
        time_struct.min = timeInfo.tm_min;
        time_struct.sec = timeInfo.tm_sec;
        M5.RTC.setTime(&time_struct);
        rtc_date_t date_struct;
        date_struct.week = timeInfo.tm_wday;
        date_struct.mon = timeInfo.tm_mon + 1;
        date_struct.day = timeInfo.tm_mday;
        date_struct.year = timeInfo.tm_year + 1900;
        M5.RTC.setDate(&date_struct);
        Serial.print("Time synced\n");
        return 1;
    }
    log_d("Time Sync failed");
    return 0;
}


int fetch_weather_image(char * url, char * file_name)
{        
        HTTPClient http;
        
        // Cheekily get a larger image by replacing 64x64 with 128x128  :)
        char url128[128];
        char * p = strstr(url, "64x64");
        if (p)
        {
           p[0] = 0;
           p+=5;
           strcpy(url128, "128x128");
           strcat(url128, p);
           strcat(url, url128); 
        }

        // Retrieve the weather image
        Serial.printf("Fetching weather image: %s\n", url);
        http.begin(url);
        int httpResponseCode = http.GET();
        if (httpResponseCode > 0) 
        {
           Serial.print("HTTP Response code: ");
           Serial.println(httpResponseCode);
           int len = http.getSize();
           if (len > 0)
           {
              // create buffer for read
              uint8_t buff[128] = { 0 };

              Serial.printf("Fetching weather image (%d bytes)\n", len);

              File file = SPIFFS.open(file_name, FILE_WRITE);

              // get tcp stream
              WiFiClient * stream = http.getStreamPtr();

              // read all data from server
              while(http.connected() && (len > 0 || len == -1)) 
              {
                  // get available data size
                  size_t size = stream->available();
                  if(size) 
                  {
                     // read up to 128 byte
                     int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                     // write it to Serial
                     //Serial.write(buff, c);
                     file.write(buff, c);
                     if(len > 0) len -= c;
                  }
              }
              delay(1);
              file.close();

              if (len == 0) Serial.printf("Weather image fetched successfully.\n");
              else return 0;
           }
           else return 0; // fail
        }
        else return 0; // fail 
        
        http.end();
        return 1; // success
}


void get_weather()
{
    char url[512];
    char file_name[32];
    uint16_t image_number = 0;
    
    WiFi.begin(wifi_ssid, wifi_password);
    int wifi_count = 0;

    while ((WiFi.status() != WL_CONNECTED) && (wifi_count < 64)) {
        delay(500);
        Serial.print(".");
        wifi_count++;
    }

    if (wifi_count == 32) 
    {
      Serial.print("\nWifi connect timed-out - abandoning weather\n");
      return;
    }
    Serial.print("Wifi connected\n");

    // Might as well since we're connected.
    sync_ntp_time();

    HTTPClient http;

    // Construct the API URL
    sprintf(url, "http://api.weatherapi.com/v1/current.json?key=%s&q=%s", weather_api_key, weather_api_location);
    http.begin(url);
    int httpResponseCode = http.GET();
      
      if (httpResponseCode>0) {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        String payload = http.getString();
        Serial.println(payload);
        http.end();

        // Parse json
        StaticJsonDocument<1100> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
           Serial.print(F("deserializeJson() failed: "));
           Serial.println(error.f_str());
           return;
        }

        outside_temp = doc["current"]["temp_c"];
        const char * image = doc["current"]["condition"]["icon"];
        
        // Construct the weather image URL
        strcpy(url, "https:");
        strcat(url, image);

        int16_t new_outdoor_temp = (int16_t) (outside_temp * 10.0);
        if (new_outdoor_temp != outdoor_temp) 
        {
           outdoor_temp = new_outdoor_temp;
           nvr_save = 1;  
        }

        // Construct the file name
        char fn[32];
        char * d = strstr(url, "day/");
        if (d) // It's a day time image
        {
           d += 4;
           strcpy(fn, d);
           fn[3] = 0;
           image_number = atoi(fn) + 1000;
           Serial.printf("image: %d\n", image_number);
        }
        else  // Night image
        {
           d = strstr(url, "night/");
           if (d) 
           {
              d += 6;
              strcpy(fn, d);
              fn[3] = 0;
              image_number = atoi(fn) + 2000;
           }
        }

        if (image_number > 0)
        {
           if (last_weather != image_number) 
           {
               last_weather = image_number;
               nvr_save = 1;  
           
               // Construct filename
               sprintf(file_name, "/%d.png", image_number);
               Serial.printf("Weather image file: %s\n", file_name);

               if (!SPIFFS.exists(file_name)) fetch_weather_image(url, file_name);

               // Update the weather image   
               M5EPD_Canvas canvas(&M5.EPD);
               canvas.createCanvas(128, 128);
               canvas.drawPngFile(SPIFFS, file_name, 0, 0, 128, 128);
               canvas.pushCanvas(140, 405, UPDATE_MODE_GC16);
               Serial.printf("Weather image pushed to display\n"); 
           }
        }
        
      }
      else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
      }
      
}



void loop()
{  
    char msg[256];

    // Draw time
    render_time();

    // Temp + humidity
    M5.SHT30.UpdateData();
    tem = M5.SHT30.GetTemperature();
    hum = M5.SHT30.GetRelHumidity();

    // Update the battery voltage every 5 mins
    //if (cleared || (current_time.min % 5 == 0)) 
    {
       uint32_t battery = M5.getBatteryVoltage();
       sprintf(msg, "%1.1fv", (float) battery / 1000.0);
       render_text(890, 5, 32, msg);

       if (battery < 3200) 
       {
          // No battery - shutdown
          M5.EPD.Clear(true); 
          render_text(360, 200, 48, "No Battery !");
          forced_shutdown = 1;
          save_persistent_data();
          delay(500);
          M5.shutdown(); 
       }

       if (battery < 3400)
       {
          render_text(360, 80, 48, "Low Battery"); 
       }
    }

    int16_t int_temp = (int16_t) (tem * 10.0);

/*
    if ((low_temp == 0) && (high_temp == 0)) 
    {
      load_persistent_data(); 
    }
*/

    // Current temperature
    if (((int_temp != last_temp) || cleared || ((current_time.hour == 0) && (current_time.min == 0))))
    {
        // Update the temperature
        sprintf(msg, "%2.1fC", tem);
        render_text(5, 5, 48, msg);
        last_temp = int_temp;

        int hi_lo_changes = 0;
     
        if (((low_temp == 0) && (high_temp == 0)) || ((current_time.hour == 0) && (current_time.min == 0))) // Load NVR failed or new day
        {
           low_temp = int_temp;
           high_temp = int_temp;
           hi_lo_changes = 1; 
        }

        if (int_temp > high_temp) 
        {
           high_temp = int_temp;
           hi_lo_changes = 1;
        }

        if (int_temp < low_temp) 
        {
           low_temp = int_temp;
           hi_lo_changes = 1;
        }

        // High and low temps have changed
        if ((hi_lo_changes) || cleared) 
        { 
           sprintf(msg, "%2.1fC  %2.1fC", (float) low_temp / 10.0, (float) high_temp / 10.0);
           render_text(130, 5, 32, msg);
        }

        nvr_save = 1;
    }
    
    delay(500);
    
    // Update the weather every 30 minutes
    if (cleared || (current_time.min % 30 == 0))
    {
       get_weather();

       // Temp has probably changed
       sprintf(msg, "%2.1fC", (float) outdoor_temp / 10.0);
       render_text(5, 475, 48, msg);
    }


    if (nvr_save) save_persistent_data();
    
    sleep_for_a_minute();
}
