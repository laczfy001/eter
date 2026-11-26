<?php
// REST endpoints. The calls from Éter (sync, catalog) are signed with the shared key; the rest are public and read
// only what the blocks show, apart from placing a request.
//
//   POST eter/v1/sync      Éter → site: on air, up next, whether requests are open, request status updates.
//                          Answer: the new requests, and whether the catalogue should be sent again.
//   POST eter/v1/catalog   Éter → site: the requestable tracks [[id, title, artist], …] and their revision.
//   GET  eter/v1/status    the blocks: on air, up next, requests open, most played (24 h, 7 days, 30 days).
//   GET  eter/v1/search    the request block: tracks matching ?q= (only while requests are open).
//   POST eter/v1/request   the request block: {item: id}.
//   GET  eter/v1/request/N the request block: the status of a request it placed.

if (!defined('ABSPATH')) {
    exit;
}

class Eter_Radio_Rest
{
    const NS = 'eter/v1';

    // Biztonsági bővítmények (vagy saját kód) gyakran letiltják a REST API-t a be nem jelentkezett látogatóknak
    // (rest_authentication_errors, rest_pre_dispatch). Az Éter útvonalai ettől még elérhetők maradnak: a szinkron és a
    // katalógus aláírt (a közös kulcs nélkül semmit nem lehet vele kezdeni), a többi pedig szándékosan nyilvános (a
    // blokkok ebből olvasnak). Más útvonalakat ez nem érint.
    public static function allow_through_blockers()
    {
        add_filter('rest_authentication_errors', function ($result) {
            return is_wp_error($result) && self::is_eter_request() ? null : $result;
        }, PHP_INT_MAX);
        add_filter('rest_pre_dispatch', function ($result, $server, $request) {
            if (is_wp_error($result) && $request instanceof WP_REST_Request && self::is_eter_route($request->get_route())) {
                return null;
            }
            return $result;
        }, PHP_INT_MAX, 3);
    }

    private static function is_eter_route($route)
    {
        $route = '/' . ltrim((string) $route, '/');
        return $route === '/' . self::NS || strpos($route, '/' . self::NS . '/') === 0;
    }

    // A most kiszolgált REST-kérés útvonala: ?rest_route=… vagy /wp-json/… alakban.
    private static function is_eter_request()
    {
        global $wp;
        $route = '';
        if (isset($wp) && is_object($wp) && !empty($wp->query_vars['rest_route'])) {
            $route = $wp->query_vars['rest_route'];
        } elseif (isset($_GET['rest_route'])) {
            $route = wp_unslash($_GET['rest_route']);
        } else {
            $path = (string) parse_url((string) ($_SERVER['REQUEST_URI'] ?? ''), PHP_URL_PATH);
            $prefix = '/' . trim(rest_get_url_prefix(), '/') . '/';
            $at = strpos($path, $prefix);
            if ($at !== false) {
                $route = substr($path, $at + strlen($prefix) - 1);
            }
        }
        return self::is_eter_route($route);
    }

    public static function register()
    {
        register_rest_route(self::NS, '/sync', [
            'methods' => 'POST',
            'callback' => [__CLASS__, 'sync'],
            'permission_callback' => [__CLASS__, 'signed'],
        ]);
        register_rest_route(self::NS, '/catalog', [
            'methods' => 'POST',
            'callback' => [__CLASS__, 'catalog'],
            'permission_callback' => [__CLASS__, 'signed'],
        ]);
        register_rest_route(self::NS, '/status', [
            'methods' => 'GET',
            'callback' => [__CLASS__, 'status'],
            'permission_callback' => '__return_true',
        ]);
        register_rest_route(self::NS, '/search', [
            'methods' => 'GET',
            'callback' => [__CLASS__, 'search'],
            'permission_callback' => '__return_true',
            'args' => ['q' => ['type' => 'string', 'required' => true]],
        ]);
        register_rest_route(self::NS, '/request', [
            'methods' => 'POST',
            'callback' => [__CLASS__, 'request'],
            'permission_callback' => '__return_true',
        ]);
        register_rest_route(self::NS, '/request/(?P<id>\d+)', [
            'methods' => 'GET',
            'callback' => [__CLASS__, 'request_status'],
            'permission_callback' => '__return_true',
        ]);
    }

    // X-Eter-Time: unix seconds; X-Eter-Signature: hex HMAC-SHA256 of "<time>\n<body>" with the shared key. Éter sends
    // them as headers and also as the eter_time / eter_sig query parameters, because some hosts strip custom headers.
    // Each refusal says exactly why, and Éter shows that message.
    public static function signed(WP_REST_Request $r)
    {
        $key = Eter_Radio_Store::key();
        $time = (int) ($r->get_header('x_eter_time') ?: $r->get_param('eter_time'));
        $sig = strtolower((string) ($r->get_header('x_eter_signature') ?: $r->get_param('eter_sig')));
        $now = time();
        if ($key === '') {
            return new WP_Error('eter_auth', 'No key is set on the site (Settings → Éter Radio).', ['status' => 401]);
        }
        if ($sig === '' || $time === 0) {
            return new WP_Error('eter_auth', 'The signature did not arrive (the server removed it). Update Éter.', ['status' => 401]);
        }
        if (abs($now - $time) > 300) {
            return new WP_Error('eter_auth', sprintf('Clock mismatch: the site says %d, Éter sent %d (%+d s). Check the date, time and time zone of both computers.',
                $now, $time, $now - $time), ['status' => 401, 'server_time' => $now]);
        }
        $expect = hash_hmac('sha256', $time . "\n" . $r->get_body(), $key);
        if (!hash_equals($expect, $sig)) {
            return new WP_Error('eter_auth', sprintf('The key does not match (copy it again from Settings → Éter Radio; %d bytes received).',
                strlen($r->get_body())), ['status' => 401]);
        }
        return true;
    }

    private static function no_cache(WP_REST_Response $res)
    {
        $res->header('Cache-Control', 'no-store, max-age=0');
        return $res;
    }

    private static function track($t)
    {
        if (!is_array($t)) {
            return null;
        }
        return [
            'title' => sanitize_text_field((string) ($t['title'] ?? '')),
            'artist' => sanitize_text_field((string) ($t['artist'] ?? '')),
            'start' => (int) ($t['start'] ?? 0),          // unix ms
            'duration' => max(0, (int) ($t['duration'] ?? 0)),   // ms
            'request' => !empty($t['request']),
        ];
    }

    public static function sync(WP_REST_Request $r)
    {
        $in = json_decode($r->get_body(), true);
        if (!is_array($in)) {
            return new WP_Error('eter_body', 'Invalid JSON.', ['status' => 400]);
        }
        foreach ((array) ($in['acks'] ?? []) as $a) {
            if (is_array($a) && isset($a['id'], $a['status'])) {
                Eter_Radio_Store::set_status((int) $a['id'], (string) $a['status']);
            }
        }
        // Several Éter installations: only the owner updates the site and gets the requests (Éter 0.36+ sends an
        // instance; older versions count as one unnamed instance).
        $inst = is_array($in['instance'] ?? null) ? $in['instance'] : [];
        $inst_id = substr(preg_replace('/[^A-Za-z0-9_-]/', '', (string) ($inst['id'] ?? '')), 0, 64);
        if ($inst_id === '') {
            $inst_id = 'legacy';
        }
        [$is_owner, $owner, $others] = Eter_Radio_Store::claim(
            $inst_id,
            sanitize_text_field((string) ($inst['name'] ?? '')),
            sanitize_text_field((string) ($in['version'] ?? '')),
            !empty($inst['on_air'])
        );
        if (!$is_owner) {
            return self::no_cache(new WP_REST_Response([
                'ok' => true,
                'secondary' => true,
                'owner' => ['name' => (string) ($owner['name'] ?? ''), 'on_air' => !empty($owner['on_air'])],
                'requests' => [],
                'need_catalog' => false,
                'others' => $others,
            ]));
        }
        $next = [];
        foreach (array_slice((array) ($in['next'] ?? []), 0, 20) as $t) {
            if ($tt = self::track($t)) {
                $next[] = $tt;
            }
        }
        Eter_Radio_Store::save_state([
            'synced_at' => time(),
            'clock' => (int) ($in['time'] ?? 0),
            'version' => sanitize_text_field((string) ($in['version'] ?? '')),
            'now' => self::track($in['now'] ?? null),
            'next' => $next,
            'requests' => [
                'enabled' => !empty($in['requests']['enabled']),
                'open' => !empty($in['requests']['open']),
            ],
            'catalog_rev' => sanitize_text_field((string) ($in['catalog_rev'] ?? '')),
        ]);
        if (isset($in['top']) && is_array($in['top'])) {
            $top = [];
            foreach (['day', 'week', 'month'] as $p) {
                $top[$p] = [];
                foreach (array_slice((array) ($in['top'][$p] ?? []), 0, 20) as $t) {
                    if (!is_array($t)) {
                        continue;
                    }
                    $top[$p][] = [
                        'title' => sanitize_text_field((string) ($t['title'] ?? '')),
                        'artist' => sanitize_text_field((string) ($t['artist'] ?? '')),
                        'plays' => max(0, (int) ($t['plays'] ?? 0)),
                    ];
                }
            }
            update_option('eter_radio_top', $top, false);
        }
        $rev = (string) ($in['catalog_rev'] ?? '');
        return self::no_cache(new WP_REST_Response([
            'ok' => true,
            'requests' => Eter_Radio_Store::new_requests(),
            'need_catalog' => $rev !== '' && $rev !== Eter_Radio_Store::catalog_rev(),
            'catalog_count' => count(Eter_Radio_Store::catalog()),
            'others' => $others,
        ]));
    }

    public static function catalog(WP_REST_Request $r)
    {
        $in = json_decode($r->get_body(), true);
        if (!is_array($in) || !isset($in['rev'], $in['items']) || !is_array($in['items'])) {
            return new WP_Error('eter_body', 'Invalid catalogue.', ['status' => 400]);
        }
        $items = [];
        foreach ($in['items'] as $it) {
            if (is_array($it) && count($it) >= 3 && (int) $it[0] > 0) {
                $items[] = [(int) $it[0], sanitize_text_field((string) $it[1]), sanitize_text_field((string) $it[2])];
            }
        }
        Eter_Radio_Store::save_catalog((string) $in['rev'], $items);
        return self::no_cache(new WP_REST_Response(['ok' => true, 'count' => count($items)]));
    }

    public static function public_state()
    {
        $s = Eter_Radio_Store::state();
        $online = Eter_Radio_Store::online($s);
        return [
            'online' => $online,
            'server_time' => (int) round(microtime(true) * 1000),
            'now' => $online ? ($s['now'] ?? null) : null,
            'next' => $online ? ($s['next'] ?? []) : [],
            'requests' => [
                'enabled' => $online && !empty($s['requests']['enabled']),
                'open' => Eter_Radio_Store::requests_open($s),
            ],
            // A legtöbbet játszott számok (24 óra, 7 nap, 30 nap); az adás szünetében is az utolsó ismert lista.
            'top' => get_option('eter_radio_top', ['day' => [], 'week' => [], 'month' => []]),
        ];
    }

    public static function status()
    {
        return self::no_cache(new WP_REST_Response(self::public_state()));
    }

    public static function search(WP_REST_Request $r)
    {
        if (!Eter_Radio_Store::requests_open()) {
            return self::no_cache(new WP_REST_Response(['open' => false, 'items' => []]));
        }
        $q = mb_substr((string) $r->get_param('q'), 0, 100);
        return self::no_cache(new WP_REST_Response(['open' => true, 'items' => Eter_Radio_Store::search($q)]));
    }

    public static function request(WP_REST_Request $r)
    {
        if (!Eter_Radio_Store::requests_open()) {
            return new WP_Error('eter_closed', __('Requests are closed right now.', 'eter-radio'), ['status' => 409]);
        }
        $item = Eter_Radio_Store::catalog_item((int) $r->get_param('item'));
        if (!$item) {
            return new WP_Error('eter_item', __('This track cannot be requested.', 'eter-radio'), ['status' => 404]);
        }
        $id = Eter_Radio_Store::add_request($item);
        return self::no_cache(new WP_REST_Response(['id' => $id, 'status' => 'new', 'title' => $item['title'],
                                                     'artist' => $item['artist']]));
    }

    public static function request_status(WP_REST_Request $r)
    {
        $status = Eter_Radio_Store::request_status((int) $r['id']);
        if ($status === null) {
            return new WP_Error('eter_item', 'Unknown request.', ['status' => 404]);
        }
        return self::no_cache(new WP_REST_Response(['id' => (int) $r['id'], 'status' => $status]));
    }
}
