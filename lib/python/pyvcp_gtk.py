#    Render pyVCP panels with GTK 3, using the GladeVCP HAL widgets.
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.

"""Build a pyVCP panel as a GTK 3 widget tree.

The panel file is the same XML that pyvcp reads, and the panel creates
the same HAL pins: same names, types, directions and automatic
numbering. A HAL file written for a pyvcp panel works unchanged.

Drawing and input come from the GladeVCP widgets (HAL_LED, HAL_Meter,
HAL_HBar, Hal_Dial, JogWheel, HAL_Button, ...). Those widgets name their
own pins differently from pyvcp, so this module never calls their
hal_init(); it creates the pyvcp pins itself and drives the widgets
from a 100 ms update, like pyvcp does.

Options that only mean something to Tk (relief, cursor, padx, ...) are
ignored, with one warning per option name.

Usage from Python:

    panel = pyvcp_gtk.Panel(comp, "panel.xml")
    some_container.add(panel.widget)
    comp.ready()
    panel.start()          # starts the 100 ms update
"""

import math
import os
import sys
import time
import xml.dom.minidom
import xml.parsers.expat

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gtk, Gdk, GLib, GObject, GdkPixbuf

import hal
from hal import Type, Dir

from gladevcp.led import HAL_LED
from gladevcp.hal_meter import HAL_Meter
from gladevcp.hal_bar import HAL_HBar
from gladevcp.hal_dial import Hal_Dial
from gladevcp.jogwheel import JogWheel
from gladevcp.hal_widgets import (HAL_Button, HAL_CheckButton,
                                  HAL_RadioButton, HAL_SpinButton,
                                  HAL_HScale, HAL_VScale)

__all__ = ["Panel", "PanelError"]


class PanelError(Exception):
    pass


# Names a pyvcp XML file can use in an evaluated value. pyvcp evaluates
# values in a module that did "from tkinter import *", so files use the
# Tk constants bare: <orient>HORIZONTAL</orient>, <relief>RIDGE</relief>.
_TK_CONSTANTS = {
    "HORIZONTAL": "horizontal", "VERTICAL": "vertical",
    "FLAT": "flat", "RAISED": "raised", "SUNKEN": "sunken",
    "GROOVE": "groove", "RIDGE": "ridge", "SOLID": "solid",
    "LEFT": "left", "RIGHT": "right", "TOP": "top", "BOTTOM": "bottom",
    "CENTER": "center", "N": "n", "S": "s", "E": "e", "W": "w",
    "NE": "ne", "NW": "nw", "SE": "se", "SW": "sw",
    "NS": "ns", "EW": "ew", "NSEW": "nsew",
    "X": "x", "Y": "y", "BOTH": "both", "NONE": "none",
    "YES": 1, "NO": 0, "TRUE": 1, "FALSE": 0,
    "NORMAL": "normal", "DISABLED": "disabled", "ACTIVE": "active",
    "BOLD": "bold", "ITALIC": "italic", "ROMAN": "roman",
}


def _eval(text):
    return eval(text, {"__builtins__": __builtins__, **_TK_CONSTANTS})


def _rgba(color):
    """A Gdk.RGBA for a Tk colour name or #rrggbb, or None."""
    if not color:
        return None
    rgba = Gdk.RGBA()
    if rgba.parse(str(color)) or rgba.parse(str(color).replace(" ", "")):
        return rgba
    return None


def _gdk_color(color, default):
    ok, c = Gdk.Color.parse(str(color))
    if not ok:
        ok, c = Gdk.Color.parse(str(color).replace(" ", ""))
    if not ok:
        print("pyvcp-gtk: unknown colour %r" % (color,), file=sys.stderr)
        ok, c = Gdk.Color.parse(default)
    return c


def _font_css(font):
    """CSS declarations for a Tk font: ("Helvetica", 20, "bold") or
    "Helvetica 20 bold". A negative size is in pixels, as in Tk."""
    if isinstance(font, str):
        parts = font.split()
        if parts and parts[0].startswith("{"):
            # "{DejaVu Sans} 12"
            end = font.index("}")
            parts = [font[1:end]] + font[end + 1:].split()
    else:
        parts = list(font)
    if not parts:
        return ""
    css = ["font-family: \"%s\";" % parts[0]]
    for p in parts[1:]:
        try:
            size = int(p)
        except (TypeError, ValueError):
            p = str(p).lower()
            if p == "bold":
                css.append("font-weight: bold;")
            elif p == "italic":
                css.append("font-style: italic;")
            continue
        if size < 0:
            css.append("font-size: %dpx;" % -size)
        elif size > 0:
            css.append("font-size: %dpt;" % size)
    return " ".join(css)


_warned = set()


def _warn_once(key, message):
    if key not in _warned:
        _warned.add(key)
        print("pyvcp-gtk: " + message, file=sys.stderr)


# Tk options that pyvcp files commonly pass and that have no GTK
# counterpart worth emulating. They are dropped silently.
_SILENT = {"relief", "bd", "borderwidth", "padx", "pady", "cursor",
           "highlightthickness", "highlightbackground", "highlightcolor",
           "activebackground", "activeforeground", "takefocus",
           "disabledforeground", "selectcolor", "indicatoron",
           "overrelief", "command", "image", "compound"}


class _Style:
    """Applies the Tk look options of one widget (text, font, colours,
    width, anchor) and warns about the rest."""

    def __init__(self, tag, kw):
        self.tag = tag
        self.text = kw.pop("text", None)
        self.font = kw.pop("font", None)
        self.fg = kw.pop("fg", kw.pop("foreground", None))
        self.bg = kw.pop("bg", kw.pop("background", None))
        self.width = kw.pop("width", None)
        self.height = kw.pop("height", None)
        self.anchor = kw.pop("anchor", None)
        self.justify = kw.pop("justify", None)
        self.wraplength = kw.pop("wraplength", None)
        for k in list(kw):
            if k not in _SILENT:
                _warn_once((tag, k), "<%s>: option '%s' is not supported, ignored" % (tag, k))
            del kw[k]

    def css(self, widget, label=None):
        decl = []
        if self.font:
            decl.append(_font_css(self.font))
        if self.fg and _rgba(self.fg):
            decl.append("color: %s;" % _rgba(self.fg).to_string())
        if self.bg and _rgba(self.bg):
            decl.append("background-color: %s; background-image: none;"
                        % _rgba(self.bg).to_string())
        if decl:
            provider = Gtk.CssProvider()
            provider.load_from_data(("* { %s }" % " ".join(decl)).encode())
            widget.get_style_context().add_provider(
                provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            if label is not None and label is not widget:
                label.get_style_context().add_provider(
                    provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    def label(self, label):
        """Width, alignment and wrapping of a Gtk.Label."""
        if self.width:
            label.set_width_chars(int(self.width))
        if self.anchor:
            a = str(self.anchor)
            label.set_xalign(0.0 if "w" in a else 1.0 if "e" in a else 0.5)
        if self.justify:
            label.set_justify({"left": Gtk.Justification.LEFT,
                               "right": Gtk.Justification.RIGHT}.get(
                                   str(self.justify), Gtk.Justification.CENTER))
        if self.wraplength:
            label.set_line_wrap(True)
            label.set_max_width_chars(max(1, int(self.wraplength) // 8))


# Options that are numbers. Tk takes a number given as a string, and
# pyvcp files rely on it: <resolution>"1"</resolution>.
_NUMERIC = {"resolution", "min_", "max_", "initval", "size", "cpr",
            "width", "height", "canvas_width", "canvas_height",
            "bar_width", "bar_height", "majorscale", "minorscale",
            "length", "param_pin", "clear_pin", "scale_pin", "reset_button",
            "increment", "from", "to"}


def _number(v):
    if isinstance(v, str):
        for conv in (int, float):
            try:
                return conv(v)
            except ValueError:
                pass
    return v


def _fmt(format, value):
    try:
        return ("%" + format) % value
    except (TypeError, ValueError):
        return str(value)


# ---------------------------------------------------------------------------
# Elements. Each has .widget (the Gtk.Widget to place, or None for the
# layout directives) and update(comp), called every 100 ms.

class _Element:
    widget = None

    def update(self, comp):
        pass


class _Directive(_Element):
    """A layout directive (<boxfill>, <tablerow>, ...); no widget."""

    def __init__(self, **kw):
        self.__dict__.update(kw)


# -- containers -------------------------------------------------------------

_ANCHOR_H = {"w": Gtk.Align.START, "e": Gtk.Align.END}
_ANCHOR_V = {"n": Gtk.Align.START, "s": Gtk.Align.END}


def _align_for(anchor, fill):
    """halign, valign for a Tk pack anchor and fill."""
    anchor = str(anchor or "center")
    fill = str(fill or "none")
    h = next((v for k, v in _ANCHOR_H.items() if k in anchor), Gtk.Align.CENTER)
    v = next((v for k, v in _ANCHOR_V.items() if k in anchor), Gtk.Align.CENTER)
    if fill in ("x", "both"):
        h = Gtk.Align.FILL
    if fill in ("y", "both"):
        v = Gtk.Align.FILL
    return h, v


def _truthy(v):
    if isinstance(v, str):
        return v.lower() in ("1", "yes", "true", "y")
    return bool(v)


class _Box(_Element):
    def __init__(self, vertical, bd=0, relief="flat"):
        self.vertical = vertical
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL if vertical
                           else Gtk.Orientation.HORIZONTAL)
        self.fill = "x" if vertical else "y"
        self.anchor = "center"
        self.expand = "yes"
        self.widget = _framed(self.box, bd, relief)

    def add(self, child):
        if isinstance(child, _Directive):
            for name in ("fill", "anchor", "expand"):
                if hasattr(child, name):
                    setattr(self, name, getattr(child, name))
            return
        w = child.widget
        if w is None:
            return
        h, v = _align_for(self.anchor, self.fill)
        w.set_halign(h)
        w.set_valign(v)
        # pyvcp's hbox never passes expand to pack; keep that behaviour.
        expand = _truthy(self.expand) if self.vertical else False
        self.box.pack_start(w, expand, True, 0)


def _framed(widget, bd, relief):
    try:
        bd = int(bd)
    except (TypeError, ValueError):
        bd = 0
    relief = str(relief or "flat").lower()
    if relief == "flat" or bd <= 0:
        return widget
    frame = Gtk.Frame()
    frame.set_shadow_type({"sunken": Gtk.ShadowType.IN,
                           "raised": Gtk.ShadowType.OUT,
                           "groove": Gtk.ShadowType.ETCHED_IN,
                           "ridge": Gtk.ShadowType.ETCHED_OUT}.get(
                               relief, Gtk.ShadowType.IN))
    widget.set_border_width(bd)
    frame.add(widget)
    return frame


class _LabelFrame(_Element):
    def __init__(self, tag, kw):
        style = _Style(tag, kw)
        self.widget = Gtk.Frame(label=style.text)
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.widget.add(self.box)
        style.css(self.widget, self.widget.get_label_widget())

    def add(self, child):
        if child.widget is not None:
            self.box.pack_start(child.widget, True, True, 0)


class _Tabs(_Element):
    def __init__(self, names=(), **kw):
        self.names = list(names)
        self.widget = Gtk.Notebook()
        for k in kw:
            _warn_once(("tabs", k), "<tabs>: option '%s' is not supported, ignored" % k)

    def add(self, child):
        if child.widget is None:
            return
        idx = self.widget.get_n_pages()
        name = self.names[idx] if idx < len(self.names) else "Tab-%d" % idx
        self.widget.append_page(child.widget, Gtk.Label(label=name))


class _Table(_Element):
    def __init__(self, flexible_rows=(), flexible_columns=(),
                 uniform_columns="", uniform_rows="", **kw):
        self.widget = Gtk.Grid()
        self.flexible_rows = set(flexible_rows)
        self.flexible_columns = set(flexible_columns)
        # GTK grids can only make all rows or all columns the same size.
        if uniform_columns and len(set(uniform_columns)) == 1:
            self.widget.set_column_homogeneous(True)
        if uniform_rows and len(set(uniform_rows)) == 1:
            self.widget.set_row_homogeneous(True)
        _Style("table", kw)
        self._r = self._c = 0
        self.occupied = set()
        self.span = (1, 1)
        self.sticky = "ne"

    def add(self, child):
        if isinstance(child, _Directive):
            if hasattr(child, "row"):
                self._r += 1
                self._c = 1
            if hasattr(child, "span"):
                self.span = child.span
            if hasattr(child, "sticky"):
                self.sticky = child.sticky
            return
        if child.widget is None:
            return
        r, c = self._r, self._c
        while (r, c) in self.occupied:
            c += 1
        rs, cs = self.span
        w = child.widget
        s = str(self.sticky)
        w.set_halign(Gtk.Align.FILL if ("e" in s and "w" in s) else
                     Gtk.Align.START if "w" in s else
                     Gtk.Align.END if "e" in s else Gtk.Align.CENTER)
        w.set_valign(Gtk.Align.FILL if ("n" in s and "s" in s) else
                     Gtk.Align.START if "n" in s else
                     Gtk.Align.END if "s" in s else Gtk.Align.CENTER)
        if any(ci in self.flexible_columns for ci in range(c, c + cs)):
            w.set_hexpand(True)
        if any(ri in self.flexible_rows for ri in range(r, r + rs)):
            w.set_vexpand(True)
        self.widget.attach(w, c, r, cs, rs)
        for ri in range(r, r + rs):
            for ci in range(c, c + cs):
                self.occupied.add((ri, ci))
        self.span = (1, 1)
        self._c = c + cs


# -- indicators -------------------------------------------------------------

class _Led(_Element):
    def __init__(self, comp, halpin, disable_pin, on_color, off_color,
                 disabled_color, shape, dia):
        self.halpin = halpin
        comp.newpin(halpin, Type.BOOL, Dir.IN)
        self.halpin_disable = None
        if disable_pin:
            self.halpin_disable = halpin + ".disable"
            comp.newpin(self.halpin_disable, Type.BOOL, Dir.IN)
        w = HAL_LED()
        w.set_shape(shape)
        w.set_dia(max(2, int(dia)))
        self.on = _gdk_color(on_color, "green")
        self.off = _gdk_color(off_color, "red")
        self.disabled = _gdk_color(disabled_color, "gray80")
        w.set_color("on", self.on)
        w.set_color("off", self.off)
        w.set_active(False)
        self.widget = w
        self.state = None

    def update(self, comp):
        on = bool(comp[self.halpin])
        disabled = bool(self.halpin_disable and comp[self.halpin_disable])
        state = (on, disabled)
        if state == self.state:
            return
        self.state = state
        w = self.widget
        if disabled:
            w.set_color("on", self.disabled)
            w.set_color("off", self.disabled)
        else:
            w.set_color("on", self.on)
            w.set_color("off", self.off)
        w.set_active(on)


class _Number(_Element):
    def __init__(self, tag, comp, halpin, pintype, format, kw):
        style = _Style(tag, kw)
        self.halpin = halpin
        self.format = format
        comp.newpin(halpin, pintype, Dir.IN)
        self.value = 0
        self.widget = Gtk.Label(label=_fmt(format, self.value))
        style.label(self.widget)
        style.css(self.widget)

    def update(self, comp):
        v = comp[self.halpin]
        if v != self.value:
            self.value = v
            self.widget.set_text(_fmt(self.format, v))


class _Label(_Element):
    def __init__(self, tag, comp, halpin_disable, kw):
        style = _Style(tag, kw)
        self.widget = Gtk.Label(label=str(style.text or ""))
        style.label(self.widget)
        style.css(self.widget)
        self.halpin_disable = halpin_disable
        if halpin_disable:
            comp.newpin(halpin_disable, Type.BOOL, Dir.IN)

    def update(self, comp):
        if self.halpin_disable:
            self.widget.set_sensitive(not comp[self.halpin_disable])


class _MultiLabel(_Label):
    def __init__(self, tag, comp, halpin, disable_pin, legends, initval, kw):
        _Label.__init__(self, tag, comp, None, kw)
        self.legends = list(legends)
        self.halpins = []
        for n, _ in enumerate(self.legends[:6]):
            pin = "%s.legend%d" % (halpin, n)
            comp.newpin(pin, Type.BOOL, Dir.IN)
            self.halpins.append(pin)
        if disable_pin:
            self.halpin_disable = halpin + ".disable"
            comp.newpin(self.halpin_disable, Type.BOOL, Dir.IN)
        val = initval if 0 <= initval < len(self.halpins) else 0
        self.widget.set_text(str(self.legends[val]))
        comp[self.halpins[val]] = 1
        self.pin_index = val

    def update(self, comp):
        _Label.update(self, comp)
        index = -1
        for x, pin in enumerate(self.halpins):
            if comp[pin]:
                index = x
                if index != self.pin_index:
                    break
        if index > -1 and index != self.pin_index:
            for pin in self.halpins:
                comp[pin] = 0
            comp[self.halpins[index]] = 1
            self.widget.set_text(str(self.legends[index]))
            self.pin_index = index


class _Timer(_Element):
    def __init__(self, comp, halpin, kw):
        style = _Style("timer", kw)
        self.reset_pin = halpin + ".reset"
        self.run_pin = halpin + ".run"
        comp.newpin(self.reset_pin, Type.BOOL, Dir.IN)
        comp.newpin(self.run_pin, Type.BOOL, Dir.IN)
        self.resetvalue = self.runvalue = 0
        self.starttime = self.basetime = 0
        self.widget = Gtk.Label(label="00:00:00")
        style.label(self.widget)
        style.css(self.widget)

    def update(self, comp):
        resetvalue = comp[self.reset_pin]
        runvalue = comp[self.run_pin]
        now = time.time()
        if resetvalue != self.resetvalue:
            self.resetvalue = resetvalue
            if resetvalue:
                self.basetime = 0
                self.starttime = now
        if runvalue != self.runvalue:
            self.runvalue = runvalue
            if runvalue:
                self.starttime = now
            else:
                self.basetime += now - self.starttime
        total = self.basetime + (now - self.starttime if runvalue else 0)
        hr = int(total / 3600)
        mn = int((total - hr * 3600) / 60)
        sec = int(total - hr * 3600 - mn * 60)
        self.widget.set_text("%02d:%02d:%02d" % (hr, mn, sec))


class _Bar(_Element):
    def __init__(self, comp, halpin, fillcolor, bgcolor, min_, max_,
                 ranges, format, canvas_width, bar_height):
        self.halpin = halpin
        comp.newpin(halpin, Type.REAL, Dir.IN)
        w = HAL_HBar()
        w.set_property("min", float(min_))
        w.set_property("max", float(max_))
        w.set_property("zero", float(max(min_, min(0.0, max_))))
        try:
            ("%" + format) % 0.0
            w.set_property("text_template", "%" + format)
        except (TypeError, ValueError):
            pass
        w.set_property("z0_border", 1.0)
        w.set_property("z1_border", 1.0)
        w.bg_color = _gdk_color(bgcolor, "grey")
        self.fill = _gdk_color(fillcolor, "green")
        w.z0_color = self.fill
        w.set_property("force_width", max(20, int(canvas_width)))
        w.set_property("force_height", max(10, int(bar_height)))
        self.ranges = [(a, b, _gdk_color(c, "green")) for a, b, c in ranges]
        self.widget = w
        self.value = None

    def update(self, comp):
        v = comp[self.halpin]
        if v == self.value:
            return
        self.value = v
        if self.ranges:
            # like pyvcp: the colour of the first range holding the value
            for start, end, color in self.ranges:
                if start < v <= end:
                    self.widget.z0_color = color
                    break
        self.widget.set_value(v)


class _MeterWidget(HAL_Meter):
    """HAL_Meter that draws pyvcp's free coloured regions instead of
    its three zones that always cover the full scale."""
    __gtype_name__ = "PyVCP_Meter"

    def __init__(self, regions):
        super().__init__()
        self.regions = regions
        self._alpha = 1

    def expose(self, widget, event):
        self._regions_drawn = False
        self._alpha = 1 if self.is_sensitive() else 0.3
        return super().expose(widget, event)

    def draw_zone(self, cr, r, start, stop):
        # HAL_Meter.expose() calls this three times with its own
        # colours; draw the pyvcp regions on the first call only.
        if self._regions_drawn:
            return
        self._regions_drawn = True
        a_delta = math.pi / 6
        a_start = 0.5 * math.pi + a_delta
        a_size = 2 * math.pi - 2 * a_delta
        span = (self.max - self.min) or 1

        def angle(v):
            v = max(self.min, min(self.max, v))
            return a_start + a_size * (v - self.min) / span

        for start_v, end_v, color in self.regions:
            cr.set_source_rgba(color.red_float, color.green_float,
                               color.blue_float, self._alpha)
            HAL_Meter.draw_zone(self, cr, r, angle(start_v), angle(end_v))


class _Meter(_Element):
    def __init__(self, comp, halpin, size, text, subtext, min_, max_,
                 majorscale, minorscale, regions):
        self.halpin = halpin
        comp.newpin(halpin, Type.REAL, Dir.IN)
        w = _MeterWidget([(a, b, _gdk_color(c, "green")) for a, b, c in regions])
        w.set_property("min", float(min_))
        w.set_property("max", float(max_))
        major = float(majorscale) if majorscale and majorscale > 0 \
            else ((max_ - min_) / 10.0 or 1.0)
        w.set_property("majorscale", major)
        # pyvcp draws no minor ticks unless asked for; HAL_Meter always
        # does, so put them on the major ticks
        w.set_property("minorscale", float(minorscale)
                       if minorscale and minorscale > 0 else major)
        w.set_property("label", str(text or ""))
        w.set_property("sublabel", str(subtext or ""))
        w.set_property("text_template", "%g")
        w.set_size_request(int(size), int(size))
        self.widget = w
        self.value = None

    def update(self, comp):
        v = comp[self.halpin]
        if v != self.value:
            self.value = v
            self.widget.set_value(v)


class _ImageSwitch(_Element):
    def __init__(self, comp, halpin, pintype, images, named, kw):
        _Style("image", kw)
        if isinstance(images, str):
            images = images.split()
        self.images = images
        self.named = named
        self.halpin = halpin
        comp.newpin(halpin, pintype, Dir.IN)
        self.widget = Gtk.Image()
        self.last = None

    def update(self, comp):
        v = comp[self.halpin]
        if v == self.last:
            return
        self.last = v
        try:
            pixbuf = self.named[self.images[int(v)]]
        except (IndexError, KeyError):
            print("Unknown image #%d on %s" % (v, self.halpin), file=sys.stderr)
            return
        self.widget.set_from_pixbuf(pixbuf)


# -- controls ---------------------------------------------------------------

class _Button(_Element):
    def __init__(self, comp, halpin, disable_pin, kw):
        style = _Style("button", kw)
        self.comp = comp
        self.halpin = halpin
        comp.newpin(halpin, Type.BOOL, Dir.OUT)
        self.halpin_disable = None
        if disable_pin:
            self.halpin_disable = halpin + ".disable"
            comp.newpin(self.halpin_disable, Type.BOOL, Dir.IN)
        w = HAL_Button(label=str(style.text or ""))
        child = w.get_child()
        if isinstance(child, Gtk.Label):
            style.label(child)
        style.css(w, child)
        w.connect("pressed", self._set, 1)
        w.connect("released", self._set, 0)
        self.widget = w

    def _set(self, w, value):
        self.comp[self.halpin] = value

    def update(self, comp):
        if self.halpin_disable:
            disabled = bool(comp[self.halpin_disable])
            if disabled and self.widget.is_sensitive():
                # the button may be held down; let go of the output
                comp[self.halpin] = 0
            self.widget.set_sensitive(not disabled)


class _CheckButton(_Element):
    def __init__(self, comp, halpin, initval, kw):
        style = _Style("checkbutton", kw)
        self.halpin = halpin
        comp.newpin(halpin, Type.BOOL, Dir.OUT)
        self.changepin = halpin + ".changepin"
        comp.newpin(self.changepin, Type.BOOL, Dir.IN)
        comp[self.changepin] = 0
        w = HAL_CheckButton(label=str(style.text or ""))
        style.css(w, w.get_child())
        w.set_active(initval >= 0.5)
        self.widget = w
        self.reset = False

    def update(self, comp):
        if comp[self.changepin] and not self.reset:
            self.widget.set_active(not self.widget.get_active())
            self.reset = True
            comp[self.changepin] = 0
        if not comp[self.changepin] and self.reset:
            self.reset = False
            comp[self.changepin] = 0
        comp[self.halpin] = self.widget.get_active()


class _RadioButton(_Element):
    def __init__(self, comp, halpin, initval, orient, choices, kw):
        _Style("radiobutton", kw)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL if orient is None
                      else Gtk.Orientation.HORIZONTAL)
        frame = Gtk.Frame()
        frame.set_shadow_type(Gtk.ShadowType.ETCHED_IN)
        frame.add(box)
        self.widget = frame
        self.buttons = []
        self.halpins = []
        group = None
        for c in choices:
            b = HAL_RadioButton(label=str(c))
            if group is not None:
                b.join_group(group)
            group = group or b
            box.pack_start(b, False, False, 0)
            pin = "%s.%s" % (halpin, c)
            comp.newpin(pin, Type.BOOL, Dir.OUT)
            self.halpins.append(pin)
            self.buttons.append(b)
        if self.buttons:
            self.buttons[initval].set_active(True)
            comp[self.halpins[initval]] = 1
        self.selected = initval

    def update(self, comp):
        index = next((i for i, b in enumerate(self.buttons) if b.get_active()),
                     self.selected)
        if index != self.selected:
            for pin in self.halpins:
                comp[pin] = 0
            comp[self.halpins[index]] = 1
            self.selected = index


def _digits(step):
    """Decimals needed to show multiples of step."""
    s = ("%.10f" % abs(step)).rstrip("0")
    return len(s.split(".")[1]) if "." in s else 0


class _Scale(_Element):
    def __init__(self, comp, halpin, halparam, resolution, min_, max_,
                 initval, orient, length, kw):
        if "label" in kw:       # Tk's name for a scale's title
            kw.setdefault("text", kw.pop("label"))
        style = _Style("scale", kw)
        self.halpin = halpin
        self.halparam = halparam
        if halparam:
            comp.newpin(halparam, Type.REAL, Dir.IN)
        comp.newpin(halpin + "-i", Type.SINT, Dir.OUT)
        comp.newpin(halpin + "-f", Type.REAL, Dir.OUT)
        value = min(max(initval, min_), max_)
        adj = Gtk.Adjustment(value=value, lower=min_, upper=max_,
                             step_increment=resolution,
                             page_increment=resolution * 10, page_size=0)
        horizontal = str(orient).lower() == "horizontal"
        w = (HAL_HScale if horizontal else HAL_VScale)(
            orientation=Gtk.Orientation.HORIZONTAL if horizontal
            else Gtk.Orientation.VERTICAL, adjustment=adj)
        w.set_digits(_digits(resolution))
        w.set_round_digits(_digits(resolution))
        w.set_value_pos(Gtk.PositionType.TOP if horizontal else Gtk.PositionType.LEFT)
        self.resolution = resolution
        # snap to the resolution, as Tk's scale does
        w.connect("change-value", self._snap)
        length = int(length or 100)
        if horizontal:
            w.set_size_request(length, -1)
        else:
            w.set_size_request(-1, length)
        style.css(w)
        if style.text:
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            box.pack_start(Gtk.Label(label=str(style.text)), False, False, 0)
            box.pack_start(w, True, True, 0)
            self.widget = box
        else:
            self.widget = w
        self.scale = w
        if halparam:
            comp[halparam] = value
        self.oldinit = value

    def _snap(self, w, scroll, value):
        lower = w.get_adjustment().get_lower()
        steps = round((value - lower) / self.resolution)
        w.get_adjustment().set_value(lower + steps * self.resolution)
        return True

    def update(self, comp):
        v = self.scale.get_value()
        comp[self.halpin + "-f"] = v
        comp[self.halpin + "-i"] = int(v)
        if self.halparam:
            init = comp[self.halparam]
            if init != self.oldinit:
                self.scale.set_value(init)
                self.oldinit = init


class _SpinBox(_Element):
    def __init__(self, comp, halpin, halparam, min_, max_, initval,
                 resolution, format, reset_button, reset_text, kw):
        increment = kw.pop("increment", resolution)
        min_ = kw.pop("from", min_)
        max_ = kw.pop("to", max_)
        kw.pop("format", None)
        style = _Style("spinbox", kw)
        self.halpin = halpin
        self.halparam = halparam
        if halparam:
            comp.newpin(halparam, Type.REAL, Dir.IN)
        self.initval = min(max(initval, min_), max_)
        if halparam:
            comp[halparam] = self.initval
        self.oldinit = self.initval
        comp.newpin(halpin, Type.REAL, Dir.OUT)
        adj = Gtk.Adjustment(value=self.initval, lower=min_, upper=max_,
                             step_increment=increment,
                             page_increment=increment * 10, page_size=0)
        sb = HAL_SpinButton(adjustment=adj)
        try:
            decimals = int(str(format).rstrip("fdg").split(".")[1])
        except (IndexError, ValueError):
            decimals = _digits(increment)
        sb.set_digits(decimals)
        sb.set_numeric(True)
        if style.width:
            sb.set_width_chars(int(style.width))
        style.css(sb)
        self.sb = sb
        if reset_button:
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            box.pack_start(sb, True, True, 0)
            b = Gtk.Button(label=str(reset_text))
            b.connect("clicked", lambda *a: sb.set_value(self.initval))
            box.pack_start(b, False, True, 0)
            self.widget = box
        else:
            self.widget = sb

    def update(self, comp):
        comp[self.halpin] = self.sb.get_value()
        if self.halparam:
            init = comp[self.halparam]
            if init != self.oldinit:
                self.oldinit = init
                self.sb.set_value(init)


class _Shown(float):
    """A float that prints with a fixed number of decimals."""
    def __new__(cls, value, decimals):
        self = float.__new__(cls, value)
        self.decimals = decimals
        return self

    def __str__(self):
        return "%.*f" % (self.decimals, self)


class _DialWidget(Hal_Dial):
    """Hal_Dial that keeps the size it was given. The centre shows
    _delta_scaled (count_type_shown 2), which _Dial sets to the output
    value; Hal_Dial's own arithmetic on it is overwritten right away."""
    __gtype_name__ = "PyVCP_Dial"

    def __init__(self, size):
        super().__init__()
        self.set_size_request(size, size)
        self.count_type_shown = 2

    def expose(self, widget, event):
        # Hal_Dial.expose() without its set_size_request(100, 100)
        self.alpha = 1 if self.is_sensitive() else 0.3
        self.draw(widget.get_property("window").cairo_create())
        return False


class _Dial(_Element):
    def __init__(self, comp, halpin, halparam, size, cpr, dialcolor, text,
                 initval, resolution, min_, max_, kw):
        _Style("dial", kw)
        self.comp = comp
        self.halpin = halpin
        self.halparam = halparam
        comp.newpin(halparam, Type.REAL, Dir.IN)
        comp.newpin(halpin, Type.REAL, Dir.OUT)
        comp[halparam] = initval
        self.oldparam = initval
        self.min_, self.max_ = min_, max_
        self.orig = initval
        w = _DialWidget(int(size))
        w.cpr = max(25, min(360, int(cpr)))
        w.label = str(text) if text is not None else ""
        w.scale = float(resolution)
        if dialcolor:
            w.center_color = _gdk_color(dialcolor, "grey")
        w.connect("count_changed", self._count_changed)
        w.connect("scale_changed", self._scale_changed)
        # shift-click puts the original value back, as in pyvcp
        w.connect("button-press-event", self._press)
        self.widget = w
        self._set_out(initval)

    def _set_out(self, value):
        w = self.widget
        w._count = int(round(value / w.scale))
        self.out = w._count * w.scale
        if self.min_ is not None and self.out < self.min_:
            self.out = self.min_
        if self.max_ is not None and self.out > self.max_:
            self.out = self.max_
        w._delta_scaled = _Shown(self.out, _digits(w.scale))
        w.queue_draw()

    def _count_changed(self, w, count, scale, delta):
        self._set_out(count * scale)

    def _scale_changed(self, w, count, scale):
        # keep the output, change only the step
        self._set_out(self.out)

    def _press(self, w, event):
        if event.button == 1 and event.state & Gdk.ModifierType.SHIFT_MASK:
            self._set_out(self.orig)
            return True
        return False

    def update(self, comp):
        comp[self.halpin] = self.out
        v = comp[self.halparam]
        if v != self.oldparam:
            self.oldparam = v
            self._set_out(v)


class _JogWheel(_Element):
    def __init__(self, comp, halpin, base, text, clear_pin, scale_pin,
                 size, cpr, kw):
        _Style("jogwheel", kw)
        self.comp = comp
        self.halpin = halpin
        comp.newpin(halpin, Type.REAL, Dir.OUT)
        comp[halpin] = 0
        self.clear_pin = base + ".reset" if clear_pin else None
        self.scale_pin = base + ".scale" if scale_pin else None
        if self.clear_pin:
            comp.newpin(self.clear_pin, Type.BOOL, Dir.IN)
        if self.scale_pin:
            comp.newpin(self.scale_pin, Type.REAL, Dir.IN)
        w = JogWheel()
        # JogWheel quits the main loop when destroyed; not ours to quit.
        try:
            w.disconnect_by_func(Gtk.main_quit)
        except (TypeError, ValueError):
            pass
        w.set_property("size", max(100, min(500, int(size))))
        w.set_property("cpr", max(25, min(360, int(cpr))))
        w.set_property("label", str(text) if text is not None else "")
        w.set_property("show_counts", False)
        if self.clear_pin:
            w._show_scaled_value = True
        self.widget = w
        self.count = 0
        self.scale = 0.0
        self.dro = 0.0
        self._show_dro()
        w.connect("button-press-event", self._press)

    def _show_dro(self):
        self.widget._scaled_value = "%.4f" % self.dro
        self.widget.queue_draw()

    def _press(self, w, event):
        if event.button == 1 and event.state & Gdk.ModifierType.SHIFT_MASK:
            self.dro = 0.0
            self._show_dro()

    def update(self, comp):
        counts = self.widget._counts
        if counts != self.count:
            self.dro += (counts - self.count) * self.scale
            self.count = counts
            comp[self.halpin] = counts
            self._show_dro()
        if self.scale_pin:
            self.scale = comp[self.scale_pin]
        if self.clear_pin and comp[self.clear_pin]:
            self.dro = 0.0
            self._show_dro()


# ---------------------------------------------------------------------------

class Panel:
    """A pyVCP panel built from an XML file.

    comp     -- the hal.component the pins are created in
    filename -- the pyVCP XML file
    window   -- the Gtk.Window holding the panel, for <title>; optional

    panel.widget is the panel's top level Gtk.Widget.
    """

    _ELEMENTS = None

    def __init__(self, comp, filename, window=None):
        self.comp = comp
        self.window = window
        self.elements = []
        self.images = {}
        self.counters = {}
        self._timer = None
        self.root = _Box(True)
        self.root.fill = "both"
        self.widget = self.root.widget
        self._load(filename, self.root)

    # -- public ------------------------------------------------------------

    def start(self, interval=100):
        """Start updating pins and widgets every interval ms."""
        if self._timer is None:
            self.update()
            self._timer = GLib.timeout_add(interval, self._tick)

    def stop(self):
        if self._timer is not None:
            GLib.source_remove(self._timer)
            self._timer = None

    def update(self):
        for e in self.elements:
            e.update(self.comp)

    # -- parsing -----------------------------------------------------------

    def _tick(self):
        self.update()
        return True

    def _load(self, filename, parent):
        try:
            doc = xml.dom.minidom.parse(filename)
        except (OSError, xml.parsers.expat.ExpatError) as detail:
            raise PanelError("could not open %s: %s" % (filename, detail))
        root = next((e for e in doc.childNodes
                     if e.nodeType == e.ELEMENT_NODE and e.localName == "pyvcp"),
                    None)
        if root is None:
            raise PanelError("no pyvcp element in %s" % filename)
        self._children(root, parent)

    def _children(self, node, parent):
        for e in node.childNodes:
            if e.nodeType != e.ELEMENT_NODE or e.nodeName not in _BUILDERS:
                continue
            params = self._params(e)
            for k in _NUMERIC.intersection(params):
                params[k] = _number(params[k])
            try:
                elem = _BUILDERS[e.nodeName](self, parent, dict(params))
            except PanelError:
                raise
            except Exception as detail:
                raise PanelError("Error constructing %s(%s):\n%s"
                                 % (e.nodeName, params, detail))
            self.elements.append(elem)
            if hasattr(elem, "add"):
                self._children(e, elem)
            parent.add(elem)

    @staticmethod
    def _params(node):
        out = {}
        for k, v in node.attributes.items():
            if v and v[0] in "{[(\"'":
                v = _eval(v)
            else:
                try:
                    v = int(v)
                except ValueError:
                    try:
                        v = float(v)
                    except ValueError:
                        pass
            out[str(k)] = v
        for e in node.childNodes:
            if e.nodeType == e.ELEMENT_NODE and e.nodeName not in _BUILDERS:
                try:
                    out[str(e.nodeName)] = _eval(e.childNodes[0].nodeValue)
                except Exception as detail:
                    raise PanelError("Error evaluating xml file:\nWidget %s, "
                                     "Property %s\n%s: %s"
                                     % (node.nodeName, e.nodeName,
                                        type(detail).__name__, detail))
        return out

    def _next(self, counter):
        """pyvcp's per-widget-type automatic numbering."""
        n = self.counters.get(counter, 0)
        self.counters[counter] = n + 1
        return n

    def _peek(self, counter):
        return self.counters.get(counter, 0)


# -- element builders: (panel, parent, params) -> element ---------------------
# Pin names, and when each counter advances, follow pyvcp_widgets.py.

def _b_vbox(p, parent, kw):
    return _Box(True, kw.get("bd", 0), kw.get("relief", "flat"))


def _b_hbox(p, parent, kw):
    return _Box(False, kw.get("bd", 0), kw.get("relief", "flat"))


def _b_led(p, parent, kw, rect=False):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "led.%d" % p._next("led")
    on = kw.pop("on_color", "green")
    off = kw.pop("off_color", "red")
    dis = kw.pop("disabled_color", "gray80")
    disable_pin = kw.pop("disable_pin", False)
    if rect:
        width = float(kw.pop("width", 30))
        height = float(kw.pop("height", 10))
        # HAL_LED's bars are 5:1; size from the height, at most 1/5 of the width
        e = _Led(p.comp, halpin, disable_pin, on, off, dis, 3,
                 min(height, width / 5.0))
        e.widget.set_size_request(int(width), int(height))
    else:
        size = float(kw.pop("size", 20))
        e = _Led(p.comp, halpin, disable_pin, on, off, dis, 0, size / 2.0)
    _Style("rectled" if rect else "led", kw)
    return e


def _b_number(p, parent, kw, pintype=Type.REAL, default_format="2.1f"):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        # number, sint, uint and the images share pyvcp's number counter
        halpin = "number.%d" % p._next("number")
    fmt = kw.pop("format", default_format)
    tag = {Type.REAL: "number", Type.SINT: "s32", Type.UINT: "u32"}[pintype]
    return _Number(tag, p.comp, halpin, pintype, fmt, kw)


def _b_label(p, parent, kw):
    halpin = kw.pop("halpin", None)
    disable_pin = kw.pop("disable_pin", False)
    pin = None
    if disable_pin:
        if halpin is None:
            halpin = "label.%d" % p._next("label")
        pin = halpin + ".disable"
    return _Label("label", p.comp, pin, kw)


def _b_multilabel(p, parent, kw):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "multilabel.%d" % p._next("multilabel")
    return _MultiLabel("multilabel", p.comp, halpin,
                       kw.pop("disable_pin", False), kw.pop("legends", []),
                       kw.pop("initval", 0), kw)


def _b_timer(p, parent, kw):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "timer.%d" % p._next("timer")
    return _Timer(p.comp, halpin, kw)


def _b_bar(p, parent, kw):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "bar.%d" % p._next("bar")
    ranges = [kw.pop(r) for r in ("range1", "range2", "range3") if r in kw]
    if len(ranges) != 3:
        ranges = []     # pyvcp only uses the ranges when all three are given
    e = _Bar(p.comp, halpin, kw.pop("fillcolor", "green"),
             kw.pop("bgcolor", "grey"), kw.pop("min_", 0.0),
             kw.pop("max_", 100.0), ranges, kw.pop("format", "3.1f"),
             kw.pop("canvas_width", 200), kw.pop("bar_height", 30))
    for k in ("canvas_height", "bar_width"):
        kw.pop(k, None)
    _Style("bar", kw)
    return e


def _b_meter(p, parent, kw):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "meter.%d.value" % p._next("meter")
    regions = [kw.pop(r) for r in ("region1", "region2", "region3") if r in kw]
    e = _Meter(p.comp, halpin, kw.pop("size", 200), kw.pop("text", None),
               kw.pop("subtext", None), kw.pop("min_", 0), kw.pop("max_", 100),
               kw.pop("majorscale", None), kw.pop("minorscale", None), regions)
    _Style("meter", kw)
    return e


def _b_button(p, parent, kw):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "button.%d" % p._next("button")
    return _Button(p.comp, halpin, kw.pop("disable_pin", False), kw)


def _b_checkbutton(p, parent, kw):
    halpin = kw.pop("halpin", None)
    n = p._next("checkbutton")
    if halpin is None:
        halpin = "checkbutton.%d" % n
    return _CheckButton(p.comp, halpin, kw.pop("initval", 0), kw)


def _b_radiobutton(p, parent, kw):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "radiobutton.%d" % p._next("radiobutton")
    return _RadioButton(p.comp, halpin, kw.pop("initval", 0),
                        kw.pop("orient", None), kw.pop("choices", []), kw)


def _b_scale(p, parent, kw):
    n = p._next("scale")
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "scale.%d" % n
    halparam = kw.pop("halparam", None)
    if kw.pop("param_pin", 0) == 1:
        if halparam is None:
            halparam = "scale.%d.param_pin" % n
    else:
        halparam = None
    return _Scale(p.comp, halpin, halparam, kw.pop("resolution", 1),
                  kw.pop("min_", 0), kw.pop("max_", 10), kw.pop("initval", 0),
                  kw.pop("orient", "vertical"), kw.pop("length", None), kw)


def _b_spinbox(p, parent, kw):
    n = p._next("spinbox")
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "spinbox.%d" % n
    halparam = kw.pop("halparam", None)
    if kw.pop("param_pin", 0) == 1:
        if halparam is None:
            halparam = "spinbox.%d.param_pin" % n
    else:
        halparam = None
    return _SpinBox(p.comp, halpin, halparam, kw.pop("min_", 0),
                    kw.pop("max_", 100), kw.pop("initval", 0),
                    kw.pop("resolution", 1), kw.pop("format", "2.1f"),
                    kw.pop("reset_button", 0), kw.pop("reset_text", "reset"), kw)


def _b_dial(p, parent, kw):
    n = p._next("dial")
    halpin = kw.pop("halpin", None)
    if halpin is None:
        halpin = "dial.%d.out" % n
    halparam = kw.pop("halparam", None)
    if halparam is None:
        # pyvcp always creates the dial's param pin
        halparam = "dial.%d.param_pin" % n
    kw.pop("param_pin", None)
    for k in ("edgecolor", "dotcolor"):
        kw.pop(k, None)
    return _Dial(p.comp, halpin, halparam, kw.pop("size", 200),
                 kw.pop("cpr", 40), kw.pop("dialcolor", ""), kw.pop("text", None),
                 kw.pop("initval", 0), kw.pop("resolution", 0.1),
                 kw.pop("min_", None), kw.pop("max_", None), kw)


def _b_jogwheel(p, parent, kw):
    halpin = kw.pop("halpin", None)
    if halpin is None:
        base = "jogwheel.%d" % p._next("jogwheel")
        halpin = base + ".count"
    else:
        base = halpin
    for k in ("fillcolor", "bgcolor"):
        kw.pop(k, None)
    return _JogWheel(p.comp, halpin, base, kw.pop("text", None),
                     kw.pop("clear_pin", 0), kw.pop("scale_pin", 0),
                     kw.pop("size", 200), kw.pop("cpr", 40), kw)


def _b_labelframe(p, parent, kw):
    return _LabelFrame("labelframe", kw)


def _b_tabs(p, parent, kw):
    return _Tabs(**kw)


def _b_table(p, parent, kw):
    return _Table(**kw)


def _b_include(p, parent, kw):
    src = kw.pop("src")
    prefix = kw.pop("prefix", None)
    box = _Box(True)
    box.fill = kw.pop("fill", "both")
    box.anchor = kw.pop("anchor", "center")
    box.expand = kw.pop("expand", "yes")
    _Style("include", kw)
    if prefix is not None:
        old = p.comp.getprefix()
        p.comp.setprefix(prefix)
    try:
        p._load(src, box)
    finally:
        if prefix is not None:
            p.comp.setprefix(old)
    # the included file's children are already in the box
    return _Included(box)


class _Included(_Element):
    def __init__(self, box):
        self.widget = box.widget


def _b_title(p, parent, kw):
    if p.window is not None:
        p.window.set_title(str(kw["title"]))
    return _Element()


def _b_ignored(p, parent, kw):
    return _Element()


def _b_image(p, parent, kw):
    name = kw.pop("name")
    path = kw.pop("file", None)
    if path is None:
        _warn_once(("image", name), "<image name=%r>: only file= images are supported" % name)
        return _Element()
    p.images[name] = GdkPixbuf.Pixbuf.new_from_file(str(path))
    return _Element()


def _b_image_switch(pintype):
    def build(p, parent, kw):
        halpin = kw.pop("halpin", None)
        if halpin is None:
            halpin = "number.%d" % p._next("number")
        return _ImageSwitch(p.comp, halpin, pintype, kw.pop("images"), p.images, kw)
    return build


def _directive(**fixed):
    def build(p, parent, kw):
        d = _Directive(**fixed)
        if "rows" in kw or "columns" in kw:
            d.span = (kw.get("rows", 1), kw.get("columns", 1))
        for k in ("fill", "anchor", "expand", "sticky"):
            if k in kw:
                setattr(d, k, kw[k])
        return d
    return build


_BUILDERS = {
    "vbox": _b_vbox, "hbox": _b_hbox,
    "boxfill": _directive(), "boxanchor": _directive(), "boxexpand": _directive(),
    "labelframe": _b_labelframe, "tabs": _b_tabs,
    "table": _b_table, "tablerow": _directive(row=True),
    "tablespan": _directive(), "tablesticky": _directive(),
    "include": _b_include,
    "led": _b_led, "rectled": lambda p, par, kw: _b_led(p, par, kw, rect=True),
    "number": _b_number,
    "s32": lambda p, par, kw: _b_number(p, par, kw, Type.SINT, "d"),
    "sint": lambda p, par, kw: _b_number(p, par, kw, Type.SINT, "d"),
    "u32": lambda p, par, kw: _b_number(p, par, kw, Type.UINT, "d"),
    "uint": lambda p, par, kw: _b_number(p, par, kw, Type.UINT, "d"),
    "label": _b_label, "multilabel": _b_multilabel, "timer": _b_timer,
    "bar": _b_bar, "meter": _b_meter,
    "button": _b_button, "checkbutton": _b_checkbutton,
    "radiobutton": _b_radiobutton,
    "scale": _b_scale, "spinbox": _b_spinbox,
    "dial": _b_dial, "jogwheel": _b_jogwheel,
    "title": _b_title, "axisoptions": _b_ignored, "option": _b_ignored,
    "image": _b_image,
    "image_bool": _b_image_switch(Type.BOOL), "image_bit": _b_image_switch(Type.BOOL),
    "image_uint": _b_image_switch(Type.UINT), "image_u32": _b_image_switch(Type.UINT),
}
