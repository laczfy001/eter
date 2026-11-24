/* Éter Web widgets. Put <div data-eter="now|next|top|request"></div> anywhere on the page and load this script
   (…/eter-web/?js). Options as data attributes: data-heading, data-count, data-period (day|week|month),
   data-times="0", data-plays="0", data-progress="0". */
(function () {
    'use strict';
    var cfg = window.EterWeb;
    if (!cfg || window.EterWebStarted) return;
    window.EterWebStarted = true;
    var T = cfg.t;
    var state = null;
    var skew = 0;

    if (!window.EterWebNoCss) {
        var css = document.createElement('link');
        css.rel = 'stylesheet';
        css.href = cfg.base + 'assets/eter-web.css';
        document.head.appendChild(css);
    }

    function api(path, query) {
        return cfg.base + '?rest_route=/eter/v1/' + path + (query ? '&' + query : '');
    }
    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) e.className = cls;
        if (text !== undefined) e.textContent = text;
        return e;
    }
    function line(t) {
        var f = document.createDocumentFragment();
        var artist = (t.artist || '').trim(), title = (t.title || '').trim();
        f.appendChild(el('span', 'eter-artist', artist));
        if (artist && title) f.appendChild(el('span', 'eter-sep', ' – '));
        f.appendChild(el('span', 'eter-title', title));
        if (t.request) f.appendChild(el('span', 'eter-badge', T.requestBadge));
        return f;
    }
    function hhmm(ms) {
        var d = new Date(ms - skew);
        return ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2);
    }
    function attr(b, name, def) {
        var v = b.getAttribute('data-' + name);
        return v === null || v === '' ? def : v;
    }
    function widgets(kind) {
        return document.querySelectorAll('[data-eter="' + kind + '"]');
    }

    // ---- The markup of each widget, once ----
    function build(b) {
        if (b.eterBuilt) return;
        b.eterBuilt = true;
        var kind = b.getAttribute('data-eter');
        b.classList.add('eter-block', 'eter-' + kind);
        var heading = attr(b, 'heading', '');
        if (heading) b.appendChild(el('h3', 'eter-heading', heading));
        if (kind === 'now') {
            b.appendChild(el('div', 'eter-body'));
            if (attr(b, 'progress', '1') !== '0') {
                var p = el('div', 'eter-progress');
                p.appendChild(el('span'));
                b.appendChild(p);
            }
        } else if (kind === 'next') {
            b.appendChild(el('ol', 'eter-list'));
        } else if (kind === 'top') {
            b.appendChild(el('ol', 'eter-list eter-ranked'));
        } else if (kind === 'request') {
            b.appendChild(el('div', 'eter-closed eter-muted'));
            var open = el('div', 'eter-open');
            var input = el('input', 'eter-search');
            input.type = 'search';
            input.autocomplete = 'off';
            input.placeholder = T.search;
            input.setAttribute('aria-label', T.search);
            open.appendChild(input);
            open.appendChild(el('ul', 'eter-results'));
            b.appendChild(open);
            var mineBox = el('div', 'eter-mine');
            mineBox.hidden = true;
            b.appendChild(mineBox);
            setupRequest(b);
        }
    }

    function render() {
        if (!state) return;
        widgets('now').forEach(function (b) {
            var body = b.querySelector('.eter-body');
            body.textContent = '';
            if (state.now) body.appendChild(line(state.now));
            else body.appendChild(el('span', 'eter-muted', state.online ? T.nothing : T.offline));
        });
        progress();
        widgets('next').forEach(function (b) {
            var list = b.querySelector('.eter-list');
            var count = parseInt(attr(b, 'count', '5'), 10) || 5;
            var times = attr(b, 'times', '1') !== '0';
            list.textContent = '';
            var items = (state.next || []).slice(0, count);
            if (!items.length) { list.appendChild(el('li', 'eter-muted', state.online ? T.empty : T.offline)); return; }
            items.forEach(function (t) {
                var li = el('li', t.request ? 'eter-is-request' : '');
                if (times) li.appendChild(el('span', 'eter-time', t.start ? hhmm(t.start) : ''));
                li.appendChild(line(t));
                list.appendChild(li);
            });
        });
        widgets('top').forEach(function (b) {
            var list = b.querySelector('.eter-list');
            var period = attr(b, 'period', 'week');
            var count = parseInt(attr(b, 'count', '10'), 10) || 10;
            var plays = attr(b, 'plays', '1') !== '0';
            var items = ((state.top || {})[period] || []).slice(0, count);
            list.textContent = '';
            if (!items.length) { list.appendChild(el('li', 'eter-muted', T.noTop)); return; }
            items.forEach(function (t) {
                var li = el('li');
                li.appendChild(line(t));
                if (plays) li.appendChild(el('span', 'eter-plays', t.plays === 1 ? T.play1 : T.plays.replace('%d', t.plays)));
                list.appendChild(li);
            });
        });
        widgets('request').forEach(function (b) {
            var open = !!(state.requests && state.requests.open);
            b.querySelector('.eter-open').hidden = !open;
            var closed = b.querySelector('.eter-closed');
            closed.hidden = open;
            closed.textContent = state.online ? T.closed : T.offline;
        });
    }
    function progress() {
        if (!state) return;
        widgets('now').forEach(function (b) {
            var wrap = b.querySelector('.eter-progress');
            if (!wrap) return;
            var n = state.now, p = 0;
            if (n && n.duration > 0 && n.start > 0) p = Math.max(0, Math.min(1, (Date.now() + skew - n.start) / n.duration));
            wrap.firstChild.style.width = (p * 100).toFixed(2) + '%';
            wrap.hidden = !n || !n.duration;
        });
    }
    function refresh() {
        fetch(api('status'), { cache: 'no-store' })
            .then(function (r) { return r.json(); })
            .then(function (s) {
                if (!s || typeof s !== 'object' || !('online' in s)) return;
                state = s;
                if (s.server_time) skew = s.server_time - Date.now();
                render();
            })
            .catch(function () {});
    }

    // ---- Song request ----
    var mine = null;
    function showMine() {
        widgets('request').forEach(function (b) {
            var box = b.querySelector('.eter-mine');
            if (!box) return;
            if (!mine) { box.hidden = true; return; }
            box.hidden = false;
            box.textContent = '';
            var name = (mine.artist ? mine.artist + ' – ' : '') + mine.title;
            box.appendChild(el('div', 'eter-mine-title', T.yourRequest.replace('%s', name)));
            box.appendChild(el('div', 'eter-mine-status', T.status[mine.status] || mine.status));
        });
    }
    function pollMine() {
        if (!mine || ['played', 'rejected', 'invalid', 'removed'].indexOf(mine.status) >= 0) return;
        fetch(api('request/' + mine.id), { cache: 'no-store' })
            .then(function (r) { return r.json(); })
            .then(function (s) { if (s && s.status && mine && s.id === mine.id) { mine.status = s.status; showMine(); } })
            .catch(function () {});
    }
    function place(item, button, msg) {
        button.disabled = true;
        button.textContent = T.sending;
        // text/plain: a "simple" cross-site request, no preflight needed
        fetch(api('request'), { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: JSON.stringify({ item: item.id }) })
            .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
            .then(function (res) {
                if (!res.ok) {
                    button.disabled = false;
                    button.textContent = T.request;
                    msg.textContent = (res.j && res.j.message) || T.error;
                    return;
                }
                button.textContent = T.requested;
                mine = { id: res.j.id, title: res.j.title, artist: res.j.artist, status: res.j.status };
                showMine();
            })
            .catch(function () {
                button.disabled = false;
                button.textContent = T.request;
                msg.textContent = T.error;
            });
    }
    function setupRequest(b) {
        var input = b.querySelector('.eter-search');
        var results = b.querySelector('.eter-results');
        var timer = null, seq = 0;
        input.addEventListener('input', function () {
            clearTimeout(timer);
            var q = input.value.trim();
            if (q.length < 2) { results.textContent = ''; return; }
            timer = setTimeout(function () {
                var my = ++seq;
                fetch(api('search', 'q=' + encodeURIComponent(q)), { cache: 'no-store' })
                    .then(function (r) { return r.json(); })
                    .then(function (res) {
                        if (my !== seq) return;
                        results.textContent = '';
                        if (!res.open) { if (state) state.requests = { open: false, enabled: true }; render(); return; }
                        if (!res.items.length) { results.appendChild(el('li', 'eter-muted', T.noResults)); return; }
                        res.items.forEach(function (it) {
                            var li = el('li');
                            var label = el('span', 'eter-result');
                            label.appendChild(line(it));
                            var btn = el('button', 'eter-button', T.request);
                            btn.type = 'button';
                            var msg = el('div', 'eter-error');
                            btn.addEventListener('click', function () { place(it, btn, msg); });
                            li.appendChild(label);
                            li.appendChild(btn);
                            li.appendChild(msg);
                            results.appendChild(li);
                        });
                    })
                    .catch(function () {});
            }, 300);
        });
    }

    function scan() {
        document.querySelectorAll('[data-eter]').forEach(build);
    }
    function start() {
        scan();
        refresh();
        var every = Math.max(5, cfg.refresh || 10) * 1000;
        setInterval(function () { if (!document.hidden) { scan(); refresh(); pollMine(); } }, every);
        setInterval(progress, 1000);
        document.addEventListener('visibilitychange', function () { if (!document.hidden) refresh(); });
    }
    window.EterWebRefresh = function () { scan(); refresh(); };   // for pages that add widgets later
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
    else start();
})();
