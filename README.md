# M5-Clock
Simple Clock App for M5Stack's M5Paper

There are very few apps and no documentation for this promising bit of hardware so I wanted to share this. Here's the features that it uses :-
* TTF font loading (from SPIFFS as I couldn't get SD loading working),
* RTC get time and syncing to NTP,
* RTC deep sleep / M5.shutdown(s) (it's more like a shutdown with timed wake-up),
* NVS storage of variables across sleep,
* HTTP API call to fetch weather,
* Loading and displaying a weather image. 

Here's what I discovered :-
* You can't use Serial and update the EPD at the same time !!
* Can't get the SD card to work at all 
* M5.shutdown(s) doesn't work when the USB cable is connected
* Shutdown restarts the sketch from scratch i.e. all state lost and runs setup() 

Have fun !
