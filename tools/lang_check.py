#!/usr/bin/env python3
"""Language check: everything in this repository is English.

    python tools/lang_check.py                  every tracked file (git ls-files)
    python tools/lang_check.py --staged         the staged content (pre-commit hook)
    python tools/lang_check.py --message FILE   a commit message (commit-msg hook)
    python tools/lang_check.py --list-words     print every German word hit as an
                                                allow-list line (to review, not to paste)

It reports:
  umlaut    a German umlaut or sharp s
  word      a German word - in prose, strings or names
  ref       a person name, an internal round, report or handover, R2/R3, a local path
  date      a date written as day.month.year (use 2026-09-30)

Existing German names that stay for now (menu file keywords, menued modules,
--frame-log columns ...) are listed in tools/lang_check_allow.txt. New entries
there need the maintainers' approval.

Exit code 0 = clean, 1 = findings, 2 = usage error.
"""
import fnmatch
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW_FILE = os.path.join(ROOT, 'tools', 'lang_check_allow.txt')

# Third-party code and license texts are not ours to translate.
SKIP_PREFIXES = ('externals/', 'include/psn00bsdk/')
SKIP_FILES = ('LICENSE',)

# German words that are not also common English words. Transliterated umlauts
# (ae, oe, ue, ss) are spelled out, because that is how German hides in ASCII.
GERMAN_WORDS = set('''
und nicht ist sind wird werden wurde wurden muss muessen kann koennen soll sollen darf
duerfen oder aber auch noch schon jetzt hier dort wenn weil dass damit denn doch nur sonst
immer nie kein keine keinen keinem keiner eine einer eines einem einen der das dem zum zur
beim vom fuer ueber nach aus bei mit auf ohne gegen zwischen waehrend gleich wieder weiter
zurueck neu neue neuen neues alle alles jede jeder jedes welche welcher welches wie warum wann
dann erst mehr weniger viel viele genau etwa bereits bisher danach vorher nachher spaeter
frueher heute gestern morgen dieser diese dieses diesem diesen sich sie wir ihr euch uns
strecke strecken datei dateien fehler spiel spieler fahrer zeile zeilen spalte spalten ordner
lauf laeufe bild bilder seite seiten wert werte kasten schrift breite hoehe groesse laenge
anzahl zahl zaehler grenze pruefung pruefen pfad pfade speicher puffer eintrag eintraege
liste tabelle tabellen ergebnis ergebnisse messung ort bezug anker wirkung bedingung vorschau
abzug abzuege runde stufe uebergabe bericht befund lade laden schreibe schreiben lesen
zeige zeigen rechne rechnet macht machen steht stehen liegt liegen gibt geben geht gehen
kommt kommen bleibt bleiben fehlt fehlen klein rechts oben unten mitte eigen fertig leer voll
richtig falsch unbekannt gesperrt oeffnen schliessen ersetzen gestapelt zuschlag vorschub
hervorhebung eingeklappt kinder nimmt wurzel suche merke gemerkte erster zweiter dritter
beenden bildpuffer takt nachgeholt verfallen emittiert kennung einzelkacheln getan gedrueckt
gefunden schlecht tasten teil vorrat hauptmenue spieler renntyp schwierigkeit abenteuer
einzel zeitfahren
'''.split())

REFS = [
    (re.compile(r'\bRunden?\b'), 'internal round'),
    (re.compile(r'docs[\\/]rebuild'), 'internal document'),
    (re.compile(r'(?<![A-Za-z0-9_$])R[23](?:-[a-z]+)?[\\/]'), 'R2/R3 path'),
    (re.compile(r'[A-Za-z]:[\\/]Users', re.I), 'local path'),
    (re.compile(r'/c/Users', re.I), 'local path'),
    (re.compile(r'\bmessung-[a-z]'), 'internal measurement folder'),
    (re.compile(r'\bbaseline-(?:det|menu|race)'), 'internal reference folder'),
    (re.compile('(?:Ue|' + chr(0xdc) + ')bergabe', re.I), 'internal handover'),
    (re.compile(r'\bBacklog\s+\d'), 'internal backlog number'),
]
DATE = re.compile(r'(?<![\d.])\d{1,2}\.\d{1,2}\.(?:19|20)\d\d(?![\d.])')
UMLAUT = re.compile('[' + ''.join(map(chr, (0xe4, 0xf6, 0xfc, 0xc4, 0xd6, 0xdc, 0xdf))) + ']')
TOKEN = re.compile(r'[A-Za-z]+')


def word_parts(tok):
    """The parts of a token that read as words: the token itself when it is
    written like a word (lower case or capitalised), else its camelCase parts.
    ALL-CAPS tokens are skipped - acronyms like MIT or SDK collide with German."""
    if tok.isupper():
        return []
    if tok.islower() or (tok[0].isupper() and tok[1:].islower()):
        return [tok.lower()]
    return [p.lower() for p in re.findall(r'[A-Z]?[a-z]+', tok) if len(p) >= 3]


def load_allow():
    rules = []
    if not os.path.exists(ALLOW_FILE):
        return rules
    for n, line in enumerate(open(ALLOW_FILE, encoding='utf-8'), 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 3 or parts[1] not in ('word', 'ref'):
            print('%s:%d: bad line (want: <glob> word|ref <text>)' % (ALLOW_FILE, n))
            sys.exit(2)
        rules.append((parts[0], parts[1], parts[2]))
    return rules


def allowed(rules, path, kind, text):
    for glob, k, t in rules:
        if k == kind and (t == text or t == '*') and fnmatch.fnmatchcase(path, glob):
            return True
    return False


def check_text(path, text, rules):
    found = []
    for i, line in enumerate(text.splitlines(), 1):
        if UMLAUT.search(line) and not allowed(rules, path, 'ref', 'umlaut'):
            found.append((i, 'umlaut', UMLAUT.search(line).group(0), line))
        for rx, why in REFS:
            m = rx.search(line)
            if m and not allowed(rules, path, 'ref', m.group(0)):
                found.append((i, 'ref', '%s (%s)' % (m.group(0), why), line))
        m = DATE.search(line)
        if m:
            found.append((i, 'date', m.group(0), line))
        seen = set()
        for tok in TOKEN.findall(line):
            for part in word_parts(tok):
                if part in GERMAN_WORDS and part not in seen:
                    seen.add(part)
                    if not allowed(rules, path, 'word', part):
                        found.append((i, 'word', part, line))
    return found


def tracked_files():
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, check=True).stdout
    return [p for p in out.decode('utf-8').split('\0') if p]


def staged_files():
    out = subprocess.run(['git', 'diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z'],
                         cwd=ROOT, capture_output=True, check=True).stdout
    return [p for p in out.decode('utf-8').split('\0') if p]


def read_blob(path, staged):
    if staged:
        r = subprocess.run(['git', 'show', ':' + path], cwd=ROOT, capture_output=True)
        return r.stdout if r.returncode == 0 else None
    try:
        return open(os.path.join(ROOT, path), 'rb').read()
    except OSError:
        return None


def main(argv):
    rules = load_allow()
    if len(argv) == 2 and argv[0] == '--message':
        text = open(argv[1], encoding='utf-8', errors='replace').read()
        text = '\n'.join(l for l in text.splitlines() if not l.startswith('#'))
        found = check_text('COMMIT_MESSAGE', text, rules)
        for i, kind, what, line in found:
            print('commit message:%d: %s %s | %s' % (i, kind, what, line.strip()[:120]))
        if found:
            print('lang_check: the commit message is not English (see above)')
        return 1 if found else 0
    if argv not in ([], ['--staged'], ['--list-words']):
        print(__doc__)
        return 2
    staged = argv == ['--staged']
    files = staged_files() if staged else tracked_files()
    total = 0
    words = {}
    for path in files:
        if path.startswith(SKIP_PREFIXES) or path in SKIP_FILES:
            continue
        data = read_blob(path, staged)
        if data is None or b'\0' in data[:8192]:
            continue
        found = check_text(path, data.decode('utf-8', errors='replace'), rules)
        for i, kind, what, line in found:
            if argv == ['--list-words']:
                if kind == 'word':
                    words.setdefault(path, set()).add(what)
                continue
            print('%s:%d: %s %s | %s' % (path, i, kind, what, line.strip()[:120]))
            total += 1
    if argv == ['--list-words']:
        for path in sorted(words):
            for w in sorted(words[path]):
                print('%s word %s' % (path, w))
        return 0
    if total:
        print('lang_check: %d finding(s) - everything in this repository is English '
              '(exceptions: tools/lang_check_allow.txt)' % total)
        return 1
    print('lang_check: %d file(s) checked, clean' % len(files))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
