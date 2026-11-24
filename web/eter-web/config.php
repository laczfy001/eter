<?php
// Éter Web – settings you edit by hand. Everything else (the key, the language, the refresh rate) is set on the
// admin page: admin.php in this folder.

// The password of the admin page. Set it before you open admin.php; the page stays locked while this is empty.
$ETER_ADMIN_PASSWORD = '';

// Which websites may show the widgets (their address, as the browser sends it, for example
// 'https://www.example.com'). '*' allows any site.
$ETER_ALLOWED_ORIGINS = ['*'];

// Where the module keeps its data. It must be writable by the web server. The files are PHP files that refuse to be
// read over the web, so the default folder is safe on any server.
$ETER_DATA_DIR = __DIR__ . '/data';
