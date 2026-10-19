#!/usr/bin/env python3
"""A fordítótábla előállítása: i18n/en.json -> src/i18n_en.cpp.
A kulcsok és az értékek C-forráskód szerinti alakban vannak (\\n, \\" stb.), pontosan úgy,
ahogy a forrásban a TR("...") belsejében állnak."""
import json, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
table = json.load(open(os.path.join(root, 'i18n', 'en.json'), encoding='utf8'))
with open(os.path.join(root, 'src', 'i18n_en.cpp'), 'w', encoding='utf8') as out:
    out.write('// GENERÁLT FÁJL – ne szerkeszd kézzel. Forrása: i18n/en.json (scripts/gen_i18n.py).\n')
    out.write('struct I18nPair { const char *hu; const char *en; };\n')
    out.write('extern const I18nPair kEnglish[];\nextern const int kEnglishCount;\n')
    out.write('const I18nPair kEnglish[] = {\n')
    for hu, en in table.items():
        out.write('    {"%s",\n     "%s"},\n' % (hu, en))
    out.write('};\nconst int kEnglishCount = %d;\n' % len(table))
print(len(table), 'szöveg')
