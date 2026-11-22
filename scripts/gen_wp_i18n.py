#!/usr/bin/env python3
# Az Éter Radio WordPress-bővítmény fordításai (magyar, holland): languages/eter-radio-<nyelv>.l10n.php (WP 6.5+) és .po/.mo.
# Új szövegnél mindkét szótárba be kell írni.
import os, struct
HU = {
 "%1$s – last update %2$s ago, Éter %3$s.": "%1$s – utolsó frissítés %2$s ezelőtt, Éter %3$s.",
 "A new key was created. Paste it into Éter, otherwise Éter can no longer update the site.": "Új kulcs készült. Másold be az Éterbe, különben az Éter nem tudja tovább frissíteni az oldalt.",
 "Add the Now playing, Up next and Song request blocks in the block editor (Widgets category). In classic themes and page builders use the shortcodes [eter_now_playing], [eter_up_next count=\"5\"] and [eter_request].": "A Most szól, a Következő számok és a Számkérés blokkot a blokkszerkesztőben adhatod hozzá (Widgetek kategória). Klasszikus sablonokban és oldalépítőkben a rövidkódokat használd: [eter_now_playing], [eter_up_next count=\"5\"] és [eter_request].",
 "Artist": "Előadó",
 "Blocks": "Blokkok",
 "Clear finished requests": "A lezárt kérések törlése",
 "Connecting Éter": "Az Éter csatlakoztatása",
 "Connection": "Kapcsolat",
 "Create a new key": "Új kulcs készítése",
 "Finished requests were cleared.": "A lezárt kérések törölve.",
 "Heading": "Cím",
 "In Éter, open Settings → Website, and enter these two values:": "Az Éterben nyisd meg a Beállítások → Weboldal fület, és add meg ezt a két értéket:",
 "Key": "Kulcs",
 "No matching tracks.": "Nincs ilyen szám.",
 "No requests yet.": "Még nem érkezett kérés.",
 "No upcoming tracks yet.": "Még nincsenek következő számok.",
 "Nothing is playing right now.": "Most nem szól semmi.",
 "Now playing": "Most szól",
 "Number of tracks": "Számok száma",
 "Offline": "Nem elérhető",
 "Online": "Elérhető",
 "Recent requests": "Legutóbbi kérések",
 "Refresh the blocks every": "A blokkok frissítése",
 "Request": "Kérem",
 "Requestable tracks: %d.": "Kérhető számok: %d.",
 "Requests are closed right now.": "Most nem lehet számot kérni.",
 "Search for an artist or title…": "Keress előadóra vagy címre…",
 "Sending…": "Küldés…",
 "Settings": "Beállítások",
 "Settings saved.": "A beállítások elmentve.",
 "Show progress bar": "Folyamatjelző",
 "Show start times": "Kezdési idők",
 "Show the station as offline after": "A rádió nem elérhetőnek látszik",
 "Something went wrong, please try again.": "Valami hiba történt, próbáld újra.",
 "Song request": "Számkérés",
 "Song requests are on, but closed at this hour.": "A számkérés be van kapcsolva, de ebben az órában nem lehet kérni.",
 "Song requests are open now.": "Most lehet számot kérni.",
 "Song requests are turned off in Éter.": "A számkérés ki van kapcsolva az Éterben.",
 "Status": "Állapot",
 "The next tracks in the playlist.": "A playlist következő számai.",
 "The station is offline right now.": "A rádió most nem elérhető.",
 "The track on air, updated live by Éter.": "Az adásban lévő szám, az Éter élőben frissíti.",
 "This track cannot be requested.": "Ez a szám nem kérhető.",
 "Time": "Idő",
 "Title": "Cím",
 "Treat it like a password: with it anyone could change what the site shows.": "Kezeld jelszóként: aki ismeri, megváltoztathatja, mit mutat az oldal.",
 "Up next": "Következő számok",
 "Visitors search the library and request a track, while requests are open.": "A látogatók kereshetnek a hangtárban és számot kérhetnek, amíg lehet kérni.",
 "Website address": "A weboldal címe",
 "You are not allowed to do this.": "Ehhez nincs jogosultságod.",
 "Your request: %s": "A kérésed: %s",
 "awaiting approval": "jóváhagyásra vár",
 "could not be played this time": "most nem kerülhetett adásba",
 "in the playlist": "a playlistben",
 "in the playlist, coming up soon": "bekerült a playlistbe, hamarosan szól",
 "new": "új",
 "not accepted this time": "most nem került be",
 "not available": "nem elérhető",
 "played": "lejátszva",
 "played – thank you!": "lejátszva – köszönjük!",
 "received, waiting for the studio": "megérkezett, a stúdióra vár",
 "rejected": "elutasítva",
 "removed from the playlist": "kivéve a playlistből",
 "request": "kérés",
 "seconds": "másodpercenként",
 "seconds without an update from Éter (requests are closed meanwhile)": "másodperc után, ha nem jön frissítés az Étertől (addig nem lehet kérni)",
 "this track is not available now": "ez a szám most nem elérhető",
 "waiting for approval in the studio": "a stúdióban jóváhagyásra vár",
 "Éter Radio": "Éter Rádió",
 "Éter has not connected yet.": "Az Éter még nem csatlakozott.",
 "Language": "Nyelv",
 "The site’s language": "Az oldal nyelve",
 "The language of the blocks on the site and of this page. With “The site’s language”, it follows Settings → General → Site Language (English if the plugin has no translation for it).": "A blokkok nyelve az oldalon, és ennek a lapnak a nyelve. „Az oldal nyelve” esetén a Beállítások → Általános → A honlap nyelve szerint alakul (angol, ha a bővítménynek nincs fordítása arra a nyelvre).",
}

NL = {
 "%1$s – last update %2$s ago, Éter %3$s.": "%1$s – laatste update %2$s geleden, Éter %3$s.",
 "A new key was created. Paste it into Éter, otherwise Éter can no longer update the site.": "Er is een nieuwe sleutel gemaakt. Plak deze in Éter, anders kan Éter de site niet meer bijwerken.",
 "Add the Now playing, Up next and Song request blocks in the block editor (Widgets category). In classic themes and page builders use the shortcodes [eter_now_playing], [eter_up_next count=\"5\"] and [eter_request].": "Voeg de blokken Nu op de radio, Straks en Verzoeknummer toe in de blokeditor (categorie Widgets). Gebruik in klassieke thema's en paginabouwers de shortcodes [eter_now_playing], [eter_up_next count=\"5\"] en [eter_request].",
 "Artist": "Artiest",
 "Blocks": "Blokken",
 "Clear finished requests": "Afgehandelde verzoeken wissen",
 "Connecting Éter": "Éter koppelen",
 "Connection": "Verbinding",
 "Create a new key": "Nieuwe sleutel maken",
 "Finished requests were cleared.": "Afgehandelde verzoeken zijn gewist.",
 "Heading": "Kop",
 "In Éter, open Settings → Website, and enter these two values:": "Open in Éter Instellingen → Website en vul deze twee waarden in:",
 "Key": "Sleutel",
 "Language": "Taal",
 "No matching tracks.": "Geen nummers gevonden.",
 "No requests yet.": "Nog geen verzoeken.",
 "No upcoming tracks yet.": "Nog geen komende nummers.",
 "Nothing is playing right now.": "Er speelt nu niets.",
 "Now playing": "Nu op de radio",
 "Number of tracks": "Aantal nummers",
 "Offline": "Offline",
 "Online": "Online",
 "Recent requests": "Recente verzoeken",
 "Refresh the blocks every": "Blokken verversen elke",
 "Request": "Aanvragen",
 "Requestable tracks: %d.": "Aan te vragen nummers: %d.",
 "Requests are closed right now.": "Verzoeken zijn nu gesloten.",
 "Search for an artist or title…": "Zoek op artiest of titel…",
 "Sending…": "Versturen…",
 "Settings": "Instellingen",
 "Settings saved.": "Instellingen opgeslagen.",
 "Show progress bar": "Voortgangsbalk tonen",
 "Show start times": "Begintijden tonen",
 "Show the station as offline after": "Station als offline tonen na",
 "Something went wrong, please try again.": "Er ging iets mis, probeer het opnieuw.",
 "Song request": "Verzoeknummer",
 "Song requests are on, but closed at this hour.": "Verzoeknummers staan aan, maar zijn op dit uur gesloten.",
 "Song requests are open now.": "Verzoeknummers zijn nu open.",
 "Song requests are turned off in Éter.": "Verzoeknummers staan uit in Éter.",
 "Status": "Status",
 "The language of the blocks on the site and of this page. With “The site’s language”, it follows Settings → General → Site Language (English if the plugin has no translation for it).": "De taal van de blokken op de site en van deze pagina. Met „Taal van de site” volgt hij Instellingen → Algemeen → Taal van de site (Engels als de plugin geen vertaling heeft voor die taal).",
 "The next tracks in the playlist.": "De volgende nummers in de playlist.",
 "The site’s language": "Taal van de site",
 "The station is offline right now.": "Het station is nu offline.",
 "The track on air, updated live by Éter.": "Het nummer dat nu speelt, live bijgewerkt door Éter.",
 "This track cannot be requested.": "Dit nummer kan niet worden aangevraagd.",
 "Time": "Tijd",
 "Title": "Titel",
 "Treat it like a password: with it anyone could change what the site shows.": "Behandel hem als een wachtwoord: met deze sleutel kan iedereen veranderen wat de site toont.",
 "Up next": "Straks",
 "Visitors search the library and request a track, while requests are open.": "Bezoekers zoeken in de muziekbibliotheek en vragen een nummer aan, zolang verzoeken open zijn.",
 "Website address": "Websiteadres",
 "You are not allowed to do this.": "Je hebt geen toestemming om dit te doen.",
 "Your request: %s": "Jouw verzoek: %s",
 "awaiting approval": "wacht op goedkeuring",
 "could not be played this time": "kon deze keer niet gedraaid worden",
 "in the playlist": "in de playlist",
 "in the playlist, coming up soon": "staat in de playlist, komt zo",
 "new": "nieuw",
 "not accepted this time": "deze keer niet geaccepteerd",
 "not available": "niet beschikbaar",
 "played": "gedraaid",
 "played – thank you!": "gedraaid – bedankt!",
 "received, waiting for the studio": "ontvangen, wacht op de studio",
 "rejected": "afgewezen",
 "removed from the playlist": "uit de playlist gehaald",
 "request": "verzoek",
 "seconds": "seconden",
 "seconds without an update from Éter (requests are closed meanwhile)": "seconden zonder update van Éter (verzoeken zijn dan gesloten)",
 "this track is not available now": "dit nummer is nu niet beschikbaar",
 "waiting for approval in the studio": "wacht op goedkeuring in de studio",
 "Éter Radio": "Éter Radio",
 "Éter has not connected yet.": "Éter heeft nog geen verbinding gemaakt.",
}
out = os.path.join(os.path.dirname(__file__), '..', 'wordpress', 'eter-radio', 'languages')
def php(s): return "'" + s.replace('\\', '\\\\').replace("'", "\\'") + "'"
def po(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
def write(locale, T, plural):
    with open(os.path.join(out, f'eter-radio-{locale}.l10n.php'), 'w', encoding='utf-8') as f:
        f.write(f"<?php\n// Generated by scripts/gen_wp_i18n.py\nreturn ['domain'=>'eter-radio','plural-forms'=>{php(plural)},'language'=>'{locale}','messages'=>[\n")
        for k in sorted(T): f.write(f" {php(k)}=>{php(T[k])},\n")
        f.write("]];\n")
    header = f"Content-Type: text/plain; charset=UTF-8\nLanguage: {locale}\nPlural-Forms: {plural}\n"
    with open(os.path.join(out, f'eter-radio-{locale}.po'), 'w', encoding='utf-8') as f:
        f.write('msgid ""\nmsgstr ' + po(header) + '\n\n')
        for k in sorted(T): f.write(f"msgid {po(k)}\nmsgstr {po(T[k])}\n\n")
    msgs = {'': header}; msgs.update(T)
    keys = sorted(msgs)
    ids = b''; strs = b''; offs = []
    for k in keys:
        kb = k.encode(); vb = msgs[k].encode()
        offs.append((len(ids), len(kb), len(strs), len(vb)))
        ids += kb + b'\0'; strs += vb + b'\0'
    n = len(keys); kt = 28; vt = kt + n * 8; ks = vt + n * 8; vs = ks + len(ids)
    data = struct.pack('<7I', 0x950412de, 0, n, kt, vt, 0, ks)
    data += b''.join(struct.pack('<2I', l, ks + o) for o, l, _, _ in offs)
    data += b''.join(struct.pack('<2I', l2, vs + o2) for _, _, o2, l2 in offs)
    data += ids + strs
    open(os.path.join(out, f'eter-radio-{locale}.mo'), 'wb').write(data)
    print(locale, len(T), 'strings')
write('hu_HU', HU, 'nplurals=2; plural=(n != 1);')
write('nl_NL', NL, 'nplurals=2; plural=(n != 1);')
missing = set(HU) ^ set(NL)
if missing: print('ELTÉRÉS:', missing)
