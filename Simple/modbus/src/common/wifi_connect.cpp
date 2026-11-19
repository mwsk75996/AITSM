#include "wifi_connect.h"

#include <Arduino.h>
#include <WiFi.h>

#include "modbus_map.h"

void wifi_connect(const char *hostname, const char *ssid, const char *password,
		  IPAddress static_ip)
{
	WiFi.mode(WiFi.getMode() == WIFI_AP ? WIFI_AP_STA : WIFI_STA);
	WiFi.setHostname(hostname);
	WiFi.setAutoReconnect(true);
	if (static_ip != IPAddress()) {
		IPAddress gateway(MODBUS_MASTER_IP);
		IPAddress netmask(MODBUS_NETMASK);

		WiFi.config(static_ip, gateway, netmask);
	}
	WiFi.begin(ssid, password);

	Serial.printf("Forbinder til WiFi \"%s\"", ssid);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print('.');
	}
	/* Modem sleep delays incoming Modbus requests; turn it off once connected. */
	WiFi.setSleep(false);
	Serial.printf("\nWiFi forbundet, IP %s, kanal %d, RSSI %d dBm\n",
		      WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
}

void wifi_start_access_point(const char *ssid, const char *password)
{
	IPAddress address(MODBUS_MASTER_IP);
	IPAddress netmask(MODBUS_NETMASK);

	WiFi.mode(WiFi.getMode() == WIFI_STA ? WIFI_AP_STA : WIFI_AP);
	WiFi.softAPConfig(address, address, netmask);
	if (!WiFi.softAP(ssid, password, WiFi.channel())) {
		Serial.println("Kunne ikke starte access point");
		return;
	}
	Serial.printf("Access point \"%s\" startet, IP %s, kanal %d\n", ssid,
		      WiFi.softAPIP().toString().c_str(), WiFi.channel());
}
