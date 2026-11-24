Éter Web 1.0.1
==============

Now playing, Up next, Most played and Song request widgets for any website, fed live by the Éter radio playout.
It does the same as the Éter Radio WordPress plugin, for sites without WordPress (static sites, other CMSs, site
builders).

Requirements: a web host with PHP 7.4 or newer (no database needed). The widgets can then be put on any website,
even one on a different address or one that has no PHP at all.

Installing
----------
1. Upload this folder (eter-web) to your web host, for example to https://radio.example.com/eter-web/
2. Open config.php in a text editor and set $ETER_ADMIN_PASSWORD.
3. Make sure the web server can write the data folder (on most hosts it already can).
4. Open https://radio.example.com/eter-web/admin.php and log in. The page shows the website address and the key.
5. In Éter: Settings → Website → turn on "Connect to the website", enter the address and the key, and press Apply.

Putting the widgets on a website
--------------------------------
Paste this once into the page (or into the site's header):

    <script src="https://radio.example.com/eter-web/?js" defer></script>

and a line for each widget, where it should appear:

    <div data-eter="now" data-heading="Now playing"></div>
    <div data-eter="next" data-count="5" data-heading="Up next"></div>
    <div data-eter="top" data-period="week" data-count="10" data-heading="Most played"></div>
    <div data-eter="request" data-heading="Song request"></div>

Options: data-count (1–20), data-period (day, week, month), data-times="0", data-plays="0", data-progress="0".
The widgets take the fonts and colours of the page.

Site builders that only allow an iframe:

    <iframe src="https://radio.example.com/eter-web/?embed=now" style="width:100%;height:90px;border:0"></iframe>

?embed= can be now, next, top or request; add &count=, &period=, &heading=, &theme=light or &theme=dark.

admin.php lists all embed codes ready to copy, the recent requests and the language (English, Nederlands, Magyar).

Licence
-------
Éter Web is part of Éter and is free software under the GNU General Public License v3.0 or later (LICENSE.txt).
It contains no third-party code.

Security
--------
- The key is like a password: with it anyone could change what the widgets show. "Create a new key" on the admin
  page replaces it (then paste the new one into Éter).
- The data files cannot be read over the web (each starts with a PHP exit).
- config.php: $ETER_ALLOWED_ORIGINS limits which websites may show the widgets (default: any).
