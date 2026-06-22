#include "Config.h"
#include <Preferences.h>

String app_wifi_ssid = "S3_GPS_Tracker";
String app_wifi_pass = "s3gpspassword";
String app_server_ip = "your-server.com";
int app_server_port = 5015;
int app_report_interval = 10;

Preferences preferences;

void loadConfig() {
    preferences.begin("s3gps", false);
    app_wifi_ssid = preferences.getString("wifi_ssid", "S3_GPS_Tracker");
    app_wifi_pass = preferences.getString("wifi_pass", "s3gpspassword");
    app_server_ip = preferences.getString("server_ip", "your-server.com");
    app_server_port = preferences.getInt("server_port", 5015);
    app_report_interval = preferences.getInt("interval", 10);
    preferences.end();
}

void saveConfig() {
    preferences.begin("s3gps", false);
    preferences.putString("wifi_ssid", app_wifi_ssid);
    preferences.putString("wifi_pass", app_wifi_pass);
    preferences.putString("server_ip", app_server_ip);
    preferences.putInt("server_port", app_server_port);
    preferences.putInt("interval", app_report_interval);
    preferences.end();
}
