#include "testenv.h"
#include "testregistry.h"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>


//! Runs every registered test class. An optional first argument naming a class runs only that class.
int main( int argc, char * argv[] )
{
	// Headless unless the caller picked a platform; the code under test opens QMessageBoxes on some error paths
	if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) )
		qputenv( "QT_QPA_PLATFORM", "offscreen" );

	QApplication app( argc, argv );

	// No QMessageBox is ever really shown (QMessageBox::showEvent() crashes on Windows' offscreen platform)
	TestEnv::installMessageBoxGuard();

	// NifModel/NifValue read QSettings; never touch the developer's real preferences
	QCoreApplication::setOrganizationName( "NifTools" );
	QCoreApplication::setApplicationName( "NifSkope-tests" );
	QTemporaryDir settingsDir;
	QSettings::setDefaultFormat( QSettings::IniFormat );
	QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, settingsDir.path() );
	QSettings::setPath( QSettings::IniFormat, QSettings::SystemScope, settingsDir.path() );

	QStringList args = app.arguments();
	QString only;
	if ( args.count() > 1 && !args.at( 1 ).startsWith( '-' ) ) {
		only = args.at( 1 );
		args.removeAt( 1 );
	}

	QVector<QByteArray> storage;
	QVector<char *> argvCopy;
	for ( const QString & a : args ) {
		storage.append( a.toLocal8Bit() );
		argvCopy.append( storage.last().data() );
	}
	int argcCopy = argvCopy.count();

	int failed = 0;
	for ( TestRegistry::Factory make : TestRegistry::factories() ) {
		QScopedPointer<QObject> test( make() );

		if ( !only.isEmpty() && only != QLatin1String( test->metaObject()->className() ) )
			continue;

		bool classFailed = QTest::qExec( test.data(), argcCopy, argvCopy.data() ) != 0;

		// A box nobody looked for (opened in initTestCase(), or by a class without a cleanup()) fails the class as well
		QStringList stray = TestEnv::takeMessageBoxes();
		if ( !stray.isEmpty() ) {
			qCritical( "%s: message box opened and not taken: %s", test->metaObject()->className(), qPrintable( stray.join( " | " ) ) );
			classFailed = true;
		}

		failed += classFailed;
	}

	return failed;
}
