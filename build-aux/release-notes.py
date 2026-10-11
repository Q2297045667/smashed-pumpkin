#!/usr/bin/env python3

import argparse
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


LANGUAGE = "{http://www.w3.org/XML/1998/namespace}lang"
ROOT = Path(__file__).resolve().parent.parent
METAINFO = ROOT / "data/dev.rotstein.SmashedPumpkin.metainfo.xml.in"


def release_notes(path: Path, version: str) -> str:
    if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) is None:
        raise ValueError("Expected a release version in X.Y.Z format")

    root = ET.parse(path).getroot()
    releases = [item for item in root.findall("releases/release")
                if item.get("version") == version]
    if len(releases) != 1:
        raise ValueError(f"Expected exactly one AppStream release for {version}")

    description = releases[0].find("description")
    if description is None or not list(description):
        raise ValueError("Release notes are missing")

    notes = {"en": [], "de": []}
    for block in description:
        if block.tag != "ul" or LANGUAGE in block.attrib:
            raise ValueError("Use lists with translations on individual list items")
        for item in block:
            if item.tag != "li":
                raise ValueError("Release lists may only contain list items")
            language = item.get(LANGUAGE, "en")
            if language not in notes:
                continue
            text = " ".join("".join(item.itertext()).split())
            if not text:
                raise ValueError(f"Empty release note for {language}")
            notes[language].append(text)

    if not notes["en"] or len(notes["en"]) != len(notes["de"]):
        raise ValueError("Release notes require matching English and German entries")

    sections = []
    for language, heading in (("en", "English"), ("de", "Deutsch")):
        items = "\n".join(f"{index}. {text}"
                          for index, text in enumerate(notes[language], start=1))
        sections.append(f"## {heading}\n\n{items}")
    return "\n\n".join(sections) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare bilingual GitHub release notes")
    parser.add_argument("version")
    args = parser.parse_args()
    try:
        sys.stdout.write(release_notes(METAINFO, args.version))
    except (OSError, ET.ParseError, ValueError) as error:
        parser.exit(1, f"Could not prepare release notes: {error}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
