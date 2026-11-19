#!/usr/bin/env python3
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.

""" pyVCP panels rendered with GTK 3

    Usage: pyvcp-gtk [-g WxH+X+Y] [-c compname] [-x XID | --xid] myfile.xml

    Reads the same XML file as pyvcp and creates the same HAL pins, so
    it is a drop-in replacement for pyvcp in a HAL file. The widgets are
    the GladeVCP HAL widgets.
"""

import sys, os
import getopt
import signal

BASE = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), ".."))
sys.path.insert(0, os.path.join(BASE, "lib", "python"))

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gtk, Gdk, GLib

import hal
import pyvcp_gtk


def usage():
    print("Usage: pyvcp-gtk [-g WIDTHxHEIGHT+XOFFSET+YOFFSET] [-c hal_component_name]")
    print("                 [-x XID | --xid] myfile.xml")
    print("If the component name is not specified, the basename of the xml file is used.")
    print("-g options are in pixel units, XOFFSET/YOFFSET is referenced from top left of screen")
    print("use -g WIDTHxHEIGHT for just setting size or -g +XOFFSET+YOFFSET for just position")
    print("-x XID   embed the panel in the existing X window XID (e.g. an AXIS tab)")
    print("--xid    put the panel in a plug and print its window id on stdout")


def geometry(window, geo):
    size, _, pos = geo.partition("+")
    if size:
        w, _, h = size.partition("x")
        window.resize(int(w), int(h))
    if pos:
        x, _, y = pos.partition("+")
        window.move(int(x), int(y))


def main():
    try:
        opts, args = getopt.getopt(sys.argv[1:], "c:g:x:h", ["xid", "help"])
    except getopt.GetoptError as detail:
        print(detail)
        usage()
        sys.exit(1)
    component_name = None
    window_geometry = None
    parent = None
    push_xid = False
    for o, a in opts:
        if o == "-c":
            component_name = a
        elif o == "-g":
            window_geometry = a
        elif o == "-x":
            parent = int(a, 0)
        elif o == "--xid":
            push_xid = True
        elif o in ("-h", "--help"):
            usage()
            sys.exit(0)
    if len(args) != 1:
        usage()
        sys.exit(1)
    filename = args[0]
    if component_name is None:
        component_name = os.path.splitext(os.path.basename(filename))[0]

    try:
        comp = hal.component(component_name)
    except Exception:
        print("Error: Multiple components with the same name.")
        sys.exit(0)

    window = Gtk.Window(title=component_name)
    try:
        panel = pyvcp_gtk.Panel(comp, filename, window)
    except pyvcp_gtk.PanelError as e:
        comp.exit()
        raise SystemExit("pyvcp-gtk: %s" % e)
    window.add(panel.widget)

    if parent or push_xid:
        from gladevcp import xembed
        # Gdk would otherwise exit without cleanup on X errors when the
        # host window goes away
        Gdk.error_trap_push()
        if parent:
            window = xembed.reparent(window, parent)
            forward = os.environ.get("AXIS_FORWARD_EVENTS_TO")
        else:
            window = xembed.add_plug(window)
            print(window.get_id(), flush=True)
            forward = os.environ.get("QTVCP_FORWARD_EVENTS_TO")
        if forward:
            xembed.keyboard_forward(window, forward)

    window.connect("destroy", lambda *a: Gtk.main_quit())
    window.show_all()
    if window_geometry and not (parent or push_xid):
        try:
            geometry(window, window_geometry)
        except ValueError:
            print("pyvcp-gtk: bad geometry %r" % window_geometry, file=sys.stderr)

    comp.ready()
    panel.start()

    try:
        gi.require_version("GLibUnix", "2.0")
        from gi.repository import GLibUnix
        signal_add = GLibUnix.signal_add
    except (ValueError, ImportError):
        signal_add = GLib.unix_signal_add
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal_add(GLib.PRIORITY_DEFAULT, sig, Gtk.main_quit)
    try:
        Gtk.main()
    finally:
        panel.stop()
        comp.exit()


if __name__ == "__main__":
    main()
