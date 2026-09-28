"""Load the compiled plugin and exercise its runtime gate without starting Skyrim."""
import ctypes as C, pathlib, sys
class Interface(C.Structure):
    _fields_ = [('skse',C.c_uint32),('runtime',C.c_uint32),('editorVersion',C.c_uint32),('editor',C.c_uint32)]
class Info(C.Structure):
    _fields_ = [('infoVersion',C.c_uint32),('name',C.c_char_p),('version',C.c_uint32)]
plugin=C.WinDLL(str(pathlib.Path(sys.argv[1]).resolve()))
query=plugin.SKSEPlugin_Query
query.argtypes=[C.POINTER(Interface),C.POINTER(Info)]
query.restype=C.c_bool
def packed(major,minor,build): return (major<<24)|(minor<<16)|(build<<4)
info=Info()
assert query(C.byref(Interface(0,packed(1,5,97),0,0)),C.byref(info))
assert info.name==b'BTPSSceneGraphGuard' and info.version==1
assert not query(C.byref(Interface(0,packed(1,6,1170),0,0)),C.byref(info))
assert not query(C.byref(Interface(0,packed(1,5,97),0,1)),C.byref(info))
assert not query(None,C.byref(info))
print('Compiled DLL loads. Query accepts Skyrim 1.5.97 and rejects another runtime, the editor and a null interface.')
