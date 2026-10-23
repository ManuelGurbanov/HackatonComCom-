#!/usr/bin/env python3
"""Cliente de Truco simulado para PC (reemplaza a la placa cliente).

Uso:
  1. La placa servidor corre el firmware y elegis KEY1 (SERVIDOR).
  2. Conecta la PC al WiFi TRUCO-ESP32 (clave: truco-hackaton).
  3. python3 sim_client.py            # interactivo
     python3 sim_client.py --bot      # juega solo
     python3 sim_client.py --host 127.0.0.1   # contra host_server (sin placa)
"""
import argparse, random, socket, struct, sys

MSG_VIEW, MSG_ACTION = 1, 2
VIEW_FMT = "<" + "B" * 30  # view_t empaquetado: 30 bytes
(ACT_PLAY, ACT_TRUCO, ACT_ENVIDO, ACT_REAL, ACT_FALTA,
 ACT_QUIERO, ACT_NOQUIERO, ACT_MAZO, ACT_RESTART) = range(1, 10)
PH_PLAY, PH_HAND_END, PH_GAME_OVER = range(3)
PEND_NONE, PEND_TRUCO, PEND_ENVIDO = range(3)
NO_CARD = 0xFF
NUMS = [1, 2, 3, 4, 5, 6, 7, 10, 11, 12]
TRUCO = {2: "TRUCO", 3: "RETRUCO", 4: "VALE 4"}
ENV = ["ENVIDO", "REAL ENVIDO", "FALTA ENVIDO"]


def card(c):
    return "---" if c == NO_CARD else f"{NUMS[c % 10]}{'EBOC'[c // 10]}"


class View:
    def __init__(self, raw):
        f = struct.unpack(VIEW_FMT, raw)
        self.phase = f[0]; self.hand = f[1:4]; self.my = f[4:7]; self.opp = f[7:10]
        self.tw = f[10:13]; self.score_me, self.score_opp = f[13], f[14]
        (self.mano_me, self.turn, self.pending, self.pending_arg, self.pending_mine,
         self.truco_level, self.raise_, self.can_envido, self.hand_no, self.winner_me) = f[15:25]
        self.ev = f[25:30]  # code, who, a, b, c

    def event_text(self):
        code, who, a, b, c = self.ev
        w = "YO" if who == 0 else "RIV"
        return {
            1: f"Mano nueva #{a}", 2: f"{w}: {TRUCO.get(a, '?')}!",
            3: f"{w}: {ENV[a % 3]}!", 4: f"{w}: QUIERO!", 5: f"{w}: NO QUIERO",
            6: f"ENVIDO {b}-{c} +{a} {w}",
            7: f"Mano para {w} (+{a}, motivo {b})", 8: f"Juego para {w}",
        }.get(code, "")

    def render(self):
        print("\n" + "=" * 44)
        print(f"YO {self.score_me}  -  RIVAL {self.score_opp}   (mano #{self.hand_no}, "
              f"mano: {'YO' if self.mano_me else 'RIV'}, vale {self.truco_level})")
        print("Rival: ", " ".join(card(c) for c in self.opp))
        print("Yo   : ", " ".join(card(c) for c in self.my))
        print("Mi mano:", "  ".join(f"[{i+1}] {card(c)}" for i, c in enumerate(self.hand) if c != NO_CARD))
        print("Evento:", self.event_text())
        if self.phase == PH_GAME_OVER:
            print("*** GANASTE ***" if self.winner_me else "*** PERDISTE ***", "- escribi 'r' para revancha")
        elif self.phase == PH_HAND_END:
            print("(fin de mano, repartiendo...)")
        elif self.turn:
            if self.pending:
                opts = ["q=quiero", "n=no quiero"]
                if self.raise_: opts.append(f"t={TRUCO[self.raise_]}")
            else:
                opts = ["1/2/3=jugar carta", "m=mazo"]
                if self.raise_: opts.append(f"t={TRUCO[self.raise_]}")
                if self.can_envido: opts.append("e=envido  er=real  ef=falta")
            print("TU TURNO ->", ", ".join(opts))
        else:
            print("Esperando al rival...")


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        d = s.recv(n - len(buf))
        if not d:
            raise ConnectionError("servidor desconectado")
        buf += d
    return buf


def send_action(s, act, arg=0):
    s.sendall(bytes([MSG_ACTION, act, arg]))


def bot_move(v):
    if v.pending:
        return (ACT_QUIERO, 0) if random.random() < 0.6 else (ACT_NOQUIERO, 0)
    if v.can_envido and random.random() < 0.25:
        return random.choice([ACT_ENVIDO, ACT_REAL]), 0
    if v.raise_ and random.random() < 0.15:
        return ACT_TRUCO, 0
    slots = [i for i, c in enumerate(v.hand) if c != NO_CARD]
    return ACT_PLAY, random.choice(slots)


def parse_input(line, v):
    line = line.strip().lower()
    if line in ("1", "2", "3"):
        return ACT_PLAY, int(line) - 1
    return {"t": (ACT_TRUCO, 0), "e": (ACT_ENVIDO, 0), "er": (ACT_REAL, 0), "ef": (ACT_FALTA, 0),
            "q": (ACT_QUIERO, 0), "n": (ACT_NOQUIERO, 0), "m": (ACT_MAZO, 0),
            "r": (ACT_RESTART, 0)}.get(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--port", type=int, default=3333)
    ap.add_argument("--bot", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    s = socket.create_connection((args.host, args.port), timeout=10)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(None)
    print(f"Conectado a {args.host}:{args.port}")

    import threading, queue
    inq = queue.Queue()
    if not args.bot:
        threading.Thread(target=lambda: [inq.put(l) for l in sys.stdin], daemon=True).start()

    import select
    view = None
    while True:
        r, _, _ = select.select([s], [], [], 0.2)
        if r:
            t = recv_exact(s, 1)[0]
            if t != MSG_VIEW:
                raise RuntimeError(f"tipo de mensaje inesperado {t}")
            view = View(recv_exact(s, 30))
            if not args.quiet:
                view.render()
            if args.bot and (view.turn or view.phase == PH_GAME_OVER):
                if view.phase == PH_GAME_OVER:
                    send_action(s, ACT_RESTART)
                else:
                    act, arg = bot_move(view)
                    if not args.quiet:
                        print(f"[bot] accion {act} arg {arg}")
                    send_action(s, act, arg)
        while not inq.empty():
            m = parse_input(inq.get(), view)
            if m and view:
                send_action(s, *m)
            else:
                print("comando no valido")


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, ConnectionError) as e:
        print("\nfin:", e)
