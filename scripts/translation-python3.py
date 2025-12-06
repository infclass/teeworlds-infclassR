# xgettext --keyword=_ --keyword="_P:1,2" --language=C --from-code=UTF-8 -o ../infclass-translation/infclasspot.po $(find ./src -name \*.cpp -or -name \*.h)

import json
import os
from dataclasses import dataclass
from typing import List

import polib


def convert_po_to_json(language: "Language"):
    language_code = language.code
    plurals = language.plurals

    po_file_name = f"other/po/{language_code}/infclass.po"
    if os.path.isfile(po_file_name):
        json_file_name = f"data/languages/{language_code}.json"

        po = polib.pofile(po_file_name)

        with open(json_file_name, "w") as f:
            target_dict = {"translation": []}
            translations = target_dict["translation"]
            for entry in po:
                if entry.msgstr:
                    target_entry = {"key": entry.msgid, "value": entry.msgstr}
                elif entry.msgstr_plural.keys():
                    target_entry = {"key": entry.msgid_plural}
                    for i in sorted(entry.msgstr_plural.keys()):
                        target_entry[plurals[i]] = entry.msgstr_plural[i]
                else:
                    continue
                translations.append(target_entry)

            json.dump(target_dict, f, ensure_ascii=False, indent=4)


@dataclass
class Language:
    code: str
    plurals: List[str]


LANGUAGES: List[Language] = [
    Language("ar", ["zero", "one", "two", "few", "many", "other"]),
    Language("bg", ["one", "other"]),
    Language("cs", ["one", "few", "other"]),
    Language("de", ["one", "other"]),
    Language("el", ["one", "other"]),
    Language("es", ["one", "other"]),
    Language("fa", ["one", "other"]),
    Language("fi", ["one", "other"]),
    Language("fr", ["one", "other"]),
    Language("hr", ["one", "few", "other"]),
    Language("hu", ["one", "other"]),
    Language("it", ["one", "other"]),
    Language("ja", ["other"]),
    Language("la", ["one", "other"]),
    Language("nl", ["one", "other"]),
    Language("pl", ["one", "few", "many", "other"]),
    Language("pt", ["one", "other"]),
    Language("pt-BR", ["one", "other"]),
    Language("ru", ["one", "few", "many", "other"]),
    Language("sah", ["other"]),
    Language("sr-Latn", ["one", "few", "other"]),
    Language("tl", ["one", "other"]),
    Language("tr", ["one", "other"]),
    Language("uk", ["one", "few", "other"]),
    Language("zh-CN", ["other"]),
]

for language in LANGUAGES:
    convert_po_to_json(language)
