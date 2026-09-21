//    This is a component of AXIS, a front-end for LinuxCNC
//    Copyright 2004, 2005, 2006 Jeff Epler <jepler@unpythonic.net> and
//    Chris Radek <chris@timeguy.com>
//    Copyright 2026 B.Stultiens
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

// What is left of the old Python extension module: the INI file reader and
// the AXIS backplot. Neither talks to task over a transport -- the reader
// parses a file, the backplot draws -- so neither moved when stat, command
// and error_channel did.
//
// They are still written against the Python C API. linuxcnc_py.cc builds
// the module with pybind11 and calls emcRegisterLegacy() to put these two
// into it, so `linuxcnc` stays one module with one name.
//
// The backplot's position logger is the one piece that did have to change:
// it used to read the NML status buffer straight from its own thread, and
// now samples linuxcnc::Status::live() instead.

#define PY_SSIZE_T_CLEAN
#include <stdio.h>
#include <stdlib.h>
#include <Python.h>
#include <structseq.h>
#include <pthread.h>
#include <structmember.h>
#include <pybind11/pybind11.h>
#include "config.h"
#include "rcs_status.hh"
#include "nml_intf/emc.hh"
#include "nml_intf/emc_nml.hh"
#include "nml_intf/debugflags.h"
#include <kinematics.h>
#include <inifile.hh>
#include "timeutil.hh"
#include <sys/types.h>
#include <unistd.h>

#include "tooldata/tooldata.hh"
#include "ws_client/emc_session.hh"
#include "emcmodule.hh"

#include <cmath>
#include <algorithm>

using namespace linuxcnc;

// Set by emcRegisterLegacy(): the module object and its error type, which
// the INI reader raises.
static PyObject *m = NULL, *error = NULL;

struct pyIniFile {
    PyObject_HEAD
    // The 'inifile' member is a pointer because we don't
    // have C++ constructor semantics here
    std::string *inifile;
};



static int Ini_init(pyIniFile *self, PyObject *a, PyObject * /*k*/) {
    const char *inifile = NULL;
    if(!PyArg_ParseTuple(a, "s", &inifile)) return -1;

    if(!inifile)
        return -1;

    self->inifile = new std::string(inifile);

    IniFile ini(self->inifile); // Test open (will parse and cache the file)
    if (!ini) {
        PyErr_Format( error, "inifile.open(%s) failed", inifile);
        return -1;
    }
    return 0;
}

//
// PyBool linuxcnc.ini.hasvariable(string:section, string:variable [, int:num])
//
// Find [section]variable and return true if found. The 'section' may be an
// empty string and the first occurrence of 'variable' in any section is
// searched.
//
static PyObject *Ini_has_variable(pyIniFile *self, PyObject *args)
{
    const char *sect, *var;
    int num = 1;
    if(!PyArg_ParseTuple(args, "ss|i:hasvariable", &sect, &var, &num))
        return NULL;

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    return PyBool_FromLong((long)ini.hasVariable(num, var, sect));
}

//
// PyBool linuxcnc.ini.hassection(string:section)
//
// Find [section] and return true if found.
//
static PyObject *Ini_has_section(pyIniFile *self, PyObject *args)
{
    const char *sect;
    if(!PyArg_ParseTuple(args, "s:hassection", &sect))
        return NULL;

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    return PyBool_FromLong((long)ini.hasSection(sect));
}

//
// The prototype of PyArg_ParseTupleAndKeywords() was originally using char**
// for the keyword array. This is obviously problematic and was changed in 3.13
// to const. C++ will generate an error when demoting const to non-const
// pointers. We need to support multiple python versions and make this
// patchwork to support them all.
#if PY_VERSION_HEX >= 0x030d00f0	// 3.13
#define PARSE_KW_CONST const
#else
#define PARSE_KW_CONST
#endif

static PARSE_KW_CONST char kw_empty[] = "";
static PARSE_KW_CONST char kw_fallback[] = "fallback";
static PARSE_KW_CONST char *kw_eeefn[] = { kw_empty, kw_empty, kw_empty, kw_fallback, NULL };
static PARSE_KW_CONST char *kw_efn[]   = { kw_empty, kw_fallback, NULL };

#undef PARSE_KW_CONST

//
// PyBool|None linuxcnc.ini.getbool(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert it to boolean. Valid boolean values are
// (case insensitive) {true, yes, on, 1} and {false, no, off, 0}.
// The optional named argument fallback= defines the object to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_bool(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getbool", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findBool(num, var, sect))
        return PyBool_FromLong(*v);

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyLong|None linuxcnc.ini.getsint(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert it to a signed integer.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_sint(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getsint", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findSInt(num, var, sect))
        return PyLong_FromLongLong(*v);

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyLong|None linuxcnc.ini.getuint(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert it to an unsigned integer.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_uint(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getuint", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findUInt(num, var, sect))
        return PyLong_FromUnsignedLongLong(*v);

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyFloat|None linuxcnc.ini.getreal(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert it to an unsigned integer.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_real(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getreal", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findReal(num, var, sect))
        return PyFloat_FromDouble(*v);

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyString|None linuxcnc.ini.getstring(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert it to a string.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_string(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getstring", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findString(num, var, sect))
        return PyUnicode_FromString(v->c_str());

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyList(PyTuple(variable,value)) linuxcnc.ini.getsection([string:section])
//
// Returns a list of tuples containing variable name and value from the named
// section or all variables from all sections if no section given.
//
static PyObject *Ini_get_variables(pyIniFile *self, PyObject *args)
{
    const char *sect = "";
    if(!PyArg_ParseTuple(args, "|s:getvariables", &sect)) return NULL;

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    PyObject *list = PyList_New(0);
    if(!list)
        return NULL;

    for(auto const &v : ini.findVariables(sect)) {
        PyObject *var = PyUnicode_FromString(v.first.c_str());
        if(!var) {
            Py_DECREF(list);
            return NULL;
        }
        PyObject *val = PyUnicode_FromString(v.second.c_str());
        if(!val) {
            Py_DECREF(var);
            Py_DECREF(list);
            return NULL;
        }
        PyObject *tup = PyTuple_New(2);
        if(!tup) {
            Py_DECREF(val);
            Py_DECREF(var);
            Py_DECREF(list);
            return NULL;
        }
        PyTuple_SET_ITEM(tup, 0, var);
        PyTuple_SET_ITEM(tup, 1, val);
        PyList_Append(list, tup);
    }
    return list;
}

//
// PyList linuxcnc.ini.sections()
//
// Returns a list of section names from the ini-file.
//
static PyObject *Ini_get_sections(pyIniFile *self, PyObject *)
{
    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    PyObject *list = PyList_New(0);
    if(!list)
        return NULL;

    for(auto const &v : ini.findSections()) {
        PyObject *val = PyUnicode_FromString(v.c_str());
        if(!val) {
            Py_DECREF(list);
            return NULL;
        }
        PyList_Append(list, val);
    }
    return list;
}

//
// PyList linuxcnc.ini.findall(string:section [, string:variable])
//
// Return a list of [section]variable entries where the variable name is the
// same for all entries found. If the section string is empty (''), then all
// variables of that name are returned.
//
static PyObject *Ini_findall(pyIniFile *self, PyObject *args) {
    const char *sect;
    const char *var = "";
    int num = 1;
    if(!PyArg_ParseTuple(args, "s|s:findall", &sect, &var)) return NULL;

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    PyObject *result = PyList_New(0);
    if(!result)
        return NULL;

    while(auto const &out = ini.findString(num, var, sect)) {
        PyList_Append(result, PyUnicode_FromString(out.value().c_str()));
        num++;
    }
    return result;
}

//
// PyTuple(filename, lineno) linuxcnc.ini.lineof(string:section, string:variable [, int:num])
//
// Return a (filname,linenr) tuple containing the filename and line number of
// the [section]variable. Optionally you can request the num'th instance of the
// variable.
//
static PyObject *Ini_lineof(pyIniFile *self, PyObject *args) {
    const char *sect;
    const char *var = "";
    int num = 1;
    if(!PyArg_ParseTuple(args, "ss|i:lineof", &sect, &var, &num)) return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    PyObject *result = PyTuple_New(2);
    if(!result)
        return NULL;

    auto v = ini.lineOf(num, var, sect);
    if(v.second < 0) {
        // Set to (None, None)
        PyTuple_SET_ITEM(result, 0, Py_None);
        PyTuple_SET_ITEM(result, 1, Py_None);
    } else {
        PyObject *fname = PyUnicode_FromString(v.first.c_str());
        if(!fname) {
            Py_DECREF(result);
            return NULL;
        }
        PyObject *lineno = PyLong_FromLong(v.second);
        if(!lineno) {
            Py_DECREF(fname);
            Py_DECREF(result);
            return NULL;
        }
        PyTuple_SET_ITEM(result, 0, fname);
        PyTuple_SET_ITEM(result, 1, lineno);
    }
    return result;
}

//
// PyFloat|None linuxcnc.ini.maplinearunits(string:enumstr [, fallback=val])
//
// Use argument enumstr and convert the enumeration to its numerical value.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_map_linearunits(pyIniFile * /*self*/, PyObject *args, PyObject *kwargs)
{
    const char *str;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "s|$O:maplinearunits", kw_efn, &str, &def))
        return NULL;

    if(auto v = IniFile::mapLinearUnits(str))
        return PyFloat_FromDouble(*v);

    // Not found, a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyFloat|None linuxcnc.ini.mapangularunits(string:enumstr [, fallback=val])
//
// Use argument enumstr and convert the enumeration to its numerical value.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_map_angularunits(pyIniFile * /*self*/, PyObject *args, PyObject *kwargs)
{
    const char *str;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "s|$O:mapangularunits", kw_efn, &str, &def))
        return NULL;

    if(auto v = IniFile::mapAngularUnits(str))
        return PyFloat_FromDouble(*v);

    // Not found, a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyFloat|None linuxcnc.ini.mapjointtype(string:enumstr [, fallback=val])
//
// Use argument enumstr and convert the enumeration to its numerical value.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_map_jointtype(pyIniFile * /*self*/, PyObject *args, PyObject *kwargs)
{
    const char *str;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "s|$O:mapjointtype", kw_efn, &str, &def))
        return NULL;

    if(auto v = IniFile::mapJointType(str))
        return PyLong_FromLong((long)*v);

    // Not found, a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyFloat|None linuxcnc.ini.getlinearunits(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert the enumeration to its numerical value.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_linearunits(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getlinearunits", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findLinearUnits(num, var, sect))
        return PyFloat_FromDouble(*v);

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyFloat|None linuxcnc.ini.getangularunits(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert the enumeration to its numerical value.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_angularunits(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getangularunits", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findAngularUnits(num, var, sect))
        return PyFloat_FromDouble(*v);

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

//
// PyFloat|None linuxcnc.ini.getjointtype(string:section, string:variable [, int:num] [, fallback=val])
//
// Find [section]variable and convert the enumeration to its numerical value.
// The optional named argument fallback= defines the value to return when the
// variable is not found or invalid. The optional named option num= selects the
// num'th variable of the section (default to 1).
//
static PyObject *Ini_get_jointtype(pyIniFile *self, PyObject *args, PyObject *kwargs)
{
    const char *sect, *var;
    int num = 1;
    PyObject *def = Py_None;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|i$O:getjointtype", kw_eeefn, &sect, &var, &num, &def))
        return NULL;

    if(num < 1) {
        PyErr_Format(error, "Argument 'num' must be >= 1");
        return NULL;
    }

    IniFile ini(self->inifile);
    if(!ini) {
        PyErr_Format(error, "Internal: ini-file could not be reopened");
        return NULL;
    }

    if(auto v = ini.findJointType(num, var, sect))
        return PyLong_FromLong((long)*v);

    // Not found or error
    // We have a fallback set by argument or as None
    Py_INCREF(def);
    return def;
}

static void Ini_dealloc(pyIniFile *self) {
    if(self->inifile) {
        delete self->inifile;
        self->inifile = NULL;
    }
    PyObject_Del(self);
}

//
// This #pragma sucks...
// In C++ casting PyCFunctionWithKeywords to PyCFunction results in a warning
// about 'cast between incompatible function types'. However, this is the way
// it is done interfacing to the C API of Python.
// We disable the diagnostic warning temporarily here so we are not bothered.
// The alternative, creating an overlapping PyMethodDef structure with a
// unionized ml_meth field with all possible function signatures, is possible
// but not Python version secure.
//
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
static PyMethodDef Ini_methods[] = {
    {"hassection", (PyCFunction)Ini_has_section, METH_VARARGS,
        "PyBool hassection(section)\n"
        "Returns a boolean indicating whether or not the given section was found "
        "in the ini-file." },
    {"hasvariable", (PyCFunction)Ini_has_variable, METH_VARARGS,
        "PyBool hasvariable(section, variable [, num])\n"
        "Returns a boolean indicating whether or not the given [section]variable "
        "was found in the ini-file. The first occurrence of the variable name will "
        "be searched if the section name is empty. The optional num argument may "
        "be used to test whether the num'th variable of that name exists in the "
        "section." },
    {"getbool", (PyCFunction)Ini_get_bool, METH_VARARGS|METH_KEYWORDS,
        "PyBool|None getbool(section, variable [, num] [, fallback=])\n"
        "Returns the value of the variable converted to boolean if it was a valid "
        "boolean. The return value is None if the variable was not found or an "
        "invalid boolean value was detected and no fallback= was provided. The "
        "optional num argument may be used to select the num'th variable of that "
        "name in the section." },
    {"getsint", (PyCFunction)Ini_get_sint, METH_VARARGS|METH_KEYWORDS,
        "PyInt|None getsint(section, variable [, num] [, fallback=])\n"
        "Returns the value of the variable converted to signed integer if it was a "
        "valid integer. The return value is None if the variable was not found or "
        "an invalid value was detected and no fallback= was provided. The optional "
        "num argument may be used to select the num'th variable of that name in "
        "the section." },
    {"getint", (PyCFunction)Ini_get_sint, METH_VARARGS|METH_KEYWORDS,
        "PyInt|None getint(section, variable [, num] [, fallback=])\n"
        "Alias of getsint" },
    {"getuint", (PyCFunction)Ini_get_uint, METH_VARARGS|METH_KEYWORDS,
        "PyInt|None getuint(section, variable [, num] [, fallback=])\n"
        "Returns the value of the variable converted to unsigned integer if it was "
        "a valid unsigned integer. The return value is None if the variable was "
        "not found or an invalid value was detected and no fallback= was provided. "
        "The optional num argument may be used to select the num'th variable of "
        "that name in the section." },
    {"getreal", (PyCFunction)Ini_get_real, METH_VARARGS|METH_KEYWORDS,
        "PyFloat|None getreal(section, variable [, num] [, fallback=])\n"
        "Returns the value of the variable converted to floating point real if it "
        "was a valid real. The return value is None if the variable was not found "
        "or an invalid value was detected and no fallback= was provided. The "
        "optional num argument may be used to select the num'th variable of that "
        "name in the section." },
    {"getfloat", (PyCFunction)Ini_get_real, METH_VARARGS|METH_KEYWORDS,
        "PyFloat|None getfloat(section, variable [, num] [, fallback=])\n"
        "Alias of getreal()." },
    {"getstring", (PyCFunction)Ini_get_string, METH_VARARGS|METH_KEYWORDS,
        "PyString|None getstring(section, variable [, num] [, fallback=])\n"
        "Returns the value of the variable as a string if it exists. "
        "The return value is None if the variable was not found and no fallback= "
        "was provided. The optional num argument may be used to select the num'th "
        "variable of that name in the section." },
    {"getsections", (PyCFunction)Ini_get_sections, METH_VARARGS,
        "PyList getsections()\n"
        "Returns a list of section names. " },
    {"getvariables", (PyCFunction)Ini_get_variables, METH_VARARGS,
        "PyList(PyTuple(name,value)) getvariables([section])\n"
        "Returns a list of (name,value) tuples of all variables in the named "
        "section or the variables from all sections if the section name is not "
        "specified." },
    {"find", (PyCFunction)Ini_get_string, METH_VARARGS|METH_KEYWORDS,
        "PyString|None find(section, variable [, num] [, fallback=])\n"
        "Alias of getstring()" },
    {"findall", (PyCFunction)Ini_findall, METH_VARARGS,
        "PyList findall(section [,variable])\n"
        "Find value(s) from named section in inifile as a list matching the "
        "variable name." },
    {"lineof", (PyCFunction)Ini_lineof, METH_VARARGS,
        "PyTuple(filename,lineno) lineof(section, variable [, num])\n"
        "Returns a tuple with the filename and line number of the num'th "
        "variable in the section. The first matching section variable is "
        "returned if num if not provided. The tuple (None, None) is returned "
        "if the variable is not found." },
    {"maplinearunits", (PyCFunction)Ini_map_linearunits, METH_VARARGS|METH_KEYWORDS,
        "PyFloat|None maplinearunits(enumstr [, fallback=])\n"
        "Take the enumeration string argument and try to convert. "
        "Returns the value associated with enumerated type defined by "
        "[mm, metric, in, inch, imperial]." },
    {"mapangularunits", (PyCFunction)Ini_map_angularunits, METH_VARARGS|METH_KEYWORDS,
        "PyFloat|None mapangularunits(enumstr [, fallback=])\n"
        "Take the enumeration string argument and try to convert. "
        "Returns the value associated with enumerated type defined by "
        "[deg, degree, grad, gon, rad, radian]." },
    {"mapjointtype", (PyCFunction)Ini_map_jointtype, METH_VARARGS|METH_KEYWORDS,
        "PyInt|None mapjointtype(enumstr [, fallback=])\n"
        "Take the enumeration string argument and try to convert. "
        "Returns the value associated with enumerated type defined by "
        "[LINEAR, ANGULAR]." },
    {"getlinearunits", (PyCFunction)Ini_get_linearunits, METH_VARARGS|METH_KEYWORDS,
        "PyFloat|None getlinearunits(section, variable [, num] [, fallback=])\n"
        "Get the ini variable from the section and convert the enumerated type. "
        "The optional num argument may be used to select the num'th variable of "
        "that name in the section. Returns the value associated with enumerated "
        "type defined by [mm, metric, in, inch, imperial]." },
    {"getangularunits", (PyCFunction)Ini_get_angularunits, METH_VARARGS|METH_KEYWORDS,
        "PyFloat|None getangularunits(section, variable [, num] [, fallback=])\n"
        "Get the ini variable from the section and convert the enumerated type. "
        "The optional num argument may be used to select the num'th variable of "
        "that name in the section. Returns the value associated with enumerated "
        "type defined by [deg, degree, grad, gon, rad, radian]." },
    {"getjointtype", (PyCFunction)Ini_get_jointtype, METH_VARARGS|METH_KEYWORDS,
        "PyInt|None getjointtype(section, variable [, num] [, fallback=])\n"
        "Get the ini variable from the section and convert the enumerated type. "
        "The optional num argument may be used to select the num'th variable of "
        "that name in the section. Returns the value associated with enumerated "
        "type defined by [LINEAR, ANGULAR]." },
    {}
};
#pragma GCC diagnostic pop

static const char linuxcncinidoc[] =
    "LinuxCNC INI-file reader, parser and query module.\n"
    "Instantiate with:\n"
    "  cfg = linuxcnc.ini('path_to_ini_file.ini')\n"
    "\n"
    "Available methods:\n"
    "  PyBool hassection(section)\n"
    "  PyBool hasvariable(section, variable)\n"
    "  PyBool|None getbool(section, variable [, num] [, fallback=])\n"
    "  PyInt|None getsint(section, variable [, num] [, fallback=])\n"
    "  PyInt|None getuint(section, variable [, num] [, fallback=])\n"
    "  PyFloat|None getreal(section, variable [, num] [, fallback=])\n"
    "  PyString|None getstring(section, variable [, num] [, fallback=])\n"
    "  PyList getsections()\n"
    "  PyList(PyTuple(name,value)) getvariables([section])\n"
    "  PyList findall(section [,variable])\n"
    "  PyTuple(filename,lineno) lineof(section, variable [, num])\n"
    "  PyFloat|None getlinearunits(section, variable [, num] [, fallback=])\n"
    "  PyFloat|None getangularunits(section, variable [, num] [, fallback=])\n"
    "  PyInt|None getjointtype(section, variable [, num] [, fallback=])\n"
    "  PyFloat|None maplinearunits(enumstr [, fallback=])\n"
    "  PyFloat|None mapangularunits(enumstr [, fallback=])\n"
    "  PyInt|None mapjointtype(enumstr [, fallback=])\n"
    "\n"
    "Several convenience methods are provided as aliases to above methods:\n"
    "  PyInt|None getint(section, variable [, num] [, fallback=])\n"
    "  PyFloat|None getfloat(section, variable [, num] [, fallback=])\n"
    "  PyString|None find(section, variable [, num] [, fallback=])\n"
    "\n"
    "Method documentation is provided with each method.\n"
    ;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
static PyTypeObject Ini_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "linuxcnc.ini",              /*tp_name*/
    sizeof(pyIniFile),      /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    (destructor)Ini_dealloc,/*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    0,                      /*tp_call*/
    0,                      /*tp_str*/
    0,                      /*tp_getattro*/
    0,                      /*tp_setattro*/
    0,                      /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    linuxcncinidoc,         /*tp_doc*/
    0,                      /*tp_traverse*/
    0,                      /*tp_clear*/
    0,                      /*tp_richcompare*/
    0,                      /*tp_weaklistoffset*/
    0,                      /*tp_iter*/
    0,                      /*tp_iternext*/
    Ini_methods,            /*tp_methods*/
    0,                      /*tp_members*/
    0,                      /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    (initproc)Ini_init,     /*tp_init*/
    0,                      /*tp_alloc*/
    PyType_GenericNew,      /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
    0,                      /*tp_bases*/
    0,                      /*tp_mro*/
    0,                      /*tp_cache*/
    0,                      /*tp_subclasses*/
    0,                      /*tp_weaklink*/
    0,                      /*tp_del*/
    0,                      /*tp_version_tag*/
    0,                      /*tp_finalize*/
#if PY_VERSION_HEX >= 0x030800f0	// 3.8
    0,                      /*tp_vectorcall*/
#if PY_VERSION_HEX >= 0x030c00f0	// 3.12
    0,                      /*tp_watched*/
#if PY_VERSION_HEX >= 0x030d00f0	// 3.13
    0,                      /*tp_versions_used*/
#endif
#endif
#endif
};
#pragma GCC diagnostic pop

#define AXIS_MASK_A 0x08
#define AXIS_MASK_B 0x10
#define AXIS_MASK_C 0x20
static struct rotation_offsets {
    double x;
    double y;
    double z;
    unsigned int axis_mask;
    unsigned int respect_offsets;
} roffsets;

static void rotate_z(double pt[3], double a) {
    double theta = a * M_PI / 180;
    double c = cos(theta), s = sin(theta);
    double tx, ty;
    if (roffsets.respect_offsets) {
        tx = (pt[0]-roffsets.x) * c - (pt[1]-roffsets.y) * s;
        ty = (pt[0]-roffsets.x) * s + (pt[1]-roffsets.y) * c;
    } else {
        tx = pt[0] * c - pt[1] * s;
        ty = pt[0] * s + pt[1] * c;
    }

    pt[0] = tx; pt[1] = ty;
}

static void rotate_y(double pt[3], double a) {
    double theta = a * M_PI / 180;
    double c = cos(theta), s = sin(theta);
    double tx, tz;
    if (roffsets.respect_offsets) {
        tx = (pt[0]-roffsets.x) * c - (pt[2]-roffsets.z) * s;
        tz = (pt[0]-roffsets.x) * s + (pt[2]-roffsets.z) * c;
    } else {
        tx = pt[0] * c - pt[2] * s;
        tz = pt[0] * s + pt[2] * c;
    }

    pt[0] = tx; pt[2] = tz;
}

static void rotate_x(double pt[3], double a) {
    double theta = a * M_PI / 180;
    double c = cos(theta), s = sin(theta);
    double ty, tz;
    if (roffsets.respect_offsets) {
        ty = (pt[1]-roffsets.y) * c - (pt[2]-roffsets.z) * s;
        tz = (pt[1]-roffsets.y) * s + (pt[2]-roffsets.z) * c;
    } else {
        ty = pt[1] * c - pt[2] * s;
        tz = pt[1] * s + pt[2] * c;
    }

    pt[1] = ty; pt[2] = tz;
}

static void translate(double pt[3], double ox, double oy, double oz) {
    pt[0] += ox;
    pt[1] += oy;
    pt[2] += oz;
}

static void vertex9(const double pt[9], double p[3], const char *geometry) {
    double sign = 1;

    p[0] = 0;
    p[1] = 0;
    p[2] = 0;

    for(; *geometry; geometry++) {
        switch(*geometry) {
            case '-': sign = -1; break;
            case 'X': translate(p, pt[0] * sign, 0, 0); sign=1; break;
            case 'Y': translate(p, 0, pt[1] * sign, 0); sign=1; break;
            case 'Z': translate(p, 0, 0, pt[2] * sign); sign=1; break;
            case 'U': translate(p, pt[6] * sign, 0, 0); sign=1; break;
            case 'V': translate(p, 0, pt[7] * sign, 0); sign=1; break;
            case 'W': translate(p, 0, 0, pt[8] * sign); sign=1; break;
            case 'A': if (roffsets.axis_mask & AXIS_MASK_A) {
                          rotate_x(p, pt[3] * sign);
                      }
                      sign=1; break;
            case 'B': if (roffsets.axis_mask & AXIS_MASK_B) {
                          rotate_y(p, pt[4] * sign);
                      }
                      sign=1; break;
            case 'C': if (roffsets.axis_mask & AXIS_MASK_C) {
                          rotate_z(p, pt[5] * sign);
                      }
                      sign=1; break;

        }
    }
}

// Retired: this emitted immediate-mode OpenGL vertices, which the 3.3 core
// profile used for preview rendering does not have.  The name, signature,
// argument checking, and return value are kept for out-of-tree callers; it no
// longer draws.  Replacement: rs274.glcanon_scene.
static PyObject *pyline9(PyObject * /*s*/, PyObject *o) {
    static bool warned = false;
    double pt1[9], pt2[9];
    const char *geometry;

    if(!PyArg_ParseTuple(o, "s(ddddddddd)(ddddddddd):line9",
            &geometry,
            &pt1[0], &pt1[1], &pt1[2],
            &pt1[3], &pt1[4], &pt1[5],
            &pt1[6], &pt1[7], &pt1[8],
            &pt2[0], &pt2[1], &pt2[2],
            &pt2[3], &pt2[4], &pt2[5],
            &pt2[6], &pt2[7], &pt2[8]))
        return NULL;

    if(!warned) {
        warned = true;
        if(PyErr_WarnEx(PyExc_DeprecationWarning,
                    "linuxcnc.line9() no longer draws; use rs274.glcanon_scene",
                    1) < 0)
            return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *pyvertex9(PyObject * /*s*/, PyObject *o) {
    double pt1[9], pt[3];
    char *geometry;
    if(!PyArg_ParseTuple(o, "s(ddddddddd):vertex9",
            &geometry,
            &pt1[0], &pt1[1], &pt1[2],
            &pt1[3], &pt1[4], &pt1[5],
            &pt1[6], &pt1[7], &pt1[8]))
        return NULL;

    vertex9(pt1, pt, geometry);
    return Py_BuildValue("(ddd)", pt[0], pt[1], pt[2]);
}

static PyObject *pygui_respect_offsets (PyObject * /*s*/, PyObject *o) {
    char* coords;

    if(!PyArg_ParseTuple(o, "si",&coords, &roffsets.respect_offsets)) {
        return NULL;
    }
    if (roffsets.respect_offsets) {
        // GEOMETRY rotations only if letters (ABC) included in [TRAJ]COORDINATES
        if (strchr(coords,'A')) roffsets.axis_mask |= AXIS_MASK_A;
        if (strchr(coords,'B')) roffsets.axis_mask |= AXIS_MASK_B;
        if (strchr(coords,'C')) roffsets.axis_mask |= AXIS_MASK_C;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *pygui_rot_offsets(PyObject * /*s*/, PyObject *o) {
    if(!PyArg_ParseTuple(o, "ddd", &roffsets.x,&roffsets.y,&roffsets.z)) {
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

// Retired: this drew with immediate-mode OpenGL (glBegin/glVertex*/glEnd),
// invalid in the 3.3 core profile now used for preview rendering.  The name,
// signature, argument checking, and return value are kept for out-of-tree
// callers; it no longer draws.  Replacement: rs274.glcanon_scene.
static PyObject *pydraw_lines(PyObject * /*s*/, PyObject *o) {
    static bool warned = false;
    PyListObject *li;
    int for_selection = 0;
    int i;
    int n;
    double p1[9], p2[9];
    char *geometry;

    if(!PyArg_ParseTuple(o, "sO!|i:draw_lines",
			    &geometry, &PyList_Type, &li, &for_selection))
        return NULL;

    for(i=0; i<PyList_GET_SIZE(li); i++) {
        PyObject *it = PyList_GET_ITEM(li, i);
        PyObject *dummy1, *dummy2, *dummy3;
        if(!PyArg_ParseTuple(it, "i(ddddddddd)(ddddddddd)|OOO", &n,
                    p1+0, p1+1, p1+2,
                    p1+3, p1+4, p1+5,
                    p1+6, p1+7, p1+8,
                    p2+0, p2+1, p2+2,
                    p2+3, p2+4, p2+5,
                    p2+6, p2+7, p2+8,
                    &dummy1, &dummy2, &dummy3))
            return NULL;
    }

    if(!warned) {
        warned = true;
        if(PyErr_WarnEx(PyExc_DeprecationWarning,
                    "linuxcnc.draw_lines() no longer draws; use rs274.glcanon_scene",
                    1) < 0)
            return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

// Retired: this drew with immediate-mode OpenGL (glBegin/glVertex*/glEnd),
// invalid in the 3.3 core profile now used for preview rendering.  The name,
// signature, argument checking, and return value are kept for out-of-tree
// callers; it no longer draws.  Replacement: rs274.glcanon_scene.
static PyObject *pydraw_dwells(PyObject * /*s*/, PyObject *o) {
    static bool warned = false;
    PyListObject *li;
    int for_selection = 0, is_lathe = 0, i, n;
    double alpha;
    char *geometry;

    if(!PyArg_ParseTuple(o, "sO!dii:draw_dwells", &geometry, &PyList_Type, &li, &alpha, &for_selection, &is_lathe))
        return NULL;

    for(i=0; i<PyList_GET_SIZE(li); i++) {
        PyObject *it = PyList_GET_ITEM(li, i);
        double red, green, blue, x, y, z;
        int axis;
        if(!PyArg_ParseTuple(it, "i(ddd)dddi", &n, &red, &green, &blue, &x, &y, &z, &axis)) {
            return NULL;
        }
    }

    if(!warned) {
        warned = true;
        if(PyErr_WarnEx(PyExc_DeprecationWarning,
                    "linuxcnc.draw_dwells() no longer draws; use rs274.glcanon_scene",
                    1) < 0)
            return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

struct color {
    unsigned char r, g, b, a;
    bool operator==(const color &o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const color &o) const {
        return r != o.r || g != o.g || b != o.b || a != o.a;
    }
} color;

struct logger_point {
    float x, y, z;
    struct color c;
    float rx, ry, rz; // or uvw
    struct color c2;
};

#define NUMCOLORS (6)
#define MAX_POINTS (100000)
typedef struct {
    PyObject_HEAD
    int npts, mpts, lpts;
    struct logger_point *p;
    struct color colors[NUMCOLORS];
    bool exit, clear, changed;
    char *geometry;
    int is_xyuv;
    double foam_z, foam_w;
    // The stat object this logger samples. The Python reference keeps it
    // alive; `st` is the C++ object inside it, so that the sampling loop
    // below can read a position with the GIL released.
    PyObject *st_obj;
    linuxcnc::Status *st;
} pyPositionLogger;

static const double epsilon = 1e-4; // 1-cos(1 deg) ~= 1e-4
static const double tiny = 1e-10;

static inline bool colinear(float xa, float ya, float za, float xb, float yb, float zb, float xc, float yc, float zc) {
    double dx1 = xa-xb, dx2 = xb-xc;
    double dy1 = ya-yb, dy2 = yb-yc;
    double dz1 = za-zb, dz2 = zb-zc;
    double dp = sqrt(dx1*dx1 + dy1*dy1 + dz1*dz1);
    double dq = sqrt(dx2*dx2 + dy2*dy2 + dz2*dz2);
    if( fabs(dp) < tiny || fabs(dq) < tiny ) return true;
    double dot = (dx1*dx2 + dy1*dy2 + dz1*dz2) / dp / dq;
    if( fabs(1-dot) < epsilon) return true;
    return false;
}

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void LOCK() { pthread_mutex_lock(&mutex); }
static void UNLOCK() { pthread_mutex_unlock(&mutex); }

static int Logger_init(pyPositionLogger *self, PyObject *a, PyObject * /*k*/) {
    char *geometry;
    struct color *c = self->colors;
    self->p = (logger_point*)malloc(sizeof(self->p[0])); // Will be realloc'ed
    self->npts = self->mpts = 0;
    self->exit = self->clear = 0;
    self->changed = 1;
    self->st_obj = NULL;
    self->st = NULL;
    self->is_xyuv = 0;
    self->foam_z = 0;
    self->foam_w = 1.5;  // temporarily hard-code
    if(!PyArg_ParseTuple(a, "O(BBBB)(BBBB)(BBBB)(BBBB)(BBBB)(BBBB)s|i",
            &self->st_obj,
            &c[0].r,&c[0].g, &c[0].b, &c[0].a,
            &c[1].r,&c[1].g, &c[1].b, &c[1].a,
            &c[2].r,&c[2].g, &c[2].b, &c[2].a,
            &c[3].r,&c[3].g, &c[3].b, &c[3].a,
            &c[4].r,&c[4].g, &c[4].b, &c[4].a,
            &c[5].r,&c[5].g, &c[5].b, &c[5].a,
            &geometry, &self->is_xyuv
            ))
        return -1;

    // The first argument has to be a linuxcnc.stat; say so plainly rather
    // than crash in the sampling thread.
    try {
        self->st = pybind11::cast<linuxcnc::Status *>(
                        pybind11::handle(self->st_obj));
    } catch(const pybind11::cast_error &) {
        PyErr_SetString(PyExc_TypeError,
                        "positionlogger() first argument must be a linuxcnc.stat");
        return -1;
    }

    Py_INCREF(self->st_obj);
    self->geometry = strdup(geometry);
    return 0;
}

static void Logger_dealloc(pyPositionLogger *s) {
    free(s->p);
    Py_XDECREF(s->st_obj);
    free(s->geometry);
    PyObject_Del(s);
}

static PyObject *Logger_set_depth(pyPositionLogger *s, PyObject *o) {
    double z, w;
    if(!PyArg_ParseTuple(o, "dd:logger.set_depth", &z, &w)) return NULL;
    s->foam_z = z;
    s->foam_w = w;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *Logger_set_colors(pyPositionLogger *s, PyObject *a) {
    struct color *c = s->colors;
    if(!PyArg_ParseTuple(a, "(BBBB)(BBBB)(BBBB)(BBBB)(BBBB)(BBBB)",
            &c[0].r,&c[0].g, &c[0].b, &c[0].a,
            &c[1].r,&c[1].g, &c[1].b, &c[1].a,
            &c[2].r,&c[2].g, &c[2].b, &c[2].a,
            &c[3].r,&c[3].g, &c[3].b, &c[3].a,
            &c[4].r,&c[4].g, &c[4].b, &c[4].a,
            &c[5].r,&c[5].g, &c[5].b, &c[5].a
            ))
        return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *Logger_get_colors(pyPositionLogger *s, PyObject *) {
    struct color *c = s->colors;
    PyObject *result = NULL;
        result = Py_BuildValue("(BBBB)(BBBB)(BBBB)(BBBB)(BBBB)(BBBB)",
             c[0].r,c[0].g,c[0].b,c[0].a,
             c[1].r,c[1].g,c[1].b,c[1].a,
             c[2].r,c[2].g,c[2].b,c[2].a,
             c[3].r,c[3].g,c[3].b,c[3].a,
             c[4].r,c[4].g,c[4].b,c[4].a,
             c[5].r,c[5].g,c[5].b,c[5].a);
    return result;
    }

static double dist2(double x1, double y1, double x2, double y2) {
    double dx = x2-x1;
    double dy = y2-y1;
    return dx*dx + dy*dy;
}

static PyObject *Logger_start(pyPositionLogger *s, PyObject *o) {
    double interval;
    struct timespec ts;

    if(!PyArg_ParseTuple(o, "d:logger.start", &interval)) return NULL;
    ts.tv_sec = (int)interval;
    ts.tv_nsec = (long int)(1e9 * (interval - ts.tv_sec));

    Py_INCREF(s->st_obj);

    s->exit = 0;
    s->clear = 0;
    s->npts = 0;

    // The sampling thread's own copy of the status, filled by
    // Status::live(). It is a whole EMC_STAT, so it lives here rather than
    // on the stack of the loop below.
    EMC_STAT statbuf;

    Py_BEGIN_ALLOW_THREADS
    while(!s->exit) {
        if(s->clear) {
            s->npts = 0;
            s->lpts = 0;
            s->clear = 0;
        }
        // A reading of its own, so that polling from the Python side and
        // sampling from here do not tread on each other.
        if(s->st->live(statbuf)) {
            EMC_STAT *status = &statbuf;
            int colornum = status->motion.traj.motion_type;
            if(colornum < 0 || colornum >= NUMCOLORS) colornum = 0;
            struct color c = s->colors[colornum];
            struct logger_point *op = &s->p[s->npts-1];
            struct logger_point *oop = &s->p[s->npts-2];
            bool add_point = s->npts < 2 || c != op->c;
            double x, y, z, rx, ry, rz;
            if(s->is_xyuv) {
                x = status->motion.traj.position.tran.x - status->task.toolOffset.tran.x,
                y = status->motion.traj.position.tran.y - status->task.toolOffset.tran.y,
                z = s->foam_z;
                rx = status->motion.traj.position.u - status->task.toolOffset.u,
                ry = status->motion.traj.position.v - status->task.toolOffset.v,
                rz = s->foam_w;
                /* TODO .01, the distance at which a preview line is dropped,
                 * should either be dependent on units or configurable, because
                 * 0.1 is inappropriate for mm systems
                 */
                add_point = add_point || (dist2(x, y, oop->x, oop->y) > .01)
                    || (dist2(rx, ry, oop->rx, oop->ry) > .01);
                add_point = add_point || !colinear( x, y, z,
                                op->x, op->y, op->z,
                                oop->x, oop->y, oop->z);
                add_point = add_point || !colinear( rx, ry, rz,
                                op->rx, op->ry, op->rz,
                                oop->rx, oop->ry, oop->rz);
            } else {
                double pt[9] = {
                    status->motion.traj.position.tran.x - status->task.toolOffset.tran.x,
                    status->motion.traj.position.tran.y - status->task.toolOffset.tran.y,
                    status->motion.traj.position.tran.z - status->task.toolOffset.tran.z,
                    status->motion.traj.position.a - status->task.toolOffset.a,
                    status->motion.traj.position.b - status->task.toolOffset.b,
                    status->motion.traj.position.c - status->task.toolOffset.c,
                    status->motion.traj.position.u - status->task.toolOffset.u,
                    status->motion.traj.position.v - status->task.toolOffset.v,
                    status->motion.traj.position.w - status->task.toolOffset.w};

                double p[3];
                vertex9(pt, p, s->geometry);
                x = p[0]; y = p[1]; z = p[2];
                rx = pt[3]; ry = -pt[4]; rz = pt[5];

                add_point = add_point || !colinear( x, y, z,
                                op->x, op->y, op->z,
                                oop->x, oop->y, oop->z);
            }
            if(add_point) {
                // 1 or 2 points may be added, make room whenever
                // fewer than 2 are left
                bool changed_color = s->npts && c != op->c;
                if(s->npts+2 > s->mpts) {
                    LOCK();
                    if(s->mpts >= MAX_POINTS) {
                        int adjust = MAX_POINTS / 10;
                        if(adjust < 2) adjust = 2;
                        s->npts -= adjust;
                        memmove(s->p, s->p + adjust,
                                sizeof(struct logger_point) * s->npts);
                    } else {
                        s->mpts = 2 * s->mpts + 2;
                        s->changed = 1;
                        s->p = (struct logger_point*) realloc(s->p,
                                    sizeof(struct logger_point) * s->mpts);
                    }
                    UNLOCK();
                    op = &s->p[s->npts-1];
                    oop = &s->p[s->npts-2];
                }
                if(changed_color) {
                    {
                    struct logger_point &np = s->p[s->npts];
                    np.x = op->x; np.y = op->y; np.z = op->z;
                    np.rx = rx; np.ry = ry; np.rz = rz;
                    np.c = np.c2 = c;
                    }
                    {
                    struct logger_point &np = s->p[s->npts+1];
                    np.x = x; np.y = y; np.z = z;
                    np.rx = rx; np.ry = ry; np.rz = rz;
                    np.c = np.c2 = c;
                    }
                    s->npts += 2;
                } else {
                    struct logger_point &np = s->p[s->npts];
                    np.x = x; np.y = y; np.z = z;
                    np.rx = rx; np.ry = ry; np.rz = rz;
                    np.c = np.c2 = c;
                    s->npts++;
                }
            } else {
                struct logger_point &np = s->p[s->npts-1];
                np.x = x; np.y = y; np.z = z;
                np.rx = rx; np.ry = ry; np.rz = rz;
            }
        }
        nanosleep(&ts, NULL);
    }
    Py_END_ALLOW_THREADS
    Py_DECREF(s->st_obj);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Logger_clear(pyPositionLogger *s, PyObject * /*o*/) {
    s->clear = true;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* Logger_stop(pyPositionLogger *s, PyObject * /*o*/) {
    s->exit = true;
    Py_INCREF(Py_None);
    return Py_None;
}

// Retired: this plotted the backplot through the fixed-function client-state
// vertex arrays (glVertexPointer/glColorPointer/glDrawArrays), which the 3.3
// core profile does not have.  The name and return value are kept for
// out-of-tree callers; it no longer draws.  Replacement: points(), which hands
// the same buffer to the core renderer for VBO upload — and which advances
// lpts, so the tool marker still tracks the plotted line.
static PyObject* Logger_call(pyPositionLogger * /*s*/, PyObject * /*o*/) {
    static bool warned = false;
    if(!warned) {
        warned = true;
        if(PyErr_WarnEx(PyExc_DeprecationWarning,
                    "positionlogger.call() no longer draws; use positionlogger.points()",
                    1) < 0)
            return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *Logger_last(pyPositionLogger *s, PyObject *o) {
    int flag=1;
    if(!PyArg_ParseTuple(o, "|i:emc.positionlogger.last", &flag)) return NULL;
    PyObject *result = NULL;
    LOCK();
    int idx = flag ? s->lpts : s->npts;
    if(!idx) {
        Py_INCREF(Py_None);
        result = Py_None;
    } else {
        result = PyTuple_New(6);
        struct logger_point &p = s->p[idx-1];
        PyTuple_SET_ITEM(result, 0, PyFloat_FromDouble(p.x));
        PyTuple_SET_ITEM(result, 1, PyFloat_FromDouble(p.y));
        PyTuple_SET_ITEM(result, 2, PyFloat_FromDouble(p.z));
        PyTuple_SET_ITEM(result, 3, PyFloat_FromDouble(p.rx));
        PyTuple_SET_ITEM(result, 4, PyFloat_FromDouble(p.ry));
        PyTuple_SET_ITEM(result, 5, PyFloat_FromDouble(p.rz));
    }
    UNLOCK();
    return result;
}

// Additive accessor for the OpenGL 3.3 core renderer: hand Python a private
// copy of the logged-point ring buffer so it can upload changed ranges to a
// VBO, instead of the deprecated immediate-mode Logger_call. The copy is taken
// under the same lock that guards realloc/memmove of s->p in the sampler
// thread. Returns (bytes, npts, is_xyuv); each point is a `struct logger_point`
// (see the matching numpy dtype in rs274.glcanon_bake.LOGGER_DTYPE).
//
// This is the core renderer's draw-time handoff of the plotted points, so it
// advances lpts to npts exactly as Logger_call did. Logger_last(flag=1) reads
// lpts to report the last *drawn* point (used to position the tool marker so it
// stays in sync with the plotted line); without this, lpts stays 0 and the tool
// marker snaps to the origin whenever the live plot is shown.
static PyObject *Logger_get_points(pyPositionLogger *s, PyObject * /*o*/) {
    LOCK();
    int npts = s->npts;
    if(npts < 0) npts = 0;
    Py_ssize_t nbytes = (Py_ssize_t)npts * (Py_ssize_t)sizeof(struct logger_point);
    PyObject *buf = PyBytes_FromStringAndSize((const char*)s->p, nbytes);
    int is_xyuv = s->is_xyuv;
    s->lpts = s->npts;
    UNLOCK();
    if(!buf) return NULL;
    return Py_BuildValue("Nii", buf, npts, is_xyuv);
}

static PyMemberDef Logger_members[] = {
    {(char*)"npts", T_INT, offsetof(pyPositionLogger, npts), READONLY, NULL},
    {},
};

static PyMethodDef Logger_methods[] = {
    {"start", (PyCFunction)Logger_start, METH_VARARGS,
        "Start the position logger and run every ARG seconds"},
    {"clear", (PyCFunction)Logger_clear, METH_NOARGS,
        "Clear the position logger"},
    {"stop", (PyCFunction)Logger_stop, METH_NOARGS,
        "Stop the position logger"},
    {"call", (PyCFunction)Logger_call, METH_NOARGS,
        "Plot the backplot now"},
    {"points", (PyCFunction)Logger_get_points, METH_NOARGS,
        "Return (bytes, npts, is_xyuv): a copy of the logged point buffer for "
        "VBO upload by the core renderer"},
    {"set_depth", (PyCFunction)Logger_set_depth, METH_VARARGS,
        "set the Z and W depths for foam cutter"},
    {"set_colors", (PyCFunction)Logger_set_colors, METH_VARARGS,
        "set the plotting colors"},
    {"get_colors", (PyCFunction)Logger_get_colors, METH_NOARGS,
        "get the plotting colors"},
    {"last", (PyCFunction)Logger_last, METH_VARARGS,
        "Return the most recent point on the plot or None"},
    {},
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
static PyTypeObject PositionLoggerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "linuxcnc.positionlogger",   /*tp_name*/
    sizeof(pyPositionLogger), /*tp_basicsize*/
    0,                      /*tp_itemsize*/
    /* methods */
    (destructor)Logger_dealloc, /*tp_dealloc*/
    0,                      /*tp_print*/
    0,                      /*tp_getattr*/
    0,                      /*tp_setattr*/
    0,                      /*tp_compare*/
    0,                      /*tp_repr*/
    0,                      /*tp_as_number*/
    0,                      /*tp_as_sequence*/
    0,                      /*tp_as_mapping*/
    0,                      /*tp_hash*/
    0,                      /*tp_call*/
    0,                      /*tp_str*/
    0,                      /*tp_getattro*/
    0,                      /*tp_setattro*/
    0,                      /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,     /*tp_flags*/
    0,                      /*tp_doc*/
    0,                      /*tp_traverse*/
    0,                      /*tp_clear*/
    0,                      /*tp_richcompare*/
    0,                      /*tp_weaklistoffset*/
    0,                      /*tp_iter*/
    0,                      /*tp_iternext*/
    Logger_methods,         /*tp_methods*/
    Logger_members,         /*tp_members*/
    0,                      /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    (initproc)Logger_init,  /*tp_init*/
    0,                      /*tp_alloc*/
    PyType_GenericNew,      /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
    0,                      /*tp_bases*/
    0,                      /*tp_mro*/
    0,                      /*tp_cache*/
    0,                      /*tp_subclasses*/
    0,                      /*tp_weaklink*/
    0,                      /*tp_del*/
    0,                      /*tp_version_tag*/
    0,                      /*tp_finalize*/
#if PY_VERSION_HEX >= 0x030800f0	// 3.8
    0,                      /*tp_vectorcall*/
#if PY_VERSION_HEX >= 0x030c00f0	// 3.12
    0,                      /*tp_watched*/
#if PY_VERSION_HEX >= 0x030d00f0	// 3.13
    0,                      /*tp_versions_used*/
#endif
#endif
#endif
};
#pragma GCC diagnostic pop


/* ------------------------------------------------------------------ */
/* registration                                                       */
/* ------------------------------------------------------------------ */

static PyMethodDef emc_methods[] = {
#define METH(name, doc) { #name, (PyCFunction) py##name, METH_VARARGS, doc }
METH(draw_lines, "Retired: no longer draws, use rs274.glcanon_scene"),
METH(draw_dwells, "Retired: no longer draws, use rs274.glcanon_scene"),
METH(line9, "Retired: no longer draws, use rs274.glcanon_scene"),
METH(vertex9, "Get the 3d location for a 9d point"),
METH(gui_rot_offsets, "Set x,y,z offsets for A,B,C rotations"),
METH(gui_respect_offsets, "Enable rotations about g5x,g92 offsets"),
    {}
#undef METH
};

void emcRegisterLegacy(PyObject *module, PyObject *errorType)
{
    m = module;
    error = errorType;

    PyType_Ready(&Ini_Type);
    Py_INCREF(&Ini_Type);
    PyModule_AddObject(m, "ini", (PyObject*)&Ini_Type);

    PyType_Ready(&PositionLoggerType);
    Py_INCREF(&PositionLoggerType);
    PyModule_AddObject(m, "positionlogger", (PyObject*)&PositionLoggerType);
    pthread_mutex_init(&mutex, NULL);

    PyModule_AddFunctions(m, emc_methods);
}

// # vim:sw=4:sts=4:et:ts=8:
