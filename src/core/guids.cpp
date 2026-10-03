// EchoPad - the single translation unit that instantiates every GUID the
// program uses.
//
// Including <initguid.h> before the SDK headers turns their DEFINE_GUID /
// DEFINE_PROPERTYKEY lines into weak definitions, so no import library has to
// provide them. MinGW-w64 in particular does not export the WASAPI GUIDs from
// libuuid.a, which is exactly why this file exists.
#include "common.h"

#include <initguid.h>

#include <propidl.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
