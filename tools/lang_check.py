#!/usr/bin/env python3
"""Language check: everything in this repository is English.

    python tools/lang_check.py                  every tracked file (git ls-files)
    python tools/lang_check.py --staged         the staged content (pre-commit hook)
    python tools/lang_check.py --message FILE   a commit message (commit-msg hook)
    python tools/lang_check.py --list-words     print every German word hit as an
                                                allow-list line (to review, not to paste)

The contents are checked, and the file and folder names too (renamed files
included). It reports:
  umlaut    a German umlaut or sharp s
  word      a German word - in prose, strings or names. camelCase and
            snake_case names are split into their parts, and a part that ends
            with a German noun or verb is a German compound (testdaten)
  ref       a person name, an internal round, report or handover, an internal
            working copy, a local path
  date      a date written as day.month.year (use 2026-09-30)

Person names and the folder of a local checkout are not written here. They
are read from tools/lang_check_names.local.txt, a local file that git ignores:
one person name per line, or "path <folder>" for a local folder ('#' starts a
comment). Without that file (a fresh clone, the automatic builds) these two
checks are skipped.

Existing German names that stay for now (--frame-log columns, the
tracks/vorschau folder ...) are listed in tools/lang_check_allow.txt. New
entries there need the maintainers' approval.

Exit code 0 = clean, 1 = findings, 2 = usage error.
"""
import fnmatch
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW_FILE = os.path.join(ROOT, 'tools', 'lang_check_allow.txt')
LOCAL_FILE = os.path.join(ROOT, 'tools', 'lang_check_names.local.txt')

# Third-party code and license texts are not ours to translate.
SKIP_PREFIXES = ('externals/', 'include/psn00bsdk/')
SKIP_FILES = ('LICENSE',)

# German words that are not also common English words. Transliterated umlauts
# (ae, oe, ue, ss) are spelled out, because that is how German hides in ASCII.
# These count as whole words only: many of them also end English words
# (encoder, caffeine, accolade, village, umlaut).
GERMAN_WORDS = set('''
und nicht ist sind wird werden wurde wurden worden waren muss muessen kann koennen soll
sollen sollte koennte waere wuerde wuerden haette darf duerfen haben habe hatte oder aber
auch noch schon jetzt hier dort wenn weil dass damit denn doch nur sonst immer nie kein keine
keinen keinem keiner ein eine einer eines einem einen der das dem zum zur beim vom fuer ueber
nach aus bei mit auf ohne gegen zwischen waehrend gleich gleiche gleichen gleicher wieder
weiter weitere weiteren zurueck neu neue neuen neues neuer alle alles aller jede jeder jedes
jedem jeden welche welcher welches wie warum wann dann erst mehr weniger viel viele genau
etwa bereits bisher danach vorher nachher spaeter frueher heute gestern morgen nachts abends
dieser diese dieses diesem diesen dieselbe dieselben dasselbe derselbe derselben denselben
demselben selben sich sie wir ihr ihre ihren ihrer euch uns ich mein dein unser sein seinen
seiner seinem dessen deren denen jemand niemand sehr als vor unter hinter neben durch seit
wegen trotz statt anstatt ausser aussen innerhalb ausserhalb sowie sowohl jedoch trotzdem
deshalb daher dafuer dagegen dabei dazu davon daraus darauf darin daran darum darunter
darueber daneben davor dorthin heraus hinaus wobei stattdessen entweder weder sondern zwar
samt sobald solange obwohl nachdem bevor sodass indem etwas nichts beide beiden beides einige
manche mehrere andere anderen anderer anderes selbst selber ebenfalls zusammen allein nochmal
einmal zweimal zuerst zuletzt schliesslich endlich sofort gerade kaum ziemlich ueberall
nirgends ueberhaupt ohnehin wirklich vielleicht wahrscheinlich natuerlich eigentlich
mindestens hoechstens bitte danke nein
eins zwei drei vier fuenf sechs sieben acht neun zehn erste ersten erster zweite zweiten
zweiter zweites dritte dritter letzte letzten letzter naechste naechsten naechster vorige
ort lade lage paar paare uhr eis geist geister feld felder laut unten klein rechts oben mitte
mittig eigen eigene eigenen eigener eigenes einzige einzigen einzel einzeln ganz ganze ganzen
halb halbe alte alten echte fertig leer voll richtig falsch unbekannt unbekannte unbekannter
gesperrt gestapelt eingeklappt aktuell aktuelle gesamt einfach doppelt mehrfach leise dunkel
schwarz gruen blau gelb grau kurz kurze langsam schnell hoch tief breit schmal weit leicht
schwer aktiv inaktiv offen sichtbar unsichtbar versteckt gueltig ungueltig moeglich
unmoeglich noetig erlaubt verboten vorhanden fehlend passend fehlerhaft kaputt exakt
verschieden verschiedene gemeinsam gemeinsame klassisch deterministisch bitgleich bytegleich
unveraendert erreichbar aufwaerts abwaerts vorwaerts rueckwaerts getan schlecht renn
rechne rechnet macht steht liegt gibt geht kommt bleibt fehlt nimmt laeuft faehrt liest
schreibt braucht zeigt setzt bekommt traegt sieht heisst faellt laesst haengt haelt kostet
nennt kennt meldet passt aendert zaehlt legt sitzt endet fragt stimmt ruft reicht folgt
findet trifft zieht baut bricht laedt faengt beginnt startet prueft schaltet fuellt liefert
verlangt erwartet entscheidet erfordert entsteht passiert erreicht verwirft landet lief ging
merke gemerkte schreibe zeige zeichne zaehle messe vergleiche starte stoppe fahre springe
warte fuehre schalte drehe druecke loesche kopiere erzeuge entferne zerlege bilde finde
nachgeholt verfallen emittiert gedrueckt gefunden geladen gelesen geschrieben gezeichnet
gerechnet gezaehlt gemessen verglichen gestartet gestoppt geoeffnet geschlossen ersetzt
getauscht geholt gesetzt genommen gesucht geloescht kopiert verschoben erzeugt entfernt
gebaut gepackt beendet gerendert skaliert gespiegelt sortiert gefiltert berechnet bestimmt
ermittelt geprueft getestet erstellt angepasst geaendert umbenannt aufgeraeumt abgebrochen
fortgesetzt wiederholt begonnen aufgerufen gesendet geschickt gewartet gezeigt ausgeblendet
eingeblendet umgeschaltet abgespielt aufgenommen entschieden gewaehlt ausgewaehlt gespeichert
gesichert benoetigt genannt gehoert gebraucht gehalten gefahren gefallen geraten getrennt
belegt behoben angefasst beantwortet deklariert ignoriert negiert erledigt verworfen
containern
'''.split())

# German nouns and verbs. Each counts as a whole word, and also at the end of
# a longer word: German glues words together (testdaten, ladeweg), so a part
# that ends with one of these after at least COMPOUND_MIN letters is German too.
# Only words that no English word ends with belong here.
GERMAN_STEMS = set('''
strecke strecken datei dateien daten fehler spiel spieler fahrer zeile zeilen spalte spalten
ordner lauf laeufe bild bilder seite seiten wert werte kasten schrift breite breiten hoehe
tiefe groesse laenge anzahl zahl zaehler zaehlung grenze grenzen pruefung pfad pfade speicher
puffer eintrag eintraege liste tabelle tabellen ergebnis ergebnisse messung bezug anker
wirkung bedingung vorschau abzug abzuege runde runden rennen stufe stufen bericht befund
zuschlag vorschub hervorhebung kinder wurzel suche bildpuffer takt kennung einzelkacheln
tasten teil teile vorrat haupt hauptmenue renntyp schwierigkeit abenteuer zeitfahren archiv
weg wege umweg rueckweg ziel ziele ende anfang zeit zeiten rekord bestzeit bestzeiten fahrt
kurve kurven sprung klang musik geraet geraete fenster knopf knoepfe eingabe ausgabe abgabe
freigabe vorgabe rueckgabe wiedergabe angabe zustand zustaende staende schalter einstellung
einstellungen pruefsumme vergleich abbruch absturz protokoll meldung meldungen hinweis warnung
ausgang eingang menue menues zeichen schirm bildschirm leinwand flaeche flaechen ebene ebenen
kachel kacheln farbe farben schatten licht himmel boden spiegel maske summe mittel
wiederholung versuch versuche proben karte karten sperre charakter figur figuren kopf wort
woerter buchstaben satz sprache wechsel vorlage aufgabe aufgaben stapel laeufer lader klasse
punkt punkte strich linie linien kreis viereck rechteck raender rahmen balken streifen schritt
schritte schnitt stueck menge mengen gruppe gruppen nummer gegner sieg sieger platz plaetze
pokal waffe waffen rakete kiste kisten frucht mauer turm insel wueste schnee nebel wasser
feuer luft paket pakete ordnung reihenfolge reihe folge schleife kette zweig kopie sicherung
werkzeug werkzeuge zeug leiste schlange haufen baum knoten kante kanten ecke ecken merkmal
merkmale marke pflicht wahl fahrpfad klangspeicher anzeige ansicht abschnitt absatz hilfe
beispiel erklaerung beschreibung kommentar aenderung aenderungen zeiger schluessel inhalt
inhalte quelle quellen verzeichnis endung stunde sekunde sekunden geschwindigkeit
beschleunigung richtung ausrichtung drehung winkel abstand versatz verschiebung entfernung
ursprung verhaeltnis massstab aufloesung vollbild grafik textur texturen kamera modell
geometrie projektion polygone segmente sektoren kollision treiber konsole steuerung tastatur
belegung lautstaerke fahrzeug lenkung bremse schub abkuerzung einflug ausstieg turnier
platzierung meisterschaft herausforderung neustart optionen aufnahme aufzeichnung ablauf
vorgang durchgang prozess funktion instanz instanzen signatur aufruf aufrufe aufrufer befehl
befehle faktor konzept modus stil titel regel regeln frage antwort grund stelle namen
bereich ursache tatsache aussage nachweis beleg aufbau auftrag nachtrag abnahme sitzung
rechnung abweichung unterschied entscheidung schwelle kaskade skizze abbild abbilder
entwickler leser schreiber zeichner merker fassung vorspann nachspann vorbehalt vorbehalte
laden lesen schreiben zeigen machen stehen liegen geben gehen kommen bleiben fehlen pruefen
oeffnen schliessen ersetzen beenden holen setzen nehmen finden suchen tauschen starten fahren
laufen rechnen zaehlen zahlen messen vergleichen stoppen springen warten fuehren ausfuehren
schalten drehen druecken loeschen kopieren verschieben erzeugen anlegen entfernen
hinzufuegen zeichnen speichern sichern bauen packen entpacken testen senden filtern rendern
anzeigen berechnen ermitteln erstellen anpassen aendern umbenennen abbrechen fortsetzen
wiederholen beginnen aufrufen empfangen verstecken ausblenden einblenden umschalten abspielen
aufnehmen waehlen auswaehlen teilen tragen sagen sehen wissen halten lassen bekommen behalten
'''.split())
COMPOUND_MIN = 3

REFS = [
    (re.compile(r'\bRunden?\b'), 'internal round'),
    (re.compile(r'docs[\\/]rebuild'), 'internal document'),
    (re.compile(r'(?<![A-Za-z0-9_$])R[23](?:-[a-z]+)?[\\/]'), 'internal working copy'),
    (re.compile(r'[A-Za-z]:[\\/]+Users', re.I), 'local path'),
    (re.compile(r'/[a-z]/Users', re.I), 'local path'),
    (re.compile(r'\bmessung-[a-z]'), 'internal measurement folder'),
    (re.compile(r'\bbaseline-(?:det|menu|race)'), 'internal reference folder'),
    (re.compile('(?:Ue|' + chr(0xdc) + ')bergabe', re.I), 'internal handover'),
    (re.compile(r'\bBacklog\s+\d'), 'internal backlog number'),
]
DATE = re.compile(r'(?<![\d.])\d{1,2}\.\d{1,2}\.(?:19|20)\d\d(?![\d.])')
UMLAUT = re.compile('[' + ''.join(map(chr, (0xe4, 0xf6, 0xfc, 0xc4, 0xd6, 0xdc, 0xdf))) + ']')
TOKEN = re.compile(r'[A-Za-z]+')
CAMEL_GAP = re.compile(r'(?<=[a-z])(?=[A-Z])')


def word_parts(tok):
    """The parts of a token that read as words: the token itself when it is
    written like a word (lower case or capitalised), else its camelCase parts."""
    if tok.islower() or (tok[0].isupper() and tok[1:].islower()):
        return [tok.lower()]
    return [p.lower() for p in re.findall(r'[A-Z]?[a-z]+', tok) if len(p) >= 3]


STEM_LENGTHS = sorted({len(s) for s in GERMAN_STEMS})
_german = {}


def german(part):
    """The German word in a word part, or None: the part itself, or the German
    noun or verb it ends with (a compound like testdaten or ladeweg)."""
    if part not in _german:
        hit = None
        if part in GERMAN_WORDS or part in GERMAN_STEMS:
            hit = part
        else:
            for n in STEM_LENGTHS:
                if len(part) - n >= COMPOUND_MIN and part[-n:] in GERMAN_STEMS:
                    hit = part[-n:]
                    break
        _german[part] = hit
    return _german[part]


def german_token(tok):
    """(part, German word) for every German part of a token. An ALL-CAPS token
    counts only as a whole word of 5+ letters (NICHTS, WERTE): short acronyms
    like MIT or SDK collide with German, and constants glue English words
    together (TOOLTIPFADE) the way German compounds look."""
    if tok.isupper():
        part = tok.lower()
        whole = len(part) >= 5 and (part in GERMAN_WORDS or part in GERMAN_STEMS)
        return [(part, part)] if whole else []
    return [(p, german(p)) for p in word_parts(tok) if german(p)]


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


def folder_pattern(folder):
    """A local folder as a path, whatever the drive letter and the slashes:
    X:\\Folder, X:/Folder, X:\\\\Folder (escaped) and /x/Folder (Git Bash).
    None for a bare drive - that would match every path."""
    folder = re.sub(r'^(?:[A-Za-z]:|/[A-Za-z](?=/))', '', folder.strip()).strip('\\/')
    if not folder:
        return None
    parts = [re.escape(p).replace('\\ ', r'(?:\\?[ ]|%20)') for p in re.split(r'[\\/]+', folder)]
    return r'(?:[A-Za-z]:|/[A-Za-z])[\\/]+' + r'[\\/]+'.join(parts)


def load_local():
    """The rules from LOCAL_FILE: (person names, local folders), each a regex
    or None. A person name matches in any case, with a possessive s, and a
    camelCase gap counts as a word boundary; a folder matches only as a path."""
    names, folders = [], []
    if os.path.exists(LOCAL_FILE):
        for line in open(LOCAL_FILE, encoding='utf-8-sig'):
            line = line.split('#', 1)[0].strip()
            if line.startswith(('path ', 'path\t')):
                folders.append(folder_pattern(line[5:]))
            elif line:
                names.append(line)
    folders = [f for f in folders if f]
    names_rx = folders_rx = None
    if names:
        alt = '|'.join(re.escape(n) for n in sorted(names, key=len, reverse=True))
        names_rx = re.compile(r'(?<![A-Za-z])(?:%s)s?(?![A-Za-z])' % alt, re.I)
    if folders:
        folders_rx = re.compile('|'.join(folders), re.I)
    return names_rx, folders_rx


def allowed(rules, path, kind, text):
    for glob, k, t in rules:
        if k == kind and (t == text or t == '*') and fnmatch.fnmatchcase(path, glob):
            return True
    return False


def scan(text, local):
    """Every finding in a text, before the allow list:
    (line number, kind, allow-list text, what to print, the line)."""
    found = []
    names_rx, folders_rx = local
    for i, line in enumerate(text.splitlines(), 1):
        m = UMLAUT.search(line)
        if m:
            found.append((i, 'umlaut', 'umlaut', m.group(0), line))
        refs = [(why, rx.search(line)) for rx, why in REFS]
        if names_rx:
            m = names_rx.search(line) or names_rx.search(CAMEL_GAP.sub(' ', line))
            refs.append(('person name', m))
        if folders_rx:
            refs.append(('local path', folders_rx.search(line)))
        for why, m in refs:
            if m:
                found.append((i, 'ref', m.group(0), '%s (%s)' % (m.group(0), why), line))
        m = DATE.search(line)
        if m:
            found.append((i, 'date', m.group(0), m.group(0), line))
        seen = set()
        for tok in TOKEN.findall(line):
            for part, hit in german_token(tok):
                if part not in seen:
                    seen.add(part)
                    what = part if hit == part else '%s (ends with %s)' % (part, hit)
                    found.append((i, 'word', part, what, line))
    return found


def keep(rules, path, found):
    """The findings the allow list does not excuse. Dates are never excused."""
    return [f for f in found if f[1] == 'date'
            or not allowed(rules, path, 'ref' if f[1] == 'umlaut' else f[1], f[2])]


def check_text(path, text, rules, local):
    return keep(rules, path, scan(text, local))


def check_paths(paths, rules, local):
    """The file and folder names. Every folder is checked once, as 'dir/sub/',
    and a finding already made for a parent folder is not repeated below it.
    Returns (path, finding) pairs."""
    raw = {}
    out = []
    for path in paths:
        parts = path.split('/')
        for n in range(1, len(parts) + 1):
            name = '/'.join(parts[:n]) + ('/' if n < len(parts) else '')
            if name in raw:
                continue
            parent = '/'.join(parts[:n - 1]) + '/' if n > 1 else None
            known = {(f[1], f[2]) for f in raw.get(parent, ())}
            raw[name] = scan(name, local)
            new = [f for f in raw[name] if (f[1], f[2]) not in known]
            out.extend((name, f) for f in keep(rules, name, new))
    return out


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
    local = load_local()
    if len(argv) == 2 and argv[0] == '--message':
        text = open(argv[1], encoding='utf-8', errors='replace').read()
        text = '\n'.join(l for l in text.splitlines() if not l.startswith('#'))
        found = check_text('COMMIT_MESSAGE', text, rules, local)
        for i, kind, key, what, line in found:
            print('commit message:%d: %s %s | %s' % (i, kind, what, line.strip()[:120]))
        if found:
            print('lang_check: the commit message is not English (see above)')
        return 1 if found else 0
    if argv not in ([], ['--staged'], ['--list-words']):
        print(__doc__)
        return 2
    staged = argv == ['--staged']
    listed = staged_files() if staged else tracked_files()
    files = [p for p in listed if not p.startswith(SKIP_PREFIXES) and p not in SKIP_FILES]
    total = 0
    words = {}
    for name, (i, kind, key, what, line) in check_paths(files, rules, local):
        if argv == ['--list-words']:
            if kind == 'word':
                words.setdefault(name, set()).add(key)
            continue
        print('%s: path: %s %s' % (name, kind, what))
        total += 1
    for path in files:
        data = read_blob(path, staged)
        if data is None or b'\0' in data[:8192]:
            continue
        found = check_text(path, data.decode('utf-8', errors='replace'), rules, local)
        for i, kind, key, what, line in found:
            if argv == ['--list-words']:
                if kind == 'word':
                    words.setdefault(path, set()).add(key)
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
    print('lang_check: %d file(s) checked, clean' % len(listed))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
