/********************************************************************
* Description: emcmodule.hh
*   What linuxcnc_py.cc needs from what is left of the old extension
*   module.
*
* License: GPL Version 2
********************************************************************/
#ifndef EMCMODULE_HH
#define EMCMODULE_HH

#include <Python.h>

/* Add the INI file reader and the AXIS backplot to an already-created
   module object. `errorType` is linuxcnc.error, which the INI reader
   raises when a file cannot be opened. */
void emcRegisterLegacy(PyObject *module, PyObject *errorType);

#endif /* EMCMODULE_HH */
