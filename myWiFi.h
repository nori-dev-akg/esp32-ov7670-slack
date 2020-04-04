#include <WiFi.h>
#include <HTTPClient.h>

#define WIFI_SSID       "your-ssid-name"
#define WIFI_PASSWORD   "your-ssid-password"
#define JST 3600 * 9

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

//const IPAddress gw(192, 168, 0, 1);
//const IPAddress subnet(255, 255, 255, 0);
//const IPAddress dns(192, 168, 0, 1);
//const IPAddress ip(192,168,0,63);

bool connectWiFi()
{
  Serial.print("WiFi connecting");
//  WiFi.config(ip, gw, subnet, dns);

  for (int j = 0; j < 3 && WiFi.status() != WL_CONNECTED; j++)
  {
	  WiFi.begin();
	  for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++)
	  {
	    delay(500);
	    Serial.print(".");
	  }
	  if (WiFi.status() != WL_CONNECTED)
	  {
	    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
	    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++)
	    {
	      delay(100);
	      Serial.print(",");
	    }
	  }
	  delay(3000);
	}

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("WiFi cannot connecting");
    return false;
  }

  Serial.print(" connected: ");
  Serial.println(WiFi.localIP());

  configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

  return true;
}

bool disconnectWiFi()
{
  // disconnect WiFi
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect(true);
    Serial.println("WiFi.disconnect");
  }
}
