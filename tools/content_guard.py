#!/usr/bin/env python3
"""Content guard: no game data in any commit.

    python tools/content_guard.py --history REV       every commit reachable from REV
    python tools/content_guard.py --range BASE HEAD   every commit in HEAD that is
                                                      not in BASE
    python tools/content_guard.py --github            the automatic check
                                                      (.github/workflows/guard.yml):
                                                      --history HEAD for a push or
                                                      a pull request

Every file of every checked commit is looked at - the whole tree of each
commit, not only what it changed and not only the newest commit: a file that
one commit adds and a later one deletes is still in the history, and pushing
the history publishes it. A check fails on
  type      a path ending in a forbidden file type (disc images, game data,
            track and character containers, music data, patches, memory
            cards, save states and replays), in any letter case, also with
            dots or spaces after it
  binary    a binary file larger than 1 MB: a NUL byte in its first 8000
            bytes, the same test git uses

On GitHub every run checks the whole history of the checked-out commit: the
pushed commit, or for a pull request the merge commit of the pull request into
its base. Push and pull request get the same check under the same name, a
direct push included, and a later commit that deletes a file never makes it
pass. A push that deletes a branch or tag has nothing to check and passes. Any
other event, a shallow clone or a git error fails the check.

tools/git-hooks/pre-push runs a broader check before anything leaves the
machine; this script is the check that GitHub runs on every push and pull
request. It needs only Python 3 and git, and it reads git objects only.

Exit code 0 = clean, 1 = forbidden content, 2 = usage or git error.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import textwrap

# Forbidden file types, compared in lower case with the last extension of the
# file name after trailing dots and spaces are removed: the type list of
# tools/git-hooks/pre-push without its name, log, picture, folder, 5 MB and
# hash rules, plus .ctrreplay (replays are forbidden by CONTRIBUTING.md).
GROUPS = (
    ('disc image', '.bin .cue .iso .img .chd .ecm .pbp .mdf .mds .nrg .ccd .sub .toc'),
    ('game data', '.lev .vrm .big .xa .str .hwl .ctr .mpk .xnf .vag .vab .vh .vb .tim .lng'),
    ('track container', '.rldtrack'),
    ('character container', '.rldchar'),
    ('track preview', '.rldprev'),
    ('music or sound data', '.sca .sndb .cseq'),
    ('patch', '.xdelta .ppf .ips .bps .vcdiff'),
    ('memory card or save file', '.mcr .mcd .gme .srm .sav'),
    ('save state', '.ctrstates .state'),
    ('replay', '.ctrreplay'),
)
TYPES = {ext: what for what, exts in GROUPS for ext in exts.split()}

LIMIT = 1024 * 1024     # a binary file larger than this fails
PROBE = 8000            # the bytes looked at for a NUL, as git does

# Known binary files above LIMIT under externals/SDL/ that may stay: exact
# path -> blob id, so an exception never covers other content under the same
# name. Empty: the only files above 1 MB in the history,
# externals/SDL/src/video/directx/d3d12.h and
# externals/SDL/test/unifont-15.1.05.hex, are text and pass without one.
ALLOWED = {}
ALLOWED_PREFIX = 'externals/SDL/'

FIX = ('remove the file from the history of your branch; never commit game data'
       ' - see CONTRIBUTING.md')
HOW_TO_FIX = textwrap.fill(
    'How to fix: %s. A later commit that deletes the file is not enough, because'
    ' every commit is checked. For example: git rebase -i <base branch>, drop or'
    ' edit the commits named above, then push the branch again with'
    ' --force-with-lease.' % FIX, 78, break_long_words=False, break_on_hyphens=False)

CONTROL = re.compile(r'[\x00-\x1f\x7f]')


class Failure(Exception):
    """A usage or git error: the check cannot say "clean", so it fails."""


# Every git call: replace refs must not hide or swap objects.
ENV = dict(os.environ, GIT_NO_REPLACE_OBJECTS='1')


def git(*args):
    r = subprocess.run(('git',) + args, capture_output=True, env=ENV)
    if r.returncode != 0:
        raise Failure('git %s failed: %s' % (args[0], r.stderr.decode('utf-8', 'replace').strip()))
    return r.stdout


def resolve(rev):
    """The commit a name or object id stands for, or None if it is not here."""
    r = subprocess.run(['git', 'rev-parse', '--verify', '--quiet', '--end-of-options',
                        rev + '^{commit}'], capture_output=True, env=ENV)
    return r.stdout.decode('ascii').strip() if r.returncode == 0 else None


def commits(head, base):
    """The commits to check, oldest first: all of head's history, or only
    those that base does not have."""
    args = ['rev-list', '--topo-order', '--reverse', head]
    if base:
        args.append('^' + base)
    return git(*args, '--').decode('ascii').split()


def entries(commit):
    """(path, type, object id, size) for every file in the commit's tree."""
    for rec in git('ls-tree', '-r', '-l', '-z', '--full-tree', commit).split(b'\0'):
        if rec:
            meta, path = rec.split(b'\t', 1)
            _mode, kind, oid, size = meta.decode('ascii').split()
            path = path.decode('utf-8', 'surrogateescape')
            if kind == 'blob' and size == 'BAD':
                raise Failure('object %s (%s) is missing from this clone' % (oid, shown(path)))
            yield path, kind, oid, size


def is_binary(oid):
    """Only asked for files larger than LIMIT, so a short read is an error."""
    p = subprocess.Popen(['git', 'cat-file', 'blob', oid], env=ENV,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    head = p.stdout.read(PROBE)
    p.stdout.close()
    p.kill()
    p.wait()
    if len(head) < PROBE:
        raise Failure('cannot read file content %s' % oid)
    return b'\0' in head


def judge(path, kind, oid, size, binary):
    """Why the file is forbidden, or None."""
    name = path.rsplit('/', 1)[-1].lower().rstrip(' .')
    dot = name.rfind('.')
    if dot >= 0 and name[dot:] in TYPES:
        return 'forbidden file type %s (%s)' % (name[dot:], TYPES[name[dot:]])
    if kind == 'blob' and int(size) > LIMIT:
        if path.startswith(ALLOWED_PREFIX) and ALLOWED.get(path) == oid:
            return None
        if oid not in binary:
            binary[oid] = is_binary(oid)
        if binary[oid]:
            return 'binary file larger than 1 MB (%s bytes)' % size
    return None


def check(todo):
    """{(path, reason): [commits]} for everything forbidden in the commits."""
    verdict = {}
    binary = {}
    found = {}
    for commit in todo:
        for path, kind, oid, size in entries(commit):
            if (path, oid) not in verdict:
                verdict[path, oid] = judge(path, kind, oid, size, binary)
            reason = verdict[path, oid]
            if reason:
                found.setdefault((path, reason), []).append(commit)
    return found


def shown(text):
    """Text as it is printed: control characters escaped, so a file name or a
    commit subject can neither break the report nor pass for a runner
    command (every printed line also starts with fixed text)."""
    return CONTROL.sub(lambda m: '\\x%02x' % ord(m.group(0)), text)


def label(commit):
    """Short commit id and subject."""
    return shown(git('log', '-1', '--format=%h %s', commit, '--')
                 .decode('utf-8', 'replace').strip())


def annotate(text):
    """An error annotation on the run page when running on GitHub."""
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        text = text.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print('::error title=Content guard::' + text)


def report(found, todo, what):
    if not found:
        print('content_guard: %s: %d commit(s) checked, clean' % (what, len(todo)))
        return 0
    print('content_guard: FAILED - %s: forbidden content in %d of %d checked commit(s):'
          % (what, len({c for cs in found.values() for c in cs}), len(todo)))
    print()
    for (path, reason), cs in sorted(found.items()):
        names = [label(c) for c in cs[:5]]
        print('  - %s' % shown(path))
        print('      %s' % reason)
        for name in names:
            print('      in commit %s' % name)
        if len(cs) > 5:
            print('      ... and in %d more commit(s)' % (len(cs) - 5))
        annotate('%s: %s, in commit %s - %s' % (shown(path), reason, names[0], FIX))
    print()
    print(HOW_TO_FIX)
    return 1


def from_github():
    """What this run checks, or None if the push deleted a branch or tag."""
    event = os.environ.get('GITHUB_EVENT_NAME', '')
    if event not in ('push', 'pull_request'):
        raise Failure('unsupported event "%s" (push and pull_request only)' % shown(event))
    path = os.environ.get('GITHUB_EVENT_PATH', '')
    if not path:
        raise Failure('--github needs GITHUB_EVENT_NAME and GITHUB_EVENT_PATH')
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise Failure('unexpected event file %s' % path)
    ref = shown(str(data.get('ref') or os.environ.get('GITHUB_REF', '')))
    if event == 'push' and data.get('deleted') is True:
        print('content_guard: push to %s deletes the ref - nothing to check' % ref)
        return None
    return 'history of HEAD (%s, %s)' % (event.replace('_', ' '), ref)


def main(argv):
    try:
        sys.stdout.reconfigure(errors='backslashreplace')
    except AttributeError:
        pass
    ap = argparse.ArgumentParser(description='No game data in any commit.')
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--history', metavar='REV', help='every commit reachable from REV')
    mode.add_argument('--range', nargs=2, metavar=('BASE', 'HEAD'),
                      help='every commit in HEAD that is not in BASE')
    mode.add_argument('--github', action='store_true',
                      help='the automatic check: --history HEAD for a push or pull request')
    args = ap.parse_args(argv)
    try:
        base = None
        if args.github:
            what = from_github()
            if what is None:
                return 0
            head = 'HEAD'
        elif args.range:
            base, head = args.range
            what = '%s..%s' % (base, head)
        else:
            head = args.history
            what = 'history of %s' % head
        if git('rev-parse', '--is-shallow-repository').strip() != b'false':
            raise Failure('this clone is shallow, so its history is incomplete '
                          '(check out with fetch-depth: 0)')
        head_id = resolve(head)
        if not head_id:
            raise Failure('%s is not a commit in this clone' % head)
        base_id = None
        if base is not None:
            base_id = resolve(base)
            if not base_id:
                raise Failure('%s is not a commit in this clone' % base)
        todo = commits(head_id, base_id)
        return report(check(todo), todo, shown(what))
    except (Failure, OSError, ValueError, KeyError) as e:
        print('content_guard: ERROR - %s' % shown(str(e)))
        print('content_guard: the check could not run, so it fails.')
        annotate('the check could not run: %s' % shown(str(e)))
        return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
