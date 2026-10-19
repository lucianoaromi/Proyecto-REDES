#pragma once
#include <Arduino.h>

// Copiá este archivo como "secrets.h" y completá tus valores reales.
// "secrets.h" está ignorado por Git y no debe subirse al repositorio.

// Arranque: primero usa la red guardada en flash; si no existe, intenta estos valores.
const char* ssid = "TU_RED_WIFI";
const char* password = "TU_PASSWORD_WIFI";

const char* WEATHER_API_KEY = "TU_WEATHER_API_KEY";

const String TELEGRAM_TOKEN = "TU_TELEGRAM_BOT_TOKEN";
const String CHAT_ID = "TU_TELEGRAM_CHAT_ID";

// Prototipo WiFiManager: clave AP propia de 12 a 63 caracteres ASCII.
// Vacia: el portal se rechaza; nunca se crea un AP abierto.
const char* WIFI_CONFIG_AP_PASSWORD = "";
