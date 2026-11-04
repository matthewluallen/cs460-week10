/* Codespace keepalive - presence-gated, credit-safe.
   WHY: a Codespace idles out (default ~30 min of no terminal/editor activity) and
   stops - even mid-lab - because driving the Studio in a separate browser tab is
   not activity the Codespace counts. This keeps it awake WHILE you are using this
   tab, and - importantly - lets it idle out normally when you are not, so you do
   not spend Codespace credits on a session you walked away from.
   HOW: while this tab is visible AND you have interacted in the last few minutes,
   it pings the Studio server's /__keepalive. The server prints a line to the lab
   terminal; that terminal activity resets the idle timer. Stop interacting (or
   hide/close the tab) and the pings stop within IDLE_GRACE, so the Codespace
   idles out on its own. A server-side hard cap stops holding it awake after
   KEEPALIVE_MAX_MIN (default 4 h) regardless. */
(function () {
  if (window.__cs460_keepalive) return;
  window.__cs460_keepalive = true;
  var PING_MS = 120000;        // ping at most every 2 minutes
  var IDLE_GRACE_MS = 300000;  // stop pinging 5 minutes after the last interaction
  var last = Date.now(), stopped = false, timer = null;
  function bump() { last = Date.now(); }
  ["pointerdown", "keydown", "mousemove", "wheel", "touchstart", "scroll"].forEach(function (e) {
    window.addEventListener(e, bump, { passive: true });
  });
  function tick() {
    if (stopped) return;
    try {
      var active = document.visibilityState === "visible" && (Date.now() - last) < IDLE_GRACE_MS;
      if (!active) return; // away -> let the Codespace idle out (saves credits)
      fetch("/__keepalive", { cache: "no-store" })
        .then(function (r) { return r.json(); })
        .then(function (j) { if (j && j.capped) { stopped = true; if (timer) clearInterval(timer); } })
        .catch(function () {});
    } catch (e) {}
  }
  timer = setInterval(tick, PING_MS);
  document.addEventListener("visibilitychange", function () {
    if (document.visibilityState === "visible") { bump(); tick(); }
  });
  if (document.visibilityState === "visible") tick(); // register a fresh focused tab now
})();
