<?php
// Éter Web: the widget script (with its texts and settings) and the pages for iframes.

if (!defined('ETER_WEB')) {
    exit;
}

function eter_widget_config()
{
    return [
        'base' => eter_base_url(),
        'refresh' => eter_settings()['refresh'],
        't' => [
            'offline' => __e('The station is offline right now.'),
            'nothing' => __e('Nothing is playing right now.'),
            'empty' => __e('No upcoming tracks yet.'),
            'request' => __e('Request'),
            'requested' => __e('Request') . ' ✓',
            'closed' => __e('Requests are closed right now.'),
            'search' => __e('Search for an artist or title…'),
            'noResults' => __e('No matching tracks.'),
            'sending' => __e('Sending…'),
            'error' => __e('Something went wrong, please try again.'),
            'yourRequest' => __e('Your request: %s'),
            'requestBadge' => __e('request'),
            'noTop' => __e('No plays in this period yet.'),
            'plays' => __e('%d plays'),
            'play1' => __e('1 play'),
            'status' => [
                'new' => __e('received, waiting for the studio'),
                'pending' => __e('waiting for approval in the studio'),
                'queued' => __e('in the playlist, coming up soon'),
                'played' => __e('played – thank you!'),
                'rejected' => __e('not accepted this time'),
                'invalid' => __e('this track is not available now'),
                'removed' => __e('could not be played this time'),
            ],
        ],
    ];
}

function eter_widget_js()
{
    eter_cors();
    header('Content-Type: application/javascript; charset=utf-8');
    header('Cache-Control: public, max-age=300');
    echo 'window.EterWeb=' . json_encode(eter_widget_config(), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES) . ";\n";
    readfile(__DIR__ . '/../assets/eter-web.js');
}

// A whole page with one widget, for sites that only allow iframes. ?embed=now|next|top|request, and optionally
// &heading=, &count=, &period=day|week|month, &times=0, &plays=0, &progress=0, &theme=light|dark (default: transparent,
// dark text), &font=inherit.
function eter_embed_page($kind)
{
    if (!in_array($kind, ['now', 'next', 'top', 'request'], true)) {
        http_response_code(404);
        return;
    }
    $attrs = ' data-eter="' . h($kind) . '"';
    foreach (['heading', 'count', 'period', 'times', 'plays', 'progress'] as $k) {
        if (isset($_GET[$k])) {
            $attrs .= ' data-' . $k . '="' . h(substr((string) $_GET[$k], 0, 120)) . '"';
        }
    }
    $theme = ($_GET['theme'] ?? '') === 'dark' ? 'dark' : (($_GET['theme'] ?? '') === 'light' ? 'light' : '');
    header('Content-Type: text/html; charset=utf-8');
    header_remove('X-Frame-Options');
    $lang = substr(eter_settings()['language'], 0, 2);
    ?><!doctype html>
<html lang="<?php echo h($lang); ?>">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Éter</title>
<style>
  html, body { margin: 0; padding: 0; background: transparent; font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; font-size: 16px; line-height: 1.45; }
  body.light { background: #ffffff; color: #1d2127; }
  body.dark { background: #15181d; color: #e8eaed; }
  body { padding: 8px 10px; }
</style>
<link rel="stylesheet" href="assets/eter-web.css?v=<?php echo ETER_WEB_VERSION; ?>">
</head>
<body class="<?php echo $theme; ?>">
<div<?php echo $attrs; ?>></div>
<script>window.EterWebNoCss = true;</script>
<script src="?js&v=<?php echo ETER_WEB_VERSION; ?>"></script>
</body>
</html>
<?php
}
