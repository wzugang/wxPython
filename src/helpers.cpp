/////////////////////////////////////////////////////////////////////////////
// Name:        helpers.cpp
// Purpose:     Helper functions/classes for the wxPython extension module
//
// Author:      Robin Dunn
//
// Created:     1-July-1997
// RCS-ID:      $Id$
// Copyright:   (c) 1998 by Total Control Software
// Licence:     wxWindows license
/////////////////////////////////////////////////////////////////////////////


#undef DEBUG
#include <Python.h>
#include "wx/wxPython/wxPython_int.h"
#include "wx/wxPython/pyistream.h"

#ifdef __WXMSW__
#include <wx/msw/private.h>
#include <wx/msw/winundef.h>
#include <wx/msw/msvcrt.h>
#endif

#ifdef __WXGTK__
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gdk/gdkprivate.h>
#include <wx/gtk/win_gtk.h>
#define GetXWindow(wxwin)   GDK_WINDOW_XWINDOW((wxwin)->m_widget->window)
#include <locale.h>
#endif

#ifdef __WXX11__
#include "wx/x11/privx.h"
#define GetXWindow(wxwin)   ((Window)(wxwin)->GetHandle())
#endif

#ifdef __WXMAC__
#include <wx/mac/private.h>
#endif

#include <wx/clipbrd.h>
#include <wx/mimetype.h>
#include <wx/image.h>

//----------------------------------------------------------------------

#if PYTHON_API_VERSION <= 1007 && wxUSE_UNICODE
#error Python must support Unicode to use wxWindows Unicode
#endif

//----------------------------------------------------------------------

wxPyApp* wxPythonApp = NULL;  // Global instance of application object
bool wxPyDoCleanup = False;
bool wxPyDoingCleanup = False;


#ifdef WXP_WITH_THREAD
struct wxPyThreadState {
    unsigned long  tid;
    PyThreadState* tstate;

    wxPyThreadState(unsigned long _tid=0, PyThreadState* _tstate=NULL)
        : tid(_tid), tstate(_tstate) {}
};

#include <wx/dynarray.h>
WX_DECLARE_OBJARRAY(wxPyThreadState, wxPyThreadStateArray);
#include <wx/arrimpl.cpp>
WX_DEFINE_OBJARRAY(wxPyThreadStateArray);

wxPyThreadStateArray* wxPyTStates = NULL;
wxMutex*              wxPyTMutex = NULL;
#endif


static PyObject* wxPython_dict = NULL;
static PyObject* wxPyAssertionError = NULL;

PyObject* wxPyPtrTypeMap = NULL;


#ifdef __WXMSW__             // If building for win32...
//----------------------------------------------------------------------
// This gets run when the DLL is loaded.  We just need to save a handle.
//----------------------------------------------------------------------

BOOL WINAPI DllMain(
    HINSTANCE   hinstDLL,    // handle to DLL module
    DWORD       fdwReason,   // reason for calling function
    LPVOID      lpvReserved  // reserved
   )
{
    // If wxPython is embedded in another wxWindows app then
    // the inatance has already been set.
    if (! wxGetInstance())
        wxSetInstance(hinstDLL);
    return True;
}
#endif

//----------------------------------------------------------------------
// Classes for implementing the wxp main application shell.
//----------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxPyApp, wxApp);


wxPyApp::wxPyApp() {
    m_assertMode = wxPYAPP_ASSERT_EXCEPTION;
    m_startupComplete = False;
}


wxPyApp::~wxPyApp() {
}


// This one isn't acutally called...  We fake it with _BootstrapApp
bool wxPyApp::OnInit() {
    return False;
}


int  wxPyApp::MainLoop() {
    int retval = 0;

    DeletePendingObjects();
    bool initialized = wxTopLevelWindows.GetCount() != 0;
    if (initialized) {
        if ( m_exitOnFrameDelete == Later ) {
            m_exitOnFrameDelete = Yes;
        }

        retval = wxApp::MainLoop();
        OnExit();
    }
    return retval;
}


bool wxPyApp::OnInitGui() {
    bool rval=True;
    wxApp::OnInitGui();  // in this case always call the base class version
    bool blocked = wxPyBeginBlockThreads();
    if (wxPyCBH_findCallback(m_myInst, "OnInitGui"))
        rval = wxPyCBH_callCallback(m_myInst, Py_BuildValue("()"));
    wxPyEndBlockThreads(blocked);
    return rval;
}


int wxPyApp::OnExit() {
    int rval=0;
    bool blocked = wxPyBeginBlockThreads();
    if (wxPyCBH_findCallback(m_myInst, "OnExit"))
        rval = wxPyCBH_callCallback(m_myInst, Py_BuildValue("()"));
    wxPyEndBlockThreads(blocked);
    wxApp::OnExit();  // in this case always call the base class version
    return rval;
}


#ifdef __WXDEBUG__
void wxPyApp::OnAssert(const wxChar *file,
                     int line,
                     const wxChar *cond,
                     const wxChar *msg) {

    // if we're not fully initialized then just log the error
    if (! m_startupComplete) {
        wxString buf;
        buf.Alloc(4096);
        buf.Printf(wxT("%s(%d): assert \"%s\" failed"),
                   file, line, cond);
        if (msg != NULL) {
            buf += wxT(": ");
            buf += msg;
        }
        wxLogDebug(buf);
        return;
    }

    // If the OnAssert is overloaded in the Python class then call it...
    bool found;
    bool blocked = wxPyBeginBlockThreads();
    if ((found = wxPyCBH_findCallback(m_myInst, "OnAssert"))) {
        PyObject* fso = wx2PyString(file);
        PyObject* cso = wx2PyString(file);
        PyObject* mso;
        if (msg != NULL)
            mso = wx2PyString(file);
        else {
            mso = Py_None; Py_INCREF(Py_None);
        }
        wxPyCBH_callCallback(m_myInst, Py_BuildValue("(OiOO)", fso, line, cso, mso));
        Py_DECREF(fso);
        Py_DECREF(cso);
        Py_DECREF(mso);
    }
    wxPyEndBlockThreads(blocked);

    // ...otherwise do our own thing with it
    if (! found) {
        // ignore it?
        if (m_assertMode & wxPYAPP_ASSERT_SUPPRESS)
            return;

        // turn it into a Python exception?
        if (m_assertMode & wxPYAPP_ASSERT_EXCEPTION) {
            wxString buf;
            buf.Alloc(4096);
            buf.Printf(wxT("C++ assertion \"%s\" failed in %s(%d)"), cond, file, line);
            if (msg != NULL) {
                buf += wxT(": ");
                buf += msg;
            }

            // set the exception
            bool blocked = wxPyBeginBlockThreads();
            PyObject* s = wx2PyString(buf);
            PyErr_SetObject(wxPyAssertionError, s);
            Py_DECREF(s);
            wxPyEndBlockThreads(blocked);

            // Now when control returns to whatever API wrapper was called from
            // Python it should detect that an exception is set and will return
            // NULL, signalling the exception to Python.
        }

        // Send it to the normal log destination, but only if
        // not _DIALOG because it will call this too
        if ( (m_assertMode & wxPYAPP_ASSERT_LOG) && !(m_assertMode & wxPYAPP_ASSERT_DIALOG)) {
            wxString buf;
            buf.Alloc(4096);
            buf.Printf(wxT("%s(%d): assert \"%s\" failed"),
                       file, line, cond);
            if (msg != NULL) {
                buf += wxT(": ");
                buf += msg;
            }
            wxLogDebug(buf);
        }

        // do the normal wx assert dialog?
        if (m_assertMode & wxPYAPP_ASSERT_DIALOG)
            wxApp::OnAssert(file, line, cond, msg);
    }
}
#endif

    // For catching Apple Events
void wxPyApp::MacOpenFile(const wxString &fileName)
{
    bool blocked = wxPyBeginBlockThreads();
    if (wxPyCBH_findCallback(m_myInst, "MacOpenFile")) {
        PyObject* s = wx2PyString(fileName);
        wxPyCBH_callCallback(m_myInst, Py_BuildValue("(O)", s));
        Py_DECREF(s);
    }
    wxPyEndBlockThreads(blocked);
}

void wxPyApp::MacPrintFile(const wxString &fileName)
{
    bool blocked = wxPyBeginBlockThreads();
    if (wxPyCBH_findCallback(m_myInst, "MacPrintFile")) {
        PyObject* s = wx2PyString(fileName);
        wxPyCBH_callCallback(m_myInst, Py_BuildValue("(O)", s));
        Py_DECREF(s);
    }
    wxPyEndBlockThreads(blocked);
}

void wxPyApp::MacNewFile()
{
    bool blocked = wxPyBeginBlockThreads();
    if (wxPyCBH_findCallback(m_myInst, "MacNewFile"))
        wxPyCBH_callCallback(m_myInst, Py_BuildValue("()"));
    wxPyEndBlockThreads(blocked);
}

void wxPyApp::MacReopenApp()
{
    bool blocked = wxPyBeginBlockThreads();
    if (wxPyCBH_findCallback(m_myInst, "MacReopenApp"))
        wxPyCBH_callCallback(m_myInst, Py_BuildValue("()"));
    wxPyEndBlockThreads(blocked);
}


/*static*/
bool wxPyApp::GetMacSupportPCMenuShortcuts() {
#ifdef __WXMAC__
    return s_macSupportPCMenuShortcuts;
#else
    return 0;
#endif
}

/*static*/
long wxPyApp::GetMacAboutMenuItemId() {
#ifdef __WXMAC__
    return s_macAboutMenuItemId;
#else
    return 0;
#endif
}

/*static*/
long wxPyApp::GetMacPreferencesMenuItemId() {
#ifdef __WXMAC__
    return s_macPreferencesMenuItemId;
#else
    return 0;
#endif
}

/*static*/
long wxPyApp::GetMacExitMenuItemId() {
#ifdef __WXMAC__
    return s_macExitMenuItemId;
#else
    return 0;
#endif
}

/*static*/
wxString wxPyApp::GetMacHelpMenuTitleName() {
#ifdef __WXMAC__
    return s_macHelpMenuTitleName;
#else
    return wxEmptyString;
#endif
}

/*static*/
void wxPyApp::SetMacSupportPCMenuShortcuts(bool val) {
#ifdef __WXMAC__
    s_macSupportPCMenuShortcuts = val;
#endif
}

/*static*/
void wxPyApp::SetMacAboutMenuItemId(long val) {
#ifdef __WXMAC__
    s_macAboutMenuItemId = val;
#endif
}

/*static*/
void wxPyApp::SetMacPreferencesMenuItemId(long val) {
#ifdef __WXMAC__
    s_macPreferencesMenuItemId = val;
#endif
}

/*static*/
void wxPyApp::SetMacExitMenuItemId(long val) {
#ifdef __WXMAC__
    s_macExitMenuItemId = val;
#endif
}

/*static*/
void wxPyApp::SetMacHelpMenuTitleName(const wxString& val) {
#ifdef __WXMAC__
    s_macHelpMenuTitleName = val;
#endif
}


// This finishes the initialization of wxWindows and then calls the OnInit
// that should be present in the derived (Python) class.
void wxPyApp::_BootstrapApp()
{
    bool        result;
    PyObject*   retval = NULL;
    PyObject*   pyint  = NULL;


    // Get any command-line args passed to this program from the sys module
    int    argc = 0;
    char** argv = NULL;
    bool blocked = wxPyBeginBlockThreads();
    PyObject* sysargv = PySys_GetObject("argv");
    if (sysargv != NULL) {
        argc = PyList_Size(sysargv);
        argv = new char*[argc+1];
        int x;
        for(x=0; x<argc; x++) {
            PyObject *pyArg = PyList_GetItem(sysargv, x);
            argv[x] = PyString_AsString(pyArg);
        }
        argv[argc] = NULL;
    }
    wxPyEndBlockThreads(blocked);

    // Initialize wxWidgets
    result = wxEntryStart(argc, argv);
    delete [] argv;

    blocked = wxPyBeginBlockThreads();
    if (! result)  {
        PyErr_SetString(PyExc_SystemError,
                        "wxEntryStart failed, unable to initialize wxWidgets!"
#ifdef __WXGTK__
                        "  (Is DISPLAY set properly?)"
#endif
            );
        goto error;
    }

    // On wxGTK the locale will be changed to match the system settings, but
    // Python needs to have LC_NUMERIC set to "C" in order for the floating
    // point conversions and such to work right.
#ifdef __WXGTK__
    setlocale(LC_NUMERIC, "C");
#endif
    
    // The stock objects were all NULL when they were loaded into
    // SWIG generated proxies, so re-init those now...
    wxPy_ReinitStockObjects(3);

    // It's now ok to generate exceptions for assertion errors.
    wxPythonApp->SetStartupComplete(True);

    // Call the Python wxApp's OnInit function
    if (wxPyCBH_findCallback(m_myInst, "OnInit")) {

        PyObject* method = m_myInst.GetLastFound();
        PyObject* argTuple = PyTuple_New(0);
        retval = PyEval_CallObject(method, argTuple);
        Py_DECREF(argTuple);
        Py_DECREF(method);
        if (retval == NULL)
            goto error;

        pyint = PyNumber_Int(retval);
        if (! pyint) {
            PyErr_SetString(PyExc_TypeError, "OnInit should return a boolean value");
            goto error;
        }
        result = PyInt_AS_LONG(pyint);
    }
    else {
        // Is it okay if there is no OnInit?  Probably so...
        result = True;
    }

    if (! result) {
        PyErr_SetString(PyExc_SystemExit, "OnInit returned False, exiting...");
    }

 error:
    Py_XDECREF(retval);
    Py_XDECREF(pyint);

    wxPyEndBlockThreads(blocked);
};

//---------------------------------------------------------------------
//----------------------------------------------------------------------


#if 0
static char* wxPyCopyCString(const wxChar* src)
{
    wxWX2MBbuf buff = (wxWX2MBbuf)wxConvCurrent->cWX2MB(src);
    size_t len = strlen(buff);
    char*  dest = new char[len+1];
    strcpy(dest, buff);
    return dest;
}

#if wxUSE_UNICODE
static char* wxPyCopyCString(const char* src)   // we need a char version too
{
    size_t len = strlen(src);
    char*  dest = new char[len+1];
    strcpy(dest, src);
    return dest;
}
#endif

static wxChar* wxPyCopyWString(const char *src)
{
    //wxMB2WXbuf buff = wxConvCurrent->cMB2WX(src);
    wxString str(src, *wxConvCurrent);
    return copystring(str);
}

#if wxUSE_UNICODE
static wxChar* wxPyCopyWString(const wxChar *src)
{
    return copystring(src);
}
#endif
#endif


inline const char* dropwx(const char* name) {
    if (name[0] == 'w' && name[1] == 'x')
        return name+2;
    else
        return name;
}

//----------------------------------------------------------------------

// This function is called when the wxc module is imported to do some initial
// setup.  (Before there is a wxApp object.)  The rest happens in
// wxPyApp::_BootstrapApp
void __wxPyPreStart(PyObject* moduleDict)
{

#ifdef __WXMSW__
//    wxCrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF);
#endif

#ifdef WXP_WITH_THREAD
    PyEval_InitThreads();
    wxPyTStates = new wxPyThreadStateArray;
    wxPyTMutex = new wxMutex;

    // Save the current (main) thread state in our array
    PyThreadState* tstate = wxPyBeginAllowThreads();
    wxPyEndAllowThreads(tstate);
#endif

    // Ensure that the build options in the DLL (or whatever) match this build
    wxApp::CheckBuildOptions(WX_BUILD_OPTIONS_SIGNATURE, "wxPython");

    // Init the stock objects to a non-NULL value so SWIG doesn't create them as None
    wxPy_ReinitStockObjects(1);

    wxInitAllImageHandlers();
}



void __wxPyCleanup() {
    wxPyDoingCleanup = True;
    if (wxPyDoCleanup) {
        wxPyDoCleanup = False;
        wxEntryCleanup();
    }
#ifdef WXP_WITH_THREAD
    delete wxPyTMutex;
    wxPyTMutex = NULL;
    wxPyTStates->Empty();
    delete wxPyTStates;
    wxPyTStates = NULL;
#endif
}


// Save a reference to the dictionary of the wx._core module, and inject
// a few more things into it.
PyObject* __wxPySetDictionary(PyObject* /* self */, PyObject* args)
{

    if (!PyArg_ParseTuple(args, "O", &wxPython_dict))
        return NULL;

    if (!PyDict_Check(wxPython_dict)) {
        PyErr_SetString(PyExc_TypeError, "_wxPySetDictionary must have dictionary object!");
        return NULL;
    }

    if (! wxPyPtrTypeMap)
        wxPyPtrTypeMap = PyDict_New();
    PyDict_SetItemString(wxPython_dict, "__wxPyPtrTypeMap", wxPyPtrTypeMap);

    // Create an exception object to use for wxASSERTions
    wxPyAssertionError = PyErr_NewException("wx._core.PyAssertionError",
                                            PyExc_AssertionError, NULL);
    PyDict_SetItemString(wxPython_dict, "PyAssertionError", wxPyAssertionError);


#ifdef __WXMOTIF__
#define wxPlatform "__WXMOTIF__"
#define wxPlatName "wxMotif"
#endif
#ifdef __WXX11__
#define wxPlatform "__WXX11__"
#define wxPlatName "wxX11"
#endif
#ifdef __WXGTK__
#define wxPlatform "__WXGTK__"
#define wxPlatName "wxGTK"
#endif
#ifdef __WXMSW__
#define wxPlatform "__WXMSW__"
#define wxPlatName "wxMSW"
#endif
#ifdef __WXMAC__
#define wxPlatform "__WXMAC__"
#define wxPlatName "wxMac"
#endif

#ifdef __WXDEBUG__
    int wxdebug = 1;
#else
    int wxdebug = 0;
#endif

    // These should be deprecated in favor of the PlatformInfo tuple built below...
    PyDict_SetItemString(wxPython_dict, "Platform", PyString_FromString(wxPlatform));
    PyDict_SetItemString(wxPython_dict, "USE_UNICODE", PyInt_FromLong(wxUSE_UNICODE));
    PyDict_SetItemString(wxPython_dict, "__WXDEBUG__", PyInt_FromLong(wxdebug));

    
    PyObject* PlatInfo = PyList_New(0);
    PyObject* obj;

#define _AddInfoString(st) \
    obj = PyString_FromString(st); \
    PyList_Append(PlatInfo, obj); \
    Py_DECREF(obj)

    _AddInfoString(wxPlatform);
    _AddInfoString(wxPlatName);
#if wxUSE_UNICODE
    _AddInfoString("unicode");
#else
    _AddInfoString("ascii");
#endif
#ifdef __WXGTK__
#ifdef __WXGTK20__
    _AddInfoString("gtk2");
#else
    _AddInfoString("gtk1");
#endif
#endif

#undef _AddInfoString

    PyObject* PlatInfoTuple = PyList_AsTuple(PlatInfo);
    Py_DECREF(PlatInfo);
    PyDict_SetItemString(wxPython_dict, "PlatformInfo", PlatInfoTuple);
    
    RETURN_NONE();
}


//---------------------------------------------------------------------------

// Python's PyInstance_Check does not return True for instances of new-style
// classes.  This should get close enough for both new and old classes but I
// should re-evaluate the need for doing instance checks...
bool wxPyInstance_Check(PyObject* obj) {
    return PyObject_HasAttrString(obj, "__class__") != 0;
}


// This one checks if the object is an instance of a SWIG proxy class (it has
// a .this attribute)
bool wxPySwigInstance_Check(PyObject* obj) {
    return PyObject_HasAttrString(obj, "this") != 0;
}

//---------------------------------------------------------------------------

// The stock objects are no longer created when the wxc module is imported,
// but only after the app object has been created.  The
// wxPy_ReinitStockObjects function will be called 3 times to pass the stock
// objects though various stages of evolution:
//
//   pass 1: Set all the pointers to a non-NULL value so the Python proxy
//           object will be created (otherwise it will just use None.)
//
//   pass 2: After the module has been imported and the python proxys have
//           been created, then set the __class__ to be _wxPyUnbornObject so
//           it will catch any access to the object and will raise an exception.
//
//   pass 3: Finally, from OnInit patch things up so the stock objects can
//           be used.


PyObject* __wxPyFixStockObjects(PyObject* /* self */, PyObject* args)
{
    wxPy_ReinitStockObjects(2);
    RETURN_NONE();
}


static void rsoPass2(const char* name)
{
    static PyObject* unbornObjectClass = NULL;
    PyObject* obj;

    if (unbornObjectClass == NULL) {
        unbornObjectClass = PyDict_GetItemString(wxPython_dict, "_wxPyUnbornObject");
        Py_INCREF(unbornObjectClass);
    }

    // Find the object instance
    obj = PyDict_GetItemString(wxPython_dict, (char*)dropwx(name));
    wxCHECK_RET(obj != NULL, wxT("Unable to find stock object"));
    wxCHECK_RET(wxPySwigInstance_Check(obj), wxT("Not a swig instance"));

    // Change its class
    PyObject_SetAttrString(obj, "__class__",  unbornObjectClass);

}

static void rsoPass3(const char* name, const char* classname, void* ptr)
{
    PyObject* obj;
    PyObject* classobj;
    PyObject* ptrobj;

    // Find the object instance
    obj = PyDict_GetItemString(wxPython_dict, (char*)dropwx(name));
    wxCHECK_RET(obj != NULL, wxT("Unable to find stock object"));
    wxCHECK_RET(wxPySwigInstance_Check(obj), wxT("Not a swig instance"));

    // Find the class object and put it back in the instance
    classobj = PyDict_GetItemString(wxPython_dict, (char*)dropwx(classname));
    wxCHECK_RET(classobj != NULL, wxT("Unable to find stock class object"));
    PyObject_SetAttrString(obj, "__class__",  classobj);

    // Rebuild the .this swigified pointer with the new value of the C++ pointer
    ptrobj = wxPyMakeSwigPtr(ptr, wxString(classname, *wxConvCurrent));
    PyObject_SetAttrString(obj, "this", ptrobj);
    Py_DECREF(ptrobj);
}



void wxPy_ReinitStockObjects(int pass)
{

#define REINITOBJ(name, classname) \
    if (pass == 1) { name = (classname*)0xC0C0C0C0; } \
    else if (pass == 2) { rsoPass2(#name); } \
    else if (pass == 3) { rsoPass3(#name, #classname, (void*)name); }


#define REINITOBJ2(name, classname) \
    if (pass == 1) { } \
    else if (pass == 2) { rsoPass2(#name); } \
    else if (pass == 3) { rsoPass3(#name, #classname, (void*)&name); }

    // If there is already an App object then wxPython is probably embedded in
    // a wx C++ application, so there is no need to do all this.
    static bool embedded = false;
    if ((pass == 1 || pass == 2) && wxTheApp) {
        embedded = true;
        return;
    }
    if (pass == 3 && embedded)
        return;
    

    REINITOBJ(wxNORMAL_FONT, wxFont);
    REINITOBJ(wxSMALL_FONT, wxFont);
    REINITOBJ(wxITALIC_FONT, wxFont);
    REINITOBJ(wxSWISS_FONT, wxFont);

    REINITOBJ(wxRED_PEN, wxPen);
    REINITOBJ(wxCYAN_PEN, wxPen);
    REINITOBJ(wxGREEN_PEN, wxPen);
    REINITOBJ(wxBLACK_PEN, wxPen);
    REINITOBJ(wxWHITE_PEN, wxPen);
    REINITOBJ(wxTRANSPARENT_PEN, wxPen);
    REINITOBJ(wxBLACK_DASHED_PEN, wxPen);
    REINITOBJ(wxGREY_PEN, wxPen);
    REINITOBJ(wxMEDIUM_GREY_PEN, wxPen);
    REINITOBJ(wxLIGHT_GREY_PEN, wxPen);

    REINITOBJ(wxBLUE_BRUSH, wxBrush);
    REINITOBJ(wxGREEN_BRUSH, wxBrush);
    REINITOBJ(wxWHITE_BRUSH, wxBrush);
    REINITOBJ(wxBLACK_BRUSH, wxBrush);
    REINITOBJ(wxTRANSPARENT_BRUSH, wxBrush);
    REINITOBJ(wxCYAN_BRUSH, wxBrush);
    REINITOBJ(wxRED_BRUSH, wxBrush);
    REINITOBJ(wxGREY_BRUSH, wxBrush);
    REINITOBJ(wxMEDIUM_GREY_BRUSH, wxBrush);
    REINITOBJ(wxLIGHT_GREY_BRUSH, wxBrush);

    REINITOBJ(wxBLACK, wxColour);
    REINITOBJ(wxWHITE, wxColour);
    REINITOBJ(wxRED, wxColour);
    REINITOBJ(wxBLUE, wxColour);
    REINITOBJ(wxGREEN, wxColour);
    REINITOBJ(wxCYAN, wxColour);
    REINITOBJ(wxLIGHT_GREY, wxColour);

    REINITOBJ(wxSTANDARD_CURSOR, wxCursor);
    REINITOBJ(wxHOURGLASS_CURSOR, wxCursor);
    REINITOBJ(wxCROSS_CURSOR, wxCursor);

    REINITOBJ2(wxNullBitmap, wxBitmap);
    REINITOBJ2(wxNullIcon, wxIcon);
    REINITOBJ2(wxNullCursor, wxCursor);
    REINITOBJ2(wxNullPen, wxPen);
    REINITOBJ2(wxNullBrush, wxBrush);
    REINITOBJ2(wxNullPalette, wxPalette);
    REINITOBJ2(wxNullFont, wxFont);
    REINITOBJ2(wxNullColour, wxColour);

    REINITOBJ(wxTheFontList, wxFontList);
    REINITOBJ(wxThePenList, wxPenList);
    REINITOBJ(wxTheBrushList, wxBrushList);
    REINITOBJ(wxTheColourDatabase, wxColourDatabase);


    REINITOBJ(wxTheClipboard, wxClipboard);
    REINITOBJ2(wxDefaultValidator, wxValidator);
    REINITOBJ2(wxNullImage, wxImage);
    REINITOBJ2(wxNullAcceleratorTable, wxAcceleratorTable);

#undef REINITOBJ
#undef REINITOBJ2
}

//---------------------------------------------------------------------------

void wxPyClientData_dtor(wxPyClientData* self) {
    if (! wxPyDoingCleanup) {           // Don't do it during cleanup as Python
                                        // may have already garbage collected the object...
        bool blocked = wxPyBeginBlockThreads();
        Py_DECREF(self->m_obj);
        self->m_obj = NULL;
        wxPyEndBlockThreads(blocked);
    }
}

void wxPyUserData_dtor(wxPyUserData* self) {
    if (! wxPyDoingCleanup) {
        bool blocked = wxPyBeginBlockThreads();
        Py_DECREF(self->m_obj);
        self->m_obj = NULL;
        wxPyEndBlockThreads(blocked);
    }
}


// This is called when an OOR controled object is being destroyed.  Although
// the C++ object is going away there is no way to force the Python object
// (and all references to it) to die too.  This causes problems (crashes) in
// wxPython when a python shadow object attempts to call a C++ method using
// the now bogus pointer... So to try and prevent this we'll do a little black
// magic and change the class of the python instance to a class that will
// raise an exception for any attempt to call methods with it.  See
// _wxPyDeadObject in _core_ex.py for the implementation of this class.
void wxPyOORClientData_dtor(wxPyOORClientData* self) {

    static PyObject* deadObjectClass = NULL;

    bool blocked = wxPyBeginBlockThreads();
    if (deadObjectClass == NULL) {
        deadObjectClass = PyDict_GetItemString(wxPython_dict, "_wxPyDeadObject");
        // TODO:  Can not wxASSERT here because inside a wxPyBeginBlock Threads,
        // will lead to a deadlock when it tries to aquire the GIL again.
        //wxASSERT_MSG(deadObjectClass != NULL, wxT("Can't get _wxPyDeadObject class!"));
        Py_INCREF(deadObjectClass);
    }


    // Only if there is more than one reference to the object
    if ( !wxPyDoingCleanup && self->m_obj->ob_refcnt > 1 ) {
        // bool isInstance = wxPyInstance_Check(self->m_obj);
        // TODO same here
        //wxASSERT_MSG(isInstance, wxT("m_obj not an instance!?!?!"));

        // Call __del__, if there is one.
        PyObject* func = PyObject_GetAttrString(self->m_obj, "__del__");
        if (func) {
            PyObject* rv = PyObject_CallMethod(self->m_obj, "__del__", NULL);
            Py_XDECREF(rv);
            Py_DECREF(func);
        }
        if (PyErr_Occurred())
            PyErr_Clear();      // just ignore it for now


        PyObject* dict = PyObject_GetAttrString(self->m_obj, "__dict__");
        if (dict) {
            // Clear the instance's dictionary
            PyDict_Clear(dict);

            // put the name of the old class into the instance, and then reset the
            // class to be the dead class.
            PyObject* klass = PyObject_GetAttrString(self->m_obj, "__class__");
            PyObject* name =  PyObject_GetAttrString(klass, "__name__");
            PyDict_SetItemString(dict, "_name", name);
            PyObject_SetAttrString(self->m_obj, "__class__",  deadObjectClass);
            //Py_INCREF(deadObjectClass);
            Py_DECREF(klass);
            Py_DECREF(name);
        }
    }

    // m_obj is DECREF'd in the base class dtor...
    wxPyEndBlockThreads(blocked);
}


//---------------------------------------------------------------------------
// Stuff used by OOR to find the right wxPython class type to return and to
// build it.


// The pointer type map is used when the "pointer" type name generated by SWIG
// is not the same as the shadow class name, for example wxPyTreeCtrl
// vs. wxTreeCtrl.  It needs to be referenced in Python as well as from C++,
// so we'll just make it a Python dictionary in the wx module's namespace.
// (See __wxSetDictionary)
void wxPyPtrTypeMap_Add(const char* commonName, const char* ptrName) {
    if (! wxPyPtrTypeMap)
        wxPyPtrTypeMap = PyDict_New();
    PyDict_SetItemString(wxPyPtrTypeMap,
                         (char*)commonName,
                         PyString_FromString((char*)ptrName));
}




PyObject*  wxPyMake_wxObject(wxObject* source, bool checkEvtHandler) {
    PyObject* target = NULL;
    bool      isEvtHandler = False;

    if (source) {
        // If it's derived from wxEvtHandler then there may
        // already be a pointer to a Python object that we can use
        // in the OOR data.
        if (checkEvtHandler && wxIsKindOf(source, wxEvtHandler)) {
            isEvtHandler = True;
            wxEvtHandler* eh = (wxEvtHandler*)source;
            wxPyOORClientData* data = (wxPyOORClientData*)eh->GetClientObject();
            if (data) {
                target = data->m_obj;
                if (target)
                    Py_INCREF(target);
            }
        }

        if (! target) {
            // Otherwise make it the old fashioned way by making a new shadow
            // object and putting this pointer in it.  Look up the class
            // heirarchy until we find a class name that is located in the
            // python module.
            const wxClassInfo* info   = source->GetClassInfo();
            wxString           name   = info->GetClassName();
            bool               exists = wxPyCheckSwigType(name);
            while (info && !exists) {
                info = info->GetBaseClass1();
                name = info->GetClassName();
                exists = wxPyCheckSwigType(name);
            }
            if (info) {
                target = wxPyConstructObject((void*)source, name, False);
                if (target && isEvtHandler)
                    ((wxEvtHandler*)source)->SetClientObject(new wxPyOORClientData(target));
            } else {
                wxString msg(wxT("wxPython class not found for "));
                msg += source->GetClassInfo()->GetClassName();
                PyErr_SetString(PyExc_NameError, msg.mbc_str());
                target = NULL;
            }
        }
    } else {  // source was NULL so return None.
        Py_INCREF(Py_None); target = Py_None;
    }
    return target;
}


PyObject*  wxPyMake_wxSizer(wxSizer* source) {
    PyObject* target = NULL;

    if (source && wxIsKindOf(source, wxSizer)) {
        // If it's derived from wxSizer then there may already be a pointer to
        // a Python object that we can use in the OOR data.
        wxSizer* sz = (wxSizer*)source;
        wxPyOORClientData* data = (wxPyOORClientData*)sz->GetClientObject();
        if (data) {
            target = data->m_obj;
            if (target)
                Py_INCREF(target);
        }
    }
    if (! target) {
        target = wxPyMake_wxObject(source, False);
        if (target != Py_None)
            ((wxSizer*)source)->SetClientObject(new wxPyOORClientData(target));
    }
    return target;
}


//---------------------------------------------------------------------------


#ifdef WXP_WITH_THREAD
inline
unsigned long wxPyGetCurrentThreadId() {
    return wxThread::GetCurrentId();
}

static wxPyThreadState gs_shutdownTState;

static
wxPyThreadState* wxPyGetThreadState() {
    if (wxPyTMutex == NULL) // Python is shutting down...
        return &gs_shutdownTState;

    unsigned long ctid = wxPyGetCurrentThreadId();
    wxPyThreadState* tstate = NULL;

    wxPyTMutex->Lock();
    for(size_t i=0; i < wxPyTStates->GetCount(); i++) {
        wxPyThreadState& info = wxPyTStates->Item(i);
        if (info.tid == ctid) {
            tstate = &info;
            break;
        }
    }
    wxPyTMutex->Unlock();
    wxASSERT_MSG(tstate, wxT("PyThreadState should not be NULL!"));
    return tstate;
}


static
void wxPySaveThreadState(PyThreadState* tstate) {
    if (wxPyTMutex == NULL) { // Python is shutting down, assume a single thread...
        gs_shutdownTState.tstate = tstate;
        return;
    }
    unsigned long ctid = wxPyGetCurrentThreadId();
    wxPyTMutex->Lock();
    for(size_t i=0; i < wxPyTStates->GetCount(); i++) {
        wxPyThreadState& info = wxPyTStates->Item(i);
        if (info.tid == ctid) {
#if 0
            if (info.tstate != tstate)
                wxLogMessage("*** tstate mismatch!???");
#endif
            // info.tstate = tstate;    *** DO NOT update existing ones???
            // Normally it will never change, but apparently COM callbacks
            // (i.e. ActiveX controls) will (incorrectly IMHO) use a transient
            // tstate which will then be garbage the next time we try to use
            // it...
            wxPyTMutex->Unlock();
            return;
        }
    }
    // not found, so add it...
    wxPyTStates->Add(new wxPyThreadState(ctid, tstate));
    wxPyTMutex->Unlock();
}

#endif



// Calls from Python to wxWindows code are wrapped in calls to these
// functions:

PyThreadState* wxPyBeginAllowThreads() {
#ifdef WXP_WITH_THREAD
    PyThreadState* saved = PyEval_SaveThread();  // Py_BEGIN_ALLOW_THREADS;
    wxPySaveThreadState(saved);
    return saved;
#else
    return NULL;
#endif
}

void wxPyEndAllowThreads(PyThreadState* saved) {
#ifdef WXP_WITH_THREAD
    PyEval_RestoreThread(saved);   // Py_END_ALLOW_THREADS;
#endif
}



// Calls from wxWindows back to Python code, or even any PyObject
// manipulations, PyDECREF's and etc. are wrapped in calls to these functions:

bool wxPyBeginBlockThreads() {
#ifdef WXP_WITH_THREAD
    // This works in for 2.3, maybe a good alternative to find the needed tstate?
    // PyThreadState *check = PyGILState_GetThisThreadState();  

    PyThreadState *current = _PyThreadState_Current;

    // Only block if there wasn't already a tstate, or if the current one is
    // not the one we are wanting to change to.  This should prevent deadlock
    // if there are nested calls to wxPyBeginBlockThreads
    bool blocked = false;
    wxPyThreadState* tstate = wxPyGetThreadState();
    if (current != tstate->tstate) {
        PyEval_RestoreThread(tstate->tstate);
        blocked = true;
    }
    return blocked;
#endif
}


void wxPyEndBlockThreads(bool blocked) {
#ifdef WXP_WITH_THREAD
    // Only unblock if we blocked in the last call to wxPyBeginBlockThreads.
    // The value of blocked passed in needs to be the same as that returned
    // from wxPyBeginBlockThreads at the same nesting level.
    if ( blocked ) {
        PyEval_SaveThread();
    }
#endif
}


//---------------------------------------------------------------------------
// wxPyInputStream and wxPyCBInputStream methods


void wxPyInputStream::close() {
    /* do nothing for now */
}

void wxPyInputStream::flush() {
    /* do nothing for now */
}

bool wxPyInputStream::eof() {
    if (m_wxis)
        return m_wxis->Eof();
    else
        return True;
}

wxPyInputStream::~wxPyInputStream() {
    /* do nothing */
}




PyObject* wxPyInputStream::read(int size) {
    PyObject* obj = NULL;
    wxMemoryBuffer buf;
    const int BUFSIZE = 1024;

    // check if we have a real wxInputStream to work with
    if (!m_wxis) {
        bool blocked = wxPyBeginBlockThreads();
        PyErr_SetString(PyExc_IOError, "no valid C-wxInputStream");
        wxPyEndBlockThreads(blocked);
        return NULL;
    }

    if (size < 0) {
        // read while bytes are available on the stream
        while ( m_wxis->CanRead() ) {
            m_wxis->Read(buf.GetAppendBuf(BUFSIZE), BUFSIZE);
            buf.UngetAppendBuf(m_wxis->LastRead());
        }

    } else {  // Read only size number of characters
        m_wxis->Read(buf.GetWriteBuf(size), size);
        buf.UngetWriteBuf(m_wxis->LastRead());
    }

    // error check
    bool blocked = wxPyBeginBlockThreads();
    wxStreamError err = m_wxis->GetLastError();
    if (err != wxSTREAM_NO_ERROR && err != wxSTREAM_EOF) {
        PyErr_SetString(PyExc_IOError,"IOError in wxInputStream");
    }
    else {
        // We use only strings for the streams, not unicode
        obj = PyString_FromStringAndSize(buf, buf.GetDataLen());
    }
    wxPyEndBlockThreads(blocked);
    return obj;
}


PyObject* wxPyInputStream::readline(int size) {
    PyObject* obj = NULL;
    wxMemoryBuffer buf;
    int i;
    char ch;

    // check if we have a real wxInputStream to work with
    if (!m_wxis) {
        bool blocked = wxPyBeginBlockThreads();
        PyErr_SetString(PyExc_IOError,"no valid C-wxInputStream");
        wxPyEndBlockThreads(blocked);
        return NULL;
    }

    // read until \n or byte limit reached
    for (i=ch=0; (ch != '\n') && (m_wxis->CanRead()) && ((size < 0) || (i < size)); i++) {
        ch = m_wxis->GetC();
        buf.AppendByte(ch);
    }

    // errorcheck
    bool blocked = wxPyBeginBlockThreads();
    wxStreamError err = m_wxis->GetLastError();
    if (err != wxSTREAM_NO_ERROR && err != wxSTREAM_EOF) {
        PyErr_SetString(PyExc_IOError,"IOError in wxInputStream");
    }
    else {
        // We use only strings for the streams, not unicode
        obj = PyString_FromStringAndSize((char*)buf.GetData(), buf.GetDataLen());
    }
    wxPyEndBlockThreads(blocked);
    return obj;
}


PyObject* wxPyInputStream::readlines(int sizehint) {
    PyObject* pylist;

    // check if we have a real wxInputStream to work with
    if (!m_wxis) {
        bool blocked = wxPyBeginBlockThreads();
        PyErr_SetString(PyExc_IOError,"no valid C-wxInputStream");
        wxPyEndBlockThreads(blocked);
        return NULL;
    }

    // init list
    bool blocked = wxPyBeginBlockThreads();
    pylist = PyList_New(0);
    wxPyEndBlockThreads(blocked);
    
    if (!pylist) {
        bool blocked = wxPyBeginBlockThreads();
        PyErr_NoMemory();
        wxPyEndBlockThreads(blocked);
        return NULL;
    }

    // read sizehint bytes or until EOF
    int i;
    for (i=0; (m_wxis->CanRead()) && ((sizehint < 0) || (i < sizehint));) {
        PyObject* s = this->readline();
        if (s == NULL) {
            bool blocked = wxPyBeginBlockThreads();
            Py_DECREF(pylist);
            wxPyEndBlockThreads(blocked);
            return NULL;
        }
        bool blocked = wxPyBeginBlockThreads();
        PyList_Append(pylist, s);
        i += PyString_Size(s);
        wxPyEndBlockThreads(blocked);
    }

    // error check
    wxStreamError err = m_wxis->GetLastError();
    if (err != wxSTREAM_NO_ERROR && err != wxSTREAM_EOF) {
        bool blocked = wxPyBeginBlockThreads();
        Py_DECREF(pylist);
        PyErr_SetString(PyExc_IOError,"IOError in wxInputStream");
        wxPyEndBlockThreads(blocked);
        return NULL;
    }

    return pylist;
}


void wxPyInputStream::seek(int offset, int whence) {
    if (m_wxis)
        m_wxis->SeekI(offset, wxSeekMode(whence));
}

int wxPyInputStream::tell(){
    if (m_wxis)
        return m_wxis->TellI();
    else return 0;
}




wxPyCBInputStream::wxPyCBInputStream(PyObject *r, PyObject *s, PyObject *t, bool block)
    : wxInputStream(), m_read(r), m_seek(s), m_tell(t), m_block(block)
{}


wxPyCBInputStream::~wxPyCBInputStream() {
    bool blocked;
    if (m_block) blocked = wxPyBeginBlockThreads();
    Py_XDECREF(m_read);
    Py_XDECREF(m_seek);
    Py_XDECREF(m_tell);
    if (m_block) wxPyEndBlockThreads(blocked);
}


wxPyCBInputStream* wxPyCBInputStream::create(PyObject *py, bool block) {
    bool blocked;
    if (block) blocked = wxPyBeginBlockThreads();

    PyObject* read = getMethod(py, "read");
    PyObject* seek = getMethod(py, "seek");
    PyObject* tell = getMethod(py, "tell");

    if (!read) {
        PyErr_SetString(PyExc_TypeError, "Not a file-like object");
        Py_XDECREF(read);
        Py_XDECREF(seek);
        Py_XDECREF(tell);
        if (block) wxPyEndBlockThreads(blocked);
        return NULL;
    }

    if (block) wxPyEndBlockThreads(blocked);
    return new wxPyCBInputStream(read, seek, tell, block);
}


wxPyCBInputStream* wxPyCBInputStream_create(PyObject *py, bool block) {
    return wxPyCBInputStream::create(py, block);
}

PyObject* wxPyCBInputStream::getMethod(PyObject* py, char* name) {
    if (!PyObject_HasAttrString(py, name))
        return NULL;
    PyObject* o = PyObject_GetAttrString(py, name);
    if (!PyMethod_Check(o) && !PyCFunction_Check(o)) {
        Py_DECREF(o);
        return NULL;
    }
    return o;
}


size_t wxPyCBInputStream::GetSize() const {
    wxPyCBInputStream* self = (wxPyCBInputStream*)this; // cast off const
    if (m_seek && m_tell) {
        off_t temp = self->OnSysTell();
        off_t ret = self->OnSysSeek(0, wxFromEnd);
        self->OnSysSeek(temp, wxFromStart);
        return ret;
    }
    else
        return 0;
}


size_t wxPyCBInputStream::OnSysRead(void *buffer, size_t bufsize) {
    if (bufsize == 0)
        return 0;

    bool blocked = wxPyBeginBlockThreads();
    PyObject* arglist = Py_BuildValue("(i)", bufsize);
    PyObject* result = PyEval_CallObject(m_read, arglist);
    Py_DECREF(arglist);

    size_t o = 0;
    if ((result != NULL) && PyString_Check(result)) {
        o = PyString_Size(result);
        if (o == 0)
            m_lasterror = wxSTREAM_EOF;
        if (o > bufsize)
            o = bufsize;
        memcpy((char*)buffer, PyString_AsString(result), o);  // strings only, not unicode...
        Py_DECREF(result);

    }
    else
        m_lasterror = wxSTREAM_READ_ERROR;
    wxPyEndBlockThreads(blocked);
    return o;
}

size_t wxPyCBInputStream::OnSysWrite(const void *buffer, size_t bufsize) {
    m_lasterror = wxSTREAM_WRITE_ERROR;
    return 0;
}

off_t wxPyCBInputStream::OnSysSeek(off_t off, wxSeekMode mode) {
    bool blocked = wxPyBeginBlockThreads();
#ifdef _LARGE_FILES
    // off_t is a 64-bit value...
    PyObject* arglist = Py_BuildValue("(Li)", off, mode);
#else
    PyObject* arglist = Py_BuildValue("(ii)", off, mode);
#endif
    PyObject* result = PyEval_CallObject(m_seek, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    wxPyEndBlockThreads(blocked);
    return OnSysTell();
}


off_t wxPyCBInputStream::OnSysTell() const {
    bool blocked = wxPyBeginBlockThreads();
    PyObject* arglist = Py_BuildValue("()");
    PyObject* result = PyEval_CallObject(m_tell, arglist);
    Py_DECREF(arglist);
    off_t o = 0;
    if (result != NULL) {
#ifdef _LARGE_FILES
        if (PyLong_Check(result))
            o = PyLong_AsLongLong(result);
        else
#endif
            o = PyInt_AsLong(result);
        Py_DECREF(result);
    };
    wxPyEndBlockThreads(blocked);
    return o;
}

//----------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxPyCallback, wxObject);

wxPyCallback::wxPyCallback(PyObject* func) {
    m_func = func;
    Py_INCREF(m_func);
}

wxPyCallback::wxPyCallback(const wxPyCallback& other) {
    m_func = other.m_func;
    Py_INCREF(m_func);
}

wxPyCallback::~wxPyCallback() {
    bool blocked = wxPyBeginBlockThreads();
    Py_DECREF(m_func);
    wxPyEndBlockThreads(blocked);
}


#define wxPy_PRECALLINIT     "_preCallInit"
#define wxPy_POSTCALLCLEANUP "_postCallCleanup"

// This function is used for all events destined for Python event handlers.
void wxPyCallback::EventThunker(wxEvent& event) {
    wxPyCallback*   cb = (wxPyCallback*)event.m_callbackUserData;
    PyObject*       func = cb->m_func;
    PyObject*       result;
    PyObject*       arg;
    PyObject*       tuple;
    bool            checkSkip = False;

    bool blocked = wxPyBeginBlockThreads();
    wxString className = event.GetClassInfo()->GetClassName();

    // If the event is one of these types then pass the original
    // event object instead of the one passed to us.
    if ( className == wxT("wxPyEvent") ) {
        arg =       ((wxPyEvent*)&event)->GetSelf();
        checkSkip = ((wxPyEvent*)&event)->GetCloned();
    }
    else if ( className == wxT("wxPyCommandEvent") ) {
        arg =       ((wxPyCommandEvent*)&event)->GetSelf();
        checkSkip = ((wxPyCommandEvent*)&event)->GetCloned();
    }
    else {
        arg = wxPyConstructObject((void*)&event, className);
    }

    if (!arg) {
        PyErr_Print();
    } else {
        // "intern" the pre/post method names to speed up the HasAttr
        static PyObject* s_preName  = NULL;
        static PyObject* s_postName = NULL;
        if (s_preName == NULL) {
            s_preName  = PyString_FromString(wxPy_PRECALLINIT);
            s_postName = PyString_FromString(wxPy_POSTCALLCLEANUP);
        }
        
        // Check if the event object needs some preinitialization
        if (PyObject_HasAttr(arg, s_preName)) {
            result = PyObject_CallMethodObjArgs(arg, s_preName, arg, NULL);
            if ( result ) {
                Py_DECREF(result);   // result is ignored, but we still need to decref it
                PyErr_Clear();       // Just in case...
            } else {
                PyErr_Print();
            }            
        }
        
        // Call the event handler, passing the event object
        tuple = PyTuple_New(1);
        PyTuple_SET_ITEM(tuple, 0, arg);  // steals ref to arg
        result = PyEval_CallObject(func, tuple);
        if ( result ) {
            Py_DECREF(result);   // result is ignored, but we still need to decref it
            PyErr_Clear();       // Just in case...
        } else {
            PyErr_Print();
        }

        // Check if the event object needs some post cleanup
        if (PyObject_HasAttr(arg, s_postName)) {
            result = PyObject_CallMethodObjArgs(arg, s_postName, arg, NULL);
            if ( result ) {
                Py_DECREF(result);   // result is ignored, but we still need to decref it
                PyErr_Clear();       // Just in case...
            } else {
                PyErr_Print();
            }            
        }

        if ( checkSkip ) {
            // if the event object was one of our special types and
            // it had been cloned, then we need to extract the Skipped
            // value from the original and set it in the clone.
            result = PyObject_CallMethod(arg, "GetSkipped", "");
            if ( result ) {
                event.Skip(PyInt_AsLong(result));
                Py_DECREF(result);
            } else {
                PyErr_Print();
            }
        }
        Py_DECREF(tuple);
    }
    wxPyEndBlockThreads(blocked);
}


//----------------------------------------------------------------------

wxPyCallbackHelper::wxPyCallbackHelper(const wxPyCallbackHelper& other) {
      m_lastFound = NULL;
      m_self = other.m_self;
      m_class = other.m_class;
      if (m_self) {
          Py_INCREF(m_self);
          Py_INCREF(m_class);
      }
}


void wxPyCallbackHelper::setSelf(PyObject* self, PyObject* klass, int incref) {
    m_self = self;
    m_class = klass;
    m_incRef = incref;
    if (incref) {
        Py_INCREF(m_self);
        Py_INCREF(m_class);
    }
}


#if PYTHON_API_VERSION >= 1011

// Prior to Python 2.2 PyMethod_GetClass returned the class object
// in which the method was defined.  Starting with 2.2 it returns
// "class that asked for the method" which seems totally bogus to me
// but apprently it fixes some obscure problem waiting to happen in
// Python.  Since the API was not documented Guido and the gang felt
// safe in changing it.  Needless to say that totally screwed up the
// logic below in wxPyCallbackHelper::findCallback, hence this icky
// code to find the class where the method is actually defined...

static
PyObject* PyFindClassWithAttr(PyObject *klass, PyObject *name)
{
    int i, n;

    if (PyType_Check(klass)) {      // new style classes
        // This code is borrowed/adapted from _PyType_Lookup in typeobject.c
        PyTypeObject* type = (PyTypeObject*)klass;
        PyObject *mro, *res, *base, *dict;
        /* Look in tp_dict of types in MRO */
        mro = type->tp_mro;
        assert(PyTuple_Check(mro));
        n = PyTuple_GET_SIZE(mro);
        for (i = 0; i < n; i++) {
            base = PyTuple_GET_ITEM(mro, i);
            if (PyClass_Check(base))
                dict = ((PyClassObject *)base)->cl_dict;
            else {
                assert(PyType_Check(base));
                dict = ((PyTypeObject *)base)->tp_dict;
            }
            assert(dict && PyDict_Check(dict));
            res = PyDict_GetItem(dict, name);
            if (res != NULL)
                return base;
        }
        return NULL;
    }

    else if (PyClass_Check(klass)) { // old style classes
        // This code is borrowed/adapted from class_lookup in classobject.c
        PyClassObject* cp = (PyClassObject*)klass;
        PyObject *value = PyDict_GetItem(cp->cl_dict, name);
        if (value != NULL) {
            return (PyObject*)cp;
        }
        n = PyTuple_Size(cp->cl_bases);
        for (i = 0; i < n; i++) {
            PyObject* base = PyTuple_GetItem(cp->cl_bases, i);
            PyObject *v = PyFindClassWithAttr(base, name);
            if (v != NULL)
                return v;
        }
        return NULL;
    }
    return NULL;
}
#endif


static
PyObject* PyMethod_GetDefiningClass(PyObject* method, const char* name)
{
    PyObject* mgc = PyMethod_GET_CLASS(method);

#if PYTHON_API_VERSION <= 1010    // prior to Python 2.2, the easy way
    return mgc;
#else                             // 2.2 and after, the hard way...

    PyObject* nameo = PyString_FromString(name);
    PyObject* klass = PyFindClassWithAttr(mgc, nameo);
    Py_DECREF(nameo);
    return klass;
#endif
}



bool wxPyCallbackHelper::findCallback(const char* name) const {
    wxPyCallbackHelper* self = (wxPyCallbackHelper*)this; // cast away const
    self->m_lastFound = NULL;

    // If the object (m_self) has an attibute of the given name...
    if (m_self && PyObject_HasAttrString(m_self, (char*)name)) {
        PyObject *method, *klass;
        method = PyObject_GetAttrString(m_self, (char*)name);

        // ...and if that attribute is a method, and if that method's class is
        // not from a base class...
        if (PyMethod_Check(method) &&
            (klass = PyMethod_GetDefiningClass(method, (char*)name)) != NULL &&
            ((klass == m_class) || PyObject_IsSubclass(klass, m_class))) {

            // ...then we'll save a pointer to the method so callCallback can call it.
            self->m_lastFound = method;
        }
        else {
            Py_DECREF(method);
        }
    }
    return m_lastFound != NULL;
}


int wxPyCallbackHelper::callCallback(PyObject* argTuple) const {
    PyObject*   result;
    int         retval = False;

    result = callCallbackObj(argTuple);
    if (result) {                       // Assumes an integer return type...
        retval = PyInt_AsLong(result);
        Py_DECREF(result);
        PyErr_Clear();                  // forget about it if it's not...
    }
    return retval;
}

// Invoke the Python callable object, returning the raw PyObject return
// value.  Caller should DECREF the return value and also manage the GIL.
PyObject* wxPyCallbackHelper::callCallbackObj(PyObject* argTuple) const {
    PyObject* result;

    // Save a copy of the pointer in case the callback generates another
    // callback.  In that case m_lastFound will have a different value when
    // it gets back here...
    PyObject* method = m_lastFound;

    result = PyEval_CallObject(method, argTuple);
    Py_DECREF(argTuple);
    Py_DECREF(method);
    if (!result) {
        PyErr_Print();
    }
    return result;
}


void wxPyCBH_setCallbackInfo(wxPyCallbackHelper& cbh, PyObject* self, PyObject* klass, int incref) {
    cbh.setSelf(self, klass, incref);
}

bool wxPyCBH_findCallback(const wxPyCallbackHelper& cbh, const char* name) {
    return cbh.findCallback(name);
}

int  wxPyCBH_callCallback(const wxPyCallbackHelper& cbh, PyObject* argTuple) {
    return cbh.callCallback(argTuple);
}

PyObject* wxPyCBH_callCallbackObj(const wxPyCallbackHelper& cbh, PyObject* argTuple) {
    return cbh.callCallbackObj(argTuple);
}


void wxPyCBH_delete(wxPyCallbackHelper* cbh) {
    if (cbh->m_incRef) {
        bool blocked = wxPyBeginBlockThreads();
        Py_XDECREF(cbh->m_self);
        Py_XDECREF(cbh->m_class);
        wxPyEndBlockThreads(blocked);
    }
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// These event classes can be derived from in Python and passed through the event
// system without losing anything.  They do this by keeping a reference to
// themselves and some special case handling in wxPyCallback::EventThunker.


wxPyEvtSelfRef::wxPyEvtSelfRef() {
    //m_self = Py_None;         // **** We don't do normal ref counting to prevent
    //Py_INCREF(m_self);        //      circular loops...
    m_cloned = False;
}

wxPyEvtSelfRef::~wxPyEvtSelfRef() {
    bool blocked = wxPyBeginBlockThreads();
    if (m_cloned)
        Py_DECREF(m_self);
    wxPyEndBlockThreads(blocked);
}

void wxPyEvtSelfRef::SetSelf(PyObject* self, bool clone) {
    bool blocked = wxPyBeginBlockThreads();
    if (m_cloned)
        Py_DECREF(m_self);
    m_self = self;
    if (clone) {
        Py_INCREF(m_self);
        m_cloned = True;
    }
    wxPyEndBlockThreads(blocked);
}

PyObject* wxPyEvtSelfRef::GetSelf() const {
    Py_INCREF(m_self);
    return m_self;
}


IMPLEMENT_ABSTRACT_CLASS(wxPyEvent, wxEvent);
IMPLEMENT_ABSTRACT_CLASS(wxPyCommandEvent, wxCommandEvent);


wxPyEvent::wxPyEvent(int winid, wxEventType commandType)
    : wxEvent(winid, commandType) {
}


wxPyEvent::wxPyEvent(const wxPyEvent& evt)
    : wxEvent(evt)
{
    SetSelf(evt.m_self, True);
}


wxPyEvent::~wxPyEvent() {
}


wxPyCommandEvent::wxPyCommandEvent(wxEventType commandType, int id)
    : wxCommandEvent(commandType, id) {
}


wxPyCommandEvent::wxPyCommandEvent(const wxPyCommandEvent& evt)
    : wxCommandEvent(evt)
{
    SetSelf(evt.m_self, True);
}


wxPyCommandEvent::~wxPyCommandEvent() {
}





//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// Convert a wxList to a Python List, only works for lists of wxObjects

PyObject* wxPy_ConvertList(wxListBase* listbase) {
    wxList*     list = (wxList*)listbase;  // this is probably bad...
    PyObject*   pyList;
    PyObject*   pyObj;
    wxObject*   wxObj;
    wxNode*     node = list->GetFirst();

    bool blocked = wxPyBeginBlockThreads();
    pyList = PyList_New(0);
    while (node) {
        wxObj = node->GetData();
        pyObj = wxPyMake_wxObject(wxObj);
        PyList_Append(pyList, pyObj);
        node = node->GetNext();
    }
    wxPyEndBlockThreads(blocked);
    return pyList;
}

//----------------------------------------------------------------------

long wxPyGetWinHandle(wxWindow* win) {

#ifdef __WXMSW__
    return (long)win->GetHandle();
#endif

#if defined(__WXGTK__) || defined(__WXX11)
    return (long)GetXWindow(win);
#endif
    
#ifdef __WXMAC__
    //return (long)MAC_WXHWND(win->MacGetTopLevelWindowRef());
    return (long)win->GetHandle();
#endif

    return 0;
}

//----------------------------------------------------------------------
// Some helper functions for typemaps in my_typemaps.i, so they won't be
// included in every file over and over again...

wxString* wxString_in_helper(PyObject* source) {
    wxString* target = NULL;

    if (!PyString_Check(source) && !PyUnicode_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "String or Unicode type required");
        return NULL;
    }
#if wxUSE_UNICODE
    PyObject* uni = source;
    if (PyString_Check(source)) {
        uni = PyUnicode_FromObject(source);
        if (PyErr_Occurred()) return NULL;
    }
    target = new wxString();
    size_t len = PyUnicode_GET_SIZE(uni);
    if (len) {
        PyUnicode_AsWideChar((PyUnicodeObject*)uni, target->GetWriteBuf(len), len);
        target->UngetWriteBuf();
    }

    if (PyString_Check(source))
        Py_DECREF(uni);
#else
    char* tmpPtr; int tmpSize;
    if (PyString_AsStringAndSize(source, &tmpPtr, &tmpSize) == -1) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert string");
        return NULL;
    }
    target = new wxString(tmpPtr, tmpSize);
#endif // wxUSE_UNICODE
    
    return target;
}


// Similar to above except doesn't use "new" and doesn't set an exception
wxString Py2wxString(PyObject* source)
{
    wxString target;

#if wxUSE_UNICODE
    // Convert to a unicode object, if not already, then to a wxString
    PyObject* uni = source;
    if (!PyUnicode_Check(source)) {
        uni = PyUnicode_FromObject(source);
        if (PyErr_Occurred()) return wxEmptyString;  // TODO:  should we PyErr_Clear?
    }    
    size_t len = PyUnicode_GET_SIZE(uni);
    if (len) {
        PyUnicode_AsWideChar((PyUnicodeObject*)uni, target.GetWriteBuf(len), len);
        target.UngetWriteBuf();
    }
    
    if (!PyUnicode_Check(source))
        Py_DECREF(uni);
#else
    // Convert to a string object if it isn't already, then to wxString
    PyObject* str = source;
    if (!PyString_Check(source)) {
        str = PyObject_Str(source);
        if (PyErr_Occurred()) return wxEmptyString;    // TODO:  should we PyErr_Clear?
    }
    char* tmpPtr; int tmpSize;
    PyString_AsStringAndSize(str, &tmpPtr, &tmpSize);
    target = wxString(tmpPtr, tmpSize);
    
    if (!PyString_Check(source))
        Py_DECREF(str);
#endif // wxUSE_UNICODE

    return target;
}


// Make either a Python String or Unicode object, depending on build mode
PyObject* wx2PyString(const wxString& src)
{
    PyObject* str;
#if wxUSE_UNICODE
    str = PyUnicode_FromWideChar(src.c_str(), src.Len());
#else
    str = PyString_FromStringAndSize(src.c_str(), src.Len());
#endif
    return str;
}


//----------------------------------------------------------------------


byte* byte_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count = PyList_Size(source);
    byte* temp = new byte[count];
    if (! temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
        if (! PyInt_Check(o)) {
            PyErr_SetString(PyExc_TypeError, "Expected a list of integers.");
            return NULL;
        }
        temp[x] = (byte)PyInt_AsLong(o);
    }
    return temp;
}


int* int_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count = PyList_Size(source);
    int* temp = new int[count];
    if (! temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
        if (! PyInt_Check(o)) {
            PyErr_SetString(PyExc_TypeError, "Expected a list of integers.");
            return NULL;
        }
        temp[x] = PyInt_AsLong(o);
    }
    return temp;
}


long* long_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count = PyList_Size(source);
    long* temp = new long[count];
    if (! temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
        if (! PyInt_Check(o)) {
            PyErr_SetString(PyExc_TypeError, "Expected a list of integers.");
            return NULL;
        }
        temp[x] = PyInt_AsLong(o);
    }
    return temp;
}


char** string_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count = PyList_Size(source);
    char** temp = new char*[count];
    if (! temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
        if (! PyString_Check(o)) {
            PyErr_SetString(PyExc_TypeError, "Expected a list of strings.");
            return NULL;
        }
        temp[x] = PyString_AsString(o);
    }
    return temp;
}

//--------------------------------
// Part of patch from Tim Hochberg
static inline bool wxPointFromObjects(PyObject* o1, PyObject* o2, wxPoint* point) {
    if (PyInt_Check(o1) && PyInt_Check(o2)) {
        point->x = PyInt_AS_LONG(o1);
        point->y = PyInt_AS_LONG(o2);
        return True;
    }
    if (PyFloat_Check(o1) && PyFloat_Check(o2)) {
        point->x = (int)PyFloat_AS_DOUBLE(o1);
        point->y = (int)PyFloat_AS_DOUBLE(o2);
        return True;
    }
    if (wxPySwigInstance_Check(o1) || wxPySwigInstance_Check(o2)) {  // TODO: Why???
        // Disallow instances because they can cause havok
        return False;
    }
    if (PyNumber_Check(o1) && PyNumber_Check(o2)) {
        // I believe this excludes instances, so this should be safe without INCREFFing o1 and o2
        point->x = PyInt_AsLong(o1);
        point->y = PyInt_AsLong(o2);
        return True;
    }
    return False;
}


wxPoint* wxPoint_LIST_helper(PyObject* source, int *count) {
    // Putting all of the declarations here allows
    // us to put the error handling all in one place.
    int x;
    wxPoint* temp;
    PyObject *o, *o1, *o2;
    bool isFast = PyList_Check(source) || PyTuple_Check(source);

    if (!PySequence_Check(source)) {
        goto error0;
    }

    // The length of the sequence is returned in count.
    *count = PySequence_Length(source);
    if (*count < 0) {
        goto error0;
    }

    temp = new wxPoint[*count];
    if (!temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (x=0; x<*count; x++) {
        // Get an item: try fast way first.
        if (isFast) {
            o = PySequence_Fast_GET_ITEM(source, x);
        }
        else {
            o = PySequence_GetItem(source, x);
            if (o == NULL) {
                goto error1;
            }
        }

        // Convert o to wxPoint.
        if ((PyTuple_Check(o) && PyTuple_GET_SIZE(o) == 2) ||
            (PyList_Check(o) && PyList_GET_SIZE(o) == 2)) {
            o1 = PySequence_Fast_GET_ITEM(o, 0);
            o2 = PySequence_Fast_GET_ITEM(o, 1);
            if (!wxPointFromObjects(o1, o2, &temp[x])) {
                goto error2;
            }
        }
        else if (wxPySwigInstance_Check(o)) {
            wxPoint* pt;
            if (! wxPyConvertSwigPtr(o, (void **)&pt, wxT("wxPoint"))) {
                goto error2;
            }
            temp[x] = *pt;
        }
        else if (PySequence_Check(o) && PySequence_Length(o) == 2) {
            o1 = PySequence_GetItem(o, 0);
            o2 = PySequence_GetItem(o, 1);
            if (!wxPointFromObjects(o1, o2, &temp[x])) {
                goto error3;
            }
            Py_DECREF(o1);
            Py_DECREF(o2);
        }
        else {
            goto error2;
        }
        // Clean up.
        if (!isFast)
            Py_DECREF(o);
    }
    return temp;

error3:
    Py_DECREF(o1);
    Py_DECREF(o2);
error2:
    if (!isFast)
        Py_DECREF(o);
error1:
    delete [] temp;
error0:
    PyErr_SetString(PyExc_TypeError, "Expected a sequence of length-2 sequences or wxPoints.");
    return NULL;
}
// end of patch
//------------------------------


wxBitmap** wxBitmap_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count = PyList_Size(source);
    wxBitmap** temp = new wxBitmap*[count];
    if (! temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
        if (wxPySwigInstance_Check(o)) {
            wxBitmap*    pt;
            if (! wxPyConvertSwigPtr(o, (void **) &pt, wxT("wxBitmap"))) {
                PyErr_SetString(PyExc_TypeError,"Expected wxBitmap.");
                return NULL;
            }
            temp[x] = pt;
        }
        else {
            PyErr_SetString(PyExc_TypeError, "Expected a list of wxBitmaps.");
            return NULL;
        }
    }
    return temp;
}



wxString* wxString_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count = PyList_Size(source);
    wxString* temp = new wxString[count];
    if (! temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
#if PYTHON_API_VERSION >= 1009
        if (! PyString_Check(o) && ! PyUnicode_Check(o)) {
            PyErr_SetString(PyExc_TypeError, "Expected a list of string or unicode objects.");
            return NULL;
        }
#else
        if (! PyString_Check(o)) {
            PyErr_SetString(PyExc_TypeError, "Expected a list of strings.");
            return NULL;
        }
#endif

        wxString* pStr = wxString_in_helper(o);
        temp[x] = *pStr;
        delete pStr;
    }
    return temp;
}


wxAcceleratorEntry* wxAcceleratorEntry_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count                = PyList_Size(source);
    wxAcceleratorEntry* temp = new wxAcceleratorEntry[count];
    if (! temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
        if (wxPySwigInstance_Check(o)) {
            wxAcceleratorEntry* ae;
            if (! wxPyConvertSwigPtr(o, (void **) &ae, wxT("wxAcceleratorEntry"))) {
                PyErr_SetString(PyExc_TypeError,"Expected wxAcceleratorEntry.");
                return NULL;
            }
            temp[x] = *ae;
        }
        else if (PyTuple_Check(o)) {
            PyObject* o1 = PyTuple_GetItem(o, 0);
            PyObject* o2 = PyTuple_GetItem(o, 1);
            PyObject* o3 = PyTuple_GetItem(o, 2);
            temp[x].Set(PyInt_AsLong(o1), PyInt_AsLong(o2), PyInt_AsLong(o3));
        }
        else {
            PyErr_SetString(PyExc_TypeError, "Expected a list of 3-tuples or wxAcceleratorEntry objects.");
            return NULL;
        }
    }
    return temp;
}


wxPen** wxPen_LIST_helper(PyObject* source) {
    if (!PyList_Check(source)) {
        PyErr_SetString(PyExc_TypeError, "Expected a list object.");
        return NULL;
    }
    int count = PyList_Size(source);
    wxPen** temp = new wxPen*[count];
    if (!temp) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate temporary array");
        return NULL;
    }
    for (int x=0; x<count; x++) {
        PyObject* o = PyList_GetItem(source, x);
        if (wxPySwigInstance_Check(o)) {
            wxPen*  pt;
            if (! wxPyConvertSwigPtr(o, (void **)&pt, wxT("wxPen"))) {
                delete temp;
                PyErr_SetString(PyExc_TypeError,"Expected wxPen.");
                return NULL;
            }
            temp[x] = pt;
        }
        else {
            delete temp;
            PyErr_SetString(PyExc_TypeError, "Expected a list of wxPens.");
            return NULL;
        }
    }
    return temp;
}


bool wxPy2int_seq_helper(PyObject* source, int* i1, int* i2) {
    bool isFast = PyList_Check(source) || PyTuple_Check(source);
    PyObject *o1, *o2;

    if (!PySequence_Check(source) || PySequence_Length(source) != 2)
        return False;

    if (isFast) {
        o1 = PySequence_Fast_GET_ITEM(source, 0);
        o2 = PySequence_Fast_GET_ITEM(source, 1);
    }
    else {
        o1 = PySequence_GetItem(source, 0);
        o2 = PySequence_GetItem(source, 1);
    }

    *i1 = PyInt_AsLong(o1);
    *i2 = PyInt_AsLong(o2);

    if (! isFast) {
        Py_DECREF(o1);
        Py_DECREF(o2);
    }
    return True;
}


bool wxPy4int_seq_helper(PyObject* source, int* i1, int* i2, int* i3, int* i4) {
    bool isFast = PyList_Check(source) || PyTuple_Check(source);
    PyObject *o1, *o2, *o3, *o4;

    if (!PySequence_Check(source) || PySequence_Length(source) != 4)
        return False;

    if (isFast) {
        o1 = PySequence_Fast_GET_ITEM(source, 0);
        o2 = PySequence_Fast_GET_ITEM(source, 1);
        o3 = PySequence_Fast_GET_ITEM(source, 2);
        o4 = PySequence_Fast_GET_ITEM(source, 3);
    }
    else {
        o1 = PySequence_GetItem(source, 0);
        o2 = PySequence_GetItem(source, 1);
        o3 = PySequence_GetItem(source, 2);
        o4 = PySequence_GetItem(source, 3);
    }

    *i1 = PyInt_AsLong(o1);
    *i2 = PyInt_AsLong(o2);
    *i3 = PyInt_AsLong(o3);
    *i4 = PyInt_AsLong(o4);

    if (! isFast) {
        Py_DECREF(o1);
        Py_DECREF(o2);
        Py_DECREF(o3);
        Py_DECREF(o4);
    }
    return True;
}


//----------------------------------------------------------------------

bool wxPySimple_typecheck(PyObject* source, const wxChar* classname, int seqLen)
{
    void* ptr;

    if (wxPySwigInstance_Check(source) &&
        wxPyConvertSwigPtr(source, (void **)&ptr, classname))
        return True;

    PyErr_Clear();
    if (PySequence_Check(source) && PySequence_Length(source) == seqLen)
        return True;

    return False;
}

bool wxSize_helper(PyObject* source, wxSize** obj)
{
    if (source == Py_None) {
        **obj = wxSize(-1,-1);
        return True;
    }
    return wxPyTwoIntItem_helper(source, obj, wxT("wxSize"));
}


bool wxPoint_helper(PyObject* source, wxPoint** obj)
{
    if (source == Py_None) {
        **obj = wxPoint(-1,-1);
        return True;
    }
    return wxPyTwoIntItem_helper(source, obj, wxT("wxPoint"));
}



bool wxRealPoint_helper(PyObject* source, wxRealPoint** obj) {

    if (source == Py_None) {
        **obj = wxRealPoint(-1,-1);
        return True;
    }

    // If source is an object instance then it may already be the right type
    if (wxPySwigInstance_Check(source)) {
        wxRealPoint* ptr;
        if (! wxPyConvertSwigPtr(source, (void **)&ptr, wxT("wxRealPoint")))
            goto error;
        *obj = ptr;
        return True;
    }
    // otherwise a 2-tuple of floats is expected
    else if (PySequence_Check(source) && PyObject_Length(source) == 2) {
        PyObject* o1 = PySequence_GetItem(source, 0);
        PyObject* o2 = PySequence_GetItem(source, 1);
        if (!PyNumber_Check(o1) || !PyNumber_Check(o2)) {
            Py_DECREF(o1);
            Py_DECREF(o2);
            goto error;
        }
        **obj = wxRealPoint(PyFloat_AsDouble(o1), PyFloat_AsDouble(o2));
        Py_DECREF(o1);
        Py_DECREF(o2);
        return True;
    }

 error:
    PyErr_SetString(PyExc_TypeError, "Expected a 2-tuple of floats or a wxRealPoint object.");
    return False;
}



bool wxRect_helper(PyObject* source, wxRect** obj) {

    if (source == Py_None) {
        **obj = wxRect(-1,-1,-1,-1);
        return True;
    }

    // If source is an object instance then it may already be the right type
    if (wxPySwigInstance_Check(source)) {
        wxRect* ptr;
        if (! wxPyConvertSwigPtr(source, (void **)&ptr, wxT("wxRect")))
            goto error;
        *obj = ptr;
        return True;
    }
    // otherwise a 4-tuple of integers is expected
    else if (PySequence_Check(source) && PyObject_Length(source) == 4) {
        PyObject* o1 = PySequence_GetItem(source, 0);
        PyObject* o2 = PySequence_GetItem(source, 1);
        PyObject* o3 = PySequence_GetItem(source, 2);
        PyObject* o4 = PySequence_GetItem(source, 3);
        if (!PyNumber_Check(o1) || !PyNumber_Check(o2) ||
            !PyNumber_Check(o3) || !PyNumber_Check(o4)) {
            Py_DECREF(o1);
            Py_DECREF(o2);
            Py_DECREF(o3);
            Py_DECREF(o4);
            goto error;
        }
        **obj = wxRect(PyInt_AsLong(o1), PyInt_AsLong(o2),
                       PyInt_AsLong(o3), PyInt_AsLong(o4));
        Py_DECREF(o1);
        Py_DECREF(o2);
        Py_DECREF(o3);
        Py_DECREF(o4);
        return True;
    }

 error:
    PyErr_SetString(PyExc_TypeError, "Expected a 4-tuple of integers or a wxRect object.");
    return False;
}



bool wxColour_helper(PyObject* source, wxColour** obj) {

    if (source == Py_None) {
        **obj = wxNullColour;
        return True;
    }

    // If source is an object instance then it may already be the right type
    if (wxPySwigInstance_Check(source)) {
        wxColour* ptr;
        if (! wxPyConvertSwigPtr(source, (void **)&ptr, wxT("wxColour")))
            goto error;
        *obj = ptr;
        return True;
    }
    // otherwise check for a string
    else if (PyString_Check(source) || PyUnicode_Check(source)) {
        wxString spec = Py2wxString(source);
        if (spec.GetChar(0) == '#' && spec.Length() == 7) {  // It's  #RRGGBB
            long red, green, blue;
            red = green = blue = 0;
            spec.Mid(1,2).ToLong(&red,   16);
            spec.Mid(3,2).ToLong(&green, 16);
            spec.Mid(5,2).ToLong(&blue,  16);

            **obj = wxColour(red, green, blue);
            return True;
        }
        else {                                       // it's a colour name
            **obj = wxColour(spec);
            return True;
        }
    }
    // last chance: 3-tuple of integers is expected
    else if (PySequence_Check(source) && PyObject_Length(source) == 3) {
        PyObject* o1 = PySequence_GetItem(source, 0);
        PyObject* o2 = PySequence_GetItem(source, 1);
        PyObject* o3 = PySequence_GetItem(source, 2);
        if (!PyNumber_Check(o1) || !PyNumber_Check(o2) || !PyNumber_Check(o3)) {
            Py_DECREF(o1);
            Py_DECREF(o2);
            Py_DECREF(o3);
            goto error;
        }
        **obj = wxColour(PyInt_AsLong(o1), PyInt_AsLong(o2), PyInt_AsLong(o3));
        Py_DECREF(o1);
        Py_DECREF(o2);
        Py_DECREF(o3);
        return True;
    }

 error:
    PyErr_SetString(PyExc_TypeError,
                    "Expected a wxColour object or a string containing a colour name or '#RRGGBB'.");
    return False;
}


bool wxColour_typecheck(PyObject* source) {

    if (wxPySimple_typecheck(source, wxT("wxColour"), 3))
        return True;

    if (PyString_Check(source) || PyUnicode_Check(source))
        return True;

    return False;
}



bool wxPoint2D_helper(PyObject* source, wxPoint2D** obj) {

    if (source == Py_None) {
        **obj = wxPoint2D(-1,-1);
        return True;
    }

    // If source is an object instance then it may already be the right type
    if (wxPySwigInstance_Check(source)) {
        wxPoint2D* ptr;
        if (! wxPyConvertSwigPtr(source, (void **)&ptr, wxT("wxPoint2D")))
            goto error;
        *obj = ptr;
        return True;
    }
    // otherwise a length-2 sequence of floats is expected
    if (PySequence_Check(source) && PySequence_Length(source) == 2) {
        PyObject* o1 = PySequence_GetItem(source, 0);
        PyObject* o2 = PySequence_GetItem(source, 1);
        // This should really check for floats, not numbers -- but that would break code.
        if (!PyNumber_Check(o1) || !PyNumber_Check(o2)) {
            Py_DECREF(o1);
            Py_DECREF(o2);
            goto error;
        }
        **obj = wxPoint2D(PyFloat_AsDouble(o1), PyFloat_AsDouble(o2));
        Py_DECREF(o1);
        Py_DECREF(o2);
        return True;
    }
 error:
    PyErr_SetString(PyExc_TypeError, "Expected a 2-tuple of floats or a wxPoint2D object.");
    return False;
}


//----------------------------------------------------------------------

PyObject* wxArrayString2PyList_helper(const wxArrayString& arr) {

    PyObject* list = PyList_New(0);
    for (size_t i=0; i < arr.GetCount(); i++) {
#if wxUSE_UNICODE
        PyObject* str = PyUnicode_FromWideChar(arr[i].c_str(), arr[i].Len());
#else
	PyObject* str = PyString_FromStringAndSize(arr[i].c_str(), arr[i].Len());
#endif
        PyList_Append(list, str);
        Py_DECREF(str);
    }
    return list;
}


PyObject* wxArrayInt2PyList_helper(const wxArrayInt& arr) {

    PyObject* list = PyList_New(0);
    for (size_t i=0; i < arr.GetCount(); i++) {
        PyObject* number = PyInt_FromLong(arr[i]);
        PyList_Append(list, number);
        Py_DECREF(number);
    }
    return list;
}


//----------------------------------------------------------------------
//----------------------------------------------------------------------




