#!/usr/bin/env python3
# Minimal xdotool stand-in over XTest (ctypes, no headers needed).
#   xin.py click X Y [button]     move to absolute X Y and click
#   xin.py move X Y
#   xin.py key NAME [NAME...]     press+release keysyms (Return, Escape, F5, grave, a, ...)
#   xin.py down NAME / up NAME    hold / release a key
#   xin.py type "text"            type text slowly (spaces, letters, digits)
#   xin.py rel DX DY              relative mouse motion
#   xin.py bdown [b] / bup [b]    hold / release a mouse button (1 left)
import ctypes, os, sys, time

x11 = ctypes.CDLL("libX11.so.6")
xt = ctypes.CDLL("libXtst.so.6")
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XFlush.argtypes = [ctypes.c_void_p]
for f in ("XTestFakeKeyEvent", "XTestFakeButtonEvent"):
    getattr(xt, f).argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xt.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
xt.XTestFakeRelativeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]

d = x11.XOpenDisplay(os.environ.get("DISPLAY", ":99").encode())
if not d:
    sys.exit("no display")

NAMES = {" ": "space", "/": "slash", "-": "minus", ".": "period", "_": "underscore", "~": "asciitilde", "`": "grave"}

def code(name):
    ks = x11.XStringToKeysym(NAMES.get(name, name).encode())
    kc = x11.XKeysymToKeycode(d, ks)
    if not kc:
        sys.exit("unknown key " + name)
    return kc

def key(name, hold=0.06):
    kc = code(name)
    shift = name == "_"
    if shift: xt.XTestFakeKeyEvent(d, code("Shift_L"), 1, 0)
    xt.XTestFakeKeyEvent(d, kc, 1, 0); x11.XFlush(d); time.sleep(hold)
    xt.XTestFakeKeyEvent(d, kc, 0, 0)
    if shift: xt.XTestFakeKeyEvent(d, code("Shift_L"), 0, 0)
    x11.XFlush(d)

cmd, args = sys.argv[1], sys.argv[2:]
if cmd == "click":
    xt.XTestFakeMotionEvent(d, -1, int(args[0]), int(args[1]), 0); x11.XFlush(d); time.sleep(0.15)
    b = int(args[2]) if len(args) > 2 else 1
    xt.XTestFakeButtonEvent(d, b, 1, 0); x11.XFlush(d); time.sleep(0.12)
    xt.XTestFakeButtonEvent(d, b, 0, 0)
elif cmd == "move":
    xt.XTestFakeMotionEvent(d, -1, int(args[0]), int(args[1]), 0)
elif cmd == "rel":
    xt.XTestFakeRelativeMotionEvent(d, int(args[0]), int(args[1]), 0)
elif cmd == "key":
    for n in args: key(n); time.sleep(0.15)
elif cmd == "down":
    xt.XTestFakeKeyEvent(d, code(args[0]), 1, 0)
elif cmd == "up":
    xt.XTestFakeKeyEvent(d, code(args[0]), 0, 0)
elif cmd == "bdown":
    xt.XTestFakeButtonEvent(d, int(args[0]) if args else 1, 1, 0)
elif cmd == "bup":
    xt.XTestFakeButtonEvent(d, int(args[0]) if args else 1, 0, 0)
elif cmd == "type":
    for ch in args[0]:
        key(ch, 0.08); time.sleep(0.18)   # software rendering is slow: type slowly
x11.XFlush(d)
