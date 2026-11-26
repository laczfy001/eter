<?php
// Éter Web: the REST endpoints (the same as the WordPress plugin's, so Éter works with both unchanged).

if (!defined('ETER_WEB')) {
    exit;
}

function eter_json($data, $status = 200)
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store, max-age=0');
    echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
}

function eter_error($code, $message, $status)
{
    eter_json(['code' => $code, 'message' => $message, 'data' => ['status' => $status]], $status);
}

// The widgets may run on other websites: allow them (config.php: $ETER_ALLOWED_ORIGINS).
function eter_cors()
{
    global $ETER_ALLOWED_ORIGINS;
    $origin = $_SERVER['HTTP_ORIGIN'] ?? '';
    $allowed = (array) ($ETER_ALLOWED_ORIGINS ?? ['*']);
    if (in_array('*', $allowed, true)) {
        header('Access-Control-Allow-Origin: *');
    } elseif ($origin !== '' && in_array(rtrim($origin, '/'), array_map(function ($o) { return rtrim($o, '/'); }, $allowed), true)) {
        header('Access-Control-Allow-Origin: ' . $origin);
        header('Vary: Origin');
    }
    header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
    header('Access-Control-Allow-Headers: Content-Type');
    header('Access-Control-Max-Age: 86400');
}

// The signature comes as headers and also as the eter_time / eter_sig query parameters (some hosts strip custom
// headers). Each refusal says exactly why; Éter shows the message.
function eter_signed($body)
{
    $key = eter_settings()['key'];
    $time = (int) ($_SERVER['HTTP_X_ETER_TIME'] ?? ($_GET['eter_time'] ?? 0));
    $sig = strtolower((string) ($_SERVER['HTTP_X_ETER_SIGNATURE'] ?? ($_GET['eter_sig'] ?? '')));
    $now = time();
    if ($key === '') {
        eter_error('eter_auth', 'No key is set (open admin.php).', 401);
        return false;
    }
    if ($sig === '' || $time === 0) {
        eter_error('eter_auth', 'The signature did not arrive (the server removed it). Update Éter.', 401);
        return false;
    }
    if (abs($now - $time) > 300) {
        eter_error('eter_auth', sprintf('Clock mismatch: the site says %d, Éter sent %d (%+d s). Check the date, time and time zone of both computers.',
            $now, $time, $now - $time), 401);
        return false;
    }
    if (!hash_equals(hash_hmac('sha256', $time . "\n" . $body, $key), $sig)) {
        eter_error('eter_auth', sprintf('The key does not match (copy it again from admin.php; %d bytes received).', strlen($body)), 401);
        return false;
    }
    return true;
}

function eter_track($t)
{
    if (!is_array($t)) {
        return null;
    }
    return [
        'title' => eter_clean($t['title'] ?? ''),
        'artist' => eter_clean($t['artist'] ?? ''),
        'start' => (int) ($t['start'] ?? 0),
        'duration' => max(0, (int) ($t['duration'] ?? 0)),
        'request' => !empty($t['request']),
    ];
}

function eter_api($route)
{
    $method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
    $route = '/' . trim($route, '/');
    if ($method === 'OPTIONS') {
        eter_cors();
        http_response_code(204);
        return;
    }
    $body = (string) file_get_contents('php://input');

    if ($route === '/eter/v1/sync' && $method === 'POST') {
        if (!eter_signed($body)) {
            return;
        }
        $in = json_decode($body, true);
        if (!is_array($in)) {
            eter_error('eter_body', 'Invalid JSON.', 400);
            return;
        }
        // Several Éter installations: the ones that are not the owner only get a warning back (their acks still count).
        $inst = is_array($in['instance'] ?? null) ? $in['instance'] : [];
        $instId = substr(preg_replace('/[^A-Za-z0-9_-]/', '', (string) ($inst['id'] ?? '')), 0, 64);
        [$isOwner, $owner, $others] = eter_claim($instId === '' ? 'legacy' : $instId, eter_clean($inst['name'] ?? '', 80),
                                                 eter_clean($in['version'] ?? '', 40), !empty($inst['on_air']));
        if (!$isOwner) {
            eter_update('requests', ['next_id' => 1, 'list' => []], function (&$r) use ($in) {
                $byId = [];
                foreach ($r['list'] as $i => $q) {
                    $byId[$q['id']] = $i;
                }
                foreach ((array) ($in['acks'] ?? []) as $a) {
                    if (is_array($a) && isset($a['id'], $a['status'], $byId[(int) $a['id']]) && in_array($a['status'], ETER_STATUSES, true)) {
                        $r['list'][$byId[(int) $a['id']]]['status'] = $a['status'];
                        $r['list'][$byId[(int) $a['id']]]['updated'] = time();
                    }
                }
            });
            eter_json(['ok' => true, 'secondary' => true, 'owner' => ['name' => (string) ($owner['name'] ?? ''),
                       'on_air' => !empty($owner['on_air'])], 'requests' => [], 'need_catalog' => false, 'others' => $others]);
            return;
        }
        $next = [];
        foreach (array_slice((array) ($in['next'] ?? []), 0, 20) as $t) {
            if ($tt = eter_track($t)) {
                $next[] = $tt;
            }
        }
        eter_write('state', [
            'synced_at' => time(),
            'version' => eter_clean($in['version'] ?? '', 40),
            'now' => eter_track($in['now'] ?? null),
            'next' => $next,
            'requests' => ['enabled' => !empty($in['requests']['enabled']), 'open' => !empty($in['requests']['open'])],
        ]);
        if (isset($in['top']) && is_array($in['top'])) {
            $top = [];
            foreach (['day', 'week', 'month'] as $p) {
                $top[$p] = [];
                foreach (array_slice((array) ($in['top'][$p] ?? []), 0, 20) as $t) {
                    if (is_array($t)) {
                        $top[$p][] = ['title' => eter_clean($t['title'] ?? ''), 'artist' => eter_clean($t['artist'] ?? ''),
                                      'plays' => max(0, (int) ($t['plays'] ?? 0))];
                    }
                }
            }
            eter_write('top', $top);
        }
        // Status updates of the requests, and the ones Éter has not collected yet.
        $new = eter_update('requests', ['next_id' => 1, 'list' => []], function (&$r) use ($in) {
            $byId = [];
            foreach ($r['list'] as $i => $q) {
                $byId[$q['id']] = $i;
            }
            foreach ((array) ($in['acks'] ?? []) as $a) {
                if (is_array($a) && isset($a['id'], $a['status'], $byId[(int) $a['id']]) && in_array($a['status'], ETER_STATUSES, true)) {
                    $r['list'][$byId[(int) $a['id']]]['status'] = $a['status'];
                    $r['list'][$byId[(int) $a['id']]]['updated'] = time();
                }
            }
            $out = [];
            foreach ($r['list'] as $q) {
                if ($q['status'] === 'new' && count($out) < 50) {
                    $out[] = ['id' => $q['id'], 'item' => $q['item'], 'time' => $q['created']];
                }
            }
            return $out;
        });
        $catalog = eter_read('catalog', ['rev' => '', 'items' => []]);
        $rev = (string) ($in['catalog_rev'] ?? '');
        eter_json(['ok' => true, 'requests' => $new, 'need_catalog' => $rev !== '' && $rev !== $catalog['rev'],
                   'catalog_count' => count($catalog['items']), 'others' => $others]);
        return;
    }

    if ($route === '/eter/v1/catalog' && $method === 'POST') {
        if (!eter_signed($body)) {
            return;
        }
        $in = json_decode($body, true);
        if (!is_array($in) || !isset($in['rev'], $in['items']) || !is_array($in['items'])) {
            eter_error('eter_body', 'Invalid catalogue.', 400);
            return;
        }
        $items = [];
        foreach ($in['items'] as $it) {
            if (is_array($it) && count($it) >= 3 && (int) $it[0] > 0) {
                $title = eter_clean($it[1]);
                $artist = eter_clean($it[2]);
                $items[] = [(int) $it[0], $title, $artist, eter_fold($artist . ' ' . $title)];
            }
        }
        eter_write('catalog', ['rev' => eter_clean($in['rev'], 64), 'items' => $items]);
        eter_json(['ok' => true, 'count' => count($items)]);
        return;
    }

    // ---- Public (the widgets) ----
    eter_cors();
    if ($route === '/eter/v1/status' && $method === 'GET') {
        eter_json(eter_public_state());
        return;
    }
    if ($route === '/eter/v1/search' && $method === 'GET') {
        $state = eter_public_state();
        if (!$state['requests']['open']) {
            eter_json(['open' => false, 'items' => []]);
            return;
        }
        $words = preg_split('/\s+/u', eter_fold(substr((string) ($_GET['q'] ?? ''), 0, 100)), -1, PREG_SPLIT_NO_EMPTY);
        $out = [];
        if ($words) {
            foreach (eter_read('catalog', ['items' => []])['items'] as $it) {
                $ok = true;
                foreach ($words as $w) {
                    if (strpos($it[3], $w) === false) {
                        $ok = false;
                        break;
                    }
                }
                if ($ok) {
                    $out[] = ['id' => $it[0], 'title' => $it[1], 'artist' => $it[2]];
                    if (count($out) >= 25) {
                        break;
                    }
                }
            }
        }
        eter_json(['open' => true, 'items' => $out]);
        return;
    }
    if ($route === '/eter/v1/request' && $method === 'POST') {
        if (!eter_public_state()['requests']['open']) {
            eter_error('eter_closed', __e('Requests are closed right now.'), 409);
            return;
        }
        $in = json_decode($body, true);
        $id = (int) ($in['item'] ?? ($_POST['item'] ?? 0));
        $item = null;
        foreach (eter_read('catalog', ['items' => []])['items'] as $it) {
            if ($it[0] === $id) {
                $item = $it;
                break;
            }
        }
        if (!$item) {
            eter_error('eter_item', __e('This track cannot be requested.'), 404);
            return;
        }
        $req = eter_update('requests', ['next_id' => 1, 'list' => []], function (&$r) use ($item) {
            $q = ['id' => $r['next_id']++, 'item' => $item[0], 'title' => $item[1], 'artist' => $item[2], 'status' => 'new',
                  'created' => time(), 'updated' => time()];
            $r['list'][] = $q;
            // The oldest finished requests go, so the file stays small.
            if (count($r['list']) > 1000) {
                $r['list'] = array_values(array_filter($r['list'], function ($x) {
                    return in_array($x['status'], ['new', 'pending', 'queued'], true);
                })) ?: [];
                $r['list'] = array_slice($r['list'], -1000);
            }
            return $q;
        });
        eter_json(['id' => $req['id'], 'status' => 'new', 'title' => $req['title'], 'artist' => $req['artist']]);
        return;
    }
    if (preg_match('#^/eter/v1/request/(\d+)$#', $route, $m) && $method === 'GET') {
        foreach (eter_read('requests', ['list' => []])['list'] as $q) {
            if ($q['id'] === (int) $m[1]) {
                eter_json(['id' => $q['id'], 'status' => $q['status']]);
                return;
            }
        }
        eter_error('eter_item', 'Unknown request.', 404);
        return;
    }
    eter_error('rest_no_route', 'No route was found matching the URL and request method.', 404);
}
