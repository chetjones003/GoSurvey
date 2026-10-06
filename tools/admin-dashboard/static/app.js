// GoSurvey admin: interactive tables, charts (canvas, no library), clipboard, refresh.
(function () {
  'use strict';

  function store(key, val) {
    try {
      if (val === undefined) return JSON.parse(localStorage.getItem(key));
      localStorage.setItem(key, JSON.stringify(val));
    } catch (e) { /* storage unavailable: widths just don't persist */ }
    return null;
  }
  function css(name, fallback) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
  }

  /* ---------------------------------------------------------------- tables */
  // Every <table class="rt"> gets: click-to-sort headers, drag-to-resize columns,
  // double-click a column edge to fit it to its content, widths remembered.
  var MIN_COL = 56, MAX_AUTO = 560, PAD = 26;

  function initTable(table) {
    if (table.dataset.ready) return;
    table.dataset.ready = '1';
    var name = table.dataset.table || 'table';
    var ths = Array.prototype.slice.call(table.querySelectorAll('thead th'));
    var colgroup = document.createElement('colgroup');
    var cols = ths.map(function () { var c = document.createElement('col'); colgroup.appendChild(c); return c; });
    table.insertBefore(colgroup, table.firstChild);
    var widths = ths.map(function () { return 0; });
    var saved = store('gs.cols.' + name) || {}; // index -> px, only for columns the user resized

    // Real content width of a cell (independent of how wide the cell currently is).
    function contentWidth(cell, extra) {
      // Measure the content only: skip the absolutely-positioned resize handle.
      var kids = Array.prototype.filter.call(cell.childNodes, function (n) { return !(n.classList && n.classList.contains('rs')); });
      var cs = getComputedStyle(cell);
      var pad = parseFloat(cs.paddingLeft) + parseFloat(cs.paddingRight) + (extra || 0);
      if (!kids.length) return pad;
      var r = document.createRange();
      r.setStartBefore(kids[0]); r.setEndAfter(kids[kids.length - 1]);
      return r.getBoundingClientRect().width + pad;
    }
    function natural(i) {
      var w = contentWidth(ths[i], ths[i].dataset.sort ? 14 : 0);
      var rows = table.querySelectorAll('tbody tr');
      for (var r = 0; r < rows.length; r++) {
        var cell = rows[r].cells[i];
        if (cell && cell.colSpan === 1) w = Math.max(w, contentWidth(cell, 0));
      }
      return Math.min(MAX_AUTO, Math.max(MIN_COL, Math.ceil(w) + 4));
    }

    function apply() {
      var total = 0;
      widths.forEach(function (w, i) { cols[i].style.width = w + 'px'; total += w; });
      table.style.width = total + 'px';
    }

    function measure(i) { widths[i] = natural(i); }

    function fitAll() {
      table.style.tableLayout = 'fixed';
      ths.forEach(function (_, i) { if (saved[i]) widths[i] = saved[i]; else measure(i); });
      apply();
    }

    // Drag handles
    ths.forEach(function (th, i) {
      var h = document.createElement('span');
      h.className = 'rs'; h.title = 'Drag to resize · double-click to fit';
      th.appendChild(h);
      h.addEventListener('click', function (e) { e.stopPropagation(); });
      h.addEventListener('dblclick', function (e) {
        e.stopPropagation(); delete saved[i]; store('gs.cols.' + name, saved);
        measure(i); apply();
      });
      h.addEventListener('pointerdown', function (e) {
        e.preventDefault(); e.stopPropagation();
        h.setPointerCapture(e.pointerId);
        var startX = e.clientX, startW = widths[i];
        document.body.classList.add('resizing');
        function move(ev) { widths[i] = Math.max(MIN_COL, startW + ev.clientX - startX); apply(); }
        function up() {
          h.removeEventListener('pointermove', move); h.removeEventListener('pointerup', up);
          document.body.classList.remove('resizing');
          saved[i] = widths[i]; store('gs.cols.' + name, saved);
        }
        h.addEventListener('pointermove', move); h.addEventListener('pointerup', up);
      });
    });

    // Sorting: none -> asc -> desc -> none
    var sortState = { key: '', dir: '' };
    function markSort() {
      ths.forEach(function (th) {
        th.removeAttribute('aria-sort');
        if (th.dataset.sort && th.dataset.sort === sortState.key) {
          th.setAttribute('aria-sort', sortState.dir === 'asc' ? 'ascending' : 'descending');
        }
      });
    }
    function cellValue(td) {
      if (td.dataset.v === undefined) return td.textContent.trim();
      return isNaN(td.dataset.v) ? td.dataset.v : Number(td.dataset.v);
    }
    function sortClient() {
      var tbody = table.tBodies[0];
      var idx = ths.findIndex(function (t) { return t.dataset.sort === sortState.key; });
      var rows = Array.prototype.slice.call(tbody.rows);
      rows.sort(function (a, b) {
        if (!sortState.key) return a.dataset.i - b.dataset.i;
        var x = cellValue(a.cells[idx]), y = cellValue(b.cells[idx]);
        var c = (typeof x === 'number' && typeof y === 'number') ? x - y : String(x).localeCompare(String(y), undefined, { numeric: true });
        return sortState.dir === 'desc' ? -c : c;
      });
      rows.forEach(function (r) { tbody.appendChild(r); });
    }
    if (table.dataset.client) Array.prototype.forEach.call(table.tBodies[0].rows, function (r, n) { r.dataset.i = n; });

    ths.forEach(function (th) {
      if (!th.dataset.sort) return;
      th.classList.add('sortable');
      th.addEventListener('click', function () {
        var k = th.dataset.sort;
        if (sortState.key !== k) sortState = { key: k, dir: 'asc' };
        else if (sortState.dir === 'asc') sortState.dir = 'desc';
        else sortState = { key: '', dir: '' };
        markSort();
        if (table.dataset.client) { sortClient(); return; }
        var f = document.querySelector(table.dataset.filters);
        f.querySelector('[name=sort]').value = sortState.key;
        f.querySelector('[name=dir]').value = sortState.dir;
        var p = new URLSearchParams();
        f.querySelectorAll('input[name],select[name]').forEach(function (el) { if (el.value) p.set(el.name, el.value); });
        htmx.ajax('GET', table.dataset.endpoint + '?' + p.toString(), { target: table.dataset.body, swap: 'innerHTML' });
      });
    });

    fitAll();
    // New rows arrive via HTMX: refit the columns the user hasn't pinned.
    table.addEventListener('htmx:afterSwap', function () {
      ths.forEach(function (_, i) { if (!saved[i]) measure(i); });
      apply();
    });
    window.addEventListener('load', fitAll); // webfonts/late layout can change text widths
  }

  /* ---------------------------------------------------------------- charts */
  function niceStep(max) {
    if (max <= 4) return 1;
    var raw = max / 4, mag = Math.pow(10, Math.floor(Math.log10(raw)));
    var f = [1, 2, 2.5, 5, 10].find(function (m) { return m * mag >= raw; });
    return f * mag;
  }
  function fmtDay(d) {
    var t = new Date(d + 'T00:00:00');
    return isNaN(t) ? d : t.toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric' });
  }

  function draw(c) {
    var src = document.getElementById(c.dataset.src);
    if (!src) return;
    var all = JSON.parse(src.textContent || '[]');
    var range = parseInt(c.dataset.range || '30', 10);
    var data = all.slice(-range);
    var n = data.length;
    var kind = c.dataset.chart, label = c.dataset.label || '';
    var color = css(c.dataset.color || '--accent', '#0f766e');
    var dpr = window.devicePixelRatio || 1;
    var W = c.parentNode.clientWidth, H = parseInt(c.dataset.h || '170', 10);
    c.style.width = W + 'px'; c.style.height = H + 'px';
    c.width = Math.round(W * dpr); c.height = Math.round(H * dpr);
    var ctx = c.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    var font = 'ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif';
    var rawMax = Math.max.apply(null, data.map(function (d) { return d.count; }).concat([0]));
    var step = niceStep(Math.max(1, rawMax));
    var top = Math.max(step, Math.ceil(rawMax / step) * step);
    var ticks = top / step;
    var padL = String(top).length * 7 + 14, padR = 10, padT = 10, padB = 24;
    var plotW = W - padL - padR, plotH = H - padT - padB;
    function xAt(i) { return kind === 'bar' ? padL + plotW * (i + 0.5) / n : padL + (n === 1 ? plotW / 2 : plotW * i / (n - 1)); }
    function yAt(v) { return padT + plotH * (1 - v / top); }
    function crisp(v) { return Math.round(v) + 0.5; }

    function render(hover) {
      ctx.clearRect(0, 0, W, H);
      ctx.font = '11px ' + font;
      ctx.textBaseline = 'middle'; ctx.textAlign = 'right';
      ctx.lineWidth = 1; ctx.lineDash = [];
      for (var g = 0; g <= ticks; g++) {
        var y = crisp(yAt(g * step));
        ctx.strokeStyle = css('--border', '#e2e8f0');
        ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(W - padR, y); ctx.stroke();
        ctx.fillStyle = css('--muted', '#64748b'); ctx.fillText(String(g * step), padL - 8, y);
      }
      if (!n) return;
      ctx.fillStyle = color; ctx.strokeStyle = color;
      if (kind === 'bar') {
        var bw = Math.max(2, Math.min(28, plotW / n * 0.68));
        data.forEach(function (d, i) {
          if (!d.count) return;
          ctx.globalAlpha = hover < 0 || hover === i ? 1 : 0.55;
          var x = Math.round(xAt(i) - bw / 2), y = Math.round(yAt(d.count));
          ctx.beginPath();
          if (ctx.roundRect) ctx.roundRect(x, y, Math.round(bw), Math.round(padT + plotH) - y, [3, 3, 0, 0]);
          else ctx.rect(x, y, bw, padT + plotH - y);
          ctx.fill();
        });
        ctx.globalAlpha = 1;
      } else {
        ctx.lineJoin = 'round'; ctx.lineCap = 'round';
        var grad = ctx.createLinearGradient(0, padT, 0, padT + plotH);
        grad.addColorStop(0, color + '38'); grad.addColorStop(1, color + '05');
        ctx.beginPath();
        data.forEach(function (d, i) { i ? ctx.lineTo(xAt(i), yAt(d.count)) : ctx.moveTo(xAt(i), yAt(d.count)); });
        ctx.lineWidth = 2; ctx.stroke();
        ctx.lineTo(xAt(n - 1), padT + plotH); ctx.lineTo(xAt(0), padT + plotH); ctx.closePath();
        ctx.fillStyle = grad; ctx.fill();
        ctx.fillStyle = color;
        if (n <= 31) data.forEach(function (d, i) { ctx.beginPath(); ctx.arc(xAt(i), yAt(d.count), hover === i ? 5 : 2.5, 0, 6.2832); ctx.fill(); });
      }
      if (hover >= 0) {
        ctx.strokeStyle = css('--muted', '#64748b'); ctx.globalAlpha = 0.5; ctx.lineWidth = 1; ctx.setLineDash([3, 3]);
        ctx.beginPath(); ctx.moveTo(crisp(xAt(hover)), padT); ctx.lineTo(crisp(xAt(hover)), padT + plotH); ctx.stroke();
        ctx.setLineDash([]); ctx.globalAlpha = 1;
      }
      ctx.fillStyle = css('--muted', '#64748b'); ctx.font = '11px ' + font; ctx.textBaseline = 'alphabetic'; ctx.textAlign = 'center';
      var every = Math.max(1, Math.ceil(n / Math.max(2, Math.floor(plotW / 62))));
      data.forEach(function (d, i) {
        if ((n - 1 - i) % every) return;
        ctx.fillText(d.day.slice(5), Math.min(W - padR - 14, Math.max(padL + 14, xAt(i))), H - 6);
      });
    }
    render(-1);

    var wrap = c.parentNode;
    var tip = wrap.querySelector('.chart-tip');
    if (!tip) { tip = document.createElement('div'); tip.className = 'chart-tip'; wrap.appendChild(tip); }
    c.onpointermove = function (e) {
      if (!n) return;
      var r = c.getBoundingClientRect();
      var f = (e.clientX - r.left - padL) / plotW;
      var i = Math.max(0, Math.min(n - 1, kind === 'bar' ? Math.floor(f * n) : Math.round(f * (n - 1))));
      render(i);
      tip.innerHTML = '<b>' + data[i].count.toLocaleString() + '</b> ' + label + '<br><span>' + fmtDay(data[i].day) + '</span>';
      tip.style.display = 'block';
      var tw = tip.offsetWidth;
      tip.style.left = Math.max(0, Math.min(W - tw, xAt(i) - tw / 2)) + 'px';
      tip.style.top = Math.max(0, yAt(data[i].count) - tip.offsetHeight - 10) + 'px';
    };
    c.onpointerleave = function () { tip.style.display = 'none'; render(-1); };
  }

  function drawAll() { document.querySelectorAll('canvas.chart').forEach(draw); }

  /* ------------------------------------------------------------------ misc */
  document.addEventListener('DOMContentLoaded', function () {
    document.querySelectorAll('table.rt').forEach(initTable);
    drawAll();

    // Range buttons (7d / 14d / 30d)
    document.querySelectorAll('.seg').forEach(function (seg) {
      var c = document.getElementById(seg.dataset.for);
      seg.addEventListener('click', function (e) {
        var b = e.target.closest('button'); if (!b || !c) return;
        seg.querySelectorAll('button').forEach(function (x) { x.classList.toggle('on', x === b); });
        c.dataset.range = b.dataset.r; draw(c);
      });
    });

    var b = document.getElementById('refresh-btn');
    if (b) b.addEventListener('click', function () {
      b.disabled = true; b.textContent = 'Refreshing…';
      fetch('/api/refresh', { method: 'POST' }).finally(function () { location.reload(); });
    });

    // First D1 fetch still running: poll until data lands, then reload once.
    if (document.body.dataset.loading) {
      var t = setInterval(function () {
        fetch('/api/health').then(function (r) { return r.json(); }).then(function (h) {
          if (h.loaded) { clearInterval(t); location.reload(); }
        }).catch(function () {});
      }, 1500);
    }
  });

  if (window.ResizeObserver) {
    var last = 0;
    new ResizeObserver(function () {
      var w = document.body.clientWidth; if (Math.abs(w - last) < 2) return; last = w;
      drawAll();
    }).observe(document.body);
  }

  document.addEventListener('click', function (e) {
    var cell = e.target.closest && e.target.closest('[data-copy]');
    if (!cell || !navigator.clipboard) return;
    navigator.clipboard.writeText(cell.dataset.copy).then(function () {
      cell.classList.add('copied'); setTimeout(function () { cell.classList.remove('copied'); }, 800);
    });
  });
})();
