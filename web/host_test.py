"""End-to-end check of the web build through web/host-test.html (dev only).

Serve a directory that contains both the repository and the shareware data, e.g.
    python -m http.server 8777 --bind 127.0.0.1 --directory M:/Projects
and run
    python web/host_test.py --base http://127.0.0.1:8777/bliss-lab/wolf4sdl/web/host-test.html \
        --data /bliss/assets/wolf3d/ --browser chromium --seconds 60 --shots out/

Flow: start -> signon/title -> main menu -> New Game -> episode 1 -> default difficulty ->
walk/turn/shoot/open doors for --seconds -> host pause/resume -> master volume -> F10 quit.
Checks: events (started, newGame, pauseChanged, quit), frame pacing (rAF and frames presented
by the game), AudioContext running, audio output present (and silent at volume 0), no console
errors / page errors. Headless only, pointer lock stubbed out. Browsers are muted: Chromium
--mute-audio, Firefox media.volume_scale=0.
Requires: pip install playwright && playwright install chromium firefox
"""
import argparse
import json
import os
import statistics
import sys
import time

from playwright.sync_api import sync_playwright

# Taps everything connected to an AudioContext destination into an analyser
# (window.__audioPeak() = peak |sample| since the last call) to measure the game's output.
AUDIO_TAP_SCRIPT = """
(() => {
  const orig = AudioNode.prototype.connect;
  const taps = new Map();
  let peak = 0;
  AudioNode.prototype.connect = function (dest, ...rest) {
    if (dest instanceof AudioDestinationNode) {
      const ctx = dest.context;
      let tap = taps.get(ctx);
      if (!tap) {
        tap = ctx.createAnalyser();
        tap.fftSize = 2048;
        const buf = new Float32Array(tap.fftSize);
        setInterval(() => {
          tap.getFloatTimeDomainData(buf);
          for (const v of buf) peak = Math.max(peak, Math.abs(v));
        }, 20);
        taps.set(ctx, tap);
      }
      orig.call(this, tap);
    }
    return orig.call(this, dest, ...rest);
  };
  window.__audioPeak = () => { const p = peak; peak = 0; return p; };
})();
"""

# Never let a page grab the pointer (tests run on the owner's desktop).
NO_POINTER_LOCK_SCRIPT = """
HTMLCanvasElement.prototype.requestPointerLock = function () { return Promise.resolve(); };
Element.prototype.requestPointerLock = function () { return Promise.resolve(); };
Document.prototype.exitPointerLock = function () {};
"""

RAF_SCRIPT = """
window.__raf = [];
(function loop(t){ window.__raf.push(t); requestAnimationFrame(loop); })(performance.now());
"""


def frame_stats(ts):
    d = [b - a for a, b in zip(ts, ts[1:])]
    if not d:
        return {}
    s = sorted(d)
    q = lambda p: s[min(len(s) - 1, int(p * len(s)))]
    median = q(0.5)
    total = (ts[-1] - ts[0]) / 1000
    return {
        "frames": len(d),
        "seconds": round(total, 1),
        "fps": round(len(d) / total, 2),
        "p50_ms": round(median, 2),
        "p95_ms": round(q(0.95), 2),
        "p99_ms": round(q(0.99), 2),
        "max_ms": round(s[-1], 1),
        "pct_over_1.5x_median": round(100 * sum(1 for x in d if x > median * 1.5) / len(d), 3),
        "pct_over_33ms": round(100 * sum(1 for x in d if x > 33.4) / len(d), 3),
    }


class Run:
    def __init__(self, page, shots, tag):
        self.page, self.shots, self.tag = page, shots, tag
        self.checks = {}
        self.console_errors = []

    def ev(self, expr):
        return self.page.evaluate(expr)

    def events(self, kind=None):
        evs = self.ev("() => window.blissEvents || []")
        return [e for e in evs if kind is None or e["type"] == kind]

    def wait_event(self, kind, count=1, timeout=30000):
        self.page.wait_for_function(
            f"() => (window.blissEvents || []).filter(e => e.type === '{kind}').length >= {count}",
            timeout=timeout)

    def frames(self):
        return self.ev("() => wolf3d._bliss_get_frame_count()")

    def audio_state(self):
        return self.ev("() => { const s = wolf3d.SDL2; return s && s.audioContext ? s.audioContext.state : 'none'; }")

    def shot(self, name):
        if self.shots:
            self.page.screenshot(path=os.path.join(self.shots, f"{self.tag}-{name}.png"),
                                 clip=self.canvas_box())

    def canvas_box(self):
        return self.page.locator("#game").bounding_box()

    def key(self, k, hold=0.12, after=0.25):
        # The game samples key state once per frame: hold keys like a person does.
        self.page.keyboard.down(k)
        time.sleep(hold)
        self.page.keyboard.up(k)
        time.sleep(after)

    def pixel(self, fx, fy):
        """RGB of the displayed canvas at fractional position (fx, fy)."""
        from io import BytesIO
        from PIL import Image
        img = Image.open(BytesIO(self.page.screenshot(clip=self.canvas_box()))).convert("RGB")
        return img.getpixel((int(fx * (img.width - 1)), int(fy * (img.height - 1))))

    def wait_menu(self, timeout=15):
        """Waits until the red menu background is shown (keys during fades may be dropped)."""
        end = time.time() + timeout
        while time.time() < end:
            r, g, b = self.pixel(0.02, 0.5)
            if r > 100 and g < 40 and b < 40:
                time.sleep(0.4)
                return True
            time.sleep(0.25)
        return False

    def check(self, name, ok, detail=None):
        self.checks[name] = {"ok": bool(ok), **({"detail": detail} if detail is not None else {})}
        print(f"  [{'ok' if ok else 'FAIL'}] {name}" + (f": {detail}" if detail is not None else ""), flush=True)


def play(run, seconds):
    """Walks the first level: forward, turns, strafing, doors, shooting."""
    kb = run.page.keyboard
    pattern = [  # (keys held, seconds)
        (["ArrowUp"], 1.2), (["ArrowUp", "Control"], 0.8), (["ArrowLeft"], 0.45), (["Space"], 0.2),
        (["ArrowUp", "Shift"], 1.0), (["Control"], 0.6), (["ArrowRight"], 0.9), (["ArrowUp"], 0.8),
        (["Alt", "ArrowLeft"], 0.5), (["Space"], 0.2), (["ArrowDown"], 0.5), (["ArrowRight", "Control"], 0.7),
    ]
    end = time.time() + seconds
    i = 0
    peaks = []
    while time.time() < end:
        keys, dur = pattern[i % len(pattern)]
        for k in keys:
            kb.down(k)
        time.sleep(dur)
        for k in reversed(keys):
            kb.up(k)
        time.sleep(0.05)
        peaks.append(run.ev("() => window.__audioPeak()"))
        i += 1
        if i == 10:
            run.shot("play-1")
        if i == 40:
            run.shot("play-2")
    return peaks


def run_browser(pw, args, name):
    print(f"== {name}", flush=True)
    if name == "chromium":
        browser = pw.chromium.launch(headless=True, args=["--mute-audio"])
    else:
        browser = pw.firefox.launch(headless=True,
                                    firefox_user_prefs={"media.volume_scale": "0.0",
                                                        "media.autoplay.default": 0})
    try:
        page = browser.new_page(viewport={"width": 1100, "height": 900})
        run = Run(page, args.shots, name)
        page.on("console", lambda m: m.type == "error" and run.console_errors.append(m.text))
        if args.verbose:
            page.on("console", lambda m: print(f"    console.{m.type}: {m.text}", flush=True))
        page.on("pageerror", lambda e: run.console_errors.append(f"pageerror: {e}"))
        page.add_init_script(NO_POINTER_LOCK_SCRIPT)
        page.add_init_script(AUDIO_TAP_SCRIPT)
        page.add_init_script(RAF_SCRIPT)
        url = f"{args.base}?data={args.data}" + (f"&build={args.build}" if args.build else "")
        t0 = time.time()
        page.goto(url)
        run.wait_event("started")
        run.check("started", True, f"{round((time.time() - t0) * 1000)} ms after navigation")
        page.focus("#game")
        time.sleep(1.0)
        run.shot("signon")

        # Signon "Press a key" -> PG-13 -> title; any key opens the main menu.
        run.key("Enter", after=1.5)          # PG-13
        run.key("Enter", after=1.5)          # title
        run.shot("title")
        run.key("Enter", after=0.5)          # main menu
        if not run.wait_menu():               # still on a title screen: one more key
            run.key("Enter", after=0.5)
            run.check("main menu shown", run.wait_menu())
        run.shot("menu")
        # Shareware v1.4 starts on "Read This!": up to "New Game" (Save Game is disabled).
        for _ in range(5):
            run.key("ArrowUp", after=0.35)
        run.key("Enter", after=1.0)          # New Game -> episode select
        run.shot("episode")
        run.key("Enter", after=1.0)          # Episode 1 -> difficulty
        run.shot("difficulty")
        run.key("Enter", after=0.2)          # default "Bring 'em on!"
        run.wait_event("newGame", timeout=10000)
        ng = run.events("newGame")[-1]["data"]
        run.check("newGame event", ng.get("episode") == 0, ng)
        time.sleep(3.0)                      # "Get Psyched!" + fade in
        run.shot("level-start")
        run.check("AudioContext running", run.audio_state() == "running", run.audio_state())

        # Gameplay with frame timing.
        page.evaluate("() => { window.__raf = []; }")
        run.ev("() => wolf3d.UTF8ToString(wolf3d._bliss_get_frame_work())")
        f0, t_play = run.frames(), time.time()
        peaks = play(run, args.seconds)
        dt = time.time() - t_play
        game_fps = (run.frames() - f0) / dt
        raf = frame_stats(page.evaluate("() => window.__raf"))
        work = json.loads(run.ev("() => wolf3d.UTF8ToString(wolf3d._bliss_get_frame_work())"))
        run.check("game CPU time per frame", work["avgMs"] < 8, work)
        run.check("rAF pacing", raf.get("pct_over_33ms", 100) < 2, raf)
        run.check("game frames presented", game_fps > 30, f"{game_fps:.1f} fps over {dt:.0f} s")
        audible = sum(1 for p in peaks if p > 0.01)
        run.check("audio output during play", audible > len(peaks) * 0.3,
                  f"{audible}/{len(peaks)} samples audible, max peak {max(peaks):.3f}")
        run.shot("after-play")
        state = run.ev("() => wolf3d._bliss_is_paused()")
        run.check("still running", state == 0 and not run.events("quit"))

        # Host pause / resume.
        page.evaluate("() => wolf3d._bliss_pause()")
        time.sleep(0.3)
        fp = run.frames()
        time.sleep(1.0)
        paused_ok = run.frames() == fp and run.audio_state() == "suspended"
        run.check("pause stops frames + audio", paused_ok, {"frames_delta": run.frames() - fp, "audio": run.audio_state()})
        page.evaluate("() => wolf3d._bliss_resume()")
        time.sleep(0.5)
        run.check("resume", run.frames() > fp and run.audio_state() == "running", run.audio_state())
        pc = [e["data"]["paused"] for e in run.events("pauseChanged")]
        run.check("pauseChanged events", pc[-2:] == [True, False], pc)

        # Master volume 0 -> silence, 100 -> sound again.
        page.evaluate("() => wolf3d._bliss_set_master_volume(0)")
        time.sleep(0.4)
        run.ev("() => window.__audioPeak()")
        for k in ["Control", "Control", "Control"]:
            run.key(k, hold=0.3, after=0.2)
        silent = run.ev("() => window.__audioPeak()")
        page.evaluate("() => wolf3d._bliss_set_master_volume(100)")
        run.check("volume 0 silences output", silent < 0.002, f"peak {silent:.4f}")

        # F10 -> "really quit?" -> Y -> quit event.
        run.key("F10", after=1.0)
        run.shot("quit-prompt")
        run.key("y", after=1.0)
        try:
            run.wait_event("quit", timeout=10000)
            run.check("quit event", True, run.events("quit")[-1]["data"])
        except Exception as e:  # noqa: BLE001
            run.check("quit event", False, str(e)[:120])

        run.check("no console errors", not run.console_errors, run.console_errors[:5])
        return {"browser": name, "checks": run.checks, "frame_stats": raf, "game_fps": round(game_fps, 2),
                "frame_work": work}
    finally:
        browser.close()   # only the browser this script started


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="http://127.0.0.1:8777/bliss-lab/wolf4sdl/web/host-test.html")
    ap.add_argument("--data", default="/bliss/assets/wolf3d/")
    ap.add_argument("--build", default=None)
    ap.add_argument("--browser", default="chromium", choices=["chromium", "firefox", "all"])
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--shots", default=None)
    ap.add_argument("--verbose", action="store_true", help="print the page console")
    ap.add_argument("--json", default=None, help="write results here")
    args = ap.parse_args()
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)
    names = ["chromium", "firefox"] if args.browser == "all" else [args.browser]
    results = []
    with sync_playwright() as pw:
        for n in names:
            results.append(run_browser(pw, args, n))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=2)
    failed = [f"{r['browser']}:{k}" for r in results for k, v in r["checks"].items() if not v["ok"]]
    print("FAILED: " + ", ".join(failed) if failed else "ALL OK")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
