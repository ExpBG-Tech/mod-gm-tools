"""Import a Heine.CRV model closure into an EXPBG addon under a remapped root.

Portable: never launches Workbench, the game or a server, and never writes to the
installed addons folder. Input is a read-only extraction of a Workshop data.pak:
one folder per source, holding the pak paths (Models/...) plus a source.json:

  {"workshopId": "61110CC4F1FF9C8A", "name": "Placeables for GM byHeine",
   "version": "3.1.5", "author": "Heine.CRV",
   "license": "Arma Public License Share Alike (APL-SA)", "pakSha256": "..."}

Rules (the tool stops instead of writing when one cannot hold):
- Every root and every Heine reference must start with "Models/". It is remapped to
  "<target>/" ("EIIArt/" by default); in compiled .xob files the replacement must
  keep the exact byte length, only inside the HEAD chunk, and every byte after HEAD
  must stay identical.
- References that are not "Models/" are kept only when listed with --keep-ref
  (base-game resources such as metal.gamemat); any other prefix stops the import.
- A file referenced from an .xob keeps its path length. A texture referenced only
  from text materials loses spaces in its filename.
- GUIDs are deterministic: sha256(<guid seed> + target path)[:16]. A GUID already
  used by another .meta in addon/ stops the import.
- .meta and provenance JSON are written as LF bytes. Provenance rows are upserted
  by target path into --provenance; the source entry is upserted by Workshop id.

Example (from the repository root):
  python tools/art/import_heine_assets.py \
    --source <extract>/61110CC4F1FF9C8A --source <extract>/628EDA2ABC937159 \
    --root 61110CC4F1FF9C8A:Models/ServerRack/ServerRack.xob \
    --root 628EDA2ABC937159:Models/ServerRack/serverrack2/serverbox2.xob \
    --root 61110CC4F1FF9C8A:Models/HD/HD.xob \
    --keep-ref "{CE9253778DD8FBDE}Common/Materials/Game/metal.gamemat" \
    --keep-ref "{CA04CFF8FA857B8D}Assets/Props/Furniture/Bookshelf_01/data/Bookshelf_01_Glass.emat"
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE_PREFIX = 'Models/'
KIND = {'.xob': 'XOBResourceClass', '.emat': 'EMATResourceClass', '.edds': 'EDDSResourceClass'}
# Compiled model: "{GUID}path" strings are NUL-terminated inside the HEAD chunk.
XOB_REF = re.compile(rb'\{([0-9A-Fa-f]{16})\}([\x20-\x7e]+?)\x00')
# Text material: quoted "{GUID}path" (paths may contain spaces).
EMAT_REF = re.compile(rb'"\{([0-9A-Fa-f]{16})\}([^"\r\n]+)"')
# Any GUID immediately followed by a path character (object ids are "{GUID}" alone).
ANY_PATH_REF = re.compile(rb'\{[0-9A-Fa-f]{16}\}(?=[A-Za-z0-9_])')
META_NAME = re.compile(r'Name\s+"\{([0-9A-Fa-f]{16})\}([^"\r\n]+)"')


class ImportStop(Exception):
    pass


def stop(message):
    raise ImportStop(message)


def xob_head_end(data, name):
    if len(data) < 20 or data[:4] != b'FORM' or data[8:12] != b'XOB9':
        stop(f'{name}: not an XOB9 FORM file')
    if struct.unpack('>I', data[4:8])[0] + 8 != len(data):
        stop(f'{name}: FORM size does not match the file size')
    offset, chunks = 12, []
    while offset < len(data):
        if offset + 8 > len(data):
            stop(f'{name}: truncated chunk header at {offset}')
        size = struct.unpack('>I', data[offset + 4:offset + 8])[0]
        chunks.append((data[offset:offset + 4], offset, size))
        offset += 8 + size
    if offset != len(data) or not chunks or chunks[0][0] != b'HEAD':
        stop(f'{name}: chunk layout is not HEAD-first and contiguous')
    return 20 + chunks[0][2], [c[0].decode('latin1') for c in chunks]


def load_source(folder):
    info_path = folder / 'source.json'
    if not info_path.is_file():
        stop(f'{folder}: source.json is missing (workshopId, name, version, author, license, pakSha256)')
    info = json.loads(info_path.read_text(encoding='utf-8'))
    for key in ('workshopId', 'name', 'version', 'author', 'license', 'pakSha256'):
        if not info.get(key):
            stop(f'{info_path}: "{key}" is missing')
    files = {}
    for path in folder.rglob('*'):
        if path.is_file() and path != info_path:
            rel = path.relative_to(folder).as_posix()
            if rel.lower() in files:
                stop(f'{folder}: case-insensitive duplicate {rel}')
            files[rel.lower()] = (rel, path)
    return info, files


def scan_refs(rel, data):
    """Return (region_end, [(start, end, guid, path)]) for the reference region of one file."""
    suffix = Path(rel).suffix.lower()
    if suffix == '.xob':
        head_end, chunks = xob_head_end(data, rel)
        tail = data[head_end:]
        if ANY_PATH_REF.search(tail) or SOURCE_PREFIX.lower().encode() in tail.lower():
            stop(f'{rel}: resource reference outside the HEAD chunk ({"/".join(chunks)}); not handled')
        refs = [(m.start(), m.end(), m.group(1).decode().upper(), m.group(2).decode('ascii'))
                for m in XOB_REF.finditer(data, 0, head_end)]
        if len(refs) != len(ANY_PATH_REF.findall(data, 0, head_end)):
            stop(f'{rel}: HEAD holds a reference that is not a NUL-terminated "{{GUID}}path" string')
        return head_end, refs
    if suffix == '.emat':
        refs = [(m.start(), m.end(), m.group(1).decode().upper(), m.group(2).decode('utf-8'))
                for m in EMAT_REF.finditer(data)]
        if len(refs) != len(ANY_PATH_REF.findall(data)):
            stop(f'{rel}: material holds an unquoted resource reference')
        return len(data), refs
    if suffix == '.edds':
        return 0, []
    stop(f'{rel}: unsupported resource type {suffix} (only .xob, .emat, .edds)')


def main(argv=None):
    parser = argparse.ArgumentParser(description='Import a Heine model closure under a remapped root.')
    parser.add_argument('--source', action='append', type=Path, required=True,
                        help='extracted pak folder holding Models/... and source.json (repeatable)')
    parser.add_argument('--root', action='append', required=True,
                        help='<workshopId>:Models/<path> resource to import with its closure (repeatable)')
    parser.add_argument('--keep-ref', action='append', default=[],
                        help='exact "{GUID}path" base-game reference allowed to stay unchanged (repeatable)')
    parser.add_argument('--addon', type=Path, default=ROOT / 'addon/intel-items', help='target addon folder')
    parser.add_argument('--target', default='EIIArt', help='target folder replacing "Models" (same length)')
    parser.add_argument('--guid-seed', help='GUID seed prefix (default EXPBG_GM_Tools/<addon folder>/)')
    parser.add_argument('--provenance', type=Path, default=ROOT / 'docs/licenses/intel-items/imported-assets.json')
    parser.add_argument('--dry-run', action='store_true', help='check and report without writing')
    parser.add_argument('--force', action='store_true', help='overwrite target files whose bytes differ')
    args = parser.parse_args(argv)

    addon = args.addon.resolve()
    seed = args.guid_seed or f'EXPBG_GM_Tools/{addon.name}/'
    target_prefix = args.target.strip('/') + '/'
    keep = set()
    for ref in args.keep_ref:
        m = re.fullmatch(r'\{([0-9A-Fa-f]{16})\}(.+)', ref)
        if not m:
            stop(f'--keep-ref "{ref}" is not "{{GUID}}path"')
        keep.add((m.group(1).upper(), m.group(2)))

    def guid_of(path):
        return hashlib.sha256((seed + path).encode('utf-8')).hexdigest()[:16].upper()

    sources = {}
    for folder in args.source:
        info, files = load_source(folder.resolve())
        if info['workshopId'] in sources:
            stop(f'source {info["workshopId"]} given twice')
        sources[info['workshopId']] = (info, files)

    # 1. Walk the closure from the roots, each reference resolved in its own source.
    todo, used, refs_of, xob_referenced = [], {}, {}, set()
    for root in args.root:
        sid, _, rel = root.partition(':')
        if sid not in sources:
            stop(f'--root {root}: unknown source {sid}')
        todo.append((sid, rel))
    while todo:
        sid, ref = todo.pop()
        info, files = sources[sid]
        if not ref.lower().startswith(SOURCE_PREFIX.lower()):
            stop(f'{sid}:{ref}: prefix is not "{SOURCE_PREFIX}"')
        if ref.lower() not in files:
            stop(f'{sid}:{ref}: referenced resource is not in the extraction')
        rel, path = files[ref.lower()]
        if (sid, rel) in used:
            continue
        data = path.read_bytes()
        region_end, refs = scan_refs(rel, data)
        used[(sid, rel)] = data
        refs_of[(sid, rel)] = (region_end, refs)
        for _, _, old_guid, ref_path in refs:
            if ref_path.lower().startswith(SOURCE_PREFIX.lower()):
                if ref_path.lower() not in files:
                    stop(f'{sid}:{rel} references {ref_path}, which is not in the extraction')
                if rel.lower().endswith('.xob'):
                    xob_referenced.add((sid, files[ref_path.lower()][0]))
                todo.append((sid, ref_path))
            elif (old_guid, ref_path) not in keep:
                stop(f'{sid}:{rel} references {{{old_guid}}}{ref_path}: prefix is not "{SOURCE_PREFIX}" and it is '
                     f'not listed with --keep-ref (check that it is a base-game resource)')

    if xob_referenced and len(target_prefix) != len(SOURCE_PREFIX):
        stop(f'target "{target_prefix}" must be {len(SOURCE_PREFIX)} bytes like "{SOURCE_PREFIX}" for compiled models')

    # 2. Target paths and GUIDs.
    target = {}
    for sid, rel in used:
        tail = rel[len(SOURCE_PREFIX):]
        if (sid, rel) not in xob_referenced:
            tail = tail.replace(' ', '')
        target[(sid, rel)] = target_prefix + tail
    seen = {}
    for key, path in target.items():
        if path.lower() in seen:
            stop(f'target collision: {path} from {seen[path.lower()]} and {key}')
        seen[path.lower()] = key
    existing = {}
    for meta in (ROOT / 'addon').rglob('*.meta'):
        m = META_NAME.search(meta.read_text(encoding='utf-8', errors='replace'))
        if m:
            existing.setdefault(m.group(1).upper(), set()).add(m.group(2))
    new_guids = {}
    for key, path in target.items():
        guid = guid_of(path)
        if guid in new_guids:
            stop(f'GUID collision between new resources: {guid}')
        new_guids[guid] = path
        if any(other != path for other in existing.get(guid, ())):
            stop(f'GUID {guid} for {path} is already used by {sorted(existing[guid])}')

    # 3. Convert.
    out, report, base_refs = {}, [], set()
    for key in sorted(used, key=lambda k: target[k]):
        sid, rel = key
        info, files = sources[sid]
        raw = used[key]
        region_end, refs = refs_of[key]
        is_xob = rel.lower().endswith('.xob')
        pieces, cursor = [], 0
        for start, end, old_guid, ref_path in refs:
            if not ref_path.lower().startswith(SOURCE_PREFIX.lower()):
                base_refs.add(f'{{{old_guid}}}{ref_path}')
                continue
            new_path = target[(sid, files[ref_path.lower()][0])]
            new_ref = ('{' + guid_of(new_path) + '}' + new_path).encode('ascii')
            if is_xob:
                old_ref = raw[start:end - 1]
                if len(new_ref) != len(old_ref):
                    stop(f'{rel}: "{old_ref.decode()}" -> "{new_ref.decode()}" changes the byte length')
                new_ref += b'\x00'
            else:
                new_ref = b'"' + new_ref + b'"'
            pieces += [raw[cursor:start], new_ref]
            cursor = end
        conv = b''.join(pieces) + raw[cursor:]
        if is_xob:
            if len(conv) != len(raw) or conv[region_end:] != raw[region_end:]:
                stop(f'{rel}: compiled model bytes outside HEAD changed')
            xob_head_end(conv, rel)
        for _, _, new_guid, new_path in scan_refs(rel, conv)[1]:
            if not ((new_guid, new_path) in keep or
                    (new_path.startswith(target_prefix) and new_guid == guid_of(new_path) and new_path in target.values())):
                stop(f'{rel}: reference {{{new_guid}}}{new_path} was left unremapped')
        path = target[key]
        changes = []
        if refs and any(r[3].lower().startswith(SOURCE_PREFIX.lower()) for r in refs):
            changes.append('resource references remapped' + (
                ' (equal-length strings in the XOB HEAD chunk only; all other bytes unchanged)' if is_xob else ''))
        if rel.lower().endswith('.edds'):
            changes.append('texture bytes unchanged')
        if ' ' in rel and ' ' not in path:
            changes.append('space removed from the filename')
        changes.append(f'path {rel} -> {path}')
        out[path] = conv
        report.append({
            'sourceMod': info['workshopId'], 'sourceVersion': info['version'], 'source': rel,
            'source_sha256': hashlib.sha256(raw).hexdigest(), 'path': path, 'guid': guid_of(path),
            'sha256': hashlib.sha256(conv).hexdigest(), 'bytes': len(conv),
            'changes': '; '.join(changes)[0].upper() + '; '.join(changes)[1:]})

    # 4. Refuse to clobber different bytes unless forced.
    for path, conv in out.items():
        dest = addon / path
        if dest.exists() and dest.read_bytes() != conv and not args.force:
            stop(f'{dest} exists with different bytes (use --force to replace)')

    for row in report:
        print(f'{row["guid"]}  {row["bytes"]:>9}  {row["path"]}  <- {row["sourceMod"]}:{row["source"]}')
    print('Kept base-game references:', ', '.join(sorted(base_refs)) or 'none')
    print(f'{len(report)} resources, {sum(r["bytes"] for r in report)} bytes, seed "{seed}"')
    if args.dry_run:
        print('Dry run: nothing written.')
        return 0

    for path, conv in out.items():
        dest = addon / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(conv)
        kind = KIND[dest.suffix.lower()]
        meta = (f'MetaFileClass {{\n Name "{{{guid_of(path)}}}{path}"\n Configurations {{\n'
                f'  {kind} PC {{}}\n  {kind} HEADLESS : PC {{}}\n }}\n}}\n')
        dest.with_name(dest.name + '.meta').write_bytes(meta.encode('ascii'))

    prov = {'sources': [], 'assets': []}
    if args.provenance.exists():
        prov = json.loads(args.provenance.read_text(encoding='utf-8'))
    srcs = {s['workshopId']: s for s in prov.get('sources', [])}
    for sid in sorted({r['sourceMod'] for r in report}):
        info = sources[sid][0]
        srcs[sid] = {k: info[k] for k in ('workshopId', 'name', 'version', 'author', 'license', 'pakSha256')}
        srcs[sid]['url'] = f'https://reforger.armaplatform.com/workshop/{sid}'
    rows = {r['path']: r for r in prov.get('assets', [])}
    rows.update({r['path']: r for r in report})
    prov['sources'] = [srcs[k] for k in sorted(srcs)]
    prov['assets'] = [rows[k] for k in sorted(rows)]
    args.provenance.parent.mkdir(parents=True, exist_ok=True)
    args.provenance.write_bytes((json.dumps(prov, indent=2, ensure_ascii=False) + '\n').encode('utf-8'))
    print(f'Wrote {len(out)} resources + .meta under {addon} and {len(prov["assets"])} provenance rows.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except ImportStop as error:
        print(f'STOP: {error}', file=sys.stderr)
        sys.exit(2)
