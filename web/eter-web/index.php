<?php
// Éter Web – the universal version of the Éter Radio WordPress plugin. One folder on any PHP web host; the widgets go
// into any website with one script tag (or an iframe). The Éter playout talks to it exactly like to the plugin:
// in Éter, Settings → Website, enter the address of this folder and the key from admin.php.
//
//   ?rest_route=/eter/v1/sync      Éter → here (signed): on air, up next, most played, request status updates
//   ?rest_route=/eter/v1/catalog   Éter → here (signed): the requestable tracks
//   ?rest_route=/eter/v1/status    widgets: on air, up next, most played, requests open
//   ?rest_route=/eter/v1/search    request widget: ?q=
//   ?rest_route=/eter/v1/request   request widget: POST {"item": id}
//   ?rest_route=/eter/v1/request/N request widget: the status of a request
//   ?js                            the widget script (with the texts and the address of this folder)
//   ?embed=now|next|top|request    a page for an iframe (&count=, &period=, &heading=, &theme=light|dark)

define('ETER_WEB', true);
require __DIR__ . '/lib/core.php';
require __DIR__ . '/lib/api.php';

if (isset($_GET['rest_route'])) {
    eter_api((string) $_GET['rest_route']);
    exit;
}
if (isset($_GET['js'])) {
    require __DIR__ . '/lib/widget.php';
    eter_widget_js();
    exit;
}
if (isset($_GET['embed'])) {
    require __DIR__ . '/lib/widget.php';
    eter_embed_page((string) $_GET['embed']);
    exit;
}
header('Location: admin.php');
