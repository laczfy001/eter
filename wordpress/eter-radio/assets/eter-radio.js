/* Éter Radio blocks: keeps Now playing, Up next and Song request up to date from the site's REST endpoints. */
(function () {
    'use strict';
    var cfg = window.eterRadio;
    if (!cfg) return;
    var T = cfg.t;
    var state = cfg.state;
    var skew = state && state.server_time ? state.server_time - Date.now() : 0;   // the site's clock minus ours

    function url(path, query) {
        // Works with and without pretty permalinks (rest_url can be …/wp-json/eter/v1/ or …?rest_route=/eter/v1/).
        var u = cfg.rest + path;
        if (query) u += (u.indexOf('?') < 0 ? '?' : '&') + query;
        return u;
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
    function blocks(kind) {
        return document.querySelectorAll('.eter-block[data-eter="' + kind + '"]');
    }

    function renderNow() {
        blocks('now').forEach(function (b) {
            var body = b.querySelector('.eter-body');
            body.textContent = '';
            if (state.now) body.appendChild(line(state.now));
            else body.appendChild(el('span', 'eter-muted', state.online ? T.nothing : T.offline));
        });
        progress();
    }
    function progress() {
        blocks('now').forEach(function (b) {
            var bar = b.querySelector('.eter-progress span');
            if (!bar) return;
            var n = state.now, p = 0;
            if (n && n.duration > 0 && n.start > 0) p = Math.max(0, Math.min(1, (Date.now() + skew - n.start) / n.duration));
            bar.style.width = (p * 100).toFixed(2) + '%';
            b.querySelector('.eter-progress').hidden = !n || !n.duration;
        });
    }
    function renderNext() {
        blocks('next').forEach(function (b) {
            var list = b.querySelector('.eter-list');
            var count = parseInt(b.getAttribute('data-count'), 10) || 5;
            var times = b.getAttribute('data-times') === '1';
            list.textContent = '';
            var items = (state.next || []).slice(0, count);
            if (!items.length) {
                list.appendChild(el('li', 'eter-muted', state.online ? T.empty : T.offline));
                return;
            }
            items.forEach(function (t) {
                var li = el('li', t.request ? 'eter-is-request' : '');
                if (times) li.appendChild(el('span', 'eter-time', t.start ? hhmm(t.start) : ''));
                li.appendChild(line(t));
                list.appendChild(li);
            });
        });
    }
    function renderRequestOpen() {
        blocks('request').forEach(function (b) {
            var open = !!(state.requests && state.requests.open);
            b.querySelector('.eter-open').hidden = !open;
            var closed = b.querySelector('.eter-closed');
            closed.hidden = open;
            closed.textContent = state.online ? T.closed : T.offline;
        });
    }
    function render() {
        renderNow();
        renderNext();
        renderRequestOpen();
    }

    function refresh() {
        fetch(url('status'), { cache: 'no-store', credentials: 'same-origin' })
            .then(function (r) { return r.json(); })
            .then(function (s) {
                if (!s || typeof s !== 'object' || !('online' in s)) return;
                state = s;
                if (s.server_time) skew = s.server_time - Date.now();
                render();
            })
            .catch(function () { /* keep showing the last state */ });
    }

    // ---- Song request ---------------------------------------------------------------------------------------------
    var mine = null;          // {id, title, artist, status} – the last request placed from this page
    function showMine() {
        blocks('request').forEach(function (b) {
            var box = b.querySelector('.eter-mine');
            if (!mine) { box.hidden = true; return; }
            box.hidden = false;
            box.textContent = '';
            var name = (mine.artist ? mine.artist + ' – ' : '') + mine.title;
            box.appendChild(el('div', 'eter-mine-title', T.yourRequest.replace('%s', name)));
            box.appendChild(el('div', 'eter-mine-status eter-status-' + mine.status, T.status[mine.status] || mine.status));
        });
    }
    function pollMine() {
        if (!mine || ['played', 'rejected', 'invalid', 'removed'].indexOf(mine.status) >= 0) return;
        fetch(url('request/' + mine.id), { cache: 'no-store', credentials: 'same-origin' })
            .then(function (r) { return r.json(); })
            .then(function (s) { if (s && s.status && mine && s.id === mine.id) { mine.status = s.status; showMine(); } })
            .catch(function () {});
    }
    function place(item, button, msgBox) {
        button.disabled = true;
        button.textContent = T.sending;
        fetch(url('request'), {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ item: item.id })
        })
            .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
            .then(function (res) {
                if (!res.ok) {
                    button.disabled = false;
                    button.textContent = T.request;
                    msgBox.textContent = (res.j && res.j.message) || T.error;
                    return;
                }
                button.textContent = T.requested;
                mine = { id: res.j.id, title: res.j.title, artist: res.j.artist, status: res.j.status };
                showMine();
            })
            .catch(function () {
                button.disabled = false;
                button.textContent = T.request;
                msgBox.textContent = T.error;
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
                fetch(url('search', 'q=' + encodeURIComponent(q)), { cache: 'no-store', credentials: 'same-origin' })
                    .then(function (r) { return r.json(); })
                    .then(function (res) {
                        if (my !== seq) return;
                        results.textContent = '';
                        if (!res.open) { state.requests = { open: false, enabled: true }; renderRequestOpen(); return; }
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

    function start() {
        render();
        blocks('request').forEach(setupRequest);
        var every = Math.max(5, cfg.refresh || 10) * 1000;
        setInterval(function () { if (!document.hidden) { refresh(); pollMine(); } }, every);
        setInterval(progress, 1000);
        document.addEventListener('visibilitychange', function () { if (!document.hidden) refresh(); });
    }
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
    else start();
})();
