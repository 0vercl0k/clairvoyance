// Mastho - 2020
#define PY_SSIZE_T_CLEAN

#include "kdmp-parser.h"
#include <Python.h>

#if PY_MINOR_VERSION >= 8
#define IS_PY3_8 1
#else
#define IS_PY3_8 0
#endif

//
// Python object handling all interactions with the library.
//

typedef struct {
  PyObject_HEAD kdmpparser::KernelDumpParser *DumpParser;
} PythonDumpParser;

//
// Python Dump type functions declarations (class instance creation and instance
// destruction).
//

PyObject *NewDumpParser(PyTypeObject *Type, PyObject *Args, PyObject *Kwds);
void DeleteDumpParser(PyObject *Object);

//
// Python Dump object methods functions declarations.
//

PyObject *DumpParserGetType(PyObject *Object, PyObject *NotUsed);
PyObject *DumpParserGetContext(PyObject *Object, PyObject *NotUsed);
PyObject *DumpParserGetPhysicalPage(PyObject *Object, PyObject *Args);
PyObject *DumpParserVirtTranslate(PyObject *Object, PyObject *Args);
PyObject *DumpParserGetVirtualPage(PyObject *Object, PyObject *Args);
PyObject *DumpParserGetBugCheckParameters(PyObject *Object, PyObject *NotUsed);

//
// Object methods of Python Dump type.
//

PyMethodDef DumpObjectMethod[] = {
    {"type", DumpParserGetType, METH_NOARGS,
     "Show Dump Type (FullDump, KernelDump)"},
    {"context", DumpParserGetContext, METH_NOARGS, "Get Register Context"},
    {"get_physical_page", DumpParserGetPhysicalPage, METH_VARARGS,
     "Get Physical Page Content"},
    {"virt_translate", DumpParserVirtTranslate, METH_VARARGS,
     "Translate Virtual to Physical Address"},
    {"get_virtual_page", DumpParserGetVirtualPage, METH_VARARGS,
     "Get Virtual Page Content"},
    {"bugcheck", DumpParserGetBugCheckParameters, METH_NOARGS,
     "Get BugCheck Parameters"},
    {nullptr, nullptr, 0, nullptr}};

//
// Define PythonDumpParserType (size, name, initialization & destruction
// functions and object methods).
//

static PyTypeObject PythonDumpParserType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "kdmp.Dump", /* tp_name */
    sizeof(PythonDumpParser),                           /* tp_basicsize */
    0,                                                  /* tp_itemsize */
    DeleteDumpParser,                                   /* tp_dealloc */
#if IS_PY3_8
    0, /* tp_vectorcall_offset */
#else
    nullptr, /* tp_print */
#endif
    nullptr,            /* tp_getattr */
    nullptr,            /* tp_setattr */
    nullptr,            /* tp_compare */
    nullptr,            /* tp_repr */
    nullptr,            /* tp_as_number */
    nullptr,            /* tp_as_sequence */
    nullptr,            /* tp_as_mapping */
    nullptr,            /* tp_hash */
    nullptr,            /* tp_call */
    nullptr,            /* tp_str */
    nullptr,            /* tp_getattro */
    nullptr,            /* tp_setattro */
    nullptr,            /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT, /* tp_flags */
    "Dump object",      /* tp_doc */
    nullptr,            /* tp_traverse */
    nullptr,            /* tp_clear */
    nullptr,            /* tp_richcompare */
    0,                  /* tp_weaklistoffset */
    nullptr,            /* tp_iter */
    nullptr,            /* tp_iternext */
    DumpObjectMethod,   /* tp_methods */
    nullptr,            /* tp_members */
    nullptr,            /* tp_getset */
    nullptr,            /* tp_base */
    nullptr,            /* tp_dict */
    nullptr,            /* tp_descr_get */
    nullptr,            /* tp_descr_set */
    0,                  /* tp_dictoffset */
    nullptr,            /* tp_init */
    nullptr,            /* tp_alloc */
    NewDumpParser,      /* tp_new */
    nullptr,            /* tp_free */
    nullptr,            /* tp_is_gc */
    nullptr,            /* tp_bases */
    nullptr,            /* tp_mro */
    nullptr,            /* tp_cache */
    nullptr,            /* tp_subclasses */
    nullptr,            /* tp_weaklist */
    nullptr,            /* tp_del */
    0,                  /* tp_version_tag */
    nullptr,            /* tp_finalize */
#if IS_PY3_8
    nullptr, /* tp_vectorcall */
    0, /* bpo-37250: kept for backwards compatibility in CPython 3.8 only */
#endif
};

//
// KDMP Module definition.
//

static struct PyModuleDef KDMPModule = {
    PyModuleDef_HEAD_INIT, /* m_base */
    "kdmp",                /* m_name */
    nullptr,               /* m_doc */
    -1,                    /* m_size */
    nullptr,               /* m_methods */
    nullptr,               /* m_slots */
    nullptr,               /* m_traverse */
    nullptr,               /* m_clear */
    nullptr,               /* m_free */
};

//
// KDMP Module initialization function.
//

PyMODINIT_FUNC PyInit_kdmp(void);