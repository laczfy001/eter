<?php
// Settings → Éter Radio: the key to paste into Éter, the connection state, block refresh and the recent requests.

if (!defined('ABSPATH')) {
    exit;
}

class Eter_Radio_Admin
{
    public static function init()
    {
        add_action('admin_menu', function () {
            add_options_page(__('Éter Radio', 'eter-radio'), __('Éter Radio', 'eter-radio'), 'manage_options', 'eter-radio',
                [__CLASS__, 'page']);
        });
        add_action('admin_post_eter_radio_save', [__CLASS__, 'save']);
        add_filter('plugin_action_links_' . plugin_basename(ETER_RADIO_FILE), function ($links) {
            array_unshift($links, '<a href="' . esc_url(admin_url('options-general.php?page=eter-radio')) . '">'
                . esc_html__('Settings', 'eter-radio') . '</a>');
            return $links;
        });
    }

    public static function save()
    {
        if (!current_user_can('manage_options')) {
            wp_die(esc_html__('You are not allowed to do this.', 'eter-radio'));
        }
        check_admin_referer('eter_radio_save');
        $what = sanitize_key($_POST['what'] ?? 'settings');
        $msg = 'saved';
        if ($what === 'newkey') {
            update_option('eter_radio_key', Eter_Radio_Store::new_key(), false);
            $msg = 'newkey';
        } elseif ($what === 'clear') {
            Eter_Radio_Store::clear_requests();
            $msg = 'cleared';
        } else {
            update_option('eter_radio_settings', [
                'refresh' => max(5, min(120, (int) ($_POST['refresh'] ?? 10))),
                'offline_after' => max(30, min(3600, (int) ($_POST['offline_after'] ?? 90))),
                'language' => in_array($_POST['language'] ?? '', Eter_Radio_Store::LANGUAGES, true) ? $_POST['language'] : 'en_US',
            ], false);
        }
        wp_safe_redirect(admin_url('options-general.php?page=eter-radio&eter_msg=' . $msg));
        exit;
    }

    private static function status_label($s)
    {
        $labels = [
            'new' => __('new', 'eter-radio'),
            'pending' => __('awaiting approval', 'eter-radio'),
            'queued' => __('in the playlist', 'eter-radio'),
            'played' => __('played', 'eter-radio'),
            'rejected' => __('rejected', 'eter-radio'),
            'invalid' => __('not available', 'eter-radio'),
            'removed' => __('removed from the playlist', 'eter-radio'),
        ];
        return $labels[$s] ?? $s;
    }

    public static function page()
    {
        if (!current_user_can('manage_options')) {
            return;
        }
        $state = Eter_Radio_Store::state();
        $set = Eter_Radio_Store::settings();
        $online = Eter_Radio_Store::online($state);
        $msg = sanitize_key($_GET['eter_msg'] ?? '');
        $action = esc_url(admin_url('admin-post.php'));
        ?>
        <div class="wrap">
            <h1><?php esc_html_e('Éter Radio', 'eter-radio'); ?></h1>
            <?php if ($msg === 'newkey') : ?>
                <div class="notice notice-warning"><p><?php esc_html_e('A new key was created. Paste it into Éter, otherwise Éter can no longer update the site.', 'eter-radio'); ?></p></div>
            <?php elseif ($msg === 'cleared') : ?>
                <div class="notice notice-success"><p><?php esc_html_e('Finished requests were cleared.', 'eter-radio'); ?></p></div>
            <?php elseif ($msg === 'saved') : ?>
                <div class="notice notice-success"><p><?php esc_html_e('Settings saved.', 'eter-radio'); ?></p></div>
            <?php endif; ?>

            <h2><?php esc_html_e('Connecting Éter', 'eter-radio'); ?></h2>
            <p><?php esc_html_e('In Éter, open Settings → Website, and enter these two values:', 'eter-radio'); ?></p>
            <table class="form-table" role="presentation">
                <tr>
                    <th scope="row"><?php esc_html_e('Website address', 'eter-radio'); ?></th>
                    <td><code><?php echo esc_html(home_url('/')); ?></code></td>
                </tr>
                <tr>
                    <th scope="row"><?php esc_html_e('Key', 'eter-radio'); ?></th>
                    <td>
                        <input type="text" class="regular-text code" readonly value="<?php echo esc_attr(Eter_Radio_Store::key()); ?>" onfocus="this.select()">
                        <p class="description"><?php esc_html_e('Treat it like a password: with it anyone could change what the site shows.', 'eter-radio'); ?></p>
                        <form method="post" action="<?php echo $action; ?>" style="margin-top:8px">
                            <?php wp_nonce_field('eter_radio_save'); ?>
                            <input type="hidden" name="action" value="eter_radio_save">
                            <input type="hidden" name="what" value="newkey">
                            <?php submit_button(__('Create a new key', 'eter-radio'), 'secondary', 'submit', false); ?>
                        </form>
                    </td>
                </tr>
                <tr>
                    <th scope="row"><?php esc_html_e('Connection', 'eter-radio'); ?></th>
                    <td>
                        <?php if (empty($state['synced_at'])) : ?>
                            <?php esc_html_e('Éter has not connected yet.', 'eter-radio'); ?>
                        <?php else : ?>
                            <?php
                            printf(
                                /* translators: 1: online/offline, 2: time ago, 3: version */
                                esc_html__('%1$s – last update %2$s ago, Éter %3$s.', 'eter-radio'),
                                $online ? '<strong style="color:#1a7f37">' . esc_html__('Online', 'eter-radio') . '</strong>'
                                        : '<strong style="color:#b32d2e">' . esc_html__('Offline', 'eter-radio') . '</strong>',
                                esc_html(human_time_diff((int) $state['synced_at'], time())),
                                esc_html($state['version'] ?? '')
                            );
                            ?>
                            <br>
                            <?php
                            $req = $state['requests'] ?? [];
                            if (empty($req['enabled'])) {
                                esc_html_e('Song requests are turned off in Éter.', 'eter-radio');
                            } elseif (!empty($req['open'])) {
                                esc_html_e('Song requests are open now.', 'eter-radio');
                            } else {
                                esc_html_e('Song requests are on, but closed at this hour.', 'eter-radio');
                            }
                            echo ' ';
                            /* translators: %d: number of tracks */
                            printf(esc_html__('Requestable tracks: %d.', 'eter-radio'), count(Eter_Radio_Store::catalog()));
                            ?>
                        <?php endif; ?>
                    </td>
                </tr>
            </table>

            <h2><?php esc_html_e('Blocks', 'eter-radio'); ?></h2>
            <p><?php esc_html_e('Add the Now playing, Up next and Song request blocks in the block editor (Widgets category). In classic themes and page builders use the shortcodes [eter_now_playing], [eter_up_next count="5"] and [eter_request].', 'eter-radio'); ?></p>
            <form method="post" action="<?php echo $action; ?>">
                <?php wp_nonce_field('eter_radio_save'); ?>
                <input type="hidden" name="action" value="eter_radio_save">
                <input type="hidden" name="what" value="settings">
                <table class="form-table" role="presentation">
                    <tr>
                        <th scope="row"><label for="eter-language"><?php esc_html_e('Language', 'eter-radio'); ?></label></th>
                        <td>
                            <select id="eter-language" name="language">
                                <?php
                                $langs = [
                                    'en_US' => 'English',
                                    'nl_NL' => 'Nederlands',
                                    'hu_HU' => 'Magyar',
                                    'auto' => __('The site’s language', 'eter-radio'),
                                ];
                                foreach ($langs as $code => $name) {
                                    printf('<option value="%s"%s>%s</option>', esc_attr($code), selected($set['language'], $code, false), esc_html($name));
                                }
                                ?>
                            </select>
                            <p class="description"><?php esc_html_e('The language of the blocks on the site and of this page. With “The site’s language”, it follows Settings → General → Site Language (English if the plugin has no translation for it).', 'eter-radio'); ?></p>
                        </td>
                    </tr>
                    <tr>
                        <th scope="row"><label for="eter-refresh"><?php esc_html_e('Refresh the blocks every', 'eter-radio'); ?></label></th>
                        <td><input id="eter-refresh" name="refresh" type="number" min="5" max="120" value="<?php echo (int) $set['refresh']; ?>" class="small-text"> <?php esc_html_e('seconds', 'eter-radio'); ?></td>
                    </tr>
                    <tr>
                        <th scope="row"><label for="eter-offline"><?php esc_html_e('Show the station as offline after', 'eter-radio'); ?></label></th>
                        <td><input id="eter-offline" name="offline_after" type="number" min="30" max="3600" value="<?php echo (int) $set['offline_after']; ?>" class="small-text"> <?php esc_html_e('seconds without an update from Éter (requests are closed meanwhile)', 'eter-radio'); ?></td>
                    </tr>
                </table>
                <?php submit_button(); ?>
            </form>

            <h2><?php esc_html_e('Recent requests', 'eter-radio'); ?></h2>
            <?php $rows = Eter_Radio_Store::recent_requests(); ?>
            <?php if (!$rows) : ?>
                <p><?php esc_html_e('No requests yet.', 'eter-radio'); ?></p>
            <?php else : ?>
                <table class="widefat striped" style="max-width:900px">
                    <thead><tr>
                        <th><?php esc_html_e('Time', 'eter-radio'); ?></th>
                        <th><?php esc_html_e('Artist', 'eter-radio'); ?></th>
                        <th><?php esc_html_e('Title', 'eter-radio'); ?></th>
                        <th><?php esc_html_e('Status', 'eter-radio'); ?></th>
                    </tr></thead>
                    <tbody>
                    <?php foreach ($rows as $r) : ?>
                        <tr>
                            <td><?php echo esc_html(get_date_from_gmt($r['created_at'], get_option('date_format') . ' H:i')); ?></td>
                            <td><?php echo esc_html($r['artist']); ?></td>
                            <td><?php echo esc_html($r['title']); ?></td>
                            <td><?php echo esc_html(self::status_label($r['status'])); ?></td>
                        </tr>
                    <?php endforeach; ?>
                    </tbody>
                </table>
                <form method="post" action="<?php echo $action; ?>" style="margin-top:12px">
                    <?php wp_nonce_field('eter_radio_save'); ?>
                    <input type="hidden" name="action" value="eter_radio_save">
                    <input type="hidden" name="what" value="clear">
                    <?php submit_button(__('Clear finished requests', 'eter-radio'), 'secondary', 'submit', false); ?>
                </form>
            <?php endif; ?>
        </div>
        <?php
    }
}
