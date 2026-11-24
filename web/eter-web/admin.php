<?php
// Éter Web – admin page: the address and key for Éter, the language, the embed codes and the recent requests.
// Protected by the password in config.php.

define('ETER_WEB', true);
require __DIR__ . '/lib/core.php';

session_name('eterweb');
session_start(['cookie_httponly' => true, 'cookie_samesite' => 'Strict']);
header('Content-Type: text/html; charset=utf-8');
header('X-Frame-Options: DENY');
header('Cache-Control: no-store');

$password = (string) ($ETER_ADMIN_PASSWORD ?? '');
$writable = is_writable(eter_data_dir());
if (empty($_SESSION['csrf'])) {
    $_SESSION['csrf'] = bin2hex(random_bytes(16));
}
$csrf = $_SESSION['csrf'];
$error = '';
$notice = '';

if (($_POST['action'] ?? '') === 'login' && $password !== '') {
    if (hash_equals($password, (string) ($_POST['password'] ?? ''))) {
        session_regenerate_id(true);
        $_SESSION['ok'] = true;
        header('Location: admin.php');
        exit;
    }
    sleep(1);
    $error = __e('Wrong password.');
}
if (($_GET['logout'] ?? '') === '1') {
    $_SESSION = [];
    session_destroy();
    header('Location: admin.php');
    exit;
}
$in = $password !== '' && !empty($_SESSION['ok']);

if ($in && $_SERVER['REQUEST_METHOD'] === 'POST' && hash_equals($csrf, (string) ($_POST['csrf'] ?? ''))) {
    $action = (string) ($_POST['action'] ?? '');
    if ($action === 'settings') {
        eter_update('settings', [], function (&$s) {
            $lang = (string) ($_POST['language'] ?? 'en_US');
            $s['language'] = isset(ETER_LANGUAGES[$lang]) ? $lang : 'en_US';
            $s['refresh'] = max(5, min(120, (int) ($_POST['refresh'] ?? 10)));
            $s['offline_after'] = max(30, min(3600, (int) ($_POST['offline_after'] ?? 90)));
        });
        header('Location: admin.php?msg=saved');
        exit;
    }
    if ($action === 'newkey') {
        eter_update('settings', [], function (&$s) {
            $s['key'] = eter_new_key();
        });
        header('Location: admin.php?msg=newkey');
        exit;
    }
    if ($action === 'clear') {
        eter_update('requests', ['next_id' => 1, 'list' => []], function (&$r) {
            $r['list'] = array_values(array_filter($r['list'], function ($q) {
                return in_array($q['status'], ['new', 'pending', 'queued'], true);
            }));
        });
        header('Location: admin.php?msg=cleared');
        exit;
    }
}
$msg = (string) ($_GET['msg'] ?? '');
if ($msg === 'saved') $notice = __e('Settings saved.');
if ($msg === 'newkey') $notice = __e('A new key was created. Paste it into Éter, otherwise Éter can no longer update the site.');
if ($msg === 'cleared') $notice = __e('Finished requests were cleared.');

$statusLabels = [
    'new' => __e('new'), 'pending' => __e('awaiting approval'), 'queued' => __e('in the playlist'), 'played' => __e('played'),
    'rejected' => __e('rejected'), 'invalid' => __e('not available'), 'removed' => __e('removed from the playlist'),
];
$base = eter_base_url();
?><!doctype html>
<html lang="<?php echo h(substr(eter_settings()['language'], 0, 2)); ?>">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title><?php echo h(__e('Éter Web')); ?></title>
<style>
  :root { --bg: #f4f5f7; --card: #fff; --text: #1d2127; --muted: #626b77; --line: #dde1e6; --accent: #c77a00; --ok: #1a7f37; --bad: #b32d2e; }
  @media (prefers-color-scheme: dark) { :root { --bg: #111317; --card: #1b1e24; --text: #e8eaed; --muted: #9aa3af; --line: #30353e; --accent: #f5a524; --ok: #4cc38a; --bad: #ff6b6b; } }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.5 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  main { max-width: 920px; margin: 0 auto; padding: 24px 16px 48px; }
  h1 { font-size: 22px; margin: 0 0 4px; } h2 { font-size: 16px; margin: 0 0 12px; }
  .sub { color: var(--muted); margin: 0 0 20px; }
  section { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 18px 20px; margin: 0 0 16px; }
  label { display: block; font-weight: 600; margin: 12px 0 4px; }
  input[type=text], input[type=password], input[type=number], select, textarea { font: inherit; color: inherit; background: var(--bg); border: 1px solid var(--line); border-radius: 6px; padding: 7px 10px; }
  input[type=text], textarea { width: 100%; }
  textarea { font: 13px/1.45 ui-monospace, "IBM Plex Mono", Menlo, monospace; resize: vertical; }
  code { font: 13px ui-monospace, "IBM Plex Mono", Menlo, monospace; background: var(--bg); padding: 2px 6px; border-radius: 4px; word-break: break-all; }
  button { font: inherit; border: 1px solid var(--line); background: var(--bg); color: var(--text); border-radius: 6px; padding: 7px 14px; cursor: pointer; }
  button.primary { background: var(--accent); border-color: var(--accent); color: #111; font-weight: 600; }
  .row { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }
  .muted { color: var(--muted); font-size: 14px; }
  .notice { border-left: 4px solid var(--accent); background: var(--card); padding: 10px 14px; margin: 0 0 16px; border-radius: 6px; }
  .error { border-left-color: var(--bad); }
  .ok { color: var(--ok); font-weight: 600; } .bad { color: var(--bad); font-weight: 600; }
  table { width: 100%; border-collapse: collapse; font-size: 14px; }
  th, td { text-align: left; padding: 7px 8px; border-bottom: 1px solid var(--line); }
  th { color: var(--muted); font-weight: 500; }
  .snippet { margin: 10px 0 16px; }
  .snippet b { display: block; margin-bottom: 4px; }
  .login { max-width: 360px; margin: 12vh auto; }
</style>
</head>
<body>
<main>
<?php if (!$writable) : ?>
  <div class="notice error"><?php echo h(__e('The data folder is not writable by the web server:')); ?> <code><?php echo h(eter_data_dir()); ?></code></div>
<?php endif; ?>
<?php if ($password === '') : ?>
  <section class="login">
    <h1><?php echo h(__e('Éter Web')); ?></h1>
    <p><?php echo h(__e('Set the admin password in config.php ($ETER_ADMIN_PASSWORD), then reload this page.')); ?></p>
  </section>
<?php elseif (!$in) : ?>
  <section class="login">
    <h1><?php echo h(__e('Éter Web')); ?></h1>
    <?php if ($error) : ?><div class="notice error"><?php echo h($error); ?></div><?php endif; ?>
    <form method="post">
      <input type="hidden" name="action" value="login">
      <label for="pw"><?php echo h(__e('Password')); ?></label>
      <input id="pw" type="password" name="password" autofocus style="width:100%">
      <p><button class="primary" type="submit"><?php echo h(__e('Log in')); ?></button></p>
    </form>
  </section>
<?php else :
    $set = eter_settings();
    $state = eter_read('state', []);
    $online = eter_online($state);
    $catalog = eter_read('catalog', ['items' => []]);
    $requests = array_reverse(eter_read('requests', ['list' => []])['list']);
    ?>
  <div class="row" style="justify-content:space-between">
    <h1><?php echo h(__e('Éter Web')); ?></h1>
    <a href="?logout=1" class="muted"><?php echo h(__e('Log out')); ?></a>
  </div>
  <p class="sub"><?php echo h(__e('Now playing, up next, most played and song requests on any website, fed live by Éter.')); ?></p>
  <?php if ($notice) : ?><div class="notice"><?php echo h($notice); ?></div><?php endif; ?>

  <section>
    <h2><?php echo h(__e('Connecting Éter')); ?></h2>
    <p><?php echo h(__e('In Éter, open Settings → Website, and enter these two values:')); ?></p>
    <label><?php echo h(__e('Website address')); ?></label>
    <code><?php echo h($base); ?></code>
    <label for="key"><?php echo h(__e('Key')); ?></label>
    <input id="key" type="text" readonly value="<?php echo h($set['key']); ?>" onfocus="this.select()">
    <p class="muted"><?php echo h(__e('Treat it like a password: with it anyone could change what the site shows.')); ?></p>
    <form method="post" onsubmit="return confirm(<?php echo h(json_encode(__e('Create a new key'))); ?> + '?')">
      <input type="hidden" name="csrf" value="<?php echo h($csrf); ?>"><input type="hidden" name="action" value="newkey">
      <button type="submit"><?php echo h(__e('Create a new key')); ?></button>
    </form>
    <label><?php echo h(__e('Connection')); ?></label>
    <div>
      <?php if (empty($state['synced_at'])) : ?>
        <?php echo h(__e('Éter has not connected yet.')); ?>
      <?php else : ?>
        <?php
        $ago = max(0, time() - (int) $state['synced_at']);
        $agoText = $ago < 120 ? $ago . ' s' : ($ago < 7200 ? round($ago / 60) . ' min' : round($ago / 3600) . ' h');
        echo sprintf(h(__e('%1$s – last update %2$s ago, Éter %3$s.')),
            $online ? '<span class="ok">' . h(__e('Online')) . '</span>' : '<span class="bad">' . h(__e('Offline')) . '</span>',
            h($agoText), h($state['version'] ?? ''));
        ?><br>
        <?php
        if (empty($state['requests']['enabled'])) echo h(__e('Song requests are turned off in Éter.'));
        elseif (!empty($state['requests']['open'])) echo h(__e('Song requests are open now.'));
        else echo h(__e('Song requests are on, but closed at this hour.'));
        echo ' ' . h(sprintf(__e('Requestable tracks: %d.'), count($catalog['items'])));
        ?>
      <?php endif; ?>
    </div>
  </section>

  <section>
    <h2><?php echo h(__e('Settings')); ?></h2>
    <form method="post">
      <input type="hidden" name="csrf" value="<?php echo h($csrf); ?>"><input type="hidden" name="action" value="settings">
      <label for="language"><?php echo h(__e('Language')); ?></label>
      <select id="language" name="language">
        <?php foreach (ETER_LANGUAGES as $code => $name) : ?>
          <option value="<?php echo h($code); ?>"<?php echo $set['language'] === $code ? ' selected' : ''; ?>><?php echo h($name); ?></option>
        <?php endforeach; ?>
      </select>
      <p class="muted"><?php echo h(__e('The language of the widgets and of this page.')); ?></p>
      <label for="refresh"><?php echo h(__e('Refresh the blocks every')); ?></label>
      <div class="row"><input id="refresh" type="number" name="refresh" min="5" max="120" value="<?php echo (int) $set['refresh']; ?>" style="width:90px"> <span><?php echo h(__e('seconds')); ?></span></div>
      <label for="offline"><?php echo h(__e('Show the station as offline after')); ?></label>
      <div class="row"><input id="offline" type="number" name="offline_after" min="30" max="3600" value="<?php echo (int) $set['offline_after']; ?>" style="width:90px"> <span><?php echo h(__e('seconds without an update from Éter (requests are closed meanwhile)')); ?></span></div>
      <p><button class="primary" type="submit"><?php echo h(__e('Save')); ?></button></p>
    </form>
  </section>

  <section>
    <h2><?php echo h(__e('Putting the widgets on a website')); ?></h2>
    <p><?php echo h(__e('Paste the script line once into the page (or the site’s header), then a line for each widget where it should appear. The widgets take the fonts and colours of the page.')); ?></p>
    <?php
    $js = '<script src="' . $base . '?js" defer></script>';
    $snippets = [
        __e('Script (once per page)') => $js,
        __e('Now playing') => '<div data-eter="now" data-heading="' . __e('Now playing') . '"></div>',
        __e('Up next') => '<div data-eter="next" data-count="5" data-heading="' . __e('Up next') . '"></div>',
        __e('Most played') => '<div data-eter="top" data-period="week" data-count="10" data-heading="' . __e('Most played') . '"></div>',
        __e('Song request') => '<div data-eter="request" data-heading="' . __e('Song request') . '"></div>',
    ];
    foreach ($snippets as $title => $code) : ?>
      <div class="snippet"><b><?php echo h($title); ?></b><textarea rows="1" readonly onfocus="this.select()"><?php echo h($code); ?></textarea></div>
    <?php endforeach; ?>
    <p class="muted"><?php echo h(__e('Options: data-count (1–20), data-period (day, week, month), data-times="0" hides the start times, data-plays="0" hides the number of plays, data-progress="0" hides the progress bar.')); ?></p>
    <h2 style="margin-top:20px"><?php echo h(__e('For site builders that only allow an iframe')); ?></h2>
    <?php
    $frames = [
        __e('Now playing') => '<iframe src="' . $base . '?embed=now" style="width:100%;height:90px;border:0" title="' . __e('Now playing') . '"></iframe>',
        __e('Up next') => '<iframe src="' . $base . '?embed=next&count=5" style="width:100%;height:240px;border:0" title="' . __e('Up next') . '"></iframe>',
        __e('Most played') => '<iframe src="' . $base . '?embed=top&period=week&count=10" style="width:100%;height:420px;border:0" title="' . __e('Most played') . '"></iframe>',
        __e('Song request') => '<iframe src="' . $base . '?embed=request" style="width:100%;height:420px;border:0" title="' . __e('Song request') . '"></iframe>',
    ];
    foreach ($frames as $title => $code) : ?>
      <div class="snippet"><b><?php echo h($title); ?></b><textarea rows="2" readonly onfocus="this.select()"><?php echo h($code); ?></textarea></div>
    <?php endforeach; ?>
    <p class="muted"><?php echo h(__e('Add &theme=light or &theme=dark to the address for a white or dark background (transparent by default), and &heading=… for a title.')); ?></p>
  </section>

  <section>
    <h2><?php echo h(__e('Recent requests')); ?></h2>
    <?php if (!$requests) : ?>
      <p><?php echo h(__e('No requests yet.')); ?></p>
    <?php else : ?>
      <table>
        <tr><th><?php echo h(__e('Time')); ?></th><th><?php echo h(__e('Artist')); ?></th><th><?php echo h(__e('Title')); ?></th><th><?php echo h(__e('Status')); ?></th></tr>
        <?php foreach (array_slice($requests, 0, 50) as $q) : ?>
          <tr><td><time data-ts="<?php echo (int) $q['created']; ?>"><?php echo h(gmdate('Y-m-d H:i', (int) $q['created'])); ?> UTC</time></td><td><?php echo h($q['artist']); ?></td><td><?php echo h($q['title']); ?></td><td><?php echo h($statusLabels[$q['status']] ?? $q['status']); ?></td></tr>
        <?php endforeach; ?>
      </table>
      <form method="post" style="margin-top:12px">
        <input type="hidden" name="csrf" value="<?php echo h($csrf); ?>"><input type="hidden" name="action" value="clear">
        <button type="submit"><?php echo h(__e('Clear finished requests')); ?></button>
      </form>
    <?php endif; ?>
  </section>
  <p class="muted">Éter Web <?php echo h(ETER_WEB_VERSION); ?></p>
  <script>
    // The request times in the browser's own time zone
    document.querySelectorAll('time[data-ts]').forEach(function (t) {
      var d = new Date(parseInt(t.getAttribute('data-ts'), 10) * 1000);
      var p = function (n) { return ('0' + n).slice(-2); };
      t.textContent = d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
    });
  </script>
<?php endif; ?>
</main>
</body>
</html>
