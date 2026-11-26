<?php
// Éter Web: storage, settings, translations and helpers. The data is kept in a few PHP files in the data folder;
// each starts with an exit, so they cannot be read over the web.

if (!defined('ETER_WEB')) {
    exit;
}

const ETER_WEB_VERSION = '1.0.1';
const ETER_STATUSES = ['new', 'pending', 'queued', 'played', 'rejected', 'invalid', 'removed'];
const ETER_LANGUAGES = ['en_US' => 'English', 'nl_NL' => 'Nederlands', 'hu_HU' => 'Magyar'];

require_once __DIR__ . '/../config.php';

function eter_data_dir()
{
    global $ETER_DATA_DIR;
    $dir = rtrim($ETER_DATA_DIR ?? (__DIR__ . '/../data'), '/\\');
    if (!is_dir($dir)) {
        @mkdir($dir, 0775, true);
    }
    return $dir;
}

// ---- Storage ------------------------------------------------------------------------------------------------------

function eter_read($name, $default)
{
    $file = eter_data_dir() . '/' . $name . '.php';
    if (!is_file($file)) {
        return $default;
    }
    $raw = @file_get_contents($file);
    if ($raw === false) {
        return $default;
    }
    $pos = strpos($raw, "\n");
    $data = json_decode($pos === false ? '' : substr($raw, $pos + 1), true);
    return is_array($data) ? $data : $default;
}

function eter_write($name, $data)
{
    $dir = eter_data_dir();
    $tmp = $dir . '/' . $name . '.' . bin2hex(random_bytes(4)) . '.tmp';
    $body = "<?php exit; ?>\n" . json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    if (@file_put_contents($tmp, $body, LOCK_EX) === false) {
        return false;
    }
    return @rename($tmp, $dir . '/' . $name . '.php');
}

// Read–modify–write under one lock, so two requests at the same time cannot lose each other's changes.
function eter_update($name, $default, callable $fn)
{
    $lock = @fopen(eter_data_dir() . '/.lock', 'c');
    if ($lock) {
        flock($lock, LOCK_EX);
    }
    $data = eter_read($name, $default);
    $result = $fn($data);
    eter_write($name, $data);
    if ($lock) {
        flock($lock, LOCK_UN);
        fclose($lock);
    }
    return $result;
}

// ---- Settings -----------------------------------------------------------------------------------------------------

function eter_settings()
{
    $s = eter_read('settings', []);
    if (empty($s['key'])) {
        $s = eter_update('settings', [], function (&$d) {
            if (empty($d['key'])) {
                $d['key'] = eter_new_key();
            }
            return $d;
        });
    }
    $lang = (string) ($s['language'] ?? 'en_US');
    return [
        'key' => (string) $s['key'],
        'language' => isset(ETER_LANGUAGES[$lang]) ? $lang : 'en_US',
        'refresh' => max(5, min(120, (int) ($s['refresh'] ?? 10))),
        'offline_after' => max(30, min(3600, (int) ($s['offline_after'] ?? 90))),
    ];
}

function eter_new_key()
{
    $chars = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789';
    $key = '';
    for ($i = 0; $i < 40; $i++) {
        $key .= $chars[random_int(0, strlen($chars) - 1)];
    }
    return $key;
}

// ---- Translations -------------------------------------------------------------------------------------------------

function __e($text)
{
    static $messages = null;
    if ($messages === null) {
        $messages = [];
        $lang = eter_settings()['language'];
        $file = __DIR__ . '/../lang/' . $lang . '.php';
        if ($lang !== 'en_US' && is_file($file)) {
            $t = include $file;
            $messages = is_array($t['messages'] ?? null) ? $t['messages'] : [];
        }
    }
    return $messages[$text] ?? $text;
}

function h($s)
{
    return htmlspecialchars((string) $s, ENT_QUOTES, 'UTF-8');
}

// ---- State --------------------------------------------------------------------------------------------------------

function eter_online(array $state)
{
    $at = (int) ($state['synced_at'] ?? 0);
    return $at > 0 && time() - $at <= eter_settings()['offline_after'];
}

function eter_public_state()
{
    $s = eter_read('state', []);
    $online = eter_online($s);
    return [
        'online' => $online,
        'server_time' => (int) round(microtime(true) * 1000),
        'now' => $online ? ($s['now'] ?? null) : null,
        'next' => $online ? ($s['next'] ?? []) : [],
        'requests' => [
            'enabled' => $online && !empty($s['requests']['enabled']),
            'open' => $online && !empty($s['requests']['open']),
        ],
        'top' => eter_read('top', ['day' => [], 'week' => [], 'month' => []]),
    ];
}

// Case- and accent-insensitive text for searching.
function eter_fold($s)
{
    $s = function_exists('mb_strtolower') ? mb_strtolower((string) $s, 'UTF-8') : strtolower((string) $s);
    if (class_exists('Normalizer')) {
        $n = Normalizer::normalize($s, Normalizer::FORM_D);
        if ($n !== false) {
            return preg_replace('/\p{Mn}+/u', '', $n);
        }
    }
    return strtr($s, [
        'á' => 'a', 'à' => 'a', 'â' => 'a', 'ä' => 'a', 'ã' => 'a', 'å' => 'a', 'é' => 'e', 'è' => 'e', 'ê' => 'e',
        'ë' => 'e', 'í' => 'i', 'ì' => 'i', 'î' => 'i', 'ï' => 'i', 'ó' => 'o', 'ò' => 'o', 'ô' => 'o', 'ö' => 'o',
        'õ' => 'o', 'ő' => 'o', 'ú' => 'u', 'ù' => 'u', 'û' => 'u', 'ü' => 'u', 'ű' => 'u', 'ç' => 'c', 'ñ' => 'n',
        'ß' => 'ss', 'ý' => 'y', 'ÿ' => 'y',
    ]);
}

function eter_clean($s, $max = 255)
{
    $s = trim(preg_replace('/[\x00-\x1F\x7F]+/u', ' ', strip_tags((string) $s)));
    return function_exists('mb_substr') ? mb_substr($s, 0, $max, 'UTF-8') : substr($s, 0, $max);
}

// The address of this folder as visitors reach it (for the embed codes and for Éter).
function eter_base_url()
{
    $https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || (($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https');
    $host = $_SERVER['HTTP_HOST'] ?? 'localhost';
    $path = rtrim(str_replace('\\', '/', dirname($_SERVER['SCRIPT_NAME'] ?? '/')), '/');
    return ($https ? 'https' : 'http') . '://' . $host . $path . '/';
}

// Several Éter installations on one site: only one of them (the owner) updates the site and receives the song
// requests; the owner is the one on air, or, if it stops syncing, the next one to sync. Returns [isOwner, owner, others].
function eter_claim($id, $name, $version, $onAir)
{
    $stale = eter_settings()['offline_after'];
    return eter_update('instances', ['owner' => '', 'list' => []], function (&$d) use ($id, $name, $version, $onAir, $stale) {
        $now = time();
        foreach ($d['list'] as $k => $i) {
            if ($now - (int) ($i['last'] ?? 0) > 86400) {
                unset($d['list'][$k]);
            }
        }
        $d['list'][$id] = ['name' => $name, 'version' => $version, 'last' => $now, 'on_air' => (bool) $onAir];
        $owner = (string) ($d['owner'] ?? '');
        $alive = $owner !== '' && isset($d['list'][$owner]) && $now - (int) $d['list'][$owner]['last'] <= $stale;
        if (!$alive || ($owner !== $id && $onAir && empty($d['list'][$owner]['on_air']))) {
            $owner = $id;
        }
        $d['owner'] = $owner;
        $others = [];
        foreach ($d['list'] as $k => $i) {
            if ($k !== $id && $now - (int) $i['last'] <= $stale) {
                $others[] = ['name' => (string) $i['name'], 'on_air' => !empty($i['on_air']), 'owner' => $k === $owner];
            }
        }
        return [$owner === $id, $d['list'][$owner] ?? null, $others];
    });
}

