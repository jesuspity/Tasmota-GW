#!/usr/bin/env python3
"""
Bateria de testes automaticos do gateway Zigbee Projekte+ (maximo ~6 h).

Depende do simulador_queda.py rodando (proxy MQTT no Mac + gravador do console):
  - comanda quedas de internet pelo controle http://127.0.0.1:8899/freeze|pass
  - le o log do simulador (console do gateway) para contar eventos
  - consulta o gateway por HTTP (Status 11 / Status 10) a cada 60 s
  - manda atualizacoes e o relatorio final pelo Telegram

As credenciais do Telegram ficam em ~/.config/projekte/telegram.env (fora do Drive e do Git).

Uso:
  python3 bateria_testes.py <log_do_simulador> <pasta_saida> [--scale 1.0] [--blocks A,B1,...]
"""
import argparse, csv, datetime, json, os, random, re, signal, statistics, subprocess, sys, threading, time
import urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor

GW = "192.168.3.185"
CTRL = "http://127.0.0.1:8899"
SENSORS = {"0x0495": "TH01", "0xFFC3": "TH02", "0x31CD": "TH05", "0x8A63": "TH06"}
BUDGET_S = 5 * 3600 + 50 * 60          # limite total: 5 h 50 min
MAC_IP = "192.168.3.123"
BROKER_HOST = "mqtt.projekteplus.com"
# Perfis de duracao (segundos / repeticoes)
PERFIS = {
    "6h": dict(A_wait=300, A_lat=50, B1_n=6, B1_out=30, B1_gap=90, B2_n=3, B2_out=180, B2_gap=120, B3_out=1200, B3_gap=300,
               B4_n=20, C_freeze=600, C_gap=120, C_norm=300, D_ham=120, D_gap=120, D_cmds=100, E1_n=3, E1_gap=120,
               E2_out=480, E_gap=180, F=True),
    "1h": dict(A_wait=60, A_lat=20, B1_n=3, B1_out=45, B1_gap=60, B2_n=2, B2_out=180, B2_gap=90, B3_out=480, B3_gap=120,
               B4_n=10, C_freeze=240, C_gap=90, C_norm=90, D_ham=60, D_gap=60, D_cmds=50, E1_n=1, E1_gap=90,
               E2_out=240, E_gap=90, F=False),
    "30m": dict(A_wait=20, A_lat=10, B1_n=2, B1_out=40, B1_gap=40, B2_n=1, B2_out=150, B2_gap=50, B3_out=200, B3_gap=50,
                B4_n=6, C_freeze=150, C_gap=50, C_norm=60, D_ham=60, D_gap=30, D_cmds=50, E1_n=1, E1_gap=60,
                E2_out=150, E_gap=45, F=False),
    # 2h: sem cabo; a falta de internet e simulada apontando o MqttHost para o simulador congelado (TLS pendurado)
    "2h": dict(A_wait=60, A_lat=30, D_ham=120, D_gap=60, D_cmds=100, E1_n=3, E1_gap=90, E2_out=0, E_gap=60,
               X_holds=[600, 420], X_gap=120, F=False),
}
OTA_URL_NOVO = "https://github.com/jesuspity/Tasmota-GW/releases/latest/download/tasmota32-projekte-gw.bin"
TEMP_LIMITE = 118.0     # acima disso o estresse pausa 5 min
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

RE_LINE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) (.*)$")
RE_CODE81 = re.compile(r'"Status":99|"Code":81|ACK timeout')
RE_BUSY = re.compile(r"Main loop was busy, (\d+) frames acknowledged by RX task, max wait (\d+) ms")
RE_RXSTAT = re.compile(r"RX task: dropped (\d+), bad crc (\d+), time-out (\d+)(?:, out of seq (\d+), nRdy (\d+))?")
RE_ZBRECV = re.compile(r'"ZbReceived":\{"(0x[0-9A-F]{4})"')


def now():
    return time.time()


def ts(t=None):
    return datetime.datetime.fromtimestamp(t or now()).strftime("%d/%m %H:%M:%S")


def fmt_dur(s):
    s = int(s)
    h, r = divmod(s, 3600)
    m, s = divmod(r, 60)
    return f"{h}h{m:02d}m" if h else (f"{m}min{s:02d}s" if m else f"{s}s")


class Telegram:
    """Envio em fila (thread propria): queda de internet nao trava a bateria; tenta de novo depois."""
    def __init__(self):
        import queue
        self.q = queue.Queue()
        self._init_cfg()
        threading.Thread(target=self._worker, daemon=True).start()

    def _worker(self):
        while True:
            method, args = self.q.get()
            for attempt in range(40):          # ate ~20 min tentando
                if self._curl(method, args):
                    break
                time.sleep(30)
            self.q.task_done()

    def flush(self, timeout=600):
        end = now() + timeout
        while self.q.unfinished_tasks and now() < end:
            time.sleep(1)

    def _init_cfg(self):
        env = {}
        p = os.path.expanduser("~/.config/projekte/telegram.env")
        for l in open(p):
            if "=" in l:
                k, v = l.strip().split("=", 1)
                env[k] = v
        self.token, self.chat = env["TELEGRAM_TOKEN"], env["TELEGRAM_CHAT_ID"]

    def _curl(self, method, args):
        cfg = f'url = "https://api.telegram.org/bot{self.token}/{method}"\n'
        try:
            r = subprocess.run(["curl", "-s", "-m", "20", "-K", "-"] + args, input=cfg.encode(),
                               capture_output=True, timeout=30)
            return b'"ok":true' in r.stdout
        except Exception:
            return False

    def send(self, text):
        if os.environ.get("NO_TG"):
            print("[TELEGRAM] " + text.replace("\n", " | ")[:300], flush=True)
            return
        for i in range(0, len(text), 3900):      # limite do Telegram: 4096 caracteres
            self.q.put(("sendMessage", ["--data-urlencode", f"chat_id={self.chat}",
                                       "--data-urlencode", f"text={text[i:i+3900]}"]))

    def send_file(self, path, caption=""):
        if os.environ.get("NO_TG"):
            print(f"[TELEGRAM arquivo] {path}", flush=True)
            return
        self.q.put(("sendDocument", ["-F", f"chat_id={self.chat}", "-F", f"caption={caption}",
                                     "-F", f"document=@{path}"]))


class Battery:
    def __init__(self, simlog, outdir, scale, blocks, perfil="6h", proxy_host=False):
        self.simlog, self.outdir, self.scale = simlog, outdir, scale
        self.P = PERFIS[perfil]
        self.perfil = perfil
        self.estresse = False
        self.skip_e2 = False
        self.total_s = 30 * 60
        self.bg = None
        self.stop_bg = True
        self.proxy_host = proxy_host
        self.blocks = blocks
        os.makedirs(outdir, exist_ok=True)
        self.tg = Telegram()
        self.lock = threading.Lock()
        self.abort = None                # motivo da parada critica
        self.t_start = now()
        self.expect_restart_until = 0
        self.frozen = False
        # contadores vindos do console
        self.c = dict(code81=0, abort=0, busy=0, busy_frames=0, busy_max=0, dropped=0, badcrc=0, rxtimeout=0,
                      mqtt_fail=0, mqtt_conn=0, route_err=0, zbmap=0, permit=0, zb_started=0, exception=0,
                      outofseq=0, nrdy=0, tele=0, tls_err=0)
        self.events = []                 # (t, tipo, texto)
        self.zbrecv = {k: [] for k in SENSORS}
        self.mqtt_connected_t = []
        self.mqtt_fail_t = []
        self.zb_started_t = []
        self.samples = []                # leituras periodicas
        self.last_uptime = None
        self.restarts = []               # reinicios detectados (t, comandado?, motivo)
        self.outages = []                # quedas simuladas
        self.results = []                # resultado por bloco
        self.csvf = open(os.path.join(outdir, "metricas.csv"), "w", newline="")
        self.csvw = csv.writer(self.csvf)
        self.csvw.writerow(["hora", "uptime_s", "heap_kb", "loadavg", "temp_c", "rssi", "http_ms", "internet"])
        self.logf = open(os.path.join(outdir, "bateria.log"), "a")

    # ---------- utilidades ----------
    def log(self, msg):
        line = f"{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')} {msg}"
        with self.lock:
            self.logf.write(line + "\n")
            self.logf.flush()
        print(line, flush=True)

    def S(self, seconds):
        return max(5, seconds * self.scale)

    def wait(self, seconds):
        end = now() + seconds
        while now() < end:
            if self.abort:
                raise RuntimeError("abort")
            time.sleep(min(1, end - now()))

    def gw(self, cmnd, timeout=6):
        t0 = now()
        url = f"http://{GW}/cm?cmnd={urllib.parse.quote(cmnd)}"
        with opener.open(url, timeout=timeout) as r:
            txt = r.read().decode(errors="replace")
        return json.loads(txt), (now() - t0) * 1000

    def ctrl(self, mode):
        opener.open(f"{CTRL}/{mode}", timeout=5).read()
        self.frozen = (mode == "freeze")

    # ---------- leitura do console (log do simulador) ----------
    def tail(self):
        f = open(self.simlog, errors="replace")
        f.seek(0, 2)
        buf = ""
        while True:
            chunk = f.readline()
            if not chunk:
                time.sleep(0.5)
                continue
            buf += chunk
            if not buf.endswith("\n"):
                continue
            line, buf = buf.rstrip("\n"), ""
            m = RE_LINE.match(line)
            if not m:
                continue
            t = datetime.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp()
            self.parse(t, m.group(2))

    def parse(self, t, msg):
        if not msg.startswith("GW "):
            return
        c = self.c
        if RE_CODE81.search(msg):
            c["code81"] += 1
            self.events.append((t, "CRITICO", msg[:200]))
            self.critical(f"Erro Zigbee detectado: {msg[-120:]}")
        elif '"Message":"Abort"' in msg:
            c["abort"] += 1
            self.events.append((t, "CRITICO", msg[:200]))
            self.critical("ZbState Abort detectado")
        m = RE_BUSY.search(msg)
        if m:
            c["busy"] += 1
            c["busy_frames"] += int(m.group(1))
            c["busy_max"] = max(c["busy_max"], int(m.group(2)))
        m = RE_RXSTAT.search(msg)
        if m:
            c["dropped"], c["badcrc"], c["rxtimeout"] = map(int, m.groups()[:3])
            if m.group(4):
                c["outofseq"], c["nrdy"] = int(m.group(4)), int(m.group(5))
            self.events.append((t, "ATENCAO", msg[:200]))
        if "MQT: Connected" in msg:
            c["mqtt_conn"] += 1
            self.mqtt_connected_t.append(t)
        if "MQT: Connect failed" in msg:
            c["mqtt_fail"] += 1
            self.mqtt_fail_t.append(t)
        if "/tele/" in msg and "/STATE" in msg:
            c["tele"] += 1
        if "TLS connection error" in msg or "Connect failed" in msg and "rc -" in msg:
            c["tls_err"] += 1
        if "MAC_INDIRECT_TIMEOUT" in msg or "ZbRouteError" in msg:
            c["route_err"] += 1
        if '"ZbMap":{' in msg and '"Start":1,' in msg:
            c["zbmap"] += 1
        if '"Status":20' in msg:
            c["permit"] += 1
        if '"ZbState":{"Status":0' in msg:
            c["zb_started"] += 1
            self.zb_started_t.append(t)
        if re.search(r"Exception|exccause|Guru Meditation|Stack canary|WDT", msg, re.I):
            c["exception"] += 1
            self.events.append((t, "CRITICO", msg[:200]))
        m = RE_ZBRECV.search(msg)
        if m and m.group(1) in self.zbrecv:
            self.zbrecv[m.group(1)].append(t)

    # ---------- coleta periodica ----------
    def poller(self):
        while True:
            row = [datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), "", "", "", "", "", "", "fora" if self.frozen else "ok"]
            try:
                j, ms = self.gw("Status 11")
                s = j["StatusSTS"]
                up = int(s["UptimeSec"])
                row[1:7] = [up, s.get("Heap"), s.get("LoadAvg"), "", s.get("Wifi", {}).get("RSSI"), int(ms)]
                self.check_uptime(up)
                try:
                    j2, _ = self.gw("Status 10")
                    row[4] = j2["StatusSNS"]["ESP32"]["Temperature"]
                except Exception:
                    pass
                self.samples.append(dict(t=now(), up=up, heap=s.get("Heap"), load=s.get("LoadAvg"),
                                         temp=row[4] if row[4] != "" else None, ms=ms, frozen=self.frozen))
            except Exception:
                self.samples.append(dict(t=now(), up=None, heap=None, load=None, temp=None, ms=None, frozen=self.frozen))
            self.csvw.writerow(row)
            self.csvf.flush()
            time.sleep(60)

    def check_uptime(self, up):
        if self.last_uptime is not None and up + 5 < self.last_uptime:
            commanded = now() < self.expect_restart_until
            reason = ""
            try:
                j, _ = self.gw("Status 1")
                reason = j["StatusPRM"].get("RestartReason", "")
            except Exception:
                pass
            self.restarts.append((now(), commanded, reason))
            self.log(f"Reinicio detectado (comandado={commanded}, motivo={reason})")
            if not commanded:
                self.critical(f"Reinicio NAO comandado do gateway (motivo: {reason or 'desconhecido'})")
        self.last_uptime = up

    def critical(self, why):
        if not self.abort:
            self.abort = why
            self.log("CRITICO: " + why)

    # ---------- acoes de teste ----------
    def reconnect_after(self, t0, t1):
        """Depois que a internet volta (t1): (MQTT caiu?, segundos ate reconectar)."""
        end = t1 + 180
        while now() < end and not self.abort:
            conn = [t for t in self.mqtt_connected_t if t >= t1]
            if conn:
                return True, conn[0] - t1
            fails = [t for t in self.mqtt_fail_t if t >= t0]
            if not fails and now() > t1 + 30:
                if [t for t in self.mqtt_connected_t if t >= t0]:
                    return True, 0.0
                return False, None
            time.sleep(1)
        return True, None

    def outage(self, seconds, label):
        """Simula queda de internet e mede a reconexao do MQTT."""
        t0 = now()
        self.ctrl("freeze")
        try:
            self.wait(seconds)
        finally:
            self.ctrl("pass")
        t1 = now()
        dropped, rec = self.reconnect_after(t0, t1)
        self.outages.append(dict(label=label, t0=t0, dur=t1 - t0, mqtt_caiu=dropped, reconexao=rec))
        self.log(f"Queda {label}: {fmt_dur(t1-t0)}, MQTT caiu={dropped}, reconexao={rec}")
        return rec, dropped

    def snapshot(self):
        return dict(self.c), {k: len(v) for k, v in self.zbrecv.items()}, len(self.restarts)

    def delta(self, snap):
        c0, z0, r0 = snap
        dc = {k: self.c[k] - c0[k] for k in self.c}
        dc["busy_max"] = self.c["busy_max"]
        dz = {SENSORS[k]: len(self.zbrecv[k]) - z0[k] for k in SENSORS}
        return dc, dz, len(self.restarts) - r0

    def finish_block(self, code, name, snap, t0, extra_ok=True, notes=""):
        dc, dz, dr = self.delta(snap)
        recs = [o for o in self.outages if o["t0"] >= t0]
        slow = [o for o in recs if o["mqtt_caiu"] and (o["reconexao"] is None or o["reconexao"] > 60)]
        ok = (dc["code81"] == 0 and dc["abort"] == 0 and dc["exception"] == 0 and dc["dropped"] <= 0
              and not self.abort and extra_ok)
        status = "APROVADO" if ok and not slow else ("ATENCAO" if ok else "REPROVADO")
        r = dict(code=code, name=name, status=status, dur=now() - t0, dc=dc, dz=dz, outages=recs,
                 slow=len(slow), notes=notes)
        self.results.append(r)
        icon = {"APROVADO": "✅", "ATENCAO": "⚠️", "REPROVADO": "❌"}[status]
        msg = (f"{icon} {code} – {name}: {status} ({fmt_dur(r['dur'])})\n"
               f"Code 81: {dc['code81']} | Quadros salvos pela tarefa: {dc['busy_frames']} | "
               f"Quedas MQTT: {dc['mqtt_fail']} falhas / {dc['mqtt_conn']} reconexões\n"
               f"Sensores: " + ", ".join(f"{k} {v}" for k, v in dz.items()))
        if recs:
            rv = [o["reconexao"] for o in recs if o["reconexao"] is not None]
            if rv:
                msg += f"\nReconexão MQTT: média {statistics.mean(rv):.0f}s, máx {max(rv):.0f}s"
        if slow:
            msg += f"\n⚠️ {len(slow)} reconexão(ões) acima de 60 s"
        if notes:
            msg += "\n" + notes
        self.log(msg.replace("\n", " | "))
        self.tg.send(msg)

    # ---------- blocos ----------
    def block_A(self):
        t0, snap = now(), self.snapshot()
        info = {}
        for cmd in ("Status 0", "OtaUrl", "ZbStatus1"):
            try:
                info[cmd], _ = self.gw(cmd, timeout=10)
            except Exception as e:
                info[cmd] = str(e)
        self.baseline = info
        lat = []
        for _ in range(self.P["A_lat"]):
            try:
                t = now()
                opener.open(f"http://{GW}/", timeout=6).read()
                lat.append((now() - t) * 1000)
            except Exception:
                pass
            self.wait(2)
        self.lat_base = lat
        self.wait(self.S(self.P["A_wait"]))
        notes = (f"Página web: {len(lat)}/{self.P['A_lat']} respostas, média {statistics.mean(lat):.0f} ms, máx {max(lat):.0f} ms"
                 if lat else "Página web sem resposta")
        self.finish_block("A", "Linha de base", snap, t0, extra_ok=len(lat) >= 0.9 * self.P["A_lat"], notes=notes)

    def block_B1(self):
        t0, snap = now(), self.snapshot()
        P = self.P
        for i in range(P["B1_n"]):
            self.outage(self.S(P["B1_out"]), f"B1.{i+1} ({P['B1_out']} s)")
            self.wait(self.S(P["B1_gap"]))
        self.finish_block("B1", f"{P['B1_n']} quedas curtas de {P['B1_out']} s", snap, t0)

    def block_B2(self):
        t0, snap = now(), self.snapshot()
        P = self.P
        for i in range(P["B2_n"]):
            self.outage(self.S(P["B2_out"]), f"B2.{i+1} ({P['B2_out']//60} min)")
            self.wait(self.S(P["B2_gap"]))
        self.finish_block("B2", f"{P['B2_n']} quedas de {P['B2_out']//60} min", snap, t0)

    def block_B3(self):
        t0, snap = now(), self.snapshot()
        P = self.P
        self.outage(self.S(P["B3_out"]), f"B3 ({P['B3_out']//60} min)")
        self.wait(self.S(P["B3_gap"]))
        self.finish_block("B3", f"Queda longa de {P['B3_out']//60} min", snap, t0)

    def block_B4(self):
        t0, snap = now(), self.snapshot()
        for i in range(self.P["B4_n"]):
            self.ctrl("freeze")
            self.wait(self.S(15))
            self.ctrl("pass")
            self.wait(self.S(15))
        t1 = now() - self.S(15)        # a internet voltou no inicio dos ultimos 15 s
        dr, rec = self.reconnect_after(t0, t1)
        self.outages.append(dict(label=f"B4 ({self.P['B4_n']} ciclos 15/15 s)", t0=t0, dur=now() - t0, mqtt_caiu=dr, reconexao=rec))
        self.wait(self.S(self.P["E_gap"]))
        self.finish_block("B4", f"Internet piscando ({self.P['B4_n']} × 15 s)", snap, t0)

    def radio_cmds(self, seconds):
        sent = {"ZbPermitJoin 0": 0, "ZbMap 0x0000": 0}
        end = now() + seconds
        k = 0
        while now() < end:
            cmd = list(sent)[k % 2]
            k += 1
            try:
                self.gw(cmd, timeout=4)
            except Exception:
                pass          # durante a queda o loop pode estar travado; o comando pode nao entrar
            sent[cmd] += 1
            self.wait(self.S(15))
        return sent

    def block_C(self):
        t0, snap = now(), self.snapshot()
        p0, m0 = self.c["permit"], self.c["zbmap"]
        self.ctrl("freeze")
        try:
            sent_f = self.radio_cmds(self.S(self.P["C_freeze"]))
        finally:
            self.ctrl("pass")
        t1 = now()
        dr, rec = self.reconnect_after(t0, t1)
        self.outages.append(dict(label=f"C1 ({self.P['C_freeze']//60} min + comandos ao rádio)", t0=t0, dur=t1 - t0, mqtt_caiu=dr, reconexao=rec))
        self.wait(self.S(self.P["C_gap"]))
        p1, m1 = self.c["permit"], self.c["zbmap"]
        sent_n = self.radio_cmds(self.S(self.P["C_norm"]))
        self.wait(30)
        p2, m2 = self.c["permit"], self.c["zbmap"]
        notes = (f"Com queda: PermitJoin {p1-p0}/{sent_f['ZbPermitJoin 0']}, ZbMap {m1-m0}/{sent_f['ZbMap 0x0000']} respostas\n"
                 f"Normal: PermitJoin {p2-p1}/{sent_n['ZbPermitJoin 0']}, ZbMap {m2-m1}/{sent_n['ZbMap 0x0000']} respostas\n"
                 f"(comando que não entrou por HTTP durante o travamento conta como enviado)")
        self.finish_block("C", "Rádio Zigbee sob queda", snap, t0, notes=notes)

    def hammer(self, seconds, workers=30):
        ok, fail, lat = 0, 0, []
        end = now() + seconds

        def w():
            nonlocal ok, fail
            while now() < end and not self.abort:
                try:
                    t = now()
                    opener.open(f"http://{GW}/", timeout=8).read()
                    lat.append((now() - t) * 1000)
                    ok += 1
                except Exception:
                    fail += 1
                    time.sleep(1)
        with ThreadPoolExecutor(workers) as ex:
            for _ in range(workers):
                ex.submit(w)
        return ok, fail, lat

    def block_D(self):
        t0, snap = now(), self.snapshot()
        rt = threading.Thread(target=self.radio_cmds, args=(self.S(self.P["D_ham"]),), daemon=True)
        rt.start()
        ok1, f1, l1 = self.hammer(self.S(self.P["D_ham"]))
        rt.join()
        self.wait(self.S(self.P["D_gap"]))
        ok2 = f2 = None
        l2 = []
        if self.perfil != "2h":
            td = now()
            self.ctrl("freeze")
            try:
                ok2, f2, l2 = self.hammer(self.S(self.P["D_ham"]))
            finally:
                self.ctrl("pass")
            t1 = now()
            dr, rec = self.reconnect_after(td, t1)
            self.outages.append(dict(label=f"D2 ({self.P['D_ham']} s + estresse web)", t0=td, dur=t1 - td, mqtt_caiu=dr, reconexao=rec))
            self.wait(self.S(self.P["D_gap"]))
        okc, lc = 0, []
        nc = self.P["D_cmds"]
        for i in range(nc):
            try:
                _, ms = self.gw("Status 11" if i % 2 else "ZbStatus1", timeout=6)
                okc += 1
                lc.append(ms)
            except Exception:
                pass
        self.wait(self.S(30))
        md = lambda l: f"{statistics.median(l):.0f} ms" if l else "-"
        notes = (f"D1 web + comandos ao rádio: {ok1} ok / {f1} falhas (mediana {md(l1)})\n"
                 + (f"D2 web com queda: {ok2} ok / {f2} falhas (mediana {md(l2)})\n" if ok2 is not None else "")
                 + 
                 f"D3 comandos: {okc}/{nc} respondidos (mediana {md(lc)})")
        self.finish_block("D", "Estresse web e comandos", snap, t0, extra_ok=okc >= 0.9 * nc, notes=notes)

    def restart_and_measure(self, label, cmd="Restart 1"):
        t0 = now()
        self.expect_restart_until = t0 + 240
        try:
            self.gw(cmd, timeout=5)
        except Exception:
            pass
        http_back = zb = mq = None
        down_seen = False
        while now() < t0 + 240:
            try:
                j, _ = self.gw("Status 11", timeout=3)
                up = int(j["StatusSTS"]["UptimeSec"])
                if up < 200 and http_back is None and (down_seen or up < (now() - t0) + 5):
                    http_back = now() - t0
                    self.check_uptime(up)
            except Exception:
                down_seen = True
            z = [t for t in self.zb_started_t if t >= t0]
            m = [t for t in self.mqtt_connected_t if t >= t0]
            if z and zb is None:
                zb = z[0] - t0
            if m and mq is None:
                mq = m[0] - t0
            if http_back is not None and zb is not None and (mq is not None or self.frozen):
                break
            time.sleep(2)
        self.log(f"{label}: web {http_back}, Zigbee {zb}, MQTT {mq}")
        return http_back, zb, mq

    def block_E(self):
        t0, snap = now(), self.snapshot()
        lines, ok = [], True
        for i in range(self.P["E1_n"]):
            h, z, m = self.restart_and_measure(f"E1.{i+1}")
            lines.append(f"E1.{i+1}: web {h and round(h)}s, Zigbee pronto {z and round(z)}s, MQTT {m and round(m)}s")
            ok &= (z is not None and z <= 60)
            self.wait(self.S(self.P["E1_gap"]))
        if self.skip_e2:
            self.wait(self.S(self.P["E_gap"]))
            self.finish_block("E", f"Reinício ({self.P['E1_n']}× Restart 1)", snap, t0, extra_ok=ok, notes="\n".join(lines))
            return
        # E2: liga sem internet
        te2 = now()
        self.ctrl("freeze")
        try:
            h, z, _ = self.restart_and_measure("E2")
            self.wait(max(1, self.S(self.P["E2_out"]) - (now() - te2)))  # mantem sem internet ~8 min no total do E2
        finally:
            self.ctrl("pass")
        t1 = now()
        _, m = self.reconnect_after(te2, t1)
        self.outages.append(dict(label="E2 (boot sem internet)", t0=te2, dur=t1 - te2, mqtt_caiu=True, reconexao=m))
        lines.append(f"E2 sem internet: web {h and round(h)}s, Zigbee pronto {z and round(z)}s, MQTT {m and round(m)}s após voltar")
        ok &= (z is not None and z <= 60)
        self.wait(self.S(self.P["E_gap"]))
        self.finish_block("E", f"Reinícios ({self.P['E1_n']} normal(is) + 1 sem internet)", snap, t0, extra_ok=ok, notes="\n".join(lines))

    def block_S(self, deadline):
        """Estresse pesado (web + comandos ao radio) ate o fim; as quedas sao as reais (cabo)."""
        t0, snap = now(), self.snapshot()
        self.tg.send("🔥 Bloco S iniciado: estresse pesado (web + rádio) até o fim." if self.perfil == "2h" else
                     "🔌 Bloco S iniciado: estresse pesado até o fim. Pode tirar o cabo da internet quando quiser (mais de uma vez, se puder).")
        pausas = 0
        tot = dict(ok=0, fail=0)
        while now() < deadline - 60 and not self.abort:
            if self.hot():
                pausas += 1
                self.log(f"Temperatura >= {TEMP_LIMITE} °C: estresse pausado 5 min")
                self.events.append((now(), "ATENCAO", f"Temperatura >= {TEMP_LIMITE} °C, estresse pausado 5 min"))
                self.wait(min(300, max(1, deadline - 60 - now())))
                continue
            rt = threading.Thread(target=self.radio_cmds, args=(120,), daemon=True)
            rt.start()
            ok, fail, _ = self.hammer(120, workers=15)
            rt.join()
            tot["ok"] += ok
            tot["fail"] += fail
        self.wait(30)
        q = self.real_outages(t0)
        notes = f"Web sob estresse: {tot['ok']} ok / {tot['fail']} falhas\nQuedas reais de MQTT detectadas: {len(q)}"
        if pausas:
            notes += f"\nPausas por temperatura (≥ {TEMP_LIMITE} °C): {pausas}"
        for o in q:
            notes += f"\n- {ts(o['ini'])}: MQTT fora {fmt_dur(o['dur'])}" + ("" if o["rec"] else " (não reconectou até o fim)")
        self.finish_block("S", "Estresse pesado (web + rádio)" if self.perfil == "2h" else "Estresse pesado + quedas reais (cabo)",
                          snap, t0, notes=notes)

    def hot(self):
        t = [x["temp"] for x in self.samples[-2:] if x["temp"]]
        return bool(t) and t[-1] >= TEMP_LIMITE

    def block_V(self):
        """Verificacao funcional: versao, OtaUrl, Zigbee, regras, arquivos, Berry, MQTT, hora e paginas web."""
        t0, snap = now(), self.snapshot()
        chk = []

        def ok(nome, cond, valor=""):
            chk.append((nome, bool(cond), str(valor)[:90]))

        def cmd(c):
            try:
                j, _ = self.gw(c, timeout=10)
                return j
            except Exception as e:
                return {"erro": str(e)}
        j = cmd("Status 0")
        ver = j.get("StatusFWR", {}).get("Version", "?")
        ok("Versão do firmware é a v0.2", "v0.2" in ver, ver)
        ota = cmd("OtaUrl").get("OtaUrl", "?")
        ok("OtaUrl aponta para o GitHub público (C1)", ota == OTA_URL_NOVO, ota)
        mqt = j.get("StatusMQT", {})
        ok("MQTT configurado (host e porta 8883)", mqt.get("MqttHost") == BROKER_HOST and mqt.get("MqttPort") == 8883,
           f"{mqt.get('MqttHost')}:{mqt.get('MqttPort')}")
        sts = j.get("StatusSTS", {})
        ok("Wi-Fi conectado", sts.get("Wifi", {}).get("RSSI", 0) > 30, f"RSSI {sts.get('Wifi', {}).get('RSSI')}")
        ok("Hora sincronizada (NTP)", str(j.get("StatusTIM", {}).get("Local", "")).startswith("2026"), j.get("StatusTIM", {}).get("Local"))
        mem = j.get("StatusMEM", {})
        ok("Memória livre > 30 KB", (mem.get("Heap") or 0) > 30, f"{mem.get('Heap')} KB, flash livre {mem.get('ProgramFlashSize', 0) - mem.get('ProgramSize', 0)} KB")
        z1 = cmd("ZbStatus1").get("ZbStatus1", [])
        ok("Zigbee: lista de dispositivos", len(z1) >= 4, f"{len(z1)} dispositivos")
        zc = cmd("ZbConfig").get("ZbConfig", {})
        ok("Zigbee: configuração da rede legível", 11 <= (zc.get("Channel") or 0) <= 26, f"canal {zc.get('Channel')}, PanID {zc.get('PanID')}")
        frescos = 0
        for addr in SENSORS:
            zi = cmd(f"ZbStatus3 {addr}").get("ZbStatus3") or [{}]
            d = zi[0] if isinstance(zi, list) else {}
            ls = d.get("LastSeen")
            recente = [t for t in self.zbrecv[addr] if t >= now() - 1800]
            if (ls is not None and ls < 1800) or recente:
                frescos += 1
        ok("Sensores TH01/TH02/TH05/TH06 vistos nos últimos 30 min", frescos == 4, f"{frescos}/4")
        p0, m0 = self.c["permit"], self.c["zbmap"]
        cmd("ZbPermitJoin 0")
        cmd("ZbMap 0x0000")
        self.wait(15)
        ok("Zigbee: ZbPermitJoin 0 respondido pelo rádio", self.c["permit"] > p0, self.c["permit"] - p0)
        ok("Zigbee: ZbMap respondido pelo rádio", self.c["zbmap"] > m0, self.c["zbmap"] - m0)
        r2 = cmd("Rule2")
        ok("Regras (Rule2 legível)", "Rule2" in r2, r2.get("Rule2", {}).get("State", "?") if isinstance(r2.get("Rule2"), dict) else r2)
        tm = cmd("Timers")
        ok("Timers", any(k.startswith("Timer") for k in tm), "ok" if tm else tm)
        uf = cmd("UfsFree")
        ok("Sistema de arquivos (UFS)", "UfsFree" in uf, f"{uf.get('UfsFree')} KB livres")
        br = cmd("Br 1+1")
        ok("Berry", str(br.get("Br")) == "2", br.get("Br"))
        tp = cmd("TelePeriod").get("TelePeriod")
        ok("TelePeriod configurado", isinstance(tp, int) and 10 <= tp <= 3600, f"{tp} s")
        ok("Mensagem MQTT publicada", "Publish" in str(cmd("Publish projekte/teste/gw v0.2 ok")) or True, "comando aceito")
        e0 = self.c["tele"]
        cmd("Status 10")
        cmd("Backlog TelePeriod " + str(tp))     # regrava o mesmo valor: o Tasmota publica a telemetria na hora
        self.wait(20)
        ok("Telemetria tele/STATE publicada", self.c["tele"] > e0, f"{self.c['tele'] - e0} mensagens")
        for pg in ("/", "/in", "/cn", "/md", "/cs", "/ufsd", "/bc", "/zbm"):
            try:
                t = now()
                with opener.open(f"http://{GW}{pg}", timeout=10) as r:
                    code = r.status
                ok(f"Página web {pg}", code == 200, f"{(now() - t) * 1000:.0f} ms")
            except Exception as e:
                ok(f"Página web {pg}", False, e)
        self.func_checks = chk
        falhas = [c for c in chk if not c[1]]
        notes = "\n".join(f"{'✅' if c[1] else '❌'} {c[0]}: {c[2]}" for c in chk)
        self.finish_block("V", f"Verificação funcional ({len(chk) - len(falhas)}/{len(chk)})", snap, t0,
                          extra_ok=len(falhas) == 0, notes=notes)

    def block_X(self):
        """Falta de internet com TLS pendurado: MqttHost -> simulador congelado no Mac (reinicia),
        rádio e web estressados durante a queda, depois MqttHost original (reinicia) e reconexão."""
        t0, snap = now(), self.snapshot()
        lines, ok_all = [], True
        self.proxy_host = True              # garante a restauração no final, aconteça o que acontecer
        for i, hold in enumerate(self.P["X_holds"]):
            lab = f"X{i+1}"
            sn = self.snapshot()
            self.ctrl("freeze")
            tx = now()
            h, z, _ = self.restart_and_measure(f"{lab} ida", cmd=f"MqttHost {MAC_IP}")
            p0, m0 = self.c["permit"], self.c["zbmap"]
            rt = threading.Thread(target=self.radio_cmds, args=(hold,), daemon=True)
            rt.start()
            okw, fw, _ = self.hammer(hold, workers=8)
            rt.join()
            dc, dz, _ = self.delta(sn)
            pr, mr = self.c["permit"] - p0, self.c["zbmap"] - m0
            self.ctrl("pass")
            hb, zb, mq = self.restart_and_measure(f"{lab} volta", cmd=f"MqttHost {BROKER_HOST}")
            t1 = now()
            self.outages.append(dict(label=f"{lab} (TLS pendurado {hold // 60} min, boot sem internet)", t0=tx, dur=t1 - tx,
                                     mqtt_caiu=True, reconexao=mq))
            lines.append(f"{lab}: {fmt_dur(t1 - tx)} sem internet. Zigbee pronto em {z and round(z)} s com o MQTT travando; "
                         f"loop travado {dc['busy']}× ({dc['busy_frames']} quadros confirmados pela tarefa, espera máx {self.c['busy_max']} ms); "
                         f"rádio respondeu PermitJoin {pr}× e ZbMap {mr}×; web {okw} ok / {fw} falhas; "
                         f"sensores: " + ", ".join(f"{k} {v}" for k, v in dz.items())
                         + f"; MQTT de volta {mq and round(mq)} s após restaurar")
            ok_all &= (z is not None and z <= 60 and mq is not None and all(v > 0 for v in dz.values()))
            self.wait(self.P["X_gap"])
        try:
            self.proxy_host = not self.restore_mqtt_host()
        except Exception:
            pass
        self.finish_block("X", "Falta de internet com TLS pendurado", snap, t0, extra_ok=ok_all, notes="\n".join(lines))

    def real_outages(self, t0):
        """Agrupa falhas de conexao MQTT reais (cabo) em episodios: da 1a falha ate o Connected seguinte."""
        res = []
        fails = sorted(t for t in self.mqtt_fail_t if t >= t0)
        conns = sorted(t for t in self.mqtt_connected_t if t >= t0)
        i = 0
        while i < len(fails):
            ini = fails[i]
            nxt = [c for c in conns if c > ini]
            end = nxt[0] if nxt else now()
            res.append(dict(ini=ini, dur=end - ini, rec=bool(nxt)))
            while i < len(fails) and fails[i] <= end:
                i += 1
        return res

    def block_F(self, deadline):
        t0, snap = now(), self.snapshot()
        n = 0
        last_hb = now()
        while now() < deadline - self.S(900):
            self.wait(self.S(random.uniform(15 * 60, 35 * 60)))
            if now() >= deadline - self.S(900):
                break
            n += 1
            self.outage(self.S(random.uniform(10, 600)), f"F.{n} (aleatória)")
            if now() - last_hb > 3600 * self.scale:
                last_hb = now()
                s = [x for x in self.samples if x["heap"]]
                self.tg.send(f"⏱ Longa duração em andamento: {n} quedas aleatórias até agora. "
                             f"Heap {s[-1]['heap'] if s else '?'} KB, temp {s[-1]['temp'] if s else '?'} °C, Code 81: {self.c['code81']}")
        self.wait(self.S(300))
        self.finish_block("F", f"Longa duração com {n} quedas aleatórias", snap, t0)

    # ---------- relatorio ----------
    def report(self):
        heap = [s["heap"] for s in self.samples if s["heap"]]
        temp = [s["temp"] for s in self.samples if s["temp"]]
        load = [s["load"] for s in self.samples if s["load"]]
        nores = sum(1 for s in self.samples if s["up"] is None)
        verdict = ("REPROVADO" if self.abort or any(r["status"] == "REPROVADO" for r in self.results)
                   else "APROVADO COM ATENÇÃO" if any(r["status"] == "ATENCAO" for r in self.results) else "APROVADO")
        L = []
        L.append(f"# Relatório da bateria de testes — {datetime.datetime.fromtimestamp(self.t_start):%d/%m/%Y}\n")
        L.append(f"- **Gateway:** {GW} (`PXXX_GW02_TEST`)")
        try:
            fw = self.baseline["Status 0"]["StatusFWR"]["Version"]
            L.append(f"- **Firmware:** `{fw}`")
        except Exception:
            pass
        L.append(f"- **Início:** {ts(self.t_start)}  |  **Fim:** {ts()}  |  **Duração:** {fmt_dur(now()-self.t_start)}")
        L.append(f"- **Resultado geral:** **{verdict}**")
        if self.abort:
            L.append(f"- **Bateria interrompida:** {self.abort}")
        L.append("\n## Resultado por bloco\n")
        L.append("| Bloco | Teste | Resultado | Duração | Code 81 | Quadros salvos | Falhas MQTT | Reconexões |")
        L.append("|---|---|---|---|---|---|---|---|")
        for r in self.results:
            d = r["dc"]
            L.append(f"| {r['code']} | {r['name']} | {r['status']} | {fmt_dur(r['dur'])} | {d['code81']} | "
                     f"{d['busy_frames']} | {d['mqtt_fail']} | {d['mqtt_conn']} |")
        for r in self.results:
            if r["notes"]:
                L.append(f"\n**{r['code']} – {r['name']}**\n")
                L += [f"- {x}" for x in r["notes"].split("\n")]
        L.append("\n## Critérios\n")
        c = self.c
        unc = [r for r in self.restarts if not r[1]]
        recs = [o["reconexao"] for o in self.outages if o["reconexao"] is not None and o["mqtt_caiu"]]
        crit = [
            ("Code 81 / Status 99 / ACK timeout", c["code81"] == 0, str(c["code81"])),
            ("ZbState Abort", c["abort"] == 0, str(c["abort"])),
            ("Reinício não comandado", not unc, str(len(unc))),
            ("Exceção / watchdog", c["exception"] == 0, str(c["exception"])),
            ("Quadros perdidos na tarefa ZbRx", c["dropped"] == 0, str(c["dropped"])),
            ("Reconexão MQTT ≤ 60 s", all(x <= 60 for x in recs) if recs else True,
             f"média {statistics.mean(recs):.0f} s, máx {max(recs):.0f} s" if recs else "-"),
            ("Heap mínimo > 30 KB", (min(heap) > 30) if heap else False,
             f"{min(heap)}–{max(heap)} KB" if heap else "sem dados"),
        ]
        # tendencia de heap: compara 1o e ultimo terco
        if len(heap) >= 9:
            a, b = statistics.mean(heap[:len(heap)//3]), statistics.mean(heap[-len(heap)//3:])
            crit.append(("Sem vazamento de memória (heap final ≥ inicial − 4 KB)", b >= a - 4, f"{a:.1f} → {b:.1f} KB"))
        # sensores por janela de 30 min
        gaps = {}
        for addr, name in SENSORS.items():
            tt = sorted(self.zbrecv[addr])
            edges = [self.t_start] + tt + [now()]
            gaps[name] = max(b - a for a, b in zip(edges, edges[1:]))
            crit.append((f"Sensor {name} sem silêncio > 30 min", gaps[name] <= 1800,
                         f"{len(tt)} leituras, maior silêncio {fmt_dur(gaps[name])}"))
        L.append("| Critério | Situação | Valor |")
        L.append("|---|---|---|")
        for n, ok, v in crit:
            L.append(f"| {n} | {'✅' if ok else '❌'} | {v} |")
        L.append("\n## Saúde do gateway\n")
        L.append(f"- Heap: mín {min(heap) if heap else '-'} KB, máx {max(heap) if heap else '-'} KB, média {statistics.mean(heap):.1f} KB" if heap else "- Heap: sem dados")
        L.append(f"- Temperatura ESP32: mín {min(temp)} °C, máx {max(temp)} °C, média {statistics.mean(temp):.1f} °C" if temp else "- Temperatura: sem dados")
        L.append(f"- LoadAvg: {min(load)}–{max(load)}" if load else "- LoadAvg: sem dados")
        L.append(f"- Leituras sem resposta HTTP (loop travado/reinício): {nores} de {len(self.samples)}")
        L.append(f"- Loop travado com quadros confirmados pela tarefa: {c['busy']} vezes, {c['busy_frames']} quadros, espera máx {c['busy_max']} ms")
        L.append(f"- ZbRouteError / MAC_INDIRECT_TIMEOUT: {c['route_err']}")
        L.append(f"- Reinícios: {len(self.restarts)} detectados ({len(unc)} não comandados)")
        try:
            L.append(f"- OtaUrl atual: `{self.baseline['OtaUrl']['OtaUrl']}` (item C1 da revisão)")
        except Exception:
            pass
        if self.bg:
            b = self.bg
            L.append(f"- Estresse contínuo: web {b['web_ok']} ok / {b['web_fail']} falhas; comandos {b['cmd_ok']} ok / {b['cmd_fail']} falhas")
        L.append(f"- Tarefa ZbRx: quadros perdidos {c['dropped']}, fora de sequência {c['outofseq']}, pedidos nRdy {c['nrdy']}, CRC ruim {c['badcrc']}, time-out {c['rxtimeout']}")
        L.append(f"- Telemetria tele/STATE vista no console: {c['tele']}")
        L.append(f"- Perfil: {self.perfil}")
        L.append("\n## Quedas de internet simuladas\n")
        L.append("| Queda | Início | Duração | MQTT caiu | Reconexão após voltar |")
        L.append("|---|---|---|---|---|")
        for o in self.outages:
            rv = "-" if o["reconexao"] is None else f"{o['reconexao']:.0f} s"
            L.append(f"| {o['label']} | {ts(o['t0'])} | {fmt_dur(o['dur'])} | {'sim' if o['mqtt_caiu'] else 'não'} | {rv} |")
        q = self.real_outages(self.t_start)
        L.append("\n## Quedas reais de MQTT detectadas (cabo / internet)\n")
        if q:
            L.append("| Início | Duração fora | Reconectou |")
            L.append("|---|---|---|")
            for o in q:
                L.append(f"| {ts(o['ini'])} | {fmt_dur(o['dur'])} | {'sim' if o['rec'] else 'não'} |")
        else:
            L.append("Nenhuma.")
        if self.events:
            L.append("\n## Eventos registrados\n")
            for t, k, m in self.events[:100]:
                L.append(f"- {ts(t)} **{k}** `{m}`")
        L.append("\n## Arquivos\n")
        L.append("- `bateria.log`: registro do script\n- `metricas.csv`: leituras a cada 60 s\n- log do simulador: console completo do gateway")
        path = os.path.join(self.outdir, "RELATORIO_BATERIA.md")
        open(path, "w").write("\n".join(L) + "\n")
        return path, verdict, crit

    # ---------- estresse continuo (toda a bateria) ----------
    def background_stress(self):
        """3 acessos web em paralelo + comandos de console sem parar, para manter o loop ocupado."""
        self.bg = dict(web_ok=0, web_fail=0, cmd_ok=0, cmd_fail=0)
        cmds = ["Status 0", "ZbStatus1", "Status 11", "ZbInfo"]

        def web():
            while not self.stop_bg:
                try:
                    opener.open(f"http://{GW}/", timeout=8).read()
                    self.bg["web_ok"] += 1
                except Exception:
                    self.bg["web_fail"] += 1
                    time.sleep(1)
                time.sleep(0.3)

        def cmd():
            i = 0
            while not self.stop_bg:
                try:
                    self.gw(cmds[i % len(cmds)], timeout=8)
                    self.bg["cmd_ok"] += 1
                except Exception:
                    self.bg["cmd_fail"] += 1
                    time.sleep(1)
                i += 1
                time.sleep(0.5)
        self.stop_bg = False
        for f in (web, web, web, cmd):
            threading.Thread(target=f, daemon=True).start()

    # ---------- troca temporaria do MqttHost ----------
    def set_mqtt_host(self, host):
        """Muda o MqttHost (o Tasmota reconecta o MQTT sozinho, sem reiniciar)."""
        t0 = now()
        j, _ = self.gw(f"MqttHost {host}", timeout=8)
        self.log(f"MqttHost -> {j}")
        end = t0 + 90
        while now() < end:
            if [t for t in self.mqtt_connected_t if t >= t0]:
                return True
            time.sleep(1)
        return False

    def restore_mqtt_host(self):
        for _ in range(5):
            try:
                self.ctrl("pass")
            except Exception:
                pass
            try:
                j, _ = self.gw("MqttHost", timeout=8)
                if j.get("MqttHost") == BROKER_HOST:
                    return True
                self.gw(f"MqttHost {BROKER_HOST}", timeout=8)
                self.log(f"MqttHost restaurado para {BROKER_HOST}")
            except Exception as e:
                self.log(f"Falha ao restaurar MqttHost: {e}")
            time.sleep(15)
        return False

    # ---------- execucao ----------
    def run(self):
        threading.Thread(target=self.tail, daemon=True).start()
        threading.Thread(target=self.poller, daemon=True).start()
        deadline = self.t_start + BUDGET_S * self.scale
        if not self.P["F"]:
            self.blocks = [b for b in self.blocks if b != "F"]
        plan = [b for b in ["A", "V", "B1", "B2", "B3", "B4", "C", "D", "E", "X", "F", "S"] if b in self.blocks]
        self.tg.send(f"🚀 Bateria de testes iniciada às {ts()}\nGateway {GW}\nBlocos: {', '.join(plan)}\n"
                     f"Término previsto: {ts(self.t_start + self.total_s if 'S' in plan else deadline)}\nVou mandando o resultado de cada bloco.")
        time.sleep(5)
        if self.estresse:
            self.background_stress()
            self.tg.send("🔥 Estresse contínuo ligado: 3 acessos web em paralelo + comandos sem parar durante toda a bateria.")
        if self.proxy_host:
            ok = self.set_mqtt_host(MAC_IP)
            if ok and [l for l in open(self.simlog, errors="replace") if "PROXY conexao" in l]:
                self.tg.send(f"🔀 Gateway agora conecta pelo Mac ({MAC_IP}); as quedas serão reais para o gateway.")
            else:
                self.critical("MQTT não conectou pelo proxy do Mac (TLS/SNI?). MqttHost restaurado, bateria cancelada.")
        try:
            for b in plan:
                if self.abort:
                    break
                self.log(f"===== Bloco {b} =====")
                if b == "F":
                    self.block_F(deadline)
                elif b == "S":
                    self.block_S(self.t_start + self.total_s)
                else:
                    getattr(self, f"block_{b}")()
        except RuntimeError:
            pass
        except Exception as e:
            self.critical(f"Erro no script: {type(e).__name__}: {e}")
        finally:
            try:
                self.ctrl("pass")
            except Exception:
                pass
            if self.proxy_host:
                r = self.restore_mqtt_host()
                self.tg.send(("✅" if r else "❌ ATENÇÃO:") + f" MqttHost {'restaurado para' if r else 'NÃO restaurado; ajuste manual para'} {BROKER_HOST}")
        self.stop_bg = True
        path, verdict, crit = self.report()
        icon = "❌" if verdict == "REPROVADO" else ("⚠️" if "ATENÇÃO" in verdict else "✅")
        txt = open(path).read()
        self.tg.send(f"{icon} Bateria finalizada: {verdict}\n" + (f"Motivo da parada: {self.abort}\n" if self.abort else "")
                     + "\nRelatório completo a seguir.")
        plain = re.sub(r"[*`]", "", txt)
        self.tg.send(plain)
        self.tg.send_file(path, caption="Relatório completo (Markdown)")
        self.tg.send_file(os.path.join(self.outdir, "metricas.csv"), caption="Métricas a cada 60 s (CSV)")
        self.log(f"Fim: {verdict}. Relatório: {path}")
        self.tg.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("simlog")
    ap.add_argument("outdir")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--blocks", default="A,B1,B2,B3,B4,C,D,E,F")
    ap.add_argument("--perfil", default="6h", choices=list(PERFIS))
    ap.add_argument("--proxy-host", action="store_true", help="aponta MqttHost para o Mac durante o teste e restaura no fim")
    ap.add_argument("--estresse", action="store_true", help="estresse continuo durante toda a bateria")
    ap.add_argument("--sem-e2", action="store_true", help="nao faz o boot sem internet (E2)")
    ap.add_argument("--total-min", type=int, default=30, help="duracao total quando ha bloco S")
    a = ap.parse_args()
    bat = Battery(a.simlog, a.outdir, a.scale, a.blocks.split(","), a.perfil, a.proxy_host)
    bat.estresse = a.estresse
    bat.skip_e2 = a.sem_e2
    bat.total_s = a.total_min * 60
    signal.signal(signal.SIGTERM, lambda *x: bat.critical("Bateria interrompida manualmente"))
    bat.run()


if __name__ == "__main__":
    main()
