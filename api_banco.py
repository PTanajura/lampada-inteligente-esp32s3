import sqlite3
from collections import deque
from datetime import datetime
from fastapi import FastAPI
from pydantic import BaseModel
import uvicorn

DB_FILE = "telemetria.db"
app = FastAPI(title="API Lâmpada Inteligente IoT")

# Fila FIFO de comandos pendentes para o ESP32
fila_comandos = deque()


class TelemetriaIn(BaseModel):
    temperatura: float
    luminosidade: int
    cor_led: str
    botao: bool


class ComandoIn(BaseModel):
    comando: str


def init_db():
    conn = sqlite3.connect(DB_FILE)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS leitura_sensores (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            temperatura REAL NOT NULL,
            luminosidade INTEGER NOT NULL,
            cor_led TEXT NOT NULL,
            botao INTEGER NOT NULL,
            data_hora TEXT NOT NULL
        )
    """)
    conn.commit()
    conn.close()


@app.on_event("startup")
def startup():
    init_db()


@app.post("/api/telemetria")
def receber_telemetria(dados: TelemetriaIn):
    agora = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    conn = sqlite3.connect(DB_FILE, timeout=10.0)
    conn.execute(
        """
        INSERT INTO leitura_sensores (temperatura, luminosidade, cor_led, botao, data_hora)
        VALUES (?, ?, ?, ?, ?)
        """,
        (dados.temperatura, dados.luminosidade, dados.cor_led, int(dados.botao), agora),
    )
    conn.commit()
    conn.close()
    return {"status": "ok", "timestamp": agora}


@app.post("/api/comando")
def enfileirar_comando(dados: ComandoIn):
    cmd = dados.comando.strip()
    if cmd:
        fila_comandos.append(cmd)
    return {"status": "enfileirado", "comando": cmd}


@app.get("/api/comando")
def consumir_comando():
    if fila_comandos:
        return {"comando": fila_comandos.popleft()}
    return {"comando": ""}


@app.get("/api/historico")
def listar_historico(limite: int = 20):
    conn = sqlite3.connect(DB_FILE, timeout=10.0)
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        "SELECT * FROM leitura_sensores ORDER BY id DESC LIMIT ?", (limite,)
    ).fetchall()
    conn.close()
    return [dict(r) for r in rows]


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)