<?php
// The three blocks (Now playing, Up next, Song request) and the same as shortcodes for classic themes and page
// builders: [eter_now_playing], [eter_up_next count="5"], [eter_request]. The server renders the current state, the
// script in the page keeps it up to date.

if (!defined('ABSPATH')) {
    exit;
}

class Eter_Radio_Blocks
{
    public static function register()
    {
        $url = plugins_url('assets/', ETER_RADIO_FILE);
        wp_register_style('eter-radio', $url . 'eter-radio.css', [], ETER_RADIO_VERSION);
        wp_register_script('eter-radio', $url . 'eter-radio.js', [], ETER_RADIO_VERSION, true);
        wp_register_script('eter-radio-editor', $url . 'editor.js',
            ['wp-blocks', 'wp-element', 'wp-components', 'wp-block-editor', 'wp-server-side-render', 'wp-i18n'],
            ETER_RADIO_VERSION, true);
        wp_set_script_translations('eter-radio-editor', 'eter-radio', plugin_dir_path(ETER_RADIO_FILE) . 'languages');
        wp_localize_script('eter-radio-editor', 'eterRadioEditor', [
            'now' => __('Now playing', 'eter-radio'),
            'nowDesc' => __('The track on air, updated live by Éter.', 'eter-radio'),
            'next' => __('Up next', 'eter-radio'),
            'nextDesc' => __('The next tracks in the playlist.', 'eter-radio'),
            'request' => __('Song request', 'eter-radio'),
            'requestDesc' => __('Visitors search the library and request a track, while requests are open.', 'eter-radio'),
            'heading' => __('Heading', 'eter-radio'),
            'count' => __('Number of tracks', 'eter-radio'),
            'showTimes' => __('Show start times', 'eter-radio'),
            'showProgress' => __('Show progress bar', 'eter-radio'),
            'top' => __('Most played', 'eter-radio'),
            'topDesc' => __('The most played songs of the last day, week or month.', 'eter-radio'),
            'period' => __('Period', 'eter-radio'),
            'day' => __('Last 24 hours', 'eter-radio'),
            'week' => __('Last 7 days', 'eter-radio'),
            'month' => __('Last 30 days', 'eter-radio'),
            'showPlays' => __('Show the number of plays', 'eter-radio'),
        ]);

        $common = ['editor_script' => 'eter-radio-editor', 'style' => 'eter-radio'];
        register_block_type('eter/now-playing', $common + [
            'attributes' => [
                'heading' => ['type' => 'string', 'default' => ''],
                'showProgress' => ['type' => 'boolean', 'default' => true],
            ],
            'render_callback' => [__CLASS__, 'render_now'],
        ]);
        register_block_type('eter/up-next', $common + [
            'attributes' => [
                'heading' => ['type' => 'string', 'default' => ''],
                'count' => ['type' => 'number', 'default' => 5],
                'showTimes' => ['type' => 'boolean', 'default' => true],
            ],
            'render_callback' => [__CLASS__, 'render_next'],
        ]);
        register_block_type('eter/most-played', $common + [
            'attributes' => [
                'heading' => ['type' => 'string', 'default' => ''],
                'period' => ['type' => 'string', 'default' => 'week'],
                'count' => ['type' => 'number', 'default' => 10],
                'showPlays' => ['type' => 'boolean', 'default' => true],
            ],
            'render_callback' => [__CLASS__, 'render_top'],
        ]);
        register_block_type('eter/request', $common + [
            'attributes' => [
                'heading' => ['type' => 'string', 'default' => ''],
            ],
            'render_callback' => [__CLASS__, 'render_request'],
        ]);

        add_shortcode('eter_now_playing', function ($a) {
            $a = shortcode_atts(['heading' => '', 'progress' => 'yes'], $a);
            return self::render_now(['heading' => $a['heading'], 'showProgress' => $a['progress'] !== 'no']);
        });
        add_shortcode('eter_up_next', function ($a) {
            $a = shortcode_atts(['heading' => '', 'count' => 5, 'times' => 'yes'], $a);
            return self::render_next(['heading' => $a['heading'], 'count' => (int) $a['count'], 'showTimes' => $a['times'] !== 'no']);
        });
        add_shortcode('eter_most_played', function ($a) {
            $a = shortcode_atts(['heading' => '', 'period' => 'week', 'count' => 10, 'plays' => 'yes'], $a);
            return self::render_top(['heading' => $a['heading'], 'period' => $a['period'], 'count' => (int) $a['count'],
                                     'showPlays' => $a['plays'] !== 'no']);
        });
        add_shortcode('eter_request', function ($a) {
            $a = shortcode_atts(['heading' => ''], $a);
            return self::render_request(['heading' => $a['heading']]);
        });
    }

    // The front-end script and its texts, once per page.
    private static function enqueue()
    {
        static $done = false;
        wp_enqueue_style('eter-radio');
        if ($done) {
            return;
        }
        $done = true;
        wp_enqueue_script('eter-radio');
        wp_localize_script('eter-radio', 'eterRadio', [
            'rest' => esc_url_raw(rest_url('eter/v1/')),
            'refresh' => Eter_Radio_Store::settings()['refresh'],
            'state' => Eter_Radio_Rest::public_state(),
            't' => [
                'offline' => __('The station is offline right now.', 'eter-radio'),
                'nothing' => __('Nothing is playing right now.', 'eter-radio'),
                'empty' => __('No upcoming tracks yet.', 'eter-radio'),
                'request' => __('Request', 'eter-radio'),
                'requested' => __('Request', 'eter-radio') . ' ✓',
                'closed' => __('Requests are closed right now.', 'eter-radio'),
                'search' => __('Search for an artist or title…', 'eter-radio'),
                'noResults' => __('No matching tracks.', 'eter-radio'),
                'sending' => __('Sending…', 'eter-radio'),
                'error' => __('Something went wrong, please try again.', 'eter-radio'),
                'yourRequest' => __('Your request: %s', 'eter-radio'),
                'requestBadge' => __('request', 'eter-radio'),
                'noTop' => __('No plays in this period yet.', 'eter-radio'),
                /* translators: %d: number of plays */
                'plays' => __('%d plays', 'eter-radio'),
                'play1' => __('1 play', 'eter-radio'),
                'status' => [
                    'new' => __('received, waiting for the studio', 'eter-radio'),
                    'pending' => __('waiting for approval in the studio', 'eter-radio'),
                    'queued' => __('in the playlist, coming up soon', 'eter-radio'),
                    'played' => __('played – thank you!', 'eter-radio'),
                    'rejected' => __('not accepted this time', 'eter-radio'),
                    'invalid' => __('this track is not available now', 'eter-radio'),
                    'removed' => __('could not be played this time', 'eter-radio'),
                ],
            ],
        ]);
    }

    private static function heading($text)
    {
        return $text !== '' ? '<h3 class="eter-heading">' . esc_html($text) . '</h3>' : '';
    }

    private static function line($t)
    {
        $artist = trim((string) ($t['artist'] ?? ''));
        $title = trim((string) ($t['title'] ?? ''));
        return '<span class="eter-artist">' . esc_html($artist) . '</span>'
            . ($artist !== '' && $title !== '' ? '<span class="eter-sep"> – </span>' : '')
            . '<span class="eter-title">' . esc_html($title) . '</span>'
            . (!empty($t['request']) ? '<span class="eter-badge">' . esc_html__('request', 'eter-radio') . '</span>' : '');
    }

    public static function render_now($a)
    {
        self::enqueue();
        $s = Eter_Radio_Rest::public_state();
        $body = $s['now'] ? self::line($s['now'])
            : '<span class="eter-muted">' . esc_html($s['online'] ? __('Nothing is playing right now.', 'eter-radio')
                                                                    : __('The station is offline right now.', 'eter-radio')) . '</span>';
        return '<div class="eter-block eter-now" data-eter="now" data-progress="' . (!empty($a['showProgress']) ? '1' : '0') . '">'
            . self::heading((string) ($a['heading'] ?? ''))
            . '<div class="eter-body">' . $body . '</div>'
            . (!empty($a['showProgress']) ? '<div class="eter-progress"><span></span></div>' : '')
            . '</div>';
    }

    public static function render_next($a)
    {
        self::enqueue();
        $count = max(1, min(20, (int) ($a['count'] ?? 5)));
        $s = Eter_Radio_Rest::public_state();
        $items = '';
        foreach (array_slice($s['next'], 0, $count) as $t) {
            $time = !empty($t['start']) ? wp_date('H:i', (int) floor($t['start'] / 1000)) : '';
            $items .= '<li' . (!empty($t['request']) ? ' class="eter-is-request"' : '') . '>'
                . (!empty($a['showTimes']) ? '<span class="eter-time">' . esc_html($time) . '</span>' : '') . self::line($t) . '</li>';
        }
        if ($items === '') {
            $items = '<li class="eter-muted">' . esc_html($s['online'] ? __('No upcoming tracks yet.', 'eter-radio')
                                                                      : __('The station is offline right now.', 'eter-radio')) . '</li>';
        }
        return '<div class="eter-block eter-next" data-eter="next" data-count="' . $count . '" data-times="'
            . (!empty($a['showTimes']) ? '1' : '0') . '">'
            . self::heading((string) ($a['heading'] ?? ''))
            . '<ol class="eter-list">' . $items . '</ol></div>';
    }

    public static function render_top($a)
    {
        self::enqueue();
        $period = in_array($a['period'] ?? 'week', ['day', 'week', 'month'], true) ? $a['period'] : 'week';
        $count = max(1, min(20, (int) ($a['count'] ?? 10)));
        $plays = !empty($a['showPlays']);
        $s = Eter_Radio_Rest::public_state();
        $items = '';
        foreach (array_slice((array) ($s['top'][$period] ?? []), 0, $count) as $t) {
            $n = (int) $t['plays'];
            $items .= '<li>' . self::line($t)
                . ($plays ? '<span class="eter-plays">' . esc_html($n === 1 ? __('1 play', 'eter-radio')
                                                                             : sprintf(__('%d plays', 'eter-radio'), $n)) . '</span>' : '')
                . '</li>';
        }
        if ($items === '') {
            $items = '<li class="eter-muted">' . esc_html__('No plays in this period yet.', 'eter-radio') . '</li>';
        }
        return '<div class="eter-block eter-top" data-eter="top" data-period="' . esc_attr($period) . '" data-count="' . $count
            . '" data-plays="' . ($plays ? '1' : '0') . '">'
            . self::heading((string) ($a['heading'] ?? ''))
            . '<ol class="eter-list eter-ranked">' . $items . '</ol></div>';
    }

    public static function render_request($a)
    {
        self::enqueue();
        $s = Eter_Radio_Rest::public_state();
        return '<div class="eter-block eter-request" data-eter="request">'
            . self::heading((string) ($a['heading'] ?? ''))
            . '<div class="eter-closed eter-muted"' . ($s['requests']['open'] ? ' hidden' : '') . '>'
            . esc_html(__('Requests are closed right now.', 'eter-radio')) . '</div>'
            . '<div class="eter-open"' . ($s['requests']['open'] ? '' : ' hidden') . '>'
            . '<input type="search" class="eter-search" autocomplete="off" placeholder="'
            . esc_attr(__('Search for an artist or title…', 'eter-radio')) . '" aria-label="'
            . esc_attr(__('Search for an artist or title…', 'eter-radio')) . '">'
            . '<ul class="eter-results"></ul></div>'
            . '<div class="eter-mine" hidden></div>'
            . '</div>';
    }
}
