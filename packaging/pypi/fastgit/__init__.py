"""fastgit 0.1.1 — ctypes wrapper around libfastgit."""
import ctypes, ctypes.util, pathlib
__version__ = "0.1.1"
def _load():
    for p in [str(pathlib.Path(__file__).parent/"libfastgit.so"), "libfastgit.so", "libfastgit.dylib"]:
        try: return ctypes.CDLL(p)
        except OSError: continue
    lib = ctypes.util.find_library("fastgit")
    if lib:
        try: return ctypes.CDLL(lib)
        except OSError: pass
    return None
_lib = _load()
def version(): return __version__
