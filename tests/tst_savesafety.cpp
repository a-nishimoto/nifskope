#include "testenv.h"
#include "testregistry.h"

#include "message.h"
#include "model/basemodel.h"
#include "model/nifmodel.h"

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>


//! A model whose save() emits some bytes and then fails, i.e. what a crash/error half way through a save leaves behind
class PartialSaveModel final : public BaseModel
{
public:
	//! What save() writes before it returns
	QByteArray payload = QByteArray( "PARTIAL-NIF-DATA" );
	//! Whether save() reports success
	bool succeed = false;

	void clear() override {}
	//! The state of the model while load() runs, -1 if it did not
	int stateWhileLoading = -1;
	bool load( QIODevice & ) override
	{
		stateWhileLoading = int( getState() );
		return false;
	}
	bool save( QIODevice & device ) const override
	{
		device.write( payload );
		return succeed;
	}
	QString getVersion() const override { return QString(); }
	quint32 getVersionNumber() const override { return 0; }

protected:
	bool setItemValue( NifItem *, const NifValue & ) override { return false; }
	bool updateArrayItem( NifItem * ) override { return false; }
	QString ver2str( quint32 ) const override { return QString(); }
	quint32 str2ver( QString ) const override { return 0; }
	bool evalVersion( NifItem *, bool = false ) const override { return true; }
	bool setHeaderString( const QString & ) override { return false; }
};


static bool writeFile( const QString & path, const QByteArray & bytes )
{
	QFile f( path );
	return f.open( QIODevice::WriteOnly ) && f.write( bytes ) == bytes.size();
}

static QByteArray readFile( const QString & path )
{
	QFile f( path );
	return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray( "<unreadable>" );
}


//! A device that rejects every write, like a full disk or a closed pipe
class FailingDevice final : public QIODevice
{
protected:
	qint64 readData( char *, qint64 ) override { return -1; }
	qint64 writeData( const char *, qint64 ) override { return -1; }
};


//! Regression tests for 2dad25d: a failed save() must not corrupt the file on disk
class tst_SaveSafety final : public QObject
{
	Q_OBJECT

	QTemporaryDir dir;

private slots:
	void initTestCase()
	{
		QVERIFY( dir.isValid() );
		QString err = TestEnv::reloadXml();
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
	}

	void cleanup()
	{
		// Nothing here may pop up a QMessageBox (the app's way of reporting load/save problems)
		QStringList boxes = TestEnv::takeMessageBoxes();
		QVERIFY2( boxes.isEmpty(), qPrintable( boxes.join( " | " ) ) );
	}

	void failedSave_keepsExistingFile()
	{
		QString path = QDir( dir.path() ).filePath( "existing.nif" );
		QByteArray original( "ORIGINAL GOOD FILE" );
		QVERIFY( writeFile( path, original ) );

		PartialSaveModel model;
		QVERIFY( !model.saveToFile( path ) );

		// Before 2dad25d the file was opened (truncated) first and save() wrote straight into it
		QCOMPARE( readFile( path ), original );
	}

	void failedSave_doesNotCreateFile()
	{
		QString path = QDir( dir.path() ).filePath( "new.nif" );
		QVERIFY( !QFile::exists( path ) );

		PartialSaveModel model;
		QVERIFY( !model.saveToFile( path ) );

		QVERIFY2( !QFile::exists( path ), "an incomplete file was left behind" );
	}

	void failedSave_afterLargePartialWrite()
	{
		QString path = QDir( dir.path() ).filePath( "large.nif" );
		QByteArray original( "ORIGINAL" );
		QVERIFY( writeFile( path, original ) );

		PartialSaveModel model;
		model.payload = QByteArray( 4 * 1024 * 1024, 'x' );
		QVERIFY( !model.saveToFile( path ) );

		QCOMPARE( readFile( path ), original );
	}

	void successfulSave_writesEverything()
	{
		QString path = QDir( dir.path() ).filePath( "ok.nif" );

		PartialSaveModel model;
		model.succeed = true;
		model.payload = QByteArray( 3 * 1024 * 1024 + 17, 'n' );
		QVERIFY( model.saveToFile( path ) );

		QCOMPARE( readFile( path ), model.payload );
	}

	void successfulSave_replacesLongerFile()
	{
		QString path = QDir( dir.path() ).filePath( "shrink.nif" );
		QVERIFY( writeFile( path, QByteArray( 1000, 'o' ) ) );

		PartialSaveModel model;
		model.succeed = true;
		model.payload = QByteArray( "short" );
		QVERIFY( model.saveToFile( path ) );

		QCOMPARE( readFile( path ), QByteArray( "short" ) );
	}

	//! A save that produced no bytes at all is not a save: a NIF is never empty, and saveToFile() says so
	void emptySave_returnsFalse()
	{
		QString path = QDir( dir.path() ).filePath( "empty.nif" );

		PartialSaveModel model;
		model.succeed = true;
		model.payload = QByteArray();
		QVERIFY( !model.saveToFile( path ) );
	}

	void unwritablePath_returnsFalse()
	{
		PartialSaveModel model;
		model.succeed = true;
		QVERIFY( !model.saveToFile( QDir( dir.path() ).filePath( "no/such/dir/file.nif" ) ) );
	}

	void loadFromFile_failure_resetsState()
	{
		PartialSaveModel model;
		QVERIFY( !model.loadFromFile( QDir( dir.path() ).filePath( "does-not-exist.nif" ) ) );
		QCOMPARE( model.getState(), BaseModel::Default );

		// A directory is not a file
		QVERIFY( !model.loadFromFile( dir.path() ) );
		QCOMPARE( model.getState(), BaseModel::Default );
	}

	//! load() runs in the Loading state (the models hold back work that a half-loaded model must not do), and the state is Default again afterwards,
	//! whether the load worked or not
	void loadFromFile_runsInLoadingState()
	{
		QString path = QDir( dir.path() ).filePath( "some.nif" );
		QVERIFY( writeFile( path, QByteArray( "SOME BYTES" ) ) );

		PartialSaveModel model;
		QCOMPARE( model.getState(), BaseModel::Default );
		QVERIFY( !model.loadFromFile( path ) );
		QCOMPARE( model.stateWhileLoading, int( BaseModel::Loading ) );
		QCOMPARE( model.getState(), BaseModel::Default );
	}

	//! The real model reports a failing device through Message::critical (a QMessageBox), returns false and resets its state
	void nifModel_saveToFailingDevice()
	{
		TestEnv::Profile oblivion{ "Oblivion", "20.0.0.5", 11, 11 };
		auto nif = TestEnv::makeModel( oblivion );
		QVERIFY( TestEnv::buildScene( *nif ) );

		FailingDevice failing;
		QVERIFY( failing.open( QIODevice::WriteOnly ) );

		QVERIFY( !nif->save( failing ) );
		QCOMPARE( nif->getState(), BaseModel::Default );

		// This is the one place a message box is expected
		QCOMPARE( TestEnv::takeMessageBoxes().count(), 1 );

		// ... and the model is still usable
		QBuffer good;
		QVERIFY( good.open( QIODevice::WriteOnly ) );
		QVERIFY( nif->save( good ) );
		QVERIFY( good.size() > 100 );
	}

	//! The test program's message box guard (TestEnv::installMessageBoxGuard()) records a box without showing it: the QShowEvent is
	//! consumed before QMessageBox::showEvent() runs, which crashes the process on Windows' offscreen platform. It is also the
	//! function that adds the Ok button, so a box that was really shown has one.
	void messageBoxGuard_recordsWithoutShowing()
	{
		Message::critical( nullptr, "guard probe" );

		QMessageBox * probe = nullptr;
		for ( QWidget * w : QApplication::topLevelWidgets() ) {
			QMessageBox * box = qobject_cast<QMessageBox *>( w );
			if ( box && box->text() == "guard probe" )
				probe = box;
		}
		QVERIFY( probe );
		QVERIFY2( probe->buttons().isEmpty(), "QMessageBox::showEvent() ran" );

		QCOMPARE( TestEnv::takeMessageBoxes(), QStringList( "guard probe " ) );
		QVERIFY( !probe->isVisible() );
		QVERIFY( TestEnv::takeMessageBoxes().isEmpty() );
	}

	//! Message::append() keeps its boxes and shows the same one again for the same text: a box that was taken is recorded again
	void messageBoxGuard_recordsACachedBoxShownAgain()
	{
		Message::append( "guard cache", "first" );
		QCOMPARE( TestEnv::takeMessageBoxes().count(), 1 );
		QVERIFY( TestEnv::takeMessageBoxes().isEmpty() );

		Message::append( "guard cache", "second" );
		QStringList again = TestEnv::takeMessageBoxes();
		QCOMPARE( again.count(), 1 );
		QVERIFY2( again.first().contains( "first" ) && again.first().contains( "second" ), qPrintable( again.first() ) );
	}
};

REGISTER_TEST( tst_SaveSafety )

#include "tst_savesafety.moc"
