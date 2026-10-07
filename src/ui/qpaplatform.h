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

#ifndef QPAPLATFORM_H
#define QPAPLATFORM_H

#include <QDir>
#include <QLibraryInfo>
#include <QString>
#include <QStringList>


//! @file qpaplatform.h preferredQpaPlatform(), xcbPlatformPluginInstalled()

//! The Qt platform plugin to ask for instead of the one Qt would pick, or an empty string to leave the choice to Qt
/*!
 *  The 3D view and the UV editor are QGLWidgets, and a QGLWidget inside a QGraphicsView draws nothing under Qt 5's
 *  native Wayland platform plugin: the viewport stays see-through. Qt 5 chooses Wayland by itself when it is
 *  installed and the session runs on Wayland, so this asks for the X11 plugin (through XWayland) instead.
 *
 *  It does so only when nothing has been asked for already and the X11 plugin can work: QT_QPA_PLATFORM is not set,
 *  there is no -platform argument, the session is a Wayland one (\p waylandDisplay, WAYLAND_DISPLAY) and has an X
 *  server to talk to (\p x11Display, DISPLAY), and the xcb plugin is installed. -no-gui starts a QCoreApplication,
 *  which has no platform. Setting QT_QPA_PLATFORM=wayland brings the native plugin back.
 *
 *  -platformtheme and -platformpluginpath are other options, not a choice of platform, and do not count.
 */
inline QString preferredQpaPlatform( const QString & qtQpaPlatform, const QStringList & arguments,
                                     const QString & waylandDisplay, const QString & x11Display, bool xcbPluginInstalled )
{
	if ( !qtQpaPlatform.isEmpty() || waylandDisplay.isEmpty() || x11Display.isEmpty() || !xcbPluginInstalled )
		return QString();

	for ( const QString & a : arguments ) {
		if ( a == "-no-gui" || a == "-platform" || a == "--platform" || a.startsWith( "-platform=" ) || a.startsWith( "--platform=" ) )
			return QString();
	}

	return QStringLiteral( "xcb" );
}

//! Whether Qt can find its xcb (X11) platform plugin: in QT_PLUGIN_PATH or in the plugin directory of this Qt
/*!
 *  Meant for before the QApplication exists, so it looks at the directories directly (the library paths of
 *  QCoreApplication need the application directory). The file name is the one Qt gives the plugin on Linux.
 */
inline bool xcbPlatformPluginInstalled()
{
	QStringList dirs = QString::fromLocal8Bit( qgetenv( "QT_PLUGIN_PATH" ) ).split( QDir::listSeparator(), Qt::SkipEmptyParts );
	dirs << QLibraryInfo::location( QLibraryInfo::PluginsPath );

	for ( const QString & dir : qAsConst( dirs ) ) {
		if ( QDir( dir ).exists( QStringLiteral( "platforms/libqxcb.so" ) ) )
			return true;
	}

	return false;
}

#endif
