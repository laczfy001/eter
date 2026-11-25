<?php
/**
 * Plugin Name:       Éter Radio
 * Description:       Now playing, up next, most played and song requests on your site, fed live by the Éter radio playout.
 * Version:           1.2.2
 * Requires at least: 6.0
 * Requires PHP:      7.4
 * Author:            Éter
 * License:           GPL-3.0-or-later
 * License URI:       https://www.gnu.org/licenses/gpl-3.0.html
 * Text Domain:       eter-radio
 * Domain Path:       /languages
 *
 * How it works: the Éter playout sends what is on air and what comes next to this site every few seconds (signed with
 * the shared key), and on the same call collects the visitors' song requests. The studio computer never has to be
 * reachable from the internet. The blocks on the site read the state through the public REST endpoints.
 */

if (!defined('ABSPATH')) {
    exit;
}

define('ETER_RADIO_VERSION', '1.2.2');
define('ETER_RADIO_FILE', __FILE__);

require_once __DIR__ . '/includes/class-store.php';
require_once __DIR__ . '/includes/class-rest.php';
require_once __DIR__ . '/includes/class-blocks.php';
require_once __DIR__ . '/includes/class-admin.php';

register_activation_hook(__FILE__, ['Eter_Radio_Store', 'activate']);

// The plugin's language (Settings → Éter Radio): English by default, or Dutch, Hungarian, or the site's language.
// English is the source text, so it needs no file; for "auto" WordPress loads the file for the site's (or the admin
// user's) language when it is first needed.
function eter_radio_load_language()
{
    $lang = Eter_Radio_Store::settings()['language'];
    $dir = __DIR__ . '/languages';
    if ($lang === 'auto') {
        load_plugin_textdomain('eter-radio', false, dirname(plugin_basename(__FILE__)) . '/languages');
    } elseif ($lang !== 'en_US') {
        // Loaded for the current locale (the site's, or in the admin the user's), so it is the one that gets used.
        load_textdomain('eter-radio', $dir . '/eter-radio-' . $lang . '.mo', determine_locale());
    }
}

// With a fixed language WordPress must not load another translation of the plugin on its own (just in time, by the
// site's language).
add_filter('override_load_textdomain', function ($override, $domain, $mofile) {
    if ($domain !== 'eter-radio') {
        return $override;
    }
    $lang = Eter_Radio_Store::settings()['language'];
    if ($lang === 'auto') {
        return $override;
    }
    return $lang === 'en_US' || basename((string) $mofile) !== 'eter-radio-' . $lang . '.mo' ? true : $override;
}, 10, 3);

add_action('init', function () {
    eter_radio_load_language();
    Eter_Radio_Store::maybe_upgrade();
    Eter_Radio_Blocks::register();
});
add_action('rest_api_init', ['Eter_Radio_Rest', 'register']);
// Ha egy biztonsági beállítás a REST API-t csak bejelentkezve engedi, az Éter útvonalai akkor is elérhetők maradnak.
Eter_Radio_Rest::allow_through_blockers();
if (is_admin()) {
    Eter_Radio_Admin::init();
}
