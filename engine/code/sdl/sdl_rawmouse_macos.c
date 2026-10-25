/*
===========================================================================
Copyright (C) 2026 STRAFE 64

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// macOS raw mouse input (in_mouse 2): reads the relative X/Y counts straight
// from the HID mouse reports through IOHIDManager, before the window server
// applies pointer acceleration. SDL relative mode on macOS hands us the
// accelerated, point-scaled deltas instead.
//
// The manager is scheduled on a private run loop mode that only
// IN_RawMouse_Read pumps, so reports are collected exactly once per input
// frame on the main thread and never race the SDL event pump.

#include "sdl_rawmouse.h"

#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>

#define RAWMOUSE_RUNLOOP_MODE CFSTR( "STRAFE64RawMouse" )

static IOHIDManagerRef	rawManager;
static long				rawDX, rawDY;
static int				rawReports;

static void IN_RawMouse_Value( void *context, IOReturn result, void *sender, IOHIDValueRef value )
{
	IOHIDElementRef	element = IOHIDValueGetElement( value );
	uint32_t		page = IOHIDElementGetUsagePage( element );
	uint32_t		usage = IOHIDElementGetUsage( element );
	CFIndex			v;

	if( page != kHIDPage_GenericDesktop || !IOHIDElementIsRelative( element ) )
		return;

	v = IOHIDValueGetIntegerValue( value );
	if( usage == kHIDUsage_GD_X )
		rawDX += v;
	else if( usage == kHIDUsage_GD_Y )
		rawDY += v;
	else
		return;

	rawReports++;
}

static CFDictionaryRef IN_RawMouse_Match( int usage )
{
	int					page = kHIDPage_GenericDesktop;
	CFNumberRef			nums[ 2 ];
	const void			*keys[ 2 ] = { CFSTR( kIOHIDDeviceUsagePageKey ), CFSTR( kIOHIDDeviceUsageKey ) };
	CFDictionaryRef		dict;

	nums[ 0 ] = CFNumberCreate( kCFAllocatorDefault, kCFNumberIntType, &page );
	nums[ 1 ] = CFNumberCreate( kCFAllocatorDefault, kCFNumberIntType, &usage );
	dict = CFDictionaryCreate( kCFAllocatorDefault, keys, (const void **)nums, 2,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks );
	CFRelease( nums[ 0 ] );
	CFRelease( nums[ 1 ] );
	return dict;
}

/*
===============
IN_RawMouse_Init

Returns qfalse (leaving SDL in charge) if no HID access, e.g. when macOS
denies the device open.
===============
*/
qboolean IN_RawMouse_Init( void )
{
	CFDictionaryRef	match[ 2 ];
	CFArrayRef		matches;
	IOReturn		ret;

	if( rawManager )
		return qtrue;

	rawManager = IOHIDManagerCreate( kCFAllocatorDefault, kIOHIDOptionsTypeNone );
	if( !rawManager )
		return qfalse;

	match[ 0 ] = IN_RawMouse_Match( kHIDUsage_GD_Mouse );
	match[ 1 ] = IN_RawMouse_Match( kHIDUsage_GD_Pointer );
	matches = CFArrayCreate( kCFAllocatorDefault, (const void **)match, 2, &kCFTypeArrayCallBacks );
	IOHIDManagerSetDeviceMatchingMultiple( rawManager, matches );
	CFRelease( matches );
	CFRelease( match[ 0 ] );
	CFRelease( match[ 1 ] );

	IOHIDManagerRegisterInputValueCallback( rawManager, IN_RawMouse_Value, NULL );
	IOHIDManagerScheduleWithRunLoop( rawManager, CFRunLoopGetCurrent( ), RAWMOUSE_RUNLOOP_MODE );

	ret = IOHIDManagerOpen( rawManager, kIOHIDOptionsTypeNone );
	if( ret != kIOReturnSuccess )
	{
		Com_Printf( "IN_RawMouse_Init: IOHIDManagerOpen failed (0x%08x)\n", ret );
		IN_RawMouse_Shutdown( );
		return qfalse;
	}

	rawDX = rawDY = 0;
	rawReports = 0;
	return qtrue;
}

void IN_RawMouse_Shutdown( void )
{
	if( !rawManager )
		return;

	IOHIDManagerUnscheduleFromRunLoop( rawManager, CFRunLoopGetCurrent( ), RAWMOUSE_RUNLOOP_MODE );
	IOHIDManagerClose( rawManager, kIOHIDOptionsTypeNone );
	CFRelease( rawManager );
	rawManager = NULL;
}

/*
===============
IN_RawMouse_Read

Pumps the pending HID reports and returns the counts accumulated since the
last call. reports (optional) receives the number of X/Y values seen.
===============
*/
void IN_RawMouse_Read( int *dx, int *dy, int *reports )
{
	*dx = *dy = 0;
	if( reports )
		*reports = 0;

	if( !rawManager )
		return;

	while( CFRunLoopRunInMode( RAWMOUSE_RUNLOOP_MODE, 0, true ) == kCFRunLoopRunHandledSource )
		;

	*dx = (int)rawDX;
	*dy = (int)rawDY;
	if( reports )
		*reports = rawReports;
	rawDX = rawDY = 0;
	rawReports = 0;
}
