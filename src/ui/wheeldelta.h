/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#ifndef WHEELDELTA_H
#define WHEELDELTA_H

#include <QWheelEvent>


//! @file wheeldelta.h wheelDelta()

//! The value QWheelEvent::delta() returned for a wheel event: the angle of the axis the event is mostly about
/*!
 *  QWheelEvent::delta() is deprecated since Qt 5.15 and not in Qt 6, and angleDelta().y() is no replacement for it:
 *  for a sideways scroll (a tilt wheel, a horizontal trackpad scroll) y is 0 and delta() was the horizontal angle.
 *
 *  A widget gets its wheel events from QWidgetWindow::handleWheelEvent(), which builds them with the QWheelEvent
 *  constructor that takes no delta() (the one with the Qt 4 value is behind QT_DEPRECATED_SINCE( 5, 0 ), false in a
 *  Qt that is built the usual way, QT_DISABLE_DEPRECATED_BEFORE being Qt 5.0). That constructor sets delta() from the
 *  angle delta alone: the horizontal angle if its magnitude is above the vertical one, else the vertical angle.
 *  This is that rule.
 *
 *  The platform hands a scroll along both axes to Qt as two events, the second with a null angleDelta(), and
 *  QWindowSystemInterface gives that second event the horizontal angle as its delta(), but no widget ever sees that:
 *  to a widget the second event has delta() 0, as has any event with a null angleDelta() (the scroll phase events).
 */
inline int wheelDelta( const QWheelEvent * e )
{
	const QPoint a = e->angleDelta();

	return qAbs( a.x() ) > qAbs( a.y() ) ? a.x() : a.y();
}

#endif
