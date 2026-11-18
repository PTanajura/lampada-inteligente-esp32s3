import os
import sys
import socket
import logging
import sqlite3
import requests
from datetime import datetime

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)

# Força resolução de DNS apenas em IPv4
_orig_getaddrinfo = socket.getaddrinfo
def _ipv4_only_getaddrinfo(host, port, family=0, type=0, proto=0, flags=0):
    return _orig_getaddrinfo(host, port, socket.AF_INET, type, proto, flags)
socket.getaddrinfo = _ipv4_only_getaddrinfo

from arduino_iot_cloud import ArduinoCloudClient

# Remove variáveis de proxy residuais apenas durante a execução
for var in ("HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy", "ALL_PROXY", "all_proxy"):
    os.environ.pop(var, None)

DB_FILE = "telemetria.db"
API_LOCAL_COMANDO = "http://localhost:8000/api/comando"

# ==========================================================
# MANTENHA AQUI O DEVICE_ID E A SECRET_KEY DO DISPOSITIVO MANUAL
# ==========================================================
DEVICE_ID  = "SEU_DEVICE_ID_AQUI".strip()
SECRET_KEY = "SUA_SECRET_KEY_AQUI".strip()

ultimo_id_sincronizado = None


def ler_ultimo_registro_sqlite():
    try:
        conn = sqlite3.connect(DB_FILE, timeout=10.0)
        conn.row_factory = sqlite3.Row
        row = conn.execute(
            "SELECT * FROM leitura_sensores ORDER BY id DESC LIMIT 1"
        ).fetchone()
        conn.close()
        return dict(row) if row else None
    except sqlite3.OperationalError:
        return None


def on_botao_cloud_change(client, value):
    cmd = "Ligar" if value else "Desligar"
    try:
        requests.post(API_LOCAL_COMANDO, json={"comando": cmd}, timeout=5)
        print(f"[CLOUD -> ESP32] Botão Dashboard alterado para: {cmd}")
    except Exception as e:
        print(f"[ERRO API LOCAL] {e}")


def on_comando_cloud_change(client, value):
    if value:
        try:
            requests.post(API_LOCAL_COMANDO, json={"comando": str(value)}, timeout=5)
            print(f"[CLOUD -> ESP32] Comando repassado: {value}")
        except Exception as e:
            print(f"[ERRO API LOCAL] {e}")


def sincronizar_sqlite_para_cloud(client, value=None):
    global ultimo_id_sincronizado
    dados = ler_ultimo_registro_sqlite()

    if dados and dados["id"] != ultimo_id_sincronizado:
        client["cloudTemperatura"]  = float(dados["temperatura"])
        client["cloudLuminosidade"] = int(dados["luminosidade"])
        client["cloudEstadoLed"]    = str(dados["cor_led"])
        client["cloudBotao"]        = bool(dados["botao"])

        ultimo_id_sincronizado = dados["id"]
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Registro #{ultimo_id_sincronizado} enviado ao Arduino IoT Cloud!")


if __name__ == "__main__":
    if len(DEVICE_ID) != 36 or "SEU_DEVICE_ID" in DEVICE_ID:
        print(f"[ERRO] DEVICE_ID inválido ({len(DEVICE_ID)} caracteres). Deve ter 36 caracteres (UUID).")
        sys.exit(1)

    if not SECRET_KEY or "SUA_SECRET_KEY" in SECRET_KEY:
        print("[ERRO] Preencha a SECRET_KEY corretamente.")
        sys.exit(1)

    print("Conectando SQLite ao Arduino IoT Cloud (porta 8884)...")
    client = ArduinoCloudClient(
        device_id=DEVICE_ID,
        username=DEVICE_ID,
        password=SECRET_KEY,
        server="mqtts-up.iot.arduino.cc",
        port=8884,
    )

    client.register("cloudTemperatura", value=0.0)
    client.register("cloudLuminosidade", value=0)
    client.register("cloudEstadoLed", value="Desligado")
    client.register("cloudBotao", value=True, on_write=on_botao_cloud_change)
    client.register("cloudComando", value="", on_write=on_comando_cloud_change)

    client.register("sync_sqlite", on_run=sincronizar_sqlite_para_cloud, interval=3.0)

    client.start()