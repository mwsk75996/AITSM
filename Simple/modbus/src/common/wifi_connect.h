#pragma once

#include <IPAddress.h>

/*
 * Join the WiFi ssid as a station and block until connected. With a static_ip
 * the address is fixed instead of coming from DHCP. The ESP32 reconnects by
 * itself if the connection drops later.
 */
void wifi_connect(const char *hostname, const char *ssid, const char *password,
		  IPAddress static_ip = IPAddress());

/*
 * Start an access point next to the station connection. It shares the radio,
 * so it always runs on the same channel as the network joined with
 * wifi_connect().
 */
void wifi_start_access_point(const char *ssid, const char *password);
