// Emscripten --pre-js of the web build (MODULARIZE + EXPORT_ES6, factory `createWolf3d`).
// `Module` here is the object the host passed to createWolf3d({...}). Contract: web/WEB.md.
//
// - Data directory (`Module.blissDataDir`, default "/data"): created before the host's preRun
//   callbacks run, so they can FS.writeFile() the game files into it; it becomes the working
//   directory of the game. Wolf4SDL looks for lower-case names (vswap.wl1, ...): files the host
//   wrote in upper case are renamed when main() starts. Config and save games are written there too (MEMFS, not
//   persisted).
// - Canvas: SDL addresses it as "#canvas"; it is mapped to the host's canvas element whatever
//   its id is. Made focusable (keyboard input is bound to it, SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT),
//   focused on pointer down, no context menu. Its pixel size is 320x200; the host scales it
//   with CSS (4:3).
// - Resumes the SDL2 AudioContext on the first user gesture (autoplay policy) unless the host
//   paused the game.
(function () {
  var dataDir = Module['blissDataDir'] || '/data';
  Module['blissDataDir'] = dataDir;

  var preRun = Module['preRun'] || [];
  if (typeof preRun === 'function') preRun = [preRun];
  preRun.unshift(function () {
    if (typeof specialHTMLTargets !== 'undefined' && Module['canvas'])
      specialHTMLTargets['#canvas'] = Module['canvas'];
    FS.mkdirTree(dataDir);
  });
  // After the host (and the dev --preload-file package) put the files in place.
  // (Upper-case names are renamed by main(), after asynchronous run dependencies resolved.)
  preRun.push(function () { FS.chdir(dataDir); });
  Module['preRun'] = preRun;

  Module['blissStarted'] = false;
  Module['blissOnEventInternal'] = function (type) {
    if (type === 'started') Module['blissStarted'] = true;
  };

  function setupCanvas() {
    var canvas = Module['canvas'];
    if (!canvas || canvas.__blissSetup) return;
    canvas.__blissSetup = true;
    if (!canvas.hasAttribute('tabindex')) canvas.setAttribute('tabindex', '0');
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    canvas.addEventListener('pointerdown', function () { canvas.focus({ preventScroll: true }); });
  }
  setupCanvas();

  function resumeAudio() {
    var sdl2 = Module['SDL2'];
    var ctx = sdl2 && sdl2.audioContext;
    if (!ctx || ctx.state === 'running') return;
    if (Module['_bliss_is_paused'] && Module['_bliss_is_paused']()) return;
    ctx.resume();
  }
  if (typeof window !== 'undefined') {
    ['keydown', 'pointerdown', 'touchstart'].forEach(function (type) {
      window.addEventListener(type, resumeAudio, { capture: true, passive: true });
    });
  }
})();
