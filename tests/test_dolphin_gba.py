"""The Dolphin GBA link (patches/dolphin/gba): the browser page's server, its picture codec and
the controllers' GBA buttons, built on the host from the pinned Dolphin source with the port patch
and the GBA patches applied (tools/build-dolphin.sh fetches it into .deps/dolphin-src)."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = ROOT / 'tests/dolphin_gba'
SOURCE = ROOT / '.deps/dolphin-src'


def gba_patches():
    """The GBA patches, in the order tools/build-dolphin.sh applies them."""
    return sorted((ROOT / 'patches/dolphin/gba').glob('*.patch')) or [ROOT / 'patches/dolphin/gba-link.patch']


def patched_files(out: Path) -> Path:
    """The files the GBA patches touch, as the build has them: the pinned revision's, with the port
    patch and then the GBA patches applied. Returns out/Source/Core."""
    revision = re.search(r'^revision=([0-9a-f]{40})', (ROOT / 'tools/build-dolphin.sh').read_text(),
                         re.M).group(1)
    paths = sorted({line[6:].strip() for p in gba_patches() for line in p.read_text().splitlines()
                    if line.startswith('+++ b/')})
    for rel in paths:
        show = subprocess.run(['git', '-C', str(SOURCE), 'show', f'{revision}:{rel}'], capture_output=True)
        if show.returncode == 0:  # absent: a file the patches add
            (out / rel).parent.mkdir(parents=True, exist_ok=True)
            (out / rel).write_bytes(show.stdout)
    include = [f'--include={rel}' for rel in paths]
    subprocess.run(['git', 'apply', '--whitespace=nowarn', *include,
                    str(ROOT / 'patches/dolphin/ps5-port.patch')], cwd=out, check=True)
    for patch in gba_patches():
        subprocess.run(['git', 'apply', '--whitespace=nowarn', str(patch)], cwd=out, check=True)
    return out / 'Source/Core'


class DolphinGba(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (SOURCE / '.git').exists():
            raise unittest.SkipTest('.deps/dolphin-src is not fetched (tools/build-dolphin.sh)')
        cls.tmp = tempfile.TemporaryDirectory()
        cls.work = Path(cls.tmp.name)
        cls.core = patched_files(cls.work / 'dolphin')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def harness(self) -> str:
        """The page server (GBAWeb.cpp) around a fake GBA that makes frames (harness.cpp)."""
        binary = self.work / 'gbaharness'
        if not binary.exists():
            lib = self.core / 'DolphinLibretro'
            subprocess.run(['c++', '-std=c++20', '-O1', '-pthread', '-DHAS_LIBMGBA', '-Wall', '-Wextra',
                            '-Wno-unused-parameter', f'-I{HERE / "stubs"}', f'-I{self.core}', f'-I{lib}',
                            str(HERE / 'harness.cpp'), str(lib / 'GBAWeb.cpp'), str(lib / 'GBAWebSocket.cpp'),
                            '-o', str(binary)], check=True)
        return str(binary)

    def run_script(self, name):
        result = subprocess.run(['python3', str(HERE / name)], env={'GBAHARNESS': self.harness(), 'PATH': '/usr/bin:/bin'},
                                capture_output=True, text=True, timeout=180)
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-2000:])

    def test_page_server(self):
        """Pages, /ports, pictures with acks and an fps cap, buttons, the page's actions, robustness."""
        self.run_script('server_test.py')

    def test_picture_codec(self):
        """Every test picture arrives at the browser unchanged."""
        self.run_script('codec_test.py')


if __name__ == '__main__':
    unittest.main()
