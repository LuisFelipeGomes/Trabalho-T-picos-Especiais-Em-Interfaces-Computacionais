#!/usr/bin/env python3
"""
=====================================================================
ReDuino-Counter — Backend Python (Flask)
Contador de pessoas via Gateway Arduino (Serial USB)
=====================================================================

Substitui o backend Node.js (index.js) mantendo os MESMOS endpoints,
para que o gateway.ino e o frontend não precisem mudar nada.

Instalar dependências:
    pip install -r requirements.txt

Configurar .env (copie de .env.example):
    SERIAL_PORT=/dev/ttyUSB0   (Linux) ou COM3 (Windows)
    BAUD_RATE=115200
    PORT=3001
    FRONTEND_URL=http://localhost:3000

Endpoints:
    GET  /api/status     -> estado atual (ocupação, entradas, saídas)
    GET  /api/historico  -> últimas 100 leituras completas
    GET  /api/eventos    -> log de eventos (entrada/saída individuais)
    POST /api/reset      -> zera contadores (envia "RESET\n" ao gateway)
    GET  /api/health     -> healthcheck

Formato JSON recebido do gateway.ino via Serial:
    {"entradas":N,"saidas":N,"ocupacao":N,"distA":N,"distB":N,"pktOK":N,"pktErr":N}
    {"evento":"entrada","total":N}
    {"evento":"saida","total":N}
    {"status":"..."}
=====================================================================
"""

import json
import os
import threading
import time
from collections import deque
from datetime import datetime, timezone

import serial
from dotenv import load_dotenv
from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS

load_dotenv()

SERIAL_PORT = os.environ.get("SERIAL_PORT", "/dev/ttyUSB0")
BAUD_RATE = int(os.environ.get("BAUD_RATE", "115200"))
PORT = int(os.environ.get("PORT", "3001"))
FRONTEND_URL = os.environ.get("FRONTEND_URL", "*")

MAX_HISTORICO = 100
MAX_EVENTOS = 200
RECONNECT_DELAY_S = 5

app = Flask(__name__)
CORS(app, origins=FRONTEND_URL if FRONTEND_URL != "*" else "*")

# ── Estado global (protegido por lock, pois a thread serial escreve
#    e as rotas Flask leem ao mesmo tempo) ──────────────────────────
lock = threading.Lock()

estado = {
    "entradas": 0,
    "saidas": 0,
    "ocupacao": 0,
    "distA": None,
    "distB": None,
    "pktOK": 0,
    "pktErr": 0,
    "ultimaLeitura": None,
    "serialConectada": False,
}

historico = deque(maxlen=MAX_HISTORICO)
eventos = deque(maxlen=MAX_EVENTOS)

ser = None  # objeto pyserial ativo


def agora_iso():
    return datetime.now(timezone.utc).isoformat()


# ── Processa uma linha JSON recebida do gateway ─────────────────────
def processa_mensagem(msg):
    ts = agora_iso()

    with lock:
        # Status completo (enviado a cada ciclo de transação)
        if "entradas" in msg and "saidas" in msg:
            estado["entradas"] = msg["entradas"]
            estado["saidas"] = msg["saidas"]
            estado["ocupacao"] = max(0, msg["entradas"] - msg["saidas"])
            estado["distA"] = msg.get("distA")
            estado["distB"] = msg.get("distB")
            estado["pktOK"] = msg.get("pktOK", estado["pktOK"])
            estado["pktErr"] = msg.get("pktErr", estado["pktErr"])
            estado["ultimaLeitura"] = ts

            historico.append({**estado, "ts": ts})
            print(
                f"[Status] entradas={estado['entradas']} "
                f"saidas={estado['saidas']} ocupacao={estado['ocupacao']}"
            )

        # Evento pontual de entrada ou saída
        if msg.get("evento") in ("entrada", "saida"):
            eventos.append({"tipo": msg["evento"], "total": msg.get("total"), "ts": ts})
            print(f"[Evento] {msg['evento'].upper()} total={msg.get('total')}")

        # Mensagens de status do gateway (boot, erro de rádio, etc.)
        if "status" in msg:
            print(f"[Gateway] {msg['status']}")
        if "erro" in msg:
            print(f"[Gateway][ERRO] {msg['erro']}")


def _detecta_porta():
    """Lê 3 s de cada porta e retorna a que mandar JSON (receptor)."""
    import glob
    if SERIAL_PORT != "auto":
        return SERIAL_PORT
    candidatos = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    print(f"[Serial] Procurando receptor em: {candidatos}")
    for p in candidatos:
        try:
            s = serial.Serial(p, BAUD_RATE, timeout=0.3)
            deadline = time.time() + 3.0
            achou = False
            while time.time() < deadline:
                linha = s.readline().decode("utf-8", errors="ignore").strip()
                if linha.startswith("{"):
                    achou = True
                    break
            s.close()
            if achou:
                print(f"[Serial] Receptor encontrado em {p}")
                return p
        except (serial.SerialException, OSError):
            continue
    print("[Serial] Receptor nao encontrado, usando primeira porta disponivel")
    return candidatos[0] if candidatos else "/dev/ttyUSB0"


# ── Thread de leitura contínua da porta serial ──────────────────────
def loop_serial():
    global ser
    while True:
        porta = _detecta_porta()
        try:
            ser = serial.Serial(porta, BAUD_RATE, timeout=1)
            print(f"[Serial] Conectado em {porta} @ {BAUD_RATE} baud")
            with lock:
                estado["serialConectada"] = True

            while True:
                raw = ser.readline()
                if not raw:
                    continue
                linha = raw.decode("utf-8", errors="ignore").strip()
                if not linha.startswith("{"):
                    continue  # ignora linhas de debug do Arduino
                try:
                    msg = json.loads(linha)
                    processa_mensagem(msg)
                except json.JSONDecodeError:
                    continue

        except (serial.SerialException, FileNotFoundError, OSError) as e:
            print(f"[Serial] Erro ao abrir {porta}: {e}")
            print(f"[Serial] Tentando reconectar em {RECONNECT_DELAY_S}s...")
            with lock:
                estado["serialConectada"] = False
            ser = None
            time.sleep(RECONNECT_DELAY_S)


# ── Envia comando ao gateway via Serial ──────────────────────────────
def envia_cmd_serial(cmd: str) -> bool:
    global ser
    if ser is not None and ser.is_open:
        try:
            ser.write((cmd + "\n").encode("utf-8"))
            return True
        except serial.SerialException as e:
            print(f"[Serial] Erro ao enviar cmd: {e}")
            return False
    return False


# ── Endpoints HTTP (mesmos contratos do index.js) ────────────────────

@app.get("/")
def index():
    return send_from_directory(".", "index.html")


@app.get("/api/status")
def get_status():
    with lock:
        resp = {**estado, "ts": agora_iso()}
    return jsonify(resp)


@app.get("/api/historico")
def get_historico():
    with lock:
        dados = list(historico)
    return jsonify(list(reversed(dados)))  # mais recente primeiro


@app.get("/api/eventos")
def get_eventos():
    with lock:
        dados = list(eventos)
    return jsonify(list(reversed(dados)))


@app.post("/api/reset")
def post_reset():
    enviou = envia_cmd_serial("RESET")
    with lock:
        estado["entradas"] = 0
        estado["saidas"] = 0
        estado["ocupacao"] = 0
        estado["pktOK"] = 0
        estado["pktErr"] = 0
        historico.clear()
        eventos.clear()
    print("[Reset] Contadores zerados")
    return jsonify({"ok": True, "gatewayNotificado": enviou})


@app.get("/api/health")
def get_health():
    with lock:
        conectada = estado["serialConectada"]
    return jsonify({
        "ok": True,
        "serialConectada": conectada,
        "porta": SERIAL_PORT,
        "uptime": time.process_time(),
    })


if __name__ == "__main__":
    t = threading.Thread(target=loop_serial, daemon=True)
    t.start()
    print(f"[Server] Rodando em http://localhost:{PORT}")
    app.run(host="0.0.0.0", port=PORT, threaded=True)
