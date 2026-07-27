/* BambuHelper Companion - UI logic.
 *
 * Talks to the local server in server.py. Every /api/ call carries the session
 * token injected into this page at serve time; see the security note in that
 * file for why.
 *
 * Long jobs (network scan, diagnostic) run server-side on a worker thread and
 * are followed here by polling with a cursor, so the console fills in as the
 * work happens instead of arriving in one lump at the end.
 */
(function () {
  "use strict";

  var $ = function (id) { return document.getElementById(id); };
  var native = window.BH.native;

  // Client-side mirror of the selected printer, for the display-only bits.
  var picked = null;
  // Set false by /api/hello when paho-mqtt is missing. Kept separate from the
  // printer selection so picking a printer cannot re-enable a button the
  // capability check disabled.
  var canDiagnose = true;

  // ── api ─────────────────────────────────────────────────────────────────
  function api(path, body) {
    var opts = {
      method: body === undefined ? "GET" : "POST",
      headers: { "X-Companion-Token": window.BH.token },
      cache: "no-store"
    };
    if (body !== undefined) {
      opts.headers["Content-Type"] = "application/json";
      opts.body = JSON.stringify(body || {});
    }
    return fetch(path, opts).then(function (r) {
      return r.text().then(function (text) {
        var data;
        try { data = text ? JSON.parse(text) : {}; } catch (e) { data = { raw: text }; }
        if (!r.ok) {
          throw new Error(data.error || data.raw || ("HTTP " + r.status));
        }
        return data;
      });
    });
  }

  // ── small helpers ───────────────────────────────────────────────────────
  function show(el, on) { el.classList.toggle("hidden", !on); }

  function banner(el, level, text) {
    el.className = "banner" + (level ? " " + level : "");
    el.textContent = text || "";
    if (!text) el.classList.add("hidden"); else el.classList.remove("hidden");
  }

  var toastTimer = null;
  function toast(text, level) {
    var el = $("toast");
    el.textContent = text;
    el.className = "toast show" + (level ? " " + level : "");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.className = "toast"; }, 3400);
  }

  function busy(spinner, button, on) {
    show(spinner, on);
    if (button) button.disabled = on;
  }

  // ── shell: nav, theme, external links ───────────────────────────────────
  var CRUMB = { setup: "Setup", diag: "Diagnostic", help: "Help" };

  function navigate(name) {
    document.querySelectorAll(".page").forEach(function (p) {
      p.classList.toggle("active", p.dataset.page === name);
    });
    document.querySelectorAll(".nav-item[data-nav]").forEach(function (b) {
      if (b.dataset.nav === name) b.setAttribute("aria-current", "true");
      else b.removeAttribute("aria-current");
    });
    $("crumb").textContent = CRUMB[name] || "";
    document.querySelector(".main").scrollTo(0, 0);
    window.scrollTo(0, 0);
  }

  document.querySelectorAll(".nav-item[data-nav]").forEach(function (b) {
    b.addEventListener("click", function () { navigate(b.dataset.nav); });
  });

  // Links must not navigate the app window away from the UI - hand them to the
  // OS browser instead. In native mode that goes through the pywebview bridge.
  document.querySelectorAll("[data-open]").forEach(function (b) {
    b.addEventListener("click", function () {
      api("/api/open", { url: b.dataset.open }).catch(function () {});
    });
  });

  $("theme-toggle").addEventListener("click", function () {
    var root = document.documentElement;
    var dark = root.getAttribute("data-theme") === "dark";
    if (dark) root.removeAttribute("data-theme");
    else root.setAttribute("data-theme", "dark");
    try { localStorage.setItem("bhTheme", dark ? "light" : "dark"); } catch (_) {}
  });

  $("pw-toggle").addEventListener("click", function () {
    var input = $("password");
    var hidden = input.type === "password";
    input.type = hidden ? "text" : "password";
    this.textContent = hidden ? "hide" : "show";
  });

  // ── step rail ───────────────────────────────────────────────────────────
  function setSteps(done) {
    document.querySelectorAll(".step").forEach(function (s) {
      var n = parseInt(s.dataset.step, 10);
      s.classList.toggle("done", n < done);
      s.classList.toggle("active", n === done);
    });
  }

  // ── printer mode switch ─────────────────────────────────────────────────
  function currentMode() {
    var el = document.querySelector('input[name="pmode"]:checked');
    return el ? el.value : "cloud";
  }

  function applyMode() {
    var cloud = currentMode() === "cloud";
    show($("cloud-login"), cloud && !$("cloud-pick").dataset.ready);
    show($("cloud-2fa"), false);
    show($("cloud-pick"), cloud && !!$("cloud-pick").dataset.ready);
    show($("lan-form"), !cloud);
  }

  document.querySelectorAll('input[name="pmode"]').forEach(function (r) {
    r.addEventListener("change", applyMode);
  });

  // ── cloud login ─────────────────────────────────────────────────────────
  function fillPrinters(devices) {
    var sel = $("printer-select");
    sel.innerHTML = "";
    devices.forEach(function (d) {
      var opt = document.createElement("option");
      opt.value = d.serial;
      opt.textContent = (d.name || "Unnamed") + "  (" + (d.model || "?") + ")  -  " + d.serial;
      sel.appendChild(opt);
    });
    $("cloud-pick").dataset.ready = "1";
    show($("cloud-login"), false);
    show($("cloud-2fa"), false);
    show($("cloud-pick"), true);
    if (!devices.length) {
      banner($("login-msg"), "warn", "No printers are bound to this account.");
      show($("login-msg"), true);
    }
  }

  $("btn-login").addEventListener("click", function () {
    var email = $("email").value.trim();
    var password = $("password").value;
    if (!email || !password) {
      banner($("login-msg"), "fail", "Enter your email and password.");
      return;
    }
    banner($("login-msg"), "", "");
    busy($("login-spin"), $("btn-login"), true);
    api("/api/login", { email: email, password: password, region: $("region").value })
      .then(function (res) {
        // The password has done its job - drop it from the DOM as well.
        $("password").value = "";
        if (res.state === "ok") {
          fillPrinters(res.devices || []);
          return;
        }
        var totp = res.state === "tfa";
        $("tfa-help").textContent = totp
          ? "Two-factor is on for this account. Enter the 6-digit code from your authenticator app."
          : "Bambu emailed you a verification code. Enter it here.";
        show($("cloud-login"), false);
        show($("cloud-2fa"), true);
        $("tfa-code").focus();
      })
      .catch(function (e) { banner($("login-msg"), "fail", e.message); })
      .then(function () { busy($("login-spin"), $("btn-login"), false); });
  });

  $("btn-verify").addEventListener("click", function () {
    var code = $("tfa-code").value.trim();
    if (!code) { toast("Enter the code first.", "fail"); return; }
    busy($("verify-spin"), $("btn-verify"), true);
    api("/api/verify", { code: code })
      .then(function (res) { fillPrinters(res.devices || []); })
      .catch(function (e) {
        banner($("login-msg"), "fail", e.message);
        show($("cloud-2fa"), false);
        show($("cloud-login"), true);
      })
      .then(function () { busy($("verify-spin"), $("btn-verify"), false); });
  });

  $("btn-verify-back").addEventListener("click", function () {
    show($("cloud-2fa"), false);
    show($("cloud-login"), true);
  });

  $("btn-signout").addEventListener("click", function () {
    api("/api/logout", {}).catch(function () {});
    delete $("cloud-pick").dataset.ready;
    $("printer-select").innerHTML = "";
    banner($("login-msg"), "", "");
    setPicked(null);
    applyMode();
  });

  $("btn-use-cloud").addEventListener("click", function () {
    var serial = $("printer-select").value;
    if (!serial) { toast("No printer to select.", "fail"); return; }
    var label = $("printer-select").selectedOptions[0].textContent;
    api("/api/select", { serial: serial })
      .then(function (res) {
        setPicked({
          mode: "cloud",
          name: res.name || "",
          serial: res.serial,
          broker: $("region").value === "cn" ? "cn.mqtt.bambulab.com" : "us.mqtt.bambulab.com"
        });
        toast("Printer selected: " + (res.name || res.serial), "ok");
        if (!$("friendly").value) $("friendly").value = (res.name || "").slice(0, 24);
      })
      .catch(function (e) { toast(e.message, "fail"); });
    void label;
  });

  // ── LAN ─────────────────────────────────────────────────────────────────
  $("btn-use-lan").addEventListener("click", function () {
    var ip = $("lan-ip").value.trim();
    var code = $("lan-code").value.trim();
    var serial = $("lan-serial").value.trim().toUpperCase();
    $("lan-serial").value = serial;
    banner($("lan-msg"), "", "");
    api("/api/lan", { ip: ip, code: code, serial: serial })
      .then(function (res) {
        setPicked({ mode: "lan", name: "", serial: res.serial, broker: ip });
        toast("Printer set: " + res.serial, "ok");
      })
      .catch(function (e) { banner($("lan-msg"), "fail", e.message); });
  });

  // ── selected-printer state ──────────────────────────────────────────────
  function setPicked(p) {
    picked = p;
    var chosen = $("printer-chosen");
    if (!p) {
      show(chosen, false);
      $("sb-printer").textContent = "No printer selected";
      $("sb-dot").classList.remove("on");
      $("btn-push").disabled = true;
      $("btn-diag").disabled = true;
      show($("diag-nopick"), true);
      setSteps(1);
    } else {
      var who = p.name || p.serial;
      chosen.textContent = "Using " + who + " (" + p.mode.toUpperCase() + ", " + p.serial + ")";
      chosen.className = "banner ok";
      show(chosen, true);
      $("sb-printer").textContent = who;
      $("sb-dot").classList.add("on");
      $("btn-push").disabled = false;
      $("btn-diag").disabled = !canDiagnose;
      show($("diag-nopick"), !canDiagnose);
      setSteps(2);
    }
    $("dt-name").textContent = p ? (p.name || "unnamed") : "none selected";
    $("dt-mode").textContent = p ? p.mode.toUpperCase() : "-";
    $("dt-broker").textContent = p ? p.broker : "-";
    $("dt-serial").textContent = p ? p.serial : "-";
  }

  // ── network scan ────────────────────────────────────────────────────────
  var scanCursor = 0;
  var foundSig = "";

  function renderFound(found) {
    // The poll fires every 400ms while the sweep runs. Rebuilding the list
    // each time would wipe the row the user just clicked, so only redraw when
    // the set of devices actually changed.
    var sig = found.map(function (d) { return d.ip + ":" + d.configured + d.connected; }).join("|");
    if (sig === foundSig) return;
    foundSig = sig;

    var list = $("device-list");
    list.innerHTML = "";
    found.forEach(function (d) {
      var row = document.createElement("button");
      row.type = "button";
      row.className = "device-row";
      row.innerHTML =
        '<span class="ip"></span><span class="who"></span><span class="tag"></span>';
      row.querySelector(".ip").textContent = d.ip;
      row.querySelector(".who").textContent = d.name || "unnamed";
      var tag = row.querySelector(".tag");
      if (!d.configured) { tag.textContent = "not set up"; tag.classList.add("new"); }
      else if (d.connected) { tag.textContent = "connected"; }
      else { tag.textContent = "configured"; }
      row.addEventListener("click", function () {
        list.querySelectorAll(".device-row").forEach(function (r) {
          r.classList.remove("selected");
        });
        row.classList.add("selected");
        $("bh-ip").value = d.ip;
      });
      list.appendChild(row);
    });
    // Auto-pick the only hit, but never overwrite an IP the user typed.
    if (found.length === 1 && !$("bh-ip").value.trim()) {
      list.firstChild.classList.add("selected");
      $("bh-ip").value = found[0].ip;
    }
  }

  function pollScan() {
    api("/api/scan?since=" + scanCursor).then(function (snap) {
      scanCursor = snap.cursor;
      var last = snap.lines.length ? snap.lines[snap.lines.length - 1] : null;
      if (last) $("scan-status").textContent = last.text;
      renderFound(snap.found || []);
      if (snap.running) {
        setTimeout(pollScan, 400);
      } else {
        busy($("scan-spin"), $("btn-scan"), false);
        var n = (snap.found || []).length;
        $("scan-status").textContent = n
          ? n + " device" + (n === 1 ? "" : "s") + " found"
          : "Nothing found - type the IP by hand.";
        $("scan-status").className = "status-line " + (n ? "ok" : "warn");
      }
    }).catch(function (e) {
      busy($("scan-spin"), $("btn-scan"), false);
      toast(e.message, "fail");
    });
  }

  $("btn-scan").addEventListener("click", function () {
    scanCursor = 0;
    foundSig = "";
    $("scan-status").className = "status-line";
    $("scan-status").textContent = "Starting scan...";
    busy($("scan-spin"), $("btn-scan"), true);
    api("/api/scan/start", {})
      .then(function () { pollScan(); })
      .catch(function (e) {
        busy($("scan-spin"), $("btn-scan"), false);
        toast(e.message, "fail");
      });
  });

  // ── push config ─────────────────────────────────────────────────────────
  $("btn-push").addEventListener("click", function () {
    var ip = $("bh-ip").value.trim();
    if (!ip) { banner($("push-msg"), "fail", "Enter the BambuHelper IP, or scan for it."); return; }
    banner($("push-msg"), "", "");
    busy($("push-spin"), $("btn-push"), true);
    api("/api/push", {
      ip: ip,
      slot: parseInt($("slot").value, 10),
      name: $("friendly").value.trim()
    })
      .then(function (res) {
        banner($("push-msg"), res.level, res.message);
        if (res.level === "ok") {
          setSteps(4);
          toast("Config sent to " + ip, "ok");
        } else {
          toast(res.level === "warn" ? "Sent with warnings" : "Failed", res.level === "warn" ? "" : "fail");
        }
      })
      .catch(function (e) { banner($("push-msg"), "fail", e.message); })
      .then(function () { busy($("push-spin"), $("btn-push"), false); });
  });

  // ── diagnostic ──────────────────────────────────────────────────────────
  var diagCursor = 0;

  function appendLines(lines) {
    var box = $("diag-console");
    // Only keep pinned to the bottom if the user hasn't scrolled up to read.
    var pinned = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
    lines.forEach(function (l) {
      var span = document.createElement("span");
      span.className = "l " + (l.level || "info");
      span.textContent = l.text;
      box.appendChild(span);
    });
    if (pinned) box.scrollTop = box.scrollHeight;
  }

  function renderSummary(snap) {
    if (!snap.summary) return;
    var box = $("diag-summary");
    box.innerHTML = "";
    snap.summary.forEach(function (row) {
      var div = document.createElement("div");
      div.className = "summary-row";
      div.innerHTML = '<span class="verdict"></span><span class="label"></span>';
      var v = div.querySelector(".verdict");
      v.textContent = row.ok ? "PASS" : "FAIL";
      v.classList.add(row.ok ? "pass" : "fail");
      div.querySelector(".label").textContent = row.label;
      box.appendChild(div);
    });
    if (snap.verdict) banner($("diag-verdict"), snap.verdict.level, snap.verdict.text);
    show($("diag-summary-card"), true);
    $("btn-save-pushall").disabled = !snap.has_dump;
    $("btn-save-ams").disabled = !snap.has_dump;
    $("btn-save-log").disabled = false;
  }

  function pollDiag() {
    api("/api/diag?since=" + diagCursor).then(function (snap) {
      diagCursor = snap.cursor;
      appendLines(snap.lines || []);
      if (snap.running) {
        setTimeout(pollDiag, 500);
        return;
      }
      busy($("diag-spin"), $("btn-diag"), false);
      show($("btn-diag-cancel"), false);
      $("diag-status").textContent = snap.error ? "Stopped" : "Finished";
      $("diag-status").className = "status-line " + (snap.error ? "error" : "ok");
      renderSummary(snap);
    }).catch(function (e) {
      busy($("diag-spin"), $("btn-diag"), false);
      show($("btn-diag-cancel"), false);
      toast(e.message, "fail");
    });
  }

  $("btn-diag").addEventListener("click", function () {
    diagCursor = 0;
    $("diag-console").innerHTML = "";
    show($("diag-summary-card"), false);
    $("diag-status").textContent = "Running...";
    $("diag-status").className = "status-line";
    busy($("diag-spin"), $("btn-diag"), true);
    show($("btn-diag-cancel"), true);
    api("/api/diag/start", {})
      .then(function () { pollDiag(); })
      .catch(function (e) {
        busy($("diag-spin"), $("btn-diag"), false);
        show($("btn-diag-cancel"), false);
        toast(e.message, "fail");
      });
  });

  $("btn-diag-cancel").addEventListener("click", function () {
    api("/api/diag/cancel", {}).catch(function () {});
    $("diag-status").textContent = "Stopping...";
  });

  // ── saving dumps ────────────────────────────────────────────────────────
  // In the native window a Blob download is a no-op, so the server opens a real
  // Save dialog for us. In a plain browser we fetch and download normally.
  var SAVE_NAMES = {
    pushall: "pushall_dump.json",
    ams: "ams_dump.json",
    log: "diagnostic_log.txt"
  };

  function saveDump(what) {
    if (native) {
      api("/api/save", { what: what })
        .then(function (res) {
          if (res.cancelled) return;
          if (res.ok) toast("Saved to " + res.path, "ok");
          else toast(res.error || "Could not save the file.", "fail");
        })
        .catch(function (e) { toast(e.message, "fail"); });
      return;
    }
    fetch("/api/diag/dump?what=" + what, {
      headers: { "X-Companion-Token": window.BH.token },
      cache: "no-store"
    })
      .then(function (r) {
        if (!r.ok) throw new Error("Nothing captured yet.");
        return r.blob();
      })
      .then(function (blob) {
        var url = URL.createObjectURL(blob);
        var a = document.createElement("a");
        a.href = url;
        a.download = SAVE_NAMES[what] || "dump.json";
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        setTimeout(function () { URL.revokeObjectURL(url); }, 5000);
      })
      .catch(function (e) { toast(e.message, "fail"); });
  }

  $("btn-save-pushall").addEventListener("click", function () { saveDump("pushall"); });
  $("btn-save-ams").addEventListener("click", function () { saveDump("ams"); });
  $("btn-save-log").addEventListener("click", function () { saveDump("log"); });

  // ── boot ────────────────────────────────────────────────────────────────
  $("app-version").textContent = "v" + window.BH.version;
  $("foot-version").textContent = "v" + window.BH.version;
  setPicked(null);
  applyMode();

  api("/api/hello").then(function (info) {
    if (!info.mqtt) {
      canDiagnose = false;
      $("btn-diag").disabled = true;
      banner($("diag-nopick"), "fail",
        "paho-mqtt is not installed, so the diagnostic cannot run. Install it with: pip install paho-mqtt");
      show($("diag-nopick"), true);
    }
  }).catch(function () {});
})();
