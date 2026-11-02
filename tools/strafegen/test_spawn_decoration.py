"""Decorative art must not turn a valid spawn into a player/solid overlap."""
import unittest

from strafegen_arena import LatticeArena
from strafegen_geom import greeble_brutal, greeble_course, validate_spawns


class SpawnDecorationTests(unittest.TestCase):
    def test_lattice_spawns_stay_clear_in_both_themes(self):
        for decorate in (greeble_brutal, greeble_course):
            for seed in (0, 7, 64, 1337):
                for density in (1.0, 1.8):
                    with self.subTest(theme=decorate.__name__, seed=seed, density=density):
                        course = LatticeArena(seed).build()
                        self.assertEqual(validate_spawns(course), 24)
                        original = course.solids[:]
                        added = decorate(course, seed=seed, density=density)
                        self.assertGreater(added, 0)
                        self.assertEqual(course.solids[:len(original)], original)
                        self.assertEqual(len(course.solids), len(original) + added)
                        self.assertEqual(validate_spawns(course), 24)


if __name__ == '__main__':
    unittest.main()
