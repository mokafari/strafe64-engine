"""Regression checks: python -m unittest discover -s tools/strafegen -p test_asset_packaging.py."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import build_assets
import strafegen_pack as pack


class PackagingTests(unittest.TestCase):
    def test_failed_shared_build_preserves_last_good_pack(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / pack.SHARED_ASSETS_PK3
            output.write_bytes(b'previous complete pack')
            with patch.object(pack, 'build_shader', return_value='shader'), \
                 patch.object(pack, 'build_detail_textures', side_effect=OSError('texture read failed')):
                with self.assertRaises(OSError):
                    pack.write_shared_assets(directory)
            self.assertEqual(output.read_bytes(), b'previous complete pack')
            self.assertEqual(list(Path(directory).iterdir()), [output])

    def test_texture_packaging_does_not_depend_on_slide_sound(self):
        for standalone in (False, True):
            for sound_present in (False, True):
                with self.subTest(standalone=standalone, sound=sound_present):
                    with tempfile.TemporaryDirectory() as directory:
                        root = Path(directory)
                        (root / 'assets').mkdir()
                        if sound_present:
                            (root / 'assets/slide.wav').write_bytes(b'sound')
                        bsp = root / 'probe.bsp'
                        bsp.write_bytes(b'bsp')
                        with patch.object(pack, '__file__', str(root / 'strafegen_pack.py')), \
                             patch.object(pack, 'build_shader', return_value='shader'), \
                             patch.object(pack, 'build_detail_textures', return_value={'textures/detail.tga': b'detail'}), \
                             patch.object(pack.gfx, 'gfx_textures', return_value={'textures/gfx.tga': b'gfx'}):
                            output = pack.write_pk3(str(bsp), 'probe', directory, standalone=standalone)
                        with zipfile.ZipFile(output) as archive:
                            names = archive.namelist()
                            self.assertEqual('textures/detail.tga' in names, standalone)
                            self.assertEqual('textures/gfx.tga' in names, standalone)
                            self.assertEqual('scripts/strafe64.shader' in names, standalone)
                            self.assertEqual('sound/player/slide.wav' in names, standalone and sound_present)

    def test_missing_or_stale_photo_assets_require_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'src').mkdir()
            (root / 'out/m').mkdir(parents=True)
            for name in ('build_assets.py', 'build_materials.py', 'ethsky_from_pano.py', 'src/source.png'):
                path = root / name
                path.write_bytes(b'source')
                os.utime(path, (100, 100))
            with patch.object(build_assets, 'HERE', str(root)), \
                 patch.object(build_assets, 'SRC', str(root / 'src')), \
                 patch.object(build_assets, 'OUT', str(root / 'out')), \
                 patch('build_materials.SOURCES', {'test': None}):
                self.assertFalse(build_assets.assets_current())
                names = ['m/test.jpg', 'm/test_nh.png', 'crete.tga', 'crete_eth.tga']
                names += [f'{sky}_{side}.tga' for sky in ('realsky', 'ethsky', 'ethnight')
                          for side in ('rt', 'lf', 'ft', 'bk', 'up', 'dn')]
                for name in names:
                    path = root / 'out' / name
                    path.write_bytes(b'texture')
                    os.utime(path, (200, 200))
                self.assertTrue(build_assets.assets_current())
                os.utime(root / 'src/source.png', (300, 300))
                self.assertFalse(build_assets.assets_current())


if __name__ == '__main__':
    unittest.main()
