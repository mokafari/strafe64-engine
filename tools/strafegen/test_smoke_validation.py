"""Smoke validation must reject a running game that renders missing materials."""
import unittest
from windows_smoke import smoke_errors


class SmokeValidationTests(unittest.TestCase):
    def test_missing_material_rejected(self):
        self.assertTrue(smoke_errors(
            "^3WARNING: R_FindImageFile could not find 'textures/reservoir/pool_coping.jpg' in shader 'pool'"))

    def test_empty_shader_stage_rejected(self):
        self.assertTrue(smoke_errors("^3Shader pool has a stage with no image"))

    def test_shader_compile_failure_rejected(self):
        self.assertTrue(smoke_errors("GLSL water program failed to link"))

    def test_coloured_engine_error_rejected(self):
        self.assertTrue(smoke_errors("^1ERROR: reliable command was cycled out"))

    def test_optional_capabilities_are_not_errors(self):
        self.assertEqual([], smoke_errors(
            "Couldn't read q3history.\n...GL_NVX_gpu_memory_info not found\nCL_InitCGame: 1.0 seconds"))


if __name__ == '__main__':
    unittest.main()
