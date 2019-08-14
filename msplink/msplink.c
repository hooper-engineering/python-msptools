/*
This file is part of python-msptools.

Python-msptools is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Python-msptools is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with python-msptools.  If not, see <https://www.gnu.org/licenses/>.
*/

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <pthread.h>

#include "msplink.h"
#include "parse.h"
#include "send.h"
#include "serial.h"

// Custom Exceptions
PyObject* MspExc_Exception = NULL;
PyObject* MspExc_CommError = NULL;
PyObject* MspExc_NoResponse = NULL;
PyObject* MspExc_NACK = NULL;
PyObject* MspExc_BadChecksum = NULL;

// Return packet type
PyTypeObject* pyMspPacketType;
PyTypeObject pyMspPacketTypeStore;


// Currently this is written to handle only one MSP link, but by allocating these structures
// dynamically and binding to a Python object, more than one link could be handled at once.
mspdev_t mspDevice;
mspPacket_t mspResponse;


/**
 *  Pack the MspPacketType objct with received data
 *
 *  @param rx   [in]    The received packet
 *
 *  This function serves to adapt the internal representation of
 *  a received packet with the Python representation.
 *
 */
PyObject *packResponse(mspPacket_t* rx) {

    PyObject* pyMspPacket = PyStructSequence_New(pyMspPacketType);
    if (!pyMspPacket) return NULL;
        
    char temp[2] = {0,0};
    PyObject* packetItem;

    temp[0] = rx->version;
    packetItem = PyUnicode_FromStringAndSize(temp, 1);
    if (packetItem == NULL) return NULL;
    PyStructSequence_SET_ITEM(pyMspPacket, 0, packetItem);

    temp[0] = rx->direction;
    packetItem = PyUnicode_FromStringAndSize(temp, 1);
    if (packetItem == NULL) return NULL;
    PyStructSequence_SET_ITEM(pyMspPacket, 1, packetItem);

    if (rx->version == 'X') {
        packetItem = PyLong_FromUnsignedLong(rx->flag);
        if (packetItem == NULL) return NULL;
        PyStructSequence_SET_ITEM(pyMspPacket, 2, packetItem);
    }
    else {
        Py_INCREF(Py_None);
        PyStructSequence_SET_ITEM(pyMspPacket, 2, Py_None);
    }

    packetItem = PyLong_FromUnsignedLong(rx->function);
    if (packetItem == NULL) return NULL;
    PyStructSequence_SET_ITEM(pyMspPacket, 3, packetItem);

    packetItem = PyBytes_FromStringAndSize((char*)rx->payload, rx->payload_size);
    if (packetItem == NULL) return NULL;
    PyStructSequence_SET_ITEM(pyMspPacket, 4, packetItem);

    packetItem = PyLong_FromUnsignedLong(rx->checksum);
    if (packetItem == NULL) return NULL;
    PyStructSequence_SET_ITEM(pyMspPacket, 5, packetItem);

    return pyMspPacket;
}


/**
 *  Throw the appropriate Python exception based on the passed-in return code
 *
 *  @param returncode   [in]    A return code from internal function calls, as defined in the enum MSP_ERRORS
 *
 */
int throwError(int returncode) {

    switch(returncode) {
    case MSP_OK:
        break;
    case MSP_SYSCALL_FAIL:
        errno = mspDevice.errornum;
        PyErr_SetFromErrno(PyExc_OSError);
        break;
    case MSP_TX_FAIL:
        // This error is really pretty serious, so throw as a base msplink.Exception instead of a msplink.CommError
        PyErr_SetString(MspExc_Exception, "Failed to write all bytes into transmit buffer");
        break;
    case MSP_RX_FAIL:
        PyErr_SetString(MspExc_NoResponse, "Failed to read expected number of bytes from input");
        break;
    case MSP_RX_SYNC_NOT_FOUND:
        PyErr_SetString(MspExc_NoResponse, "Could not find sync byte");
        break;
    case MSP_LIB_INTERNAL_ERROR:
        PyErr_SetString(MspExc_Exception,
            "You found an msplink bug. Please consider reporting it with example code on github!");
        break;
    case MSP_OUT_OF_MEMORY:
        PyErr_SetString(PyExc_MemoryError,
            "Payload data does not fit in allocated buffer");
        break;
    default:
        PyErr_Format(MspExc_Exception,
            "An unknown error occurred (%i). Please consider reporting it with example code on github!",
            returncode);
        break;
    }

    return returncode;
}

/**
 *  Throw the appropriate Python exception based on the passed-in return code
 *
 *  @param returncode   [in]    A return code from internal function calls, as defined in the enum MSP_ERRORS
 *  @param rxPkt        [in]    Packet data to attach to the exception
 *
 *  In contrast with throwError(), this function is only called when there is the possibility to attach
 *  the returned packet data to the exception.
 *
 */
int throwPacketError(int returncode, mspPacket_t *rxPkt) {
    PyObject* errorResponse = NULL;

    assert(rxPkt);

    switch(returncode) {
    case MSP_OK:
        break;
    case MSP_RX_CHECKSUM_MISMATCH:
        assert(rxPkt);
        errorResponse = packResponse(rxPkt);
        if (errorResponse == NULL) {return returncode;}
        PyErr_SetObject(MspExc_BadChecksum, errorResponse);
        break;
    case MSP_RX_CLIENT_NACK:
        assert(rxPkt);
        errorResponse = packResponse(rxPkt);
        if (errorResponse == NULL) {return returncode;}
        PyErr_SetObject(MspExc_NACK, errorResponse);
        break;
    default:
        return throwError(returncode);
    }

    return returncode;
}

/**
 *  Opens an MSP link to the given serial device
 *
 *  Python parameters are: serial_device, read_retries, and msp_version.
 *  serial_device is required, and may be a string or a Python path-like object.
 *
 *  This function is thread-safe, protected by a mutex against running concurrently
 *  with itself or other function calls.
 */
static PyObject *pyMsplinkOpen(PyObject *self, PyObject *args, PyObject *kwargs)
{

    const char* PARAM_FORMAT = "O&|$ii:open";
    char* PARAM_NAMES[] = {"serial_device", "read_retries", "msp_version", NULL};

    const char* devname;
    PyObject* pyoPath = NULL;       // This will be a PyBytesObject*

    int ret = 0;

    mspdev_t *mdev = &mspDevice;

    // The instance lock ends up needing to be around everything since there is a lot of fiddling with
    // mdev inside these outer Python functions. We're releasing the GIL to allow *other* code to run,
    // not make a re-entrant mess. If this is eventually turned into a multi-connection capable library,
    // this lock will conveniently guard each *instance* and allow connections to run fully concurrently.

    // Note that this presents an *extreme* hazard of deadlock:
    //  1) Python holds GIL
    //  2) Enters function, acquires instanceLock
    //  3) Releases GIL during serial operation
    //  4) Python acquires GIL in another thread, evil user tries to re-enter
    //     this module (possibly by calling open() and then quickly get() in a different thread).
    //  5) New thread blocks trying to acquire an already locked instanceLock
    //  6) Previous thread is unable to reacquire GIL because a Python thread holding the GIL is busy in (5).


    Py_BEGIN_ALLOW_THREADS                          // This is either brilliant or horrifying
    pthread_mutex_lock(&(mdev->instanceLock));      // but it should prevent a deadlock 
    Py_END_ALLOW_THREADS                            // between instanceLock and the GIL

    if (mspDevice.device_open) {
        PyErr_SetString(MspExc_Exception, "An msplink connection is already open");
        goto release_mutex_handler;
    }

    if ( !PyArg_ParseTupleAndKeywords(
            args, 
            kwargs, 
            PARAM_FORMAT,
            PARAM_NAMES,
            PyUnicode_FSConverter, &pyoPath,    // pyoPath is a bytes object that must be released later!
            &(mspDevice.read_retries), 
            &(mspDevice.mspversion)
         )
    ) {
        Py_XDECREF(pyoPath);                    // just in case ParseTuple leaves a mess on failure
        goto release_mutex_handler;
    }

    // Copy path name to our memory
    devname = PyBytes_AsString(pyoPath);
    if (devname == NULL) {
        Py_XDECREF(pyoPath);
        goto release_mutex_handler;
    }

    mspDevice.devname = malloc(PyBytes_Size(pyoPath)+1);
    if (mspDevice.devname == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate space for device name string");
        goto release_mutex_handler;
    }

    // This may look weird but PyBytesObject's buffer is guaranteed to have len(o)+1 with a NULL terminator in that last location
    memcpy(mspDevice.devname, devname, PyBytes_Size(pyoPath)+1);
    Py_XDECREF(pyoPath);
    // End path handling


    if (mspDevice.mspversion != 1 && mspDevice.mspversion != 2) {
        PyErr_Format(PyExc_ValueError, "msp_version must be 1 or 2 (got %i)", mspDevice.mspversion);
        goto release_mutex_handler;
    }

    if (mspDevice.read_retries <= 0) {
        PyErr_Format(PyExc_ValueError,
                    "read_retries must be a positive number (got %i, default is %i)",
                    mspDevice.read_retries, MSP_RETRY_DEFAULT);
        goto release_mutex_handler;
    }

    Py_BEGIN_ALLOW_THREADS
    ret = msplink_open(&mspDevice);
    Py_END_ALLOW_THREADS
    if (ret<0) {
        throwError(ret);
        goto release_mutex_handler;
    }

    mspDevice.device_open = 1;
    pthread_mutex_unlock(&(mdev->instanceLock));

    Py_RETURN_NONE;

release_mutex_handler:
    pthread_mutex_unlock(&(mdev->instanceLock));
    return NULL;
}


/**
 *  Closes the MSP link
 *
 *  This function is thread-safe, protected by a mutex against running concurrently
 *  with itself or other function calls.
 */
static PyObject *pyMsplinkClose(PyObject *self, PyObject __attribute__((__unused__)) *always_null)
{
    mspdev_t *mdev = &mspDevice;

    Py_BEGIN_ALLOW_THREADS
    pthread_mutex_lock(&(mdev->instanceLock));
    Py_END_ALLOW_THREADS

    if (!mspDevice.device_open) {
        PyErr_WarnEx(PyExc_ResourceWarning, "You appear to be closing an already closed msplink.", 1);
    }

    if (mspDevice.devname != NULL) {
        free(mspDevice.devname);
        mspDevice.devname = NULL;
    }

    mspDevice.device_open = 0;

    if (throwError(msplink_close(&mspDevice)) < 0) {goto release_mutex_handler;}

    pthread_mutex_unlock(&(mdev->instanceLock));
    Py_RETURN_NONE;

release_mutex_handler:
    pthread_mutex_unlock(&(mdev->instanceLock));
    return NULL;
}

/**
 *  Sends the given command and payload data to the MSP responder
 *
 *  Python parameters are: command, payload, flag, and wait_for_ack.
 *  command and payload are required.
 *
 *  This function is thread-safe, protected by a mutex against running concurrently
 *  with itself or other function calls.
 */
static PyObject *pyMsplinkSet(PyObject *self, PyObject *args, PyObject *kwargs) {

    const char* PARAM_FORMAT = "hy*|$bp:set";
    char* PARAM_NAMES[] = {"command", "payload", "flag", "wait_for_ack", NULL};


    uint16_t cmd=0;
    uint8_t flag=0;
    int wait_for_ack=1;
    Py_buffer payload;

    int retval = MSP_OK;

    mspdev_t *mdev = &mspDevice;


    Py_BEGIN_ALLOW_THREADS
    pthread_mutex_lock(&(mdev->instanceLock));
    Py_END_ALLOW_THREADS

    if (!mspDevice.device_open) {
        PyErr_SetString(MspExc_Exception, "You must call msplink.open successfully first");
        goto release_mutex_handler;
    }

    if (
    !PyArg_ParseTupleAndKeywords(
        args, 
        kwargs,
        PARAM_FORMAT, 
        PARAM_NAMES,
        &cmd, 
        &payload, 
        &flag, 
        &wait_for_ack
    )
    ) {goto release_buffer_and_mutex_handler;}

    // Note: From this point on, Py_buffer payload needs to be released to prevent a memory leak!

    if(!PyBuffer_IsContiguous(&payload, 'C')) {
        PyErr_SetString(PyExc_BufferError, "Input data must be a bytes-like object with contiguous layout");
        goto release_buffer_and_mutex_handler;
    }

    Py_BEGIN_ALLOW_THREADS
    retval = msplink_clearRxBuffer(&mspDevice);
    Py_END_ALLOW_THREADS

    if(retval < 0) {
        throwError(retval);
        goto release_buffer_and_mutex_handler;
    }

    switch (mspDevice.mspversion) {
        case 1:
            if (cmd > 255) {
                PyErr_SetString(PyExc_ValueError, "Command can't be greater than 255 when using MSP v1");
                goto release_buffer_and_mutex_handler;
            }
            Py_BEGIN_ALLOW_THREADS
            retval = send_V1(&mspDevice, cmd, payload.buf, payload.len);
            Py_END_ALLOW_THREADS
            if (retval < 0) {
                throwError(retval);
                goto release_buffer_and_mutex_handler;
            }
            break;
        case 2:
            Py_BEGIN_ALLOW_THREADS
            retval = send_V2(&mspDevice, flag, cmd, payload.buf, payload.len);
            Py_END_ALLOW_THREADS
            if (retval < 0) {
                throwError(retval);
                goto release_buffer_and_mutex_handler;
            }
            break;
        default:
            PyErr_SetString(MspExc_Exception,
                "You found an msplink bug in pyMsplinkSet(). Please consider reporting it with example code on github!");
            goto release_buffer_and_mutex_handler;
    }

    PyBuffer_Release(&payload);     // input payload is no longer needed, go ahead and allow Python to reclaim it


    // Usually you will want to wait on the ACK packet. This can be bypassed for speed if you're careful,
    // but if the client has a shared TX/RX buffer it can cause problems.
    if (wait_for_ack) {

        Py_BEGIN_ALLOW_THREADS
        retval = parse_packet(&mspDevice, &mspResponse);
        Py_END_ALLOW_THREADS
        if(retval < 0) {
            throwPacketError(retval, &mspResponse);
            goto release_mutex_handler;
        }

        // Otherwise just return what we got as-is
        pthread_mutex_unlock(&(mdev->instanceLock));
        return packResponse(&mspResponse);      // normal termination with response
    }

    pthread_mutex_unlock(&(mdev->instanceLock));
    Py_RETURN_NONE;                             // normal termination without response


release_buffer_and_mutex_handler:
    PyBuffer_Release(&payload);  
release_mutex_handler:
    pthread_mutex_unlock(&(mdev->instanceLock));
    return NULL;

}

/**
 *  Gets the requested data from an MSP responder
 *
 *  Python parameters are: command and flag, where command is required.
 *
 *  On success, this function returns the requested data in the payload field
 *  of an MspPacketType object.
 *
 *  This function is thread-safe, protected by a mutex against running concurrently
 *  with itself or other function calls.
 */
static PyObject *pyMsplinkGet(PyObject *self, PyObject *args, PyObject *kwargs) {


    const char* PARAM_FORMAT = "h|$b:get";
    char* PARAM_NAMES[] = {"command", "flag", NULL};

    uint16_t cmd=0;
    uint8_t flag=0;
    int retval = MSP_OK;

    mspdev_t *mdev = &mspDevice;


    Py_BEGIN_ALLOW_THREADS
    pthread_mutex_lock(&(mdev->instanceLock));
    Py_END_ALLOW_THREADS

    if (!mspDevice.device_open) {
        PyErr_SetString(MspExc_Exception, "You must call msplink.open successfully first");
        goto release_mutex_handler;
    }

    if (
    !PyArg_ParseTupleAndKeywords(
        args, 
        kwargs, 
        PARAM_FORMAT,
        PARAM_NAMES,
        &cmd, &flag
    )
    ) {goto release_mutex_handler;}


    Py_BEGIN_ALLOW_THREADS
    retval = msplink_clearRxBuffer(&mspDevice);
    Py_END_ALLOW_THREADS
    if(retval < 0) {
        throwError(retval);
        goto release_mutex_handler;
    }

    switch (mspDevice.mspversion) {
        case 1:
            if (cmd > 255) {
                PyErr_SetString(PyExc_ValueError, "Command can't be greater than 255 when using MSP v1");
                goto release_mutex_handler;
            }
            Py_BEGIN_ALLOW_THREADS
            retval = send_V1(&mspDevice, (uint8_t)cmd, NULL, 0);
            Py_END_ALLOW_THREADS
            if(retval < 0) {
                throwError(retval);
                goto release_mutex_handler;
            }
            break;
        case 2:
            Py_BEGIN_ALLOW_THREADS
            retval = send_V2(&mspDevice, flag, cmd, NULL, 0);
            Py_END_ALLOW_THREADS
            if(retval < 0) {
                throwError(retval);
                goto release_mutex_handler;
            }
            break;
        default:
            PyErr_SetString(MspExc_Exception,
                "You found an msplink bug in pyMsplinkGet(). Please consider reporting it with example code on github!");
            goto release_mutex_handler;
    }

    Py_BEGIN_ALLOW_THREADS
    retval = parse_packet(&mspDevice, &mspResponse);
    Py_END_ALLOW_THREADS
    if(retval < 0) {
        throwPacketError(retval, &mspResponse);
        goto release_mutex_handler;
    }

    pthread_mutex_unlock(&(mdev->instanceLock));
    return packResponse(&mspResponse);

release_mutex_handler:
    pthread_mutex_unlock(&(mdev->instanceLock));
    return NULL;
}

static PyMethodDef msplinkMethods[] =
{
    { "open", (PyCFunction)pyMsplinkOpen, METH_VARARGS | METH_KEYWORDS,
      "Opens an MSP connection with the given serial device"},
    { "close", (PyCFunction)pyMsplinkClose, METH_NOARGS,
      "Closes an open MSP connection"},
    { "set", (PyCFunction)pyMsplinkSet, METH_VARARGS | METH_KEYWORDS,
      "Sends data to the MSP device"},
    { "get", (PyCFunction)pyMsplinkGet, METH_VARARGS | METH_KEYWORDS,
      "Gets data from the MSP device"},
    {NULL, NULL, 0, NULL}
};


// TODO: There should be a way to declare a cleanup function here for free()-ing on an unload. Otherwise
//       mspDevice.devname doesn't get free()d.
static struct PyModuleDef  msplink_definition= { 
    PyModuleDef_HEAD_INIT,
    "msplink",
    "A module that handles a Multi-Wii Serial Protocol (MSP) device connection.",
    -1, 
    msplinkMethods
};


/**
 *  Initialize the msplink Python module
 *
 *  This function creates the MspPacketType, several exceptions, the msplink module object,
 *  and adds the exceptions to the module.
 *
 *  On failure, it attempts to decrement all created objects and return NULL,
 *  indicating a thrown error to the caller.
 */
PyMODINIT_FUNC PyInit_msplink(void) {

    PyObject* msplinkModule = NULL;

    mspDevice.device_open = 0;
    mspDevice.fd = 0;
    mspDevice.devname = NULL;
    mspDevice.read_retries = MSP_RETRY_DEFAULT;
    mspDevice.mspversion = 1;
    mspDevice.errornum = 0;


    mspdev_t *mdev = &mspDevice;


    pthread_mutex_init(&(mdev->instanceLock), NULL);


    // Initizlize module exceptions
    MspExc_Exception = PyErr_NewExceptionWithDoc(
        "msplink.Exception",
        "The base exception for the msplink module.",
        NULL,
        NULL);
    if (MspExc_Exception == NULL) {goto setup_error;}

    MspExc_CommError = PyErr_NewExceptionWithDoc(
        "msplink.CommError",
        "An error occurred reading or writing to the client device.",
        MspExc_Exception,
        NULL);
    if (MspExc_CommError == NULL) {goto setup_error;}

    MspExc_NoResponse = PyErr_NewExceptionWithDoc(
        "msplink.NoResponse",
        "A response was expected from the client, but no valid packet was received.",
        MspExc_CommError,
        NULL);
    if (MspExc_NoResponse == NULL) {goto setup_error;}

    MspExc_NACK = PyErr_NewExceptionWithDoc(
        "msplink.NACK",
        "Recieved a negative-acknowledge (NACK) from the client.",
        MspExc_CommError,
        NULL);
    if (MspExc_NACK == NULL) {goto setup_error;}

    MspExc_BadChecksum = PyErr_NewExceptionWithDoc(
        "msplink.BadChecksum",
        "Recieved checksum and calculated checksum do not match.",
        MspExc_CommError,
        NULL);
    if (MspExc_BadChecksum == NULL) {goto setup_error;}

    
    // Initialize MspPacketType
    static PyStructSequence_Field mspPacketFields[7] = 
    {
         {"version", "version character (M=1 or X=2)"},
         {"direction", "direction indicator or error character"},
         {"flag", "flag value (V2 only)"},
         {"command", "command number"},
         {"payload", "packet payload data"},
         {"checksum", "checksum value"},
         {0, NULL}
    };

    static PyStructSequence_Desc mspPacketDesc = 
    {
        "MspPacketType", 
        "Container type for received MSP packets", 
        mspPacketFields,
        6
    };

    PyStructSequence_InitType(&pyMspPacketTypeStore, &mspPacketDesc);
    pyMspPacketType = &pyMspPacketTypeStore;

    // Create module object and make exceptions accessible from the interpreter
    msplinkModule = PyModule_Create(&msplink_definition);
    if (msplinkModule == NULL) {goto setup_error;}

    if (PyModule_AddObject(msplinkModule, "Exception", MspExc_Exception) < 0) {goto setup_error;}
    if (PyModule_AddObject(msplinkModule, "CommError", MspExc_CommError) < 0) {goto setup_error;}
    if (PyModule_AddObject(msplinkModule, "NoResponse", MspExc_NoResponse) < 0) {goto setup_error;}
    if (PyModule_AddObject(msplinkModule, "NACK", MspExc_NACK) < 0) {goto setup_error;}
    if (PyModule_AddObject(msplinkModule, "BadChecksum", MspExc_BadChecksum) < 0) {goto setup_error;}

    return msplinkModule;


    // Try to be a good Python citizen and return on error without memory leaks
setup_error:
    Py_XDECREF(MspExc_Exception);
    Py_XDECREF(MspExc_CommError);
    Py_XDECREF(MspExc_NoResponse);
    Py_XDECREF(MspExc_NACK);
    Py_XDECREF(MspExc_BadChecksum);
    Py_XDECREF(msplinkModule);

    return NULL;
}
