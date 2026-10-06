#include "testenv.h"
#include "testregistry.h"

#include "model/kfmmodel.h"

#include <QBuffer>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>


class tst_KfmModel final : public QObject
{
	Q_OBJECT

	//! A minimal valid KFM 2.0.0.0b stream: header line, then zeroes for every field (no animations).
	//! Built here rather than saved from a fresh KfmModel: see load() below.
	static QByteArray emptyKfm()
	{
		QByteArray bytes;
		QBuffer buf( &bytes );
		buf.open( QIODevice::WriteOnly );
		buf.write( ";Gamebryo KFM File Version 2.0.0.0b\n" );
		QDataStream ds( &buf );
		ds.setByteOrder( QDataStream::LittleEndian );
		ds << quint8( 0 )						// Unknown Byte
		   << quint32( 0 ) << quint32( 0 )		// NIF File Name, Master (empty strings)
		   << qint32( 0 ) << qint32( 0 )		// Unknown Int 1, 2
		   << 0.0f << 0.0f						// Unknown Float 1, 2
		   << qint32( 0 )						// Num Animations
		   << qint32( 0 );						// Unknown Int 3
		return bytes;
	}

	//! A freshly constructed KfmModel evaluates version conditions before it has a version (kfmroot is inserted
	//! before 'version' is assigned in clear()), so edit a model that went through load(), which resets them.
	static bool loadEmpty( KfmModel & kfm )
	{
		QBuffer in;
		in.setData( emptyKfm() );
		return in.open( QIODevice::ReadOnly ) && kfm.load( in );
	}

	static bool build( KfmModel & kfm )
	{
		if ( !loadEmpty( kfm ) )
			return false;

		bool ok = true;
		QModelIndex iKfm = kfm.getKFMroot();
		ok &= iKfm.isValid();

		ok &= kfm.set<QString>( iKfm, "NIF File Name", "actor.nif" );
		ok &= kfm.set<QString>( iKfm, "Master", "Bip01" );
		ok &= kfm.set<float>( iKfm, "Unknown Float 1", 0.5f );
		ok &= kfm.set<int>( iKfm, "Num Animations", 2 );
		ok &= kfm.updateArray( iKfm, "Animations" );

		QModelIndex iAnims = kfm.getIndex( iKfm, "Animations" );
		for ( int i = 0; i < 2; i++ ) {
			QModelIndex iAnim = kfm.index( i, 0, iAnims );
			ok &= kfm.set<int>( iAnim, "Event Code", 10 + i );
			ok &= kfm.set<QString>( iAnim, "KF File Name", QString( "anim%1.kf" ).arg( i ) );
			ok &= kfm.set<int>( iAnim, "Index", i );
		}

		// one transition on the first animation
		QModelIndex iAnim0 = kfm.index( 0, 0, iAnims );
		ok &= kfm.set<int>( iAnim0, "Num Transitions", 1 );
		ok &= kfm.updateArray( iAnim0, "Transitions" );
		QModelIndex iTrans = kfm.index( 0, 0, kfm.getIndex( iAnim0, "Transitions" ) );
		ok &= kfm.set<int>( iTrans, "Animation", 1 );
		ok &= kfm.set<int>( iTrans, "Type", 1 );
		ok &= kfm.set<float>( iTrans, "Duration", 0.25f );
		return ok;
	}

private slots:
	void initTestCase()
	{
		QString err = TestEnv::reloadXml();
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
	}

	void cleanup()
	{
		// Nothing here may pop up a QMessageBox (the app's way of reporting load/save problems)
		QStringList boxes = TestEnv::takeMessageBoxes();
		QVERIFY2( boxes.isEmpty(), qPrintable( boxes.join( " | " ) ) );
	}

	void newModel()
	{
		KfmModel kfm;
		QCOMPARE( kfm.getVersionNumber(), 0x0200000bu );
		QVERIFY( kfm.getKFMroot().isValid() );
		QCOMPARE( kfm.get<QString>( kfm.getKFMroot(), "Header String" ), QString( ";Gamebryo KFM File Version 2.0.0.0b" ) );
	}

	void saveLoadSave()
	{
		KfmModel a;
		QVERIFY( build( a ) );

		QBuffer first;
		QVERIFY( first.open( QIODevice::WriteOnly ) );
		QVERIFY( a.save( first ) );
		QVERIFY( first.data().startsWith( ";Gamebryo KFM File Version 2.0.0.0b\n" ) );

		KfmModel b;
		QBuffer in;
		in.setData( first.data() );
		QVERIFY( in.open( QIODevice::ReadOnly ) );
		QVERIFY( b.load( in ) );

		QCOMPARE( b.getVersionNumber(), 0x0200000bu );
		QCOMPARE( b.get<QString>( b.getKFMroot(), "NIF File Name" ), QString( "actor.nif" ) );
		QCOMPARE( b.get<int>( b.getKFMroot(), "Num Animations" ), 2 );

		QString d = TestEnv::diffModels( a, b );
		QVERIFY2( d.isEmpty(), qPrintable( d ) );

		QBuffer second;
		QVERIFY( second.open( QIODevice::WriteOnly ) );
		QVERIFY( b.save( second ) );
		QCOMPARE( second.data(), first.data() );
	}

	void file()
	{
		KfmModel a;
		QVERIFY( build( a ) );

		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		QString path = QDir( dir.path() ).filePath( "actor.kfm" );
		QVERIFY( a.saveToFile( path ) );

		KfmModel b;
		QVERIFY( b.loadFromFile( path ) );
		QCOMPARE( b.getFilename(), QString( "actor" ) );
		QVERIFY2( TestEnv::diffModels( a, b ).isEmpty(), qPrintable( TestEnv::diffModels( a, b ) ) );
	}

	void load_unsupportedVersion()
	{
		// KfmModel reports through Message::critical regardless of message mode
		KfmModel kfm;
		QBuffer in;
		in.setData( QByteArray( ";Gamebryo KFM File Version 9.9.9.9\n" ) );
		QVERIFY( in.open( QIODevice::ReadOnly ) );
		QVERIFY( !kfm.load( in ) );
		QVERIFY( TestEnv::takeMessageBoxes().count() >= 1 );
	}
};

REGISTER_TEST( tst_KfmModel )

#include "tst_kfmmodel.moc"
