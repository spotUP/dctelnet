#!/usr/bin/env python3
"""tools/check_edit_layout.py -- the Edit Address Book Profile window is
built from four parallel tables that MUST agree element-by-element:
editProfileNGad[] (coords/labels), editProfileGTypes[] (kinds),
editProfileGTags[] (tag blocks) and the GD_* ids (edit.h, positional).
Three separate "invisible gadget" sessions came from these drifting apart
(appended gadgets landing at wrong indexes), so CI fails on any mismatch.

Usage: python3 tools/check_edit_layout.py src/guis.c src/edit.h
"""
import re
import sys

KIND_LONGS = {
    "STRING_KIND": 9,
    "INTEGER_KIND": 9,
    "TEXT_KIND": 5,
    "BUTTON_KIND": 3,
    "CHECKBOX_KIND": 5,
    "CYCLE_KIND": 5,
}

# gadget index -> expected kind, from the GD_* contract
EXPECTED_KIND = {
    0: "STRING_KIND", 1: "STRING_KIND", 2: "TEXT_KIND",
    3: "BUTTON_KIND", 4: "BUTTON_KIND", 5: "INTEGER_KIND",
    6: "STRING_KIND", 7: "STRING_KIND", 8: "TEXT_KIND",
    9: "BUTTON_KIND", 10: "BUTTON_KIND", 11: "CHECKBOX_KIND",
    12: "TEXT_KIND", 13: "BUTTON_KIND", 14: "CYCLE_KIND",
    15: "CHECKBOX_KIND", 16: "CHECKBOX_KIND", 17: "CHECKBOX_KIND",
    18: "CHECKBOX_KIND",
}


def fail(msg):
    print("check_edit_layout: FAIL: %s" % msg)
    sys.exit(1)


def main(guis_path, edit_path):
    guis = open(guis_path, encoding="ascii").read()
    edith = open(edit_path, encoding="ascii").read()

    # 1. GD_* ids are 0..N-1 in order.
    ids = re.findall(r"#define\s+(GD_\w+)\s+(\d+)", edith)
    for expect, (name, num) in enumerate(ids):
        if int(num) != expect:
            fail("edit.h: %s is %s, want %d" % (name, num, expect))
    count = len(ids)

    # 2. NGad / GTypes have exactly N entries.
    ngad = re.search(r"editProfileNGad\[\] = \{(.*?)\n\};", guis, re.S).group(1)
    ngad_rows = [r for r in ngad.strip().split("\n") if r.strip()]
    if len(ngad_rows) != count:
        fail("NGad rows %d != GD count %d" % (len(ngad_rows), count))

    gtypes = re.search(r"editProfileGTypes\[\] = \{(.*?)\n\};", guis, re.S).group(1)
    gtypes = [g.strip().rstrip(",") for g in gtypes.strip().split("\n") if g.strip()]
    if len(gtypes) != count:
        fail("GTypes entries %d != GD count %d" % (len(gtypes), count))
    for i, (got, want) in enumerate(zip(gtypes, [EXPECTED_KIND[i] for i in range(count)])):
        if got != want:
            fail("GTypes[%d] is %s, want %s" % (i, got, want))

    # 3. Tag blocks: N blocks, each with the right long count for its kind.
    tags = re.search(r"editProfileGTags\[\] = \{(.*?)\n\};", guis, re.S).group(1)
    blocks = [b.strip() for b in tags.split("(TAG_DONE),")]
    # last block ends with "(TAG_DONE)" without trailing comma
    if blocks and blocks[-1].endswith("(TAG_DONE)"):
        blocks[-1] = blocks[-1][: -len("(TAG_DONE)")].strip().rstrip(",").strip()
        if blocks[-1] == "":
            blocks.pop()
    if len(blocks) != count:
        fail("tag blocks %d != GD count %d" % (len(blocks), count))
    pos = 0
    etags = dict(re.findall(r"#define\s+(ETAG_\w+)\s+(\d+)", guis))
    for i, b in enumerate(blocks):
        longs = [x.strip() for x in b.split(",") if x.strip()]
        want = KIND_LONGS[EXPECTED_KIND[i]] - 1  # without TAG_DONE
        if len(longs) != want:
            fail("tag block %d has %d longs, want %d" % (i, len(longs), want))
        pos += want + 1

    # 4. Every ETAG_* value slot must equal its block base + value offset.
    # Strings/text/integer values sit at +1; checkbox state and cycle
    # active at +3 (tag, marker/value, tag, VALUE, done). Poking the tag
    # instead corrupts the stream for every downstream gadget (FALSE reads
    # as TAG_DONE) -- this exact bug hid here once.
    expected_etags = {
        "ETAG_SITE": (0, 1), "ETAG_ADDRESS": (1, 1), "ETAG_LAST": (2, 1),
        "ETAG_PORT": (5, 1), "ETAG_USERNAME": (6, 1), "ETAG_PASSWORD": (7, 1),
        "ETAG_SETTINGS": (8, 1), "ETAG_PETSCII": (11, 3),
        "ETAG_FONTNAME": (12, 1), "ETAG_SCREEN_ACTIVE": (14, 3),
        "ETAG_ECHO": (15, 3), "ETAG_RAW": (16, 3),
        "ETAG_BSDEL": (17, 3), "ETAG_CRLF": (18, 3),
    }
    base = 0
    block_base = []
    for i, b in enumerate(blocks):
        block_base.append(base)
        longs = [x.strip() for x in b.split(",") if x.strip()]
        base += len(longs) + 1
    etags = dict(re.findall(r"#define\s+(ETAG_\w+)\s+(\d+)", guis))
    for name, (block, off) in expected_etags.items():
        if name not in etags:
            fail("missing %s" % name)
        want = block_base[block] + off
        if int(etags[name]) != want:
            fail("%s is %s, want %d" % (name, etags[name], want))

    print("check_edit_layout: OK (%d gadgets aligned)" % count)


main(sys.argv[1], sys.argv[2])
