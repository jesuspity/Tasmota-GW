#!/usr/bin/env python3
"""
Simulador de queda de internet para o gateway Tasmota + gravador de log.

- Proxy TCP: o gateway conecta em <IP do Mac>:8883 e o proxy repassa para o broker real.
- Modo "pass": tudo normal.
- Modo "freeze": simula internet caida. Conexoes abertas ficam mudas (nada passa) e
  novas conexoes sao aceitas mas nunca respondidas (o TLS fica esperando ate o timeout).
- Gravador: le o console web do gateway (/cs) e o Status 11 a cada 2 s e grava em arquivo,
  funciona mesmo com a internet fora (so precisa da rede local).

Controle (no proprio Mac):
  curl http://127.0.0.1:8899/pass
  curl http://127.0.0.1:8899/freeze
  curl http://127.0.0.1:8899/status
"""
import socket, threading, time, sys, json, urllib.request, urllib.parse, http.server, datetime, os

BROKER = ("mqtt.projekteplus.com", 8883)
LISTEN = ("0.0.0.0", 8883)
CTRL = ("127.0.0.1", 8899)
GW = sys.argv[1] if len(sys.argv) > 1 else "192.168.3.185"
LOGFILE = sys.argv[2] if len(sys.argv) > 2 else "gateway_log.txt"

mode = "pass"
lock = threading.Lock()
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def log(msg):
    line = f"{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]} {msg}"
    with lock:
        with open(LOGFILE, "a") as f:
            f.write(line + "\n")
    print(line, flush=True)


def pipe(src, dst, tag):
    try:
        while True:
            data = src.recv(4096)
            if not data:
                break
            while mode == "freeze":       # internet "caiu": segura os dados, nao repassa
                time.sleep(0.2)
            dst.sendall(data)
    except Exception:
        pass
    for s in (src, dst):
        try:
            s.close()
        except Exception:
            pass


def handle(client, addr):
    if mode == "freeze":
        log(f"PROXY nova conexao de {addr[0]} durante FREEZE (sera ignorada)")
        while mode == "freeze":
            time.sleep(0.2)
        client.close()
        return
    try:
        upstream = socket.create_connection(BROKER, timeout=10)
        upstream.settimeout(None)
    except Exception as e:
        log(f"PROXY falha ao conectar no broker: {e}")
        client.close()
        return
    log(f"PROXY conexao {addr[0]} -> broker aberta")
    threading.Thread(target=pipe, args=(client, upstream, "up"), daemon=True).start()
    threading.Thread(target=pipe, args=(upstream, client, "down"), daemon=True).start()


def proxy_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(LISTEN)
    srv.listen(5)
    while True:
        c, a = srv.accept()
        threading.Thread(target=handle, args=(c, a), daemon=True).start()


class Ctrl(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global mode
        p = self.path.strip("/")
        if p in ("pass", "freeze"):
            mode = p
            log(f"===== MODO {mode.upper()} =====")
        self.send_response(200)
        self.end_headers()
        self.wfile.write((mode + "\n").encode())

    def log_message(self, *a):
        pass


def gw_get(path, timeout=4):
    with opener.open(f"http://{GW}{path}", timeout=timeout) as r:
        return r.read().decode(errors="replace")


def recorder():
    # Le so o console web (/cs): nao gera publicacoes MQTT extras no gateway
    idx = 0
    offline = False
    while True:
        try:
            txt = gw_get(f"/cs?c2={idx}")
            if offline:
                log("REC gateway voltou a responder")
                offline = False
            parts = txt.split("}1", 2)
            if len(parts) == 3 and parts[0].isdigit():
                new_idx = int(parts[0])
                if new_idx < idx:
                    log(f"***** INDICE DO CONSOLE VOLTOU ({idx} -> {new_idx}): REINICIO PROVAVEL *****")
                for ln in parts[2].replace("}1", "").splitlines():
                    if ln.strip():
                        log("GW  " + ln)
                idx = new_idx
        except Exception as e:
            if not offline:
                log(f"REC gateway sem resposta: {type(e).__name__}")
                offline = True
        time.sleep(1)


if __name__ == "__main__":
    log(f"Iniciando simulador. Gateway {GW}, broker {BROKER}, log {os.path.abspath(LOGFILE)}")
    threading.Thread(target=proxy_server, daemon=True).start()
    threading.Thread(target=recorder, daemon=True).start()
    http.server.ThreadingHTTPServer(CTRL, Ctrl).serve_forever()
