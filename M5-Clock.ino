
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

int16_t low_temp = 0, high_temp = 0, outdoor_temp = -2000;
int nvr_save = 0;

int count = 1;
rtc_time_t current_time;

char * days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
char * month[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

M5EPD_Canvas canvas(&M5.EPD);


/* After M5.shutdown( secs ), the whole sketch restarts so it will reenter setup - just like a reboot.  
 * This means that all state (variables etc) are lost so you need to use NVS to store anything essential. 
 * M5.shutdown doesn't appear to do anything if you have the USB cable connected so we still need the loop with a delay in there. 
 * This platform needs some serious work.
 */

void setup()
{
 
    
    M5.begin();


    // It seems that you can't have serial on and EPD display updating :( 
    //Serial.begin(9600); 
    
    M5.SHT30.Begin();
    M5.RTC.begin();

    if (!SPIFFS.begin(true))
    {
        log_e("SPIFFS Mount Failed");
        while(1);
    }

    M5.RTC.getTime(&current_time);

    M5.EPD.SetRotation(0);
    M5.TP.SetRotation(0);
    if (current_time.min % 5 == 0) M5.EPD.Clear(true);
    canvas.createCanvas(960, 540);    

    // I did try the SD card but couldn't get it to work :( 
    canvas.loadFont("/liquid-crystal.ttf", SPIFFS); // Load font files internal flash
    
    Serial.print("M5-Clock started.");
}


void load_persistent_data(void)
{
    // Load our 3 state variables from NVS
    nvs_handle nvs_arg;
    nvs_open("M5-Clock", NVS_READONLY, &nvs_arg);
    nvs_get_i16(nvs_arg, "low_temp", &low_temp);
    nvs_get_i16(nvs_arg, "high_temp", &high_temp);
    nvs_get_i16(nvs_arg, "outdoor_temp", &outdoor_temp);
    nvs_close(nvs_arg);
}

void save_persistent_data(void)
{
    // Save our 3 state variables - used sparingly to avoid wear on the flash
    nvs_handle nvs_arg;
    nvs_open("M5-Clock", NVS_READWRITE, &nvs_arg);
    nvs_set_i16(nvs_arg, "low_temp", low_temp);
    nvs_set_i16(nvs_arg, "high_temp", high_temp);
    nvs_set_i16(nvs_arg, "outdoor_temp", outdoor_temp);
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

void render_time()
{
    // Display Time
    char timeString[16];
    char dateString[64];
    
    M5.RTC.getTime(&current_time);

    sprintf(timeString, " %02d:%02d ", current_time.hour, current_time.min);

    // createRender appears to be necessary to generate the font once for each point size - some docs would be nice.
    canvas.createRender(120);
    canvas.setTextSize(120);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(timeString, 480, 180);

    // Display Date
    rtc_date_t current_date;
    M5.RTC.getDate(&current_date);

    char * day = days[dayofweek(current_date.year, current_date.mon, current_date.day)];

    sprintf(dateString, "  %s %2dth %s %4d  ", day, current_date.day, month[current_date.mon-1], current_date.year);
    canvas.createRender(48);
    canvas.setTextSize(48);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(dateString, 480, 320);

}


void sleep_for_a_minute()
{
    /*
     * I tried the shutdown(&rtc_time_t) but it doesn't seem to work - please can we have some docs and more examples
     * 
    current_time.sec = 0;
    current_time.min++;
    if (current_time.min == 60)
    {
       current_time.min = 0;
       current_time.hour++;
       if (current_time.hour == 24)
       {
          current_time.hour == 0; 
       }
    }
    */
    
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
        return 1;
    }
    log_d("Time Sync failed");
    return 0;
}


void get_weather()
{
    char url[512];
    
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

        int16_t new_outdoor_temp = (int16_t) (outside_temp * 10.0);
        if (new_outdoor_temp != outdoor_temp) 
        {
           outdoor_temp = new_outdoor_temp;
           nvr_save = 1;  
        }

        // Construct the weather image URL
        strcpy(url, "https:");
        strcat(url, image);
        Serial.printf("Weather image: %s\n", url);
        
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
        
        Serial.printf("Weather image: %s\n", url);
        canvas.drawPngUrl(url, 160, 420, 128, 128);  
        
      }
      else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
      }
      
      // Free resources
      http.end();
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

    uint32_t battery = M5.getBatteryVoltage();
    if (battery < 3200) strcpy(msg, "Chrg");
    else sprintf(msg, "%1.1fv", (float) battery / 1000.0);

    if ((low_temp == 0) && (high_temp == 0)) 
    {
      load_persistent_data(); 
    }

    int16_t int_temp = (int16_t) (tem * 10.0);

    if (((low_temp == 0) && (high_temp == 0)) || ((current_time.hour == 0) && (current_time.min == 0))) // Load NVR failed or new day
    {
      low_temp = int_temp;
      high_temp = int_temp;
      nvr_save = 1; 
    }

    if (int_temp > high_temp) 
    {
       high_temp = int_temp;
       nvr_save = 1;
    }

    if (int_temp < low_temp) 
    {
       low_temp = int_temp;
       nvr_save = 1;
    }

    
    canvas.createRender(32);
    canvas.setTextSize(32);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(msg, 920, 5);

    sprintf(msg, "%2.1fC", tem);
    canvas.setTextSize(48);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(msg, 70, 5);

    // High and low temps
    sprintf(msg, "%2.1fC  %2.1fC", (float) low_temp / 10.0, (float) high_temp / 10.0);
    canvas.setTextSize(32);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(msg, 240, 10);

    // Update the weather every 15 minutes
    if (current_time.min % 15 == 0)
    {
       // get_weather will be slow so push what we have for now
       canvas.pushCanvas(0,0,UPDATE_MODE_A2); 
       get_weather();
    }

    sprintf(msg, "%2.1fC", (float) outdoor_temp / 10.0);
    canvas.setTextSize(48);
    canvas.setTextDatum(TC_DATUM);
    canvas.drawString(msg, 70, 480);

    if (nvr_save) save_persistent_data();
    canvas.pushCanvas(0,0,UPDATE_MODE_GC16);
    
    delay(500);

    sleep_for_a_minute();
    
}
