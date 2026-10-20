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
# Las sondas temporales son sentencias independientes; comparar la logica sin ellas.
after = re.sub(r"^[ \t]*PERF_[^\n]*\n", "", after, flags=re.M)
def without_blank_lines(text):
    return "\n".join(line for line in text.splitlines() if line.strip())
functions = ["horaValidaParaTLS", "configurarTLS", "enviarTelegram", "actualizarClimaApi",
             "actualizarFiltro", "actualizarHistorial",
             "obtenerFechaHora", "urlEncode", "fechaFutura", "calcularLunas", "faseLunar", "faseNombre",
             "faseIcono", "stopRainWindow", "startRainWindow", "disableRainPattern", "enableRainPattern",
             "actualizarPatronLluvia", "actualizarLedRain", "obtenerNombreCSV",
             "imprimirClimaExterior"]
for name in functions:
    assert without_blank_lines(function(before, name)) == without_blank_lines(function(after, name)), f"Funcion modificada: {name}"
for filename in ["weather_json.h", "tls_certificates.h"]:
    path = "ESP8266_DHT22/" + filename
    assert original(path) == (root/path).read_text(encoding="utf-8"), filename
# El servidor original, salvo los cambios DHT, CSV y grafico de temperatura autorizados.
# leerDHT y sus consumidores se verifican ejecutando el C++ real en dht_history_test.py.
anchor = "  WiFiClient client = server.available();"
expected_server = before.split(anchor, 1)[1]
clear_begin, clear_end = "  // /clearcsv", "  // /csvinfo"
def without_clear_endpoint(text):
    return text.split(clear_begin, 1)[0] + clear_begin + clear_end + text.split(clear_end, 1)[1]
expected_server = without_clear_endpoint(expected_server)
start = expected_server.index("        let bufferTemp = [];")
end = expected_server.index("        function crearGraficos()", start)
expected_server = expected_server[:start] + expected_server[end:]
start = expected_server.index("                    bufferTemp.push(d.temp);")
end = expected_server.index("                    agregarPunto(tempChart", start)
expected_server = expected_server[:start] + expected_server[end:]
expected_server = expected_server.replace("tempSmoothed", "d.temp").replace("humSmoothed", "d.hum")
actual_server = without_clear_endpoint(after.split(anchor, 1)[1])
actual_server = actual_server.replace(
    "function " + function(actual_server, "actualizarEscalaTemperatura"), "")
expected_server = expected_server.replace(
    "                    tempChart.options.scales.y.min = d.temp - 0.5;\n"
    "                    tempChart.options.scales.y.max = d.temp + 0.5;",
    "                    ESCALA_TEMPORAL();")
actual_server = actual_server.replace("                    actualizarEscalaTemperatura();",
                                      "                    ESCALA_TEMPORAL();")
def without_temp_chart(text):
    start = "            tempChart = new Chart(tctx, {"
    end = "            humChart = new Chart(hctx, {"
    return text.split(start, 1)[0] + "            GRAFICO_TEMPORAL;\n" + end + text.split(end, 1)[1]
assert without_blank_lines(without_temp_chart(expected_server)) == without_blank_lines(
    without_temp_chart(actual_server)), "Cambio no autorizado en servidor/interfaz"
# Solo se permiten los dos parametros de adquisicion solicitados.
anchor = "const uint16_t BRILLO_LLUVIA"
expected_constants = before.split(anchor, 1)[1].split("WiFiServer server(80);", 1)[0]
expected_constants = expected_constants.replace("// Variables globales (valor crudo promediado)", "// Variables globales (ultima lectura valida con offsets)")
expected_constants = expected_constants.replace("ALPHA_FILTRO = 0.2;", "ALPHA_FILTRO = 0.5;")
expected_constants = expected_constants.replace("10000UL;  // 10 segundos", "2500UL;   // 2,5 segundos (DHT: minimo 2 s)")
assert expected_constants == after.split(anchor, 1)[1].split("WiFiServer server(80);", 1)[0]
assert "wifiProvisioning.begin(WIFI_CONFIG_AP_PASSWORD, ssid, password);" in after
assert "WiFi.begin(ssid, password)" not in after
assert subprocess.check_output(["git", "ls-files", "--", "ESP8266_DHT22/secrets.h"], cwd=root) == b""
subprocess.run(["git", "check-ignore", "-q", "ESP8266_DHT22/secrets.h"], cwd=root, check=True)
print(f"PASS: {len(functions)} funciones originales, certificados, parser JSON y umbrales intactos; servidor/interfaz con cambios DHT, CSV y escala de temperatura autorizados; secrets.h ignorado.")
