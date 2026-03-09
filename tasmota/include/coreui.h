#pragma once
#include <Arduino.h>

const char HTTP_COREUI_JS[] PROGMEM = R"TSCOREUI(
(function (w, d) {
  'use strict';

  var TS = w.TS || (w.TS = {});

  TS.rootRefreshMs = 2000;
  TS.consoleRefreshMs = 2000;

  TS.counter = {
    remaining: 180,
    timer: null
  };

  TS.reload = {
    timer: null
  };

  TS.root = {
    xhr: null,
    loopTimer: null,
    failTimer: null
  };

  TS.console = {
    xhr: null,
    loopTimer: null,
    failTimer: null,
    scrollTop: 0,
    logId: 0,
    history: [],
    historyPos: 0,
    bound: false
  };

  TS.init = {
    shellBound: false,
    jdDone: false
  };

  function eb(id) {
    return d.getElementById(id);
  }

  function qs(sel) {
    return d.querySelector(sel);
  }

  function wl(fn) {
    w.addEventListener('load', fn);
  }

  function jd() {
    if (TS.init.jdDone) { return; }
    TS.init.jdDone = true;

    var items = d.querySelectorAll('input,button,textarea,select');
    var i, el;

    for (i = 0; i < items.length; i++) {
      el = items[i];
      if (el.hasAttribute('id') && !el.hasAttribute('name')) {
        el.name = el.id;
      }
    }
  }

  function sf(show) {
    var items = d.querySelectorAll('.hf');
    var i;
    for (i = 0; i < items.length; i++) {
      items[i].style.display = show ? 'block' : 'none';
    }
  }

  function su(t) {
    var f3 = eb('f3');
    var f2 = eb('f2');

    if (f3) { f3.style.display = 'none'; }
    if (f2) { f2.style.display = 'block'; }

    if (t && t.form) {
      t.form.submit();
    }
  }

  function fct(t) {
    var xhr = new XMLHttpRequest();

    xhr.open('GET', '/u4?u4=fct&api=', true);
    xhr.onreadystatechange = function () {
      if (xhr.readyState === 4 && xhr.status === 200) {
        var s = xhr.responseText;
        if (s === 'false') {
          setTimeout(function () { fct(t); }, 6000);
        }
        if (s === 'true') {
          setTimeout(function () { su(t); }, 1000);
        }
      } else if (xhr.readyState === 4 && xhr.status === 0) {
        setTimeout(function () { fct(t); }, 2000);
      }
    };
    xhr.send();
  }

  function upl(t) {
    if (!t || !t.form || !t.form['u2'] || !t.form['u2'].files || !t.form['u2'].files[0]) {
      return false;
    }

    var sl = t.form['u2'].files[0].slice(0, 1);
    var rd = new FileReader();

    rd.onload = function () {
      var bb = new Uint8Array(rd.result);
      if (bb.length === 1 && bb[0] === 0xE9) {
        fct(t);
      } else {
        t.form.submit();
      }
    };

    rd.readAsArrayBuffer(sl);
    return false;
  }

  function counterTick() {
    var e = eb('t');

    if (TS.counter.remaining >= 0) {
      if (e) {
        e.innerHTML = 'Restart in ' + TS.counter.remaining + ' seconds';
      }
      TS.counter.remaining--;
      TS.counter.timer = setTimeout(counterTick, 1000);
    }
  }

  function tsCounterStart(seconds) {
    clearTimeout(TS.counter.timer);

    TS.counter.remaining = (typeof seconds === 'number') ? seconds : 180;
    counterTick();
  }

  function tsRfIds() {
    var n = d.querySelectorAll('[data-tsrf="1"][id]');
    var a = [];
    var i;

    for (i = 0; i < n.length; i++) {
      a.push(n[i].id);
    }
    return a.join(',');
  }

  function tsRfFmt(s) {
    return s
      .replace(/{t}/g, "<table style='width:100%'>")
      .replace(/{s}/g, "<tr><th>")
      .replace(/{m}/g, "</th><td style='width:20px;white-space:nowrap'>")
      .replace(/{e}/g, "</td></tr>");
  }

  function tsRfApply(s) {
    var b = '~#RF#~';
    var m = '~#RM#~';
    var e = '~#RE#~';
    var p = 0;

    while (true) {
      var i = s.indexOf(b, p);
      if (i < 0) { break; }

      var j = s.indexOf(m, i + b.length);
      if (j < 0) { break; }

      var k = s.indexOf(e, j + m.length);
      if (k < 0) { break; }

      var id = s.substring(i + b.length, j);
      var html = s.substring(j + m.length, k);

      if (id === '@js') {
        try { (new Function(html))(); } catch (ex) {}
      } else {
        var el = eb(id);
        if (el) {
          el.innerHTML = tsRfFmt(html);
        }
      }

      p = k + e.length;
    }
  }

  function la(extra) {
    var a = extra || '';
    var ids = tsRfIds();
    var u = '?m=1';

    if (!ids && !a) { return; }

    clearTimeout(TS.root.failTimer);
    clearTimeout(TS.root.loopTimer);

    if (TS.root.xhr !== null) {
      TS.root.xhr.abort();
    }

    if (ids) {
      u += '&rf=' + encodeURIComponent(ids);
    }
    u += a;

    TS.root.xhr = new XMLHttpRequest();
    TS.root.xhr.onreadystatechange = function () {
      if (TS.root.xhr.readyState === 4 && TS.root.xhr.status === 200) {
        tsRfApply(TS.root.xhr.responseText);
        clearTimeout(TS.root.failTimer);
        clearTimeout(TS.root.loopTimer);
        TS.root.loopTimer = setTimeout(function () { la(); }, TS.rootRefreshMs);
      }
    };

    TS.root.xhr.open('GET', u, true);
    TS.root.xhr.send();

    TS.root.failTimer = setTimeout(function () { la(); }, 20000);
  }

  function lc(v, i, p) {
    if (eb('s')) {
      if (v === 'h' || v === 'd') {
        var sl = eb('sl4') ? eb('sl4').value : 0;
        var s = eb('s');
        var sl2 = eb('sl2');
        if (s && sl2) {
          s.style.background =
            'linear-gradient(to right,rgb(' + sl + '%,' + sl + '%,' + sl + '%),hsl(' +
            sl2.value + ',100%,50%))';
        }
      }
    }
    la('&' + v + i + '=' + p);
  }

  function tsRootStart(refreshMs) {
    if (typeof refreshMs === 'number' && refreshMs > 0) {
      TS.rootRefreshMs = refreshMs;
    }
    la();
  }

  function tsSetReload(ms, url) {
    clearTimeout(TS.reload.timer);
    TS.reload.timer = setTimeout(function () {
      location.href = url || '.';
    }, ms);
  }

  function consoleRequest(p) {
    var t = eb('t1');
    var c1 = eb('c1');
    var o = '';

    if (!t) { return false; }

    clearTimeout(TS.console.loopTimer);
    clearTimeout(TS.console.failTimer);

    if (p === 1 && c1) {
      o = '&c1=' + encodeURIComponent(c1.value);
      c1.value = '';
      t.scrollTop = 1000000000;
      TS.console.scrollTop = t.scrollTop;
    }

    if (t.scrollTop >= TS.console.scrollTop) {
      if (TS.console.xhr !== null) {
        TS.console.xhr.abort();
      }

      TS.console.xhr = new XMLHttpRequest();
      TS.console.xhr.onreadystatechange = function () {
        if (TS.console.xhr.readyState === 4 && TS.console.xhr.status === 200) {
          var d = TS.console.xhr.responseText.split(/}1/);
          var z;

          TS.console.logId = d.shift();
          if (d.shift() == 0) {
            t.value = '';
          }

          z = d.shift();
          if (z && z.length > 0) {
            t.value += z;
          }

          t.scrollTop = 1000000000;
          TS.console.scrollTop = t.scrollTop;

          clearTimeout(TS.console.failTimer);
          TS.console.loopTimer = setTimeout(function () {
            consoleRequest();
          }, TS.consoleRefreshMs);
        }
      };

      TS.console.xhr.open('GET', 'cs?c2=' + TS.console.logId + o, true);
      TS.console.xhr.send();

      TS.console.failTimer = setTimeout(function () {
        consoleRequest();
      }, 20000);
    } else {
      TS.console.loopTimer = setTimeout(function () {
        consoleRequest();
      }, TS.consoleRefreshMs);
    }

    return false;
  }

  function consoleBindHistory() {
    var input = eb('c1');

    if (!input || TS.console.bound) { return; }
    TS.console.bound = true;

    input.addEventListener('keydown', function (e) {
      var b = eb('c1');
      var c = e.keyCode;

      if (!b) { return; }

      if (c === 38 || c === 40) {
        b.autocomplete = 'off';
        setTimeout(function () {
          b.focus();
          b.setSelectionRange(1000000000, 1000000000);
        }, 0);
      }

      if (c === 38) {
        TS.console.historyPos++;
        if (TS.console.historyPos > TS.console.history.length) {
          TS.console.historyPos = TS.console.history.length;
        }
        b.value = TS.console.history[TS.console.historyPos - 1] || '';
      } else if (c === 40) {
        TS.console.historyPos--;
        if (TS.console.historyPos < 0) {
          TS.console.historyPos = 0;
        }
        b.value = TS.console.history[TS.console.historyPos - 1] || '';
      } else if (c === 13) {
        if (TS.console.history.length > 19) {
          TS.console.history.pop();
        }
        TS.console.history.unshift(b.value);
        TS.console.historyPos = 0;
      }
    });
  }

  function tsConsoleStart(refreshMs) {
    if (typeof refreshMs === 'number' && refreshMs > 0) {
      TS.consoleRefreshMs = refreshMs;
    }
    consoleBindHistory();
    consoleRequest();
  }

  wl(jd);

  w.eb = eb;
  w.qs = qs;
  w.wl = wl;

  w.jd = jd;
  w.sf = sf;

  w.su = su;
  w.upl = upl;
  w.fct = fct;

  w.u = counterTick;
  w.tsCounterStart = tsCounterStart;

  w.tsRfIds = tsRfIds;
  w.tsRfFmt = tsRfFmt;
  w.tsRfApply = tsRfApply;
  w.la = la;
  w.lc = lc;
  w.tsRootStart = tsRootStart;

  w.tsSetReload = tsSetReload;

  w.l = consoleRequest;
  w.h = consoleBindHistory;
  w.tsConsoleStart = tsConsoleStart;

})(window, document);
)TSCOREUI";

const size_t HTTP_COREUI_JS_LEN = sizeof(HTTP_COREUI_JS) - 1;