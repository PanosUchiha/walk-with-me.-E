"""Regression checks for the optional archive and Nemesis's line-based reader."""
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from animation_package import nemesis_text, split_gestures, verify_patch


class AnimationPackageTests(unittest.TestCase):
    def test_base_has_no_gesture_dependencies(self):
        files = {p.relative_to(ROOT / 'package').as_posix(): p
                 for p in (ROOT / 'package').rglob('*') if p.is_file()}
        base, gestures = split_gestures(files)
        self.assertEqual(set(base) | set(gestures), set(files))
        self.assertFalse(set(base) & set(gestures))
        self.assertFalse(any(n.startswith('Nemesis_Engine/') for n in base))
        self.assertFalse(any('Walk With Me Signals/' in n for n in base))
        self.assertFalse(any('Walk With Me Victory/' in n for n in files))
        self.assertFalse(any('WWM Clasp/' in n for n in files))

    def test_shipped_patch(self):
        verify_patch(ROOT / 'package/Nemesis_Engine/mod/gpma/0_master')

    def test_nemesis_can_discover_state_references(self):
        original = '<hkobject name="#gpma$30"><hkparam name="states" numelements="2">#gpma$28 #gpma$42</hkparam></hkobject>'
        text = nemesis_text(ET.fromstring(original))
        # Nemesis searches for tab-indented reference lines. Valid XML alone
        # accepted our old one-line states array but left this list empty.
        states = [word for line in text.splitlines() if '\t\t\t#' in line for word in line.split()]
        self.assertEqual(states, ['#gpma$28', '#gpma$42'])
        self.assertEqual(ET.fromstring(text).find('hkparam').text.split(), states)

    def test_rejects_truncated_array(self):
        with self.assertRaises(ValueError):
            nemesis_text(ET.fromstring('<hkobject><hkparam name="states" numelements="2">#gpma$28</hkparam></hkobject>'))

    def test_rejects_incomplete_optional_archive(self):
        with self.assertRaises(ValueError):
            split_gestures({'Nemesis_Engine/mod/gpma/0_master/#gpma$30.txt': b'patch'})


if __name__ == '__main__':
    unittest.main()
