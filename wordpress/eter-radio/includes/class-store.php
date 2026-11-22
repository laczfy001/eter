<?php
// The plugin's data: the state Éter last sent, the request catalogue, the requests table and the settings.

if (!defined('ABSPATH')) {
    exit;
}

class Eter_Radio_Store
{
    const DB_VERSION = 1;

    // Request statuses. new: waiting for Éter to collect it; pending: Éter has it, waiting for approval in the studio;
    // queued: in the playlist; played: has been on air; rejected: turned down in the studio; invalid: the track is
    // no longer available; removed: taken out of the playlist before it played.
    const STATUSES = ['new', 'pending', 'queued', 'played', 'rejected', 'invalid', 'removed'];

    public static function table()
    {
        global $wpdb;
        return $wpdb->prefix . 'eter_requests';
    }

    public static function activate()
    {
        self::create_table();
        if (!get_option('eter_radio_key')) {
            update_option('eter_radio_key', self::new_key(), false);
        }
    }

    public static function maybe_upgrade()
    {
        if ((int) get_option('eter_radio_db_version', 0) < self::DB_VERSION) {
            self::create_table();
        }
        if (!get_option('eter_radio_key')) {
            update_option('eter_radio_key', self::new_key(), false);
        }
    }

    private static function create_table()
    {
        global $wpdb;
        require_once ABSPATH . 'wp-admin/includes/upgrade.php';
        $charset = $wpdb->get_charset_collate();
        $table = self::table();
        dbDelta("CREATE TABLE $table (
            id bigint(20) unsigned NOT NULL AUTO_INCREMENT,
            item_id bigint(20) unsigned NOT NULL,
            title varchar(255) NOT NULL DEFAULT '',
            artist varchar(255) NOT NULL DEFAULT '',
            status varchar(16) NOT NULL DEFAULT 'new',
            created_at datetime NOT NULL,
            updated_at datetime NOT NULL,
            PRIMARY KEY  (id),
            KEY status (status)
        ) $charset;");
        update_option('eter_radio_db_version', self::DB_VERSION, false);
    }

    public static function new_key()
    {
        return wp_generate_password(40, false, false);
    }

    public static function key()
    {
        return (string) get_option('eter_radio_key', '');
    }

    const LANGUAGES = ['en_US', 'nl_NL', 'hu_HU', 'auto'];

    public static function settings()
    {
        $s = get_option('eter_radio_settings', []);
        $lang = (string) ($s['language'] ?? 'en_US');
        return [
            'language' => in_array($lang, self::LANGUAGES, true) ? $lang : 'en_US',
            'refresh' => max(5, min(120, (int) ($s['refresh'] ?? 10))),
            'offline_after' => max(30, min(3600, (int) ($s['offline_after'] ?? 90))),
        ];
    }

    // ---- State sent by Éter -----------------------------------------------------------------------------------------

    public static function state()
    {
        $s = get_option('eter_radio_state', []);
        return is_array($s) ? $s : [];
    }

    public static function save_state(array $state)
    {
        update_option('eter_radio_state', $state, false);
    }

    // Éter has synced recently enough for the site to treat the station as online.
    public static function online(array $state = null)
    {
        $state = $state ?? self::state();
        $at = (int) ($state['synced_at'] ?? 0);
        return $at > 0 && time() - $at <= self::settings()['offline_after'];
    }

    public static function requests_open(array $state = null)
    {
        $state = $state ?? self::state();
        return self::online($state) && !empty($state['requests']['open']);
    }

    // ---- Catalogue of requestable tracks ----------------------------------------------------------------------------

    public static function catalog()
    {
        $c = get_option('eter_radio_catalog', []);
        return is_array($c) ? $c : [];
    }

    public static function catalog_rev()
    {
        return (string) get_option('eter_radio_catalog_rev', '');
    }

    // Items: [id, title, artist]; the folded search text is added once here, not on every search.
    public static function save_catalog($rev, array $items)
    {
        foreach ($items as &$it) {
            $it[3] = self::fold($it[2] . ' ' . $it[1]);
        }
        unset($it);
        update_option('eter_radio_catalog', $items, false);
        update_option('eter_radio_catalog_rev', (string) $rev, false);
    }

    public static function catalog_item($id)
    {
        foreach (self::catalog() as $it) {
            if ((int) $it[0] === (int) $id) {
                return ['id' => (int) $it[0], 'title' => (string) $it[1], 'artist' => (string) $it[2]];
            }
        }
        return null;
    }

    // Case- and accent-insensitive search in title and artist; every word must match.
    public static function search($q, $limit = 25)
    {
        $words = preg_split('/\s+/u', self::fold($q), -1, PREG_SPLIT_NO_EMPTY);
        if (!$words) {
            return [];
        }
        $out = [];
        foreach (self::catalog() as $it) {
            $hay = $it[3] ?? self::fold($it[2] . ' ' . $it[1]);
            $ok = true;
            foreach ($words as $w) {
                if (strpos($hay, $w) === false) {
                    $ok = false;
                    break;
                }
            }
            if ($ok) {
                $out[] = ['id' => (int) $it[0], 'title' => (string) $it[1], 'artist' => (string) $it[2]];
                if (count($out) >= $limit) {
                    break;
                }
            }
        }
        return $out;
    }

    public static function fold($s)
    {
        $s = function_exists('mb_strtolower') ? mb_strtolower((string) $s, 'UTF-8') : strtolower((string) $s);
        return remove_accents($s);
    }

    // ---- Requests ---------------------------------------------------------------------------------------------------

    public static function add_request(array $item)
    {
        global $wpdb;
        $now = current_time('mysql', true);
        $wpdb->insert(self::table(), [
            'item_id' => (int) $item['id'],
            'title' => mb_substr((string) $item['title'], 0, 255),
            'artist' => mb_substr((string) $item['artist'], 0, 255),
            'status' => 'new',
            'created_at' => $now,
            'updated_at' => $now,
        ]);
        return (int) $wpdb->insert_id;
    }

    public static function new_requests($limit = 50)
    {
        global $wpdb;
        $table = self::table();
        $rows = $wpdb->get_results($wpdb->prepare(
            "SELECT id, item_id, created_at FROM $table WHERE status = 'new' ORDER BY id ASC LIMIT %d",
            $limit
        ), ARRAY_A);
        $out = [];
        foreach ($rows ?: [] as $r) {
            $out[] = [
                'id' => (int) $r['id'],
                'item' => (int) $r['item_id'],
                'time' => (int) strtotime($r['created_at'] . ' UTC'),
            ];
        }
        return $out;
    }

    public static function set_status($id, $status)
    {
        global $wpdb;
        if (!in_array($status, self::STATUSES, true)) {
            return;
        }
        $wpdb->update(self::table(), ['status' => $status, 'updated_at' => current_time('mysql', true)], ['id' => (int) $id]);
    }

    public static function request_status($id)
    {
        global $wpdb;
        $table = self::table();
        return $wpdb->get_var($wpdb->prepare("SELECT status FROM $table WHERE id = %d", (int) $id));
    }

    public static function recent_requests($limit = 50)
    {
        global $wpdb;
        $table = self::table();
        return $wpdb->get_results($wpdb->prepare(
            "SELECT id, title, artist, status, created_at FROM $table ORDER BY id DESC LIMIT %d",
            $limit
        ), ARRAY_A) ?: [];
    }

    public static function clear_requests()
    {
        global $wpdb;
        $table = self::table();
        // Requests that are still waiting stay, so nothing gets lost in the middle of a show.
        $wpdb->query("DELETE FROM $table WHERE status NOT IN ('new', 'pending', 'queued')");
    }
}
