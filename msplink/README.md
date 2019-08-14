# msplink

The msplink module is a *middleware* library that efficiently handles all aspects of the Multi-Wii Serial Protocol (MSP) below the command layer as the caller.

msplink can talk to any MSP responder (typically a UAV flight controller) but cannot itself act as a responder.

The major goals of this module are

- Implementation hiding
- Improve performance
- Thread enabled and thread safe
- Low overhead
- Ease of use

### Implementation hiding

This module abstracts away many of the details necessary to use MSP. It's especially useful in that it transparently handles MSP V1, MSP V1 with JUMBO packets, MSP V2, and valid mixtures of these.

*Note: msplink currently does not send V2 encapsulated in V1 packets but it can receive them*

Frame formatting and checksumming is handled automatically, freeing the developer to think about the command and parameter data format level.

### Improve performance / threading

Because this module uses the Python C API, it has the opportunity to "let go" of the Python interpreter, specifically the Python Global Interpreter Lock (GIL) during slow serial port operations.

If desired, this allows all serial communication to occur outside of the one-thing-at-a-time execution of pure Python programs. ([It's true! Multi-threaded pure-Python programs generally do not run threads concurrently!](https://docs.python.org/3/c-api/init.html#thread-state-and-the-global-interpreter-lock))

In this way, using msplink inside of a 'comms' thread while doing other processing in a 'processing' thread allows the processing thread to continue running while the comms thread blocks during serial access.

Additionally, the library is thread safe in that two functions in the module can't be executed simultaneously from different threads. If it is attempted, the second call will block until the first completes. Note that it's not a great idea to queue more than one call this way because they may not execute in the order that they were called. The threading scheduler will control which one executes next, and we have no control over that.

Note that it **is not necessary** to use threading, you can use msplink as a standard blocking library if desired to keep things simple or for quick-and-dirty hacking.

### Low overhead

An attempt was made to minimize the number of buffer copies and to keep the memory footprint low. By default, 1KB is statically allocated to the receive buffer, and data is processed as it arrives as much as possible.

For most Python installations, using many times more resources probably wouldn't even be noticable, but it was just as easy to do things this way.

### Ease of use

Not much needs to be said about this one, except that as a design goal this library shouldn't force the developer to do much work to get useful results.

Knowledge of [struct.pack() and struct.unpack()](https://docs.python.org/3/library/struct.html) is required, but that interface provides thorough and consistent access to payload data fields once it's groked.

Exceptions are as informative as possible, and this may be helpful for running basic tests against MSP responder implementations. Its intended use is as an interface, though, not a debugging tool. Certain conditions like data underruns and overruns may be difficult to debug due to the raw bytes being inaccessible, but communicating with a generally functional MSP responder should be relatively painless with exceptions providing feedback in ...exceptional circumstances.

## Experimental software notice

This module is early on in its development. At this stage, it's likely that bugs will be found, especially in odd corners that have received little testing (i.e. JUMBO packets, etc). The directory structure may change. The interfaces may change.

This software is provided in the hope that it will be useful and save you some time and effort, and comes with no guarantee whatsoever as to how well it works for you or any purpose.

## Installing

Ensure that Python 3 is installed using your system. Python 3.3 may work, but Python 3.5 is preferred.

Clone the repo into a directory of your choice:

```
cd ~
mkdir my_checkout
cd my_checkout
git clone https://github.com/hooper-engineering/python-msptools.git
cd python-msptools/msplink
```

Build and install the msplink package:

```
sudo python3 setup.py install
```

This should build and install the package automatically. 

## Usage

First, import msplink:

```
import msplink
```

This will create several exception types and the `MspPacketType` which is used to pass data back from `msplink.get()` commands.

### msplink.open()

Once the package is imported, you can call `msplink.open()` to initiate a connection.


 `open()` parameter | Required | Default Value | Description | Example
 -------------------|----------|-------------|-----------|---
 `serial_device`    | Yes      | *no default* | A string or a Python *path-like object*, the path of a serial device  | `"/dev/ttyUSB0"`
 `read_retries`     | No       | `3` | Number of reads allowed before the raw serial read fails. Each attempt consumes about 0.1s. | `read_retries=4`
 `msp_version`      | No       | `1` | MSP version to use (1 or 2) | `msp_version=2`
 
Note that the `serial_device` parameter is *positional* so it must occur first if it is not named, but the parameter name is optional:

```
msplink.open("/dev/ttyACM0")                 # Open a new MSP connection
```
or

```
msplink.open("/dev/ttyACM0", msp_version=2)  # Open a new MSP V2 connection
```

If `open()` is successful, it returns `None`.

If `open()` is not successful, it can throw

`open()` Exception | Cause
----------|-------
`ValueError` | Bad input parameter
`OSError` | One of the syscalls failed. Most likely you specified an invalid or inaccessible serial device.
`MemoryError` | Unable to allocate memory (i.e. an `alloc()` call failed)
`msplink.Exception` | Something else (probably catastrophic) happened.

If the `open()` completed successfully, you are now free to use `get()`, `set()`, or `close()`.

### msplink.get()

get() parameter | Required | Default value | Description | Example
----------------|----------|---------------|-------------|---------
`command`       | Yes      | *no default*   | A command number | `108` (get attitude)
`flag`          | No       | `0` or `None` | Optional flag (V2 only) | *Reserved for future use*

As with `open()`, there is a positional, required parameter and optional named parameters:

```
result = msplink.get(108)                   # Get UAV attitude
print(result)                               # Print received packet
print(struct.unpack("<3h",result.payload))  # Print attitude (X,Y,Bearing)
```

or

```
# Note: There's currently no reason to use the flag parameter,
#       but this is how you use it

result = msplink.get(108, flag=10)  # Get UAV attitude, with a custom flag value
```

If `get()` is successful, it returns a `MspPacketType` object with the following fields:

MspPacketType field | Description
--------------------|-------------
`version`             | MSP version character (`"M"` for V1 or `"X"` for V2)
`direction`           | Direction indicator or error character (`"<"`,`">"`, or `"!"`).
`flag`                | V2: flag value, V1: `None`
`command`             | Packet command number
`payload`             | A Python *bytes* object containing the returned parameters
`checksum`            | The returned checksum value

If `get()` is unsuccesful, one of the following exceptions is thrown:

`get()` Exception     | Cause
----------------------|------------------------------
`ValueError`          | Bad input parameter
`OSError` | One of the syscalls failed. Something went wrong with the serial port or your USB/serial device may have been unplugged.
`msplink.NoResponse`  | A sync byte was not received or the expected number of bytes did not arrive.
`msplink.BadChecksum` | The recieved checksum does not match the calculated checksum. The returned packet is attached to this exception.
`msplink.NACK`        | The responder replied but indicated an error. The returned packet is attached to this exception.
`msplink.Exception` | Something else (probably catastrophic) happened.

Note that `msplink.NoResponse`, `msplink.BadChecksum`, and `msplink.NACK` all inherit from `msplink.CommError`, so catching this parent exception will handle all three.

`msplink.CommError` is a less critical class of errors, and possible handler behaviors include incrementing an error counter, initiating a retry, or indicating to the control program that received data may be out of date.

### msplink.set()

set() parameter | Required | Default value | Description | Example
----------------|----------|---------------|-------------|---------
`command`       | Yes      | *no default*   | A command number | `108` (get attitude)
`payload`       | Yes      | *no default*   | Parameter data, a Python *bytes* object | *See examples*
`flag`          | No       | `0` | Optional flag (V2 only) | *Reserved for future use*
`wait_for_ack` | No      | `True`        | `wait_for_ack=False` allows `set()` to return without waiting for an ACK packet. | --

For `set()`, both `command` and `payload` are required fields, and they are positional in that order:

```
# Send sticks neutral (as four little-endian unsigned short ints)
raw_rc_values = struct.pack('<4H', 1500, 1500, 1500, 1500)
msplink.set(200, raw_rc_values)  # SET_RAW_RC

# Note: Stick values are in micro-seconds of servo pulse width.
#       The range is typically 1000us to 2000us. Yes, it does seem silly.
```

If `set()` is successful, it returns the ACK packet unless `wait_for_ack=False` was specified. In that case it will always return `None`.

If `set()` is not successful, it can throw

`set()` Exception     | Cause
----------------------|----------------------
`ValueError`          | Bad input parameter
`OSError` | One of the syscalls failed. Something went wrong with the serial port or your USB/serial device may have been unplugged.
`msplink.NoResponse`  | A sync byte was not received or the expected number of bytes did not arrive.
`msplink.BadChecksum` | The recieved checksum does not match the calculated checksum. The returned packet is attached to this exception.
`msplink.NACK`        | The responder replied but indicated an error. The returned packet is attached to this exception.
`msplink.Exception`   | Something else (probably catastrophic) happened.

Link prolems can be caught as `msplink.CommError` exceptions, just as with `get()`. Note that the `msplink.CommError` exceptions are only thrown when `wait_for_ack=True`.

#### A note about `wait_for_ack=False`:

This is provided as a mechanism for saving blocking time when you need to quickly send data to the MSP responder and return. It can cause problems when rapidly sending these commands if the responder you are talking to uses a shared TX/RX buffer, or otherwise is incapable of receiving data before the previous ACK packet is sent.

This is extra dicey because if you send a series of `set(...,wait_for_ack=False)` commands, you will not be able to tell when or whether they have failed.

In summary, *don't use it unless you need it and still don't use it unless you understand these caveats.*

### msplink.close()

Mostly included for completeness, this call will close the opened port, de-allocate resources, and allow another `msplink.open()` call if desired.

`close()` Exception   | Cause
----------------------|----------------------
`OSError`             | One of the syscalls failed. Something went wrong with the serial port or your USB/serial device may have been unplugged.

If you attempt to close an already closed connection, the module will issue a `ResourceWarning`.

## Exceptions

Exception higherarchy:

```
    msplink.Exception
    |--msplink.CommError
       |--msplink.NoResponse
       |--msplink.BadChecksum
       |--msplink.NACK
```

Exception Name         | Description  | Inherits from | Attached Object
-----------------------|--------------|---------------------|-------
`msplink.Exception`    | Generic error exception in msplink module. All msplink exceptions inherit from this exception, and a couple of less-recoverable situations throw this exception directly. | `Exception` | 
`msplink.CommError`    | An error occurred while receiving an MSP packet. | `msplink.Exception` | 
`msplink.NoResponse`   | No sync byte found or not enough bytes were received. | `msplink.CommError` | 
`msplink.BadChecksum`  | The received checksum and calculated checksum do not match. | `msplink.CommError` | MspPacketType
`msplink.NACK`         | A negative-acknowledge (NACK) response was recieved from the responder. | `msplink.CommError` | MspPacketType
`MemoryError`          | Python built-in exception. Could not allocate memory. | `Exception`
`OSError`              | Python built-in exception. The actual exception may be a sub-class of `OSError` as generated by the Python `errno` handlers. | `Exception`
`ValueError`           | Python built-in exception. Bad input parameter. | `Exception`
`BufferError`          | Python built-in exception. There was a problem with a passed-in buffer. | `Exception`

Note that `msplink.NACK` and `msplink.BadChecksum` return `MspPacketType` objects to allow closer inspection of their contents. All other exceptions contain the standard string description, unless one of these two exceptions is handled as a parent exception class.

## Examples

A program to repeatedly read responder UAV attitude:

```
#!/usr/bin/env python3
# encoding: utf-8

import msplink
import struct

msplink.open("/dev/ttyACM0", msp_version=1)

while True:
	try:
		result = msplink.get(108)                   # get UAV attitude
		print(result)
		print(struct.unpack("<3h",result.payload))  # parse and print attitude
	except msplink.CommError as e:
		print("Requested data didn't arrive!")
		print(e)
```