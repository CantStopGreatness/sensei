"""Play Sensei in the browser. Standard library only.

Usage: python3 tools/play.py [path/to/sensei] [--port 8765]   then open http://localhost:8765
The page checks move legality with chess.js; this server only relays UCI to the engine.
"""
import argparse
import json
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("engine", nargs="?", default=str(Path(__file__).resolve().parent.parent / "build" / "sensei"))
ap.add_argument("--port", type=int, default=8765)
args = ap.parse_args()

engine = subprocess.Popen([args.engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
lock = threading.Lock()


def send(cmd):
    engine.stdin.write(cmd + "\n")
    engine.stdin.flush()


def read_until(prefix):
    lines = []
    while True:
        line = engine.stdout.readline()
        if not line:
            raise RuntimeError("engine exited")
        lines.append(line.strip())
        if line.startswith(prefix):
            return lines


send("uci")
read_until("uciok")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, body, ctype="application/json"):
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.reply(PAGE, "text/html; charset=utf-8")

    def do_POST(self):
        req = json.loads(self.rfile.read(int(self.headers["Content-Length"])) or "{}")
        with lock:
            if self.path == "/new":
                send("ucinewgame")
                send("isready")
                read_until("readyok")
                return self.reply("{}")
            moves = [m for m in req.get("moves", []) if m.isalnum() and len(m) <= 5]
            ms = max(50, min(int(req.get("movetime", 1000)), 30000))
            send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
            send(f"go movetime {ms}")
            lines = read_until("bestmove")
        infos = [l for l in lines if l.startswith("info depth")]
        self.reply(json.dumps({"bestmove": lines[-1].split()[1], "info": infos[-1] if infos else ""}))


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Play Sensei</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/chess.js/0.10.3/chess.min.js"></script>
<style>
:root{--bg:#f4f1ea;--fg:#222;--light:#eed9b6;--dark:#b58863;--hi:#f6f669aa;--sel:#7fb36a;--panel:#fff}
@media (prefers-color-scheme:dark){:root{--bg:#1d1d1f;--fg:#e8e8e8;--panel:#2a2a2d}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif;display:flex;justify-content:center}
main{display:flex;gap:24px;flex-wrap:wrap;padding:16px;max-width:1000px}
#board{display:grid;grid-template-columns:repeat(8,1fr);grid-template-rows:repeat(8,1fr);width:min(92vw,560px);aspect-ratio:1;border:2px solid #444;user-select:none}
.sq{display:flex;align-items:center;justify-content:center;font-size:min(9vw,54px);cursor:pointer;position:relative;min-height:0;line-height:1;overflow:hidden}
.l{background:var(--light)}.d{background:var(--dark)}.last{box-shadow:inset 0 0 0 100px var(--hi)}
.sel{box-shadow:inset 0 0 0 4px var(--sel)}.dot::after{content:"";width:28%;height:28%;border-radius:50%;background:#0003;position:absolute}
.w{color:#fff;text-shadow:0 0 2px #000,0 0 1px #000}.b{color:#111}
aside{flex:1;min-width:260px;background:var(--panel);border-radius:8px;padding:16px}
button,select{font:inherit;padding:6px 10px;margin:2px 4px 2px 0}
#status{font-weight:600;margin:10px 0}#info{font-family:ui-monospace,monospace;font-size:13px;word-break:break-word;min-height:3em}
#moves{font-family:ui-monospace,monospace;max-height:240px;overflow:auto;white-space:pre-wrap}
</style></head><body><main>
<div id="board"></div>
<aside>
<h2 style="margin-top:0">Sensei</h2>
<div><label>You play <select id="color"><option value="w">White</option><option value="b">Black</option></select></label>
<label>Engine time <select id="time"><option>200</option><option selected>1000</option><option>3000</option><option>10000</option></select> ms</label></div>
<div><button id="new">New game</button><button id="undo">Undo</button><button id="flip">Flip</button></div>
<div id="status"></div>
<div>Engine: <span id="info">–</span></div>
<h3>Moves</h3><div id="moves"></div>
</aside></main>
<script>
const G = {UNI:{p:"♟",n:"♞",b:"♝",r:"♜",q:"♛",k:"♚"}};
let game = new Chess(), me = "w", flipped = false, sel = null, last = null, thinking = false;
const $ = id => document.getElementById(id);
function squares(){ const f="abcdefgh", out=[]; for(let r=7;r>=0;r--) for(let c=0;c<8;c++) out.push(f[c]+(r+1)); return flipped?out.reverse():out; }
function render(){
  const b=$("board"); b.innerHTML="";
  const targets = sel ? game.moves({square:sel,verbose:true}).map(m=>m.to) : [];
  for(const s of squares()){
    const d=document.createElement("div"), p=game.get(s), dark=("abcdefgh".indexOf(s[0])+ +s[1])%2===1;
    d.className="sq "+(dark?"d":"l")+(last&&(s===last.from||s===last.to)?" last":"")+(s===sel?" sel":"")+(targets.includes(s)?" dot":"");
    if(p){ d.textContent=G.UNI[p.type]; d.classList.add(p.color); }
    d.onclick=()=>click(s); b.appendChild(d);
  }
  const h=game.history(); let t=""; for(let i=0;i<h.length;i+=2) t+=`${i/2+1}. ${h[i]} ${h[i+1]||""}\n`; $("moves").textContent=t; $("moves").scrollTop=1e9;
  $("status").textContent = game.in_checkmate() ? `Checkmate — ${game.turn()==="w"?"Black":"White"} wins`
    : game.in_stalemate() ? "Stalemate" : game.in_threefold_repetition() ? "Draw by repetition"
    : game.insufficient_material() ? "Draw: insufficient material" : game.in_draw() ? "Draw (50-move rule)"
    : thinking ? "Sensei is thinking…" : (game.turn()===me ? "Your move" : "") + (game.in_check() ? " — check!" : "");
}
function click(s){
  if(thinking || game.game_over() || game.turn()!==me) return;
  const p=game.get(s);
  if(sel){
    const m=game.moves({square:sel,verbose:true}).find(m=>m.to===s);
    if(m){ let promo; if(m.flags.includes("p")){ promo=(prompt("Promote to (q, r, b, n)","q")||"q").toLowerCase()[0]; if(!"qrbn".includes(promo)) promo="q"; }
      last=game.move({from:sel,to:s,promotion:promo}); sel=null; render(); engineMove(); return; }
  }
  sel = p && p.color===me ? s : null; render();
}
async function engineMove(){
  if(game.game_over() || game.turn()===me) return;
  thinking=true; render();
  const moves=game.history({verbose:true}).map(m=>m.from+m.to+(m.promotion||""));
  const r=await (await fetch("/move",{method:"POST",body:JSON.stringify({moves,movetime:+$("time").value})})).json();
  thinking=false;
  const bm=r.bestmove; if(bm && bm!=="0000") last=game.move({from:bm.slice(0,2),to:bm.slice(2,4),promotion:bm[4]});
  const i=r.info.match(/depth (\d+) score (cp|mate) (-?\d+).* pv (.*)/);
  if(i){ let sc = i[2]==="mate" ? `mate in ${i[3]}` : (i[3]/100).toFixed(2); if(i[2]==="cp") sc=(i[3]>0?"+":"")+sc;
    $("info").textContent=`${bm}  ·  depth ${i[1]}  ·  ${sc} (for Sensei)  ·  pv ${i[4].split(" ").slice(0,6).join(" ")}`; }
  render();
}
async function newGame(){ await fetch("/new",{method:"POST",body:"{}"}); game=new Chess(); me=$("color").value; flipped= me==="b"; sel=last=null; $("info").textContent="–"; render(); engineMove(); }
$("new").onclick=newGame; $("color").onchange=newGame;
$("flip").onclick=()=>{flipped=!flipped; render();};
$("undo").onclick=()=>{ if(thinking) return; game.undo(); if(game.turn()!==me) game.undo(); sel=null; last=null; render(); };
newGame();
</script></body></html>"""

print(f"Sensei at http://localhost:{args.port}  (Ctrl+C to stop)", flush=True)
ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
