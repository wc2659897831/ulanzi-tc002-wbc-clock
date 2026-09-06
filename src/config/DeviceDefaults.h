#ifndef CONFIG_DEVICE_DEFAULTS_H_
#define CONFIG_DEVICE_DEFAULTS_H_

namespace device_defaults {

static const char WEB_CONFIG_PATH[] = "/mnt/usb1/tc002-settings.json";

static const int BRIGHTNESS = 100;
static const int LOW_BRIGHTNESS = 50;
static const int MID_BRIGHTNESS = 80;
static const int HIGH_BRIGHTNESS = 100;
static const int VOLUME = 3;
static const int CAROUSEL_SPEED = 60;
static const int SCROLL_SPEED = 7;
static const char TIMEZONE[] = "UTC+8";
static const char DATE_FORMAT[] = "MM/DD";
static const bool SHOW_WEEK = true;
static const int WEEK_START = 1;
static const bool LOW_BATTERY_AUTO_SLEEP = false;

static const char WIFI_SSID[] = "CMCC-hDFQ-5G";
static const char WIFI_PASSWORD[] = "usz7yuud";
static const bool WIFI_CONNECT = true;

}

#endif
