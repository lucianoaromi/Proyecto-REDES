"""Comparacion offline del prototipo con el commit de partida; no ejecuta red."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
base = "53d8c0f"
def original(path):
    return subprocess.check_output(["git", "show", f"{base}:{path}"], cwd=root).decode("utf-8").replace("\r\n", "\n")

def function(source, name):
    match = re.search(r"\b" + name + r"\([^;{}]*\)\s*\{", source)
    assert match, name
    start = match.start()
    tokens = re.compile(r'R"rawliteral\([\s\S]*?\)rawliteral"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[{}]')
    depth = 0
    for token in tokens.finditer(source, match.end() - 1):
        if token.group() == "{": depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0: return source[start:token.end()]
    raise AssertionError(name)

path = "ESP8266_DHT22/ESP8266_DHT22.ino"
before, after = original(path), (root/path).read_text(encoding="utf-8")
functions = ["horaValidaParaTLS", "configurarTLS", "enviarTelegram", "actualizarClimaApi",
             "leerDHT", "actualizarFiltro", "iniciarLog", "agregarRegistroLog", "actualizarHistorial",
             "obtenerFechaHora", "urlEncode", "fechaFutura", "calcularLunas", "faseLunar", "faseNombre",
             "faseIcono", "stopRainWindow", "startRainWindow", "disableRainPattern", "enableRainPattern",
             "actualizarPatronLluvia", "actualizarLedRain", "obtenerNombreCSV", "obtenerFechasCSV",
             "imprimirClimaExterior"]
for name in functions:
    assert function(before, name) == function(after, name), f"Funcion modificada: {name}"
for filename in ["weather_json.h", "tls_certificates.h"]:
    path = "ESP8266_DHT22/" + filename
    assert original(path) == (root/path).read_text(encoding="utf-8"), filename
# Todo el servidor original, incluyendo HTML en PROGMEM, JS, CSS y rutas CSV.
anchor = "  WiFiClient client = server.available();"
assert before.split(anchor, 1)[1] == after.split(anchor, 1)[1]
# Los intervalos, umbrales, pines, tablas y constantes previos al servidor permanecen iguales.
anchor = "const uint16_t BRILLO_LLUVIA"
assert before.split(anchor, 1)[1].split("WiFiServer server(80);", 1)[0] == after.split(anchor, 1)[1].split("WiFiServer server(80);", 1)[0]
assert "wifiProvisioning.begin(WIFI_CONFIG_AP_PASSWORD, ssid, password);" in after
assert "WiFi.begin(ssid, password)" not in after
assert subprocess.check_output(["git", "ls-files", "--", "ESP8266_DHT22/secrets.h"], cwd=root) == b""
subprocess.run(["git", "check-ignore", "-q", "ESP8266_DHT22/secrets.h"], cwd=root, check=True)
print(f"PASS: {len(functions)} funciones originales, certificados, parser JSON, umbrales, interfaz y servidor intactos; secrets.h ignorado.")
