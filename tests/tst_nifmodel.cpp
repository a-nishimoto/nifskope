#include "testenv.h"
#include "testregistry.h"

#include "message.h"
#include "model/nifmodel.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>


//! NifModel behaviour outside the happy-path round trip: bad input, block edits
class tst_NifModel final : public QObject
{
	Q_OBJECT

	static QByteArray sceneBytes( const char * version, int uv, int uv2 )
	{
		TestEnv::Profile p{ "", version, uv, uv2 };
		auto nif = TestEnv::makeModel( p );
		if ( !TestEnv::buildScene( *nif ) )
			return QByteArray();

		bool ok = false;
		QByteArray bytes = TestEnv::saveBytes( *nif, &ok );
		return ok ? bytes : QByteArray();
	}

	static bool loadBytes( NifModel & nif, const QByteArray & bytes ) { return TestEnv::loadBytes( nif, bytes ); }

	static std::unique_ptr<NifModel> make( const char * version, int uv = 0, int uv2 = 0 )
	{
		TestEnv::Profile p{ "", version, uv, uv2 };
		return TestEnv::makeModel( p );
	}

	static QByteArray saveBytes( const NifModel & nif, bool * ok = nullptr ) { return TestEnv::saveBytes( nif, ok ); }

	static QByteArray le32( quint32 v )
	{
		QByteArray b( 4, 0 );
		for ( int i = 0; i < 4; i++ )
			b[i] = char( ( v >> ( 8 * i ) ) & 0xff );

		return b;
	}

	//! What the model reported while loading or editing, as text
	static QString messages( NifModel & nif )
	{
		QStringList list;
		for ( const TestMessage & m : nif.getMessages() )
			list << QString( m );

		return list.join( " | " );
	}

	//! "type/name" of every block and, per block, the sorted "type/name" of the blocks it links to, as a sorted list: the links as
	//! the scene means them, whatever the order of the blocks is
	static QStringList linkGraph( const NifModel & nif )
	{
		auto key = [&]( int i ) {
			QModelIndex b = nif.getBlock( i );
			QString name = nif.getIndex( b, "Name" ).isValid() ? nif.get<QString>( b, "Name" ) : QString();
			return nif.getBlockName( b ) + "/" + name;
		};

		QStringList out;
		for ( int i = 0; i < nif.getBlockCount(); i++ ) {
			QStringList targets;
			for ( int t : nif.getChildLinks( i ) )
				targets << key( t );

			targets.sort();
			out << key( i ) + " -> " + targets.join( "," );
		}

		out.sort();
		return out;
	}

	//! What is wrong with the footer of a model, as text, empty when nothing is: its roots are the ones the model reports, and Num Roots counts them
	static QString footerProblem( NifModel & a )
	{
		QList<int> roots = a.getRootLinks();
		int count = a.get<int>( a.getFooter(), "Num Roots" );
		if ( count != roots.count() )
			return QString( "Num Roots is %1, the model has %2 roots" ).arg( count ).arg( roots.count() );

		QVector<qint32> listed = a.getLinkArray( a.getFooter(), "Roots" );
		if ( listed.count() != roots.count() )
			return QString( "the footer lists %1 roots, the model has %2" ).arg( listed.count() ).arg( roots.count() );

		for ( int i = 0; i < roots.count(); i++ ) {
			if ( int( listed[i] ) != roots[i] )
				return QString( "root %1 is block %2 in the footer, the model says block %3" ).arg( i ).arg( listed[i] ).arg( roots[i] );
		}

		return QString();
	}

	//! What is wrong with the header's list of blocks: Num Blocks, and the type of every block through the table of block types
	static QString headerProblem( NifModel & a )
	{
		int count = a.get<int>( a.getHeader(), "Num Blocks" );
		if ( count != a.getBlockCount() )
			return QString( "Num Blocks is %1, the model has %2 blocks" ).arg( count ).arg( a.getBlockCount() );

		QVector<QString> types = a.getArray<QString>( a.getHeader(), "Block Types" );
		QVector<int> index = a.getArray<int>( a.getHeader(), "Block Type Index" );
		if ( index.count() != a.getBlockCount() )
			return QString( "Block Type Index has %1 entries, the model has %2 blocks" ).arg( index.count() ).arg( a.getBlockCount() );

		for ( int i = 0; i < index.count(); i++ ) {
			if ( types.value( index[i] ) != a.getBlockName( a.getBlock( i ) ) )
				return QString( "block %1 is a %2, the header says %3" ).arg( i ).arg( a.getBlockName( a.getBlock( i ) ), types.value( index[i] ) );
		}

		return QString();
	}

	//! The bytes a model saves (empty when it does not save)
	static QByteArray savedBytes( const NifModel & a )
	{
		bool ok = false;
		QByteArray bytes = saveBytes( a, &ok );
		return ok ? bytes : QByteArray();
	}

	//! The bytes of one block as saveIndex() writes them, for loadIndex() and loadAndMapLinks()
	static QByteArray blockBytes( const NifModel & a, int block )
	{
		QByteArray bytes;
		QBuffer buf( &bytes );
		if ( !buf.open( QIODevice::WriteOnly ) || !a.saveIndex( buf, a.getBlock( block ) ) )
			return QByteArray();

		return bytes;
	}

	//! Puts a setting back the way it was, also when the test fails halfway
	struct SettingRestorer
	{
		QString key;
		explicit SettingRestorer( const QString & k ) : key( k ) {}
		~SettingRestorer() { QSettings().remove( key ); }
	};

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

	void newModel_hasHeaderAndFooterOnly()
	{
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		auto nif = TestEnv::makeModel( p );

		QCOMPARE( nif->getBlockCount(), 0 );
		QCOMPARE( nif->rowCount(), 2 );
		QCOMPARE( nif->getVersion(), QString( "20.0.0.5" ) );
		QCOMPARE( nif->getUserVersion(), 11u );
		QCOMPARE( nif->getUserVersion2(), 11u );
		QCOMPARE( nif->get<QString>( nif->getHeader(), "Header String" ), QString( "Gamebryo File Format, Version 20.0.0.5" ) );
		QVERIFY( nif->getHeader().isValid() );
		QVERIFY( nif->getFooter().isValid() );
	}

	void load_rejectsGarbage_data()
	{
		QTest::addColumn<QByteArray>( "bytes" );

		QTest::newRow( "empty" ) << QByteArray();
		QTest::newRow( "text" ) << QByteArray( "not a nif file\n" );
		QTest::newRow( "unsupported version" ) << QByteArray( "Gamebryo File Format, Version 99.99.99.99\n" );
		QTest::newRow( "header only" ) << QByteArray( "Gamebryo File Format, Version 20.2.0.7\n" );
	}

	void load_rejectsGarbage()
	{
		QFETCH( QByteArray, bytes );

		NifModel nif;
		QVERIFY( !loadBytes( nif, bytes ) );
		// reported through the model's message list (TstMessage mode), not a message box
		QVERIFY( !nif.getMessages().isEmpty() );
		QCOMPARE( nif.getState(), BaseModel::Default );

		// and the model is still usable
		QCOMPARE( nif.getBlockCount(), 0 );
		QVERIFY( nif.insertNiBlock( "NiNode" ).isValid() );
	}

	//! Cutting a valid file anywhere inside a block must fail cleanly (no crash, no hang, no message box)
	void load_truncated_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "uv" );
		QTest::addColumn<int>( "uv2" );

		QTest::newRow( "Morrowind" ) << "4.0.0.2" << 0 << 0;
		QTest::newRow( "Oblivion" ) << "20.0.0.5" << 11 << 11;
		QTest::newRow( "Skyrim" ) << "20.2.0.7" << 12 << 83;
	}

	void load_truncated()
	{
		QFETCH( QString, version );
		QFETCH( int, uv );
		QFETCH( int, uv2 );

		QByteArray full = sceneBytes( version.toLatin1().constData(), uv, uv2 );
		QVERIFY( full.size() > 200 );

		int failures = 0;
		for ( int cut = 0; cut < full.size() - 8; cut += 3 ) {
			NifModel nif;
			bool ok = loadBytes( nif, full.left( cut ) );
			if ( !ok )
				failures++;
		}

		// everything but a cut in the (optional) footer must fail
		QVERIFY2( failures > ( full.size() - 8 ) / 3 - 4, qPrintable( QString::number( failures ) ) );
	}

	void insertNiBlock_unknownType()
	{
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		auto nif = TestEnv::makeModel( p );

		QVERIFY( !nif->insertNiBlock( "NoSuchBlock" ).isValid() );
		QCOMPARE( nif->getBlockCount(), 0 );
		QCOMPARE( nif->getMessages().count(), 1 );
	}

	void removeNiBlock_adjustsLinks()
	{
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		auto nif = TestEnv::makeModel( p );
		QVERIFY( TestEnv::buildScene( *nif ) );

		// 0 root, 1 child, 2 shape, 3 data, 4 extra
		QModelIndex iShape = nif->getBlock( 2 );
		QCOMPARE( nif->getLink( iShape, "Data" ), 3 );

		nif->removeNiBlock( 1 );	// the child node

		QCOMPARE( nif->getBlockCount(), 4 );
		// links above the removed block shift down, links to it are cut
		QCOMPARE( nif->getLinkArray( nif->getBlock( 0 ), "Children" ), QVector<qint32>() << -1 << 1 );
		QCOMPARE( nif->getLink( nif->getBlock( 1 ), "Data" ), 2 );
		QCOMPARE( nif->getLinkArray( nif->getBlock( 0 ), "Extra Data List" ), QVector<qint32>() << 3 );

		QCOMPARE( nif->getRootLinks(), QList<int>() << 0 );

		// the result still saves and loads
		QBuffer out;
		QVERIFY( out.open( QIODevice::WriteOnly ) );
		QVERIFY( nif->save( out ) );
		NifModel again;
		QVERIFY( loadBytes( again, out.data() ) );
		QCOMPARE( again.getBlockCount(), 4 );
	}

	void moveNiBlock_updatesLinks()
	{
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		auto nif = TestEnv::makeModel( p );
		QVERIFY( TestEnv::buildScene( *nif ) );

		// move the shape (2) to the front; every link must follow the block it pointed at
		nif->moveNiBlock( 2, 0 );

		QCOMPARE( nif->getBlockName( nif->getBlock( 0 ) ), QString( "NiTriShape" ) );
		QCOMPARE( nif->getBlockName( nif->getBlock( 1 ) ), QString( "NiNode" ) );
		QCOMPARE( nif->get<QString>( nif->getBlock( 1 ), "Name" ), QString( "Scene Root" ) );
		QCOMPARE( nif->getLinkArray( nif->getBlock( 1 ), "Children" ), QVector<qint32>() << 2 << 0 );
		QCOMPARE( nif->getLink( nif->getBlock( 0 ), "Data" ), 3 );
		QCOMPARE( nif->getRootLinks(), QList<int>() << 1 );
	}

	//! The hash of the block type names (a table index from 20.3.1.2 on): h = h * 33 + character, then modulo the size of the table
	void DJB1Hash_values()
	{
		// python3: h = (h * 33 + c) & 0xffffffff for every character of the name
		QCOMPARE( DJB1Hash( "" ), 0u );
		QCOMPARE( DJB1Hash( "a" ), 97u );
		QCOMPARE( DJB1Hash( "abc" ), 108966u );
		QCOMPARE( DJB1Hash( "abc", 1000 ), 966u );
		QCOMPARE( DJB1Hash( "NiNode" ), 3180009725u );
		QCOMPARE( DJB1Hash( "NiTriShapeData" ), 2910445873u );
		QCOMPARE( DJB1Hash( "NiTriShapeData", 65536 ), 57649u );
		QCOMPARE( DJB1Hash( "BSLightingShaderProperty", 1024 ), 455u );
	}

	// ---- header and footer

	void newModel_headerString_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<QString>( "header" );

		// "NetImmerse" up to 10.0.1.0, "Gamebryo" from 10.1.0.0 (nif.xml, HeaderString); 10.0.1.2 and 10.0.1.3 are in between, and nif.xml says
		// both: its HeaderString description stops "NetImmerse" at 10.0.1.0, the Header compound at 10.0.1.2. NifSkope calls them Gamebryo.
		QTest::newRow( "3.1" ) << "3.1" << "NetImmerse File Format, Version 3.1";
		QTest::newRow( "4.0.0.2" ) << "4.0.0.2" << "NetImmerse File Format, Version 4.0.0.2";
		QTest::newRow( "10.0.1.0" ) << "10.0.1.0" << "NetImmerse File Format, Version 10.0.1.0";
		QTest::newRow( "10.0.1.2" ) << "10.0.1.2" << "Gamebryo File Format, Version 10.0.1.2";
		QTest::newRow( "10.0.1.3" ) << "10.0.1.3" << "Gamebryo File Format, Version 10.0.1.3";
		QTest::newRow( "10.1.0.0" ) << "10.1.0.0" << "Gamebryo File Format, Version 10.1.0.0";
		QTest::newRow( "20.0.0.5" ) << "20.0.0.5" << "Gamebryo File Format, Version 20.0.0.5";
		QTest::newRow( "20.2.0.7" ) << "20.2.0.7" << "Gamebryo File Format, Version 20.2.0.7";
	}

	void newModel_headerString()
	{
		QFETCH( QString, version );
		QFETCH( QString, header );

		auto nif = make( version.toLatin1().constData() );
		QCOMPARE( nif->getVersion(), version );
		QCOMPARE( nif->getValue( nif->getIndex( nif->getHeader(), "Header String" ) ).get<QString>(), header );
	}

	//! The user versions of the start-up profile only exist from 20.0.0.5 on: a header of an older version has none
	void newModel_userVersions_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );

		QTest::newRow( "10.2.0.0" ) << "10.2.0.0" << 0 << 0;
		QTest::newRow( "20.0.0.4" ) << "20.0.0.4" << 0 << 0;
		QTest::newRow( "20.0.0.5" ) << "20.0.0.5" << 11 << 11;
		QTest::newRow( "20.2.0.7" ) << "20.2.0.7" << 11 << 11;
	}

	void newModel_userVersions()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );

		// the profile always asks for 11 and 11
		auto nif = make( version.toLatin1().constData(), 11, 11 );
		QCOMPARE( int( nif->getUserVersion() ), userVersion );
		QCOMPARE( int( nif->getUserVersion2() ), userVersion2 );
	}

	//! Without any setting the application starts with Oblivion's version and user versions
	void newModel_startupDefaults()
	{
		{
			QSettings settings;
			settings.beginGroup( "Settings/NIF/Startup Defaults" );
			settings.remove( "" );
			settings.endGroup();
		}

		NifModel nif;
		QCOMPARE( nif.getVersion(), QString( "20.0.0.5" ) );
		QCOMPARE( int( nif.getUserVersion() ), 11 );
		QCOMPARE( int( nif.getUserVersion2() ), 11 );
	}

	//! A new header of an old version holds the three copyright lines (Numerical Design Limited ...) that those files start with
	void newModel_copyrightLines()
	{
		auto nif = make( "3.1" );
		QVector<QString> lines = nif->getArray<QString>( nif->getHeader(), "Copyright" );
		QEXPECT_FAIL( "", "clear() fills the Copyright array before it has any rows, so a new 3.1 header has no lines (nifmodel.cpp); fix then remove", Continue );
		QCOMPARE( lines.count(), 3 );

		// the lines the tools of those versions wrote (once there are lines to look at)
		if ( lines.count() == 3 ) {
			QCOMPARE( lines[0], QString( "Numerical Design Limited, Chapel Hill, NC 27514" ) );
			QCOMPARE( lines[1], QString( "Copyright (c) 1996-2000" ) );
			QCOMPARE( lines[2], QString( "All Rights Reserved" ) );
		}
	}

	void checkVersion_bounds()
	{
		auto nif = make( "20.2.0.7", 12, 83 );

		// a limit of 0 is no limit; a limit that is the version itself is within it
		QVERIFY( nif->checkVersion( 0, 0 ) );
		QVERIFY( nif->checkVersion( 0x14020007, 0 ) );
		QVERIFY( !nif->checkVersion( 0x14020008, 0 ) );
		QVERIFY( nif->checkVersion( 0x14020006, 0 ) );
		QVERIFY( nif->checkVersion( 0, 0x14020007 ) );
		QVERIFY( !nif->checkVersion( 0, 0x14020006 ) );
		QVERIFY( nif->checkVersion( 0, 0x14020008 ) );
		QVERIFY( nif->checkVersion( 0x14020007, 0x14020007 ) );
		QVERIFY( !nif->checkVersion( 0x14020008, 0x14020009 ) );
	}

	//! Header lines of other products and other spellings: the version is what follows the word Version, whatever the case
	void headerString_variants_data()
	{
		QTest::addColumn<QString>( "header" );
		QTest::addColumn<QString>( "version" );

		QTest::newRow( "Atlantica" ) << "NDSNIF....@....@...., Version 20.2.0.8" << "20.2.0.8";
		QTest::newRow( "Howling Sword" ) << "Joymaster HS1 Object Format - (JMI), Version 20.3.0.9" << "20.3.0.9";
		QTest::newRow( "NeoSteam without a version" ) << "NS File Format" << "10.1.0.0";
		QTest::newRow( "lower case version" ) << "Gamebryo File Format, version 20.2.0.7" << "20.2.0.7";
		QTest::newRow( "upper case version" ) << "Gamebryo File Format, VERSION 20.2.0.7" << "20.2.0.7";
		QTest::newRow( "text after the number" ) << "Gamebryo File Format, Version 20.2.0.7 (export)" << "20.2.0.7";
	}

	void headerString_variants()
	{
		QFETCH( QString, header );
		QFETCH( QString, version );

		// the header line is the first thing of a file: loading it and nothing else reads the version, and then fails for the rest
		// (a model of a version that none of the rows has, so that a version that was not read cannot be the one that was expected)
		auto loaded = make( "4.0.0.2" );
		QVERIFY( !loadBytes( *loaded, header.toLatin1() + "\n" ) );
		QCOMPARE( loaded->getVersion(), version );
	}

	void headerString_refused_data()
	{
		QTest::addColumn<QString>( "header" );

		QTest::newRow( "not a NIF header" ) << "Hello, Version 4.0.0.2";
		QTest::newRow( "no version" ) << "Gamebryo File Format, Version";
		QTest::newRow( "unsupported version" ) << "Gamebryo File Format, Version 99.0.0.0";
	}

	void headerString_refused()
	{
		QFETCH( QString, header );

		NifModel loaded;
		QVERIFY( !loadBytes( loaded, header.toLatin1() + "\n" ) );
		QVERIFY( !messages( loaded ).isEmpty() );
	}

	//! The block types of the header are listed in the order they first occur, and every block names its type by position; a type that comes back
	//! after another one is the first one's position again
	void blockTypeIndex_interleaved_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "uv" );
		QTest::addColumn<int>( "uv2" );

		QTest::newRow( "10.0.1.0" ) << "10.0.1.0" << 0 << 0;
		QTest::newRow( "Oblivion" ) << "20.0.0.5" << 11 << 11;
		QTest::newRow( "Skyrim" ) << "20.2.0.7" << 12 << 83;
	}

	void blockTypeIndex_interleaved()
	{
		QFETCH( QString, version );
		QFETCH( int, uv );
		QFETCH( int, uv2 );

		auto a = make( version.toLatin1().constData(), uv, uv2 );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		QVERIFY( a->insertNiBlock( "NiStringExtraData" ).isValid() );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );

		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( b.getArray<QString>( b.getHeader(), "Block Types" ), QVector<QString>() << "NiNode" << "NiStringExtraData" );
		QCOMPARE( b.getArray<int>( b.getHeader(), "Block Type Index" ), QVector<int>() << 0 << 1 << 0 );
		QCOMPARE( b.getBlockName( b.getBlock( 0 ) ), QString( "NiNode" ) );
		QCOMPARE( b.getBlockName( b.getBlock( 1 ) ), QString( "NiStringExtraData" ) );
		QCOMPARE( b.getBlockName( b.getBlock( 2 ) ), QString( "NiNode" ) );
	}

	//! Bit 15 of a block type index is a flag of the PhysX data some files carry: the index is the other 15 bits
	void load_blockTypeIndexPhysXBit()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		int off = a->fileOffset( a->index( 0, 0, a->getIndex( a->getHeader(), "Block Type Index" ) ) );
		QVERIFY( off > 0 );
		QCOMPARE( int( quint8( bytes[off + 1] ) & 0x80 ), 0 );
		bytes[off + 1] = char( bytes[off + 1] | 0x80 );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( b.getBlockCount(), 5 );
		QCOMPARE( b.getBlockName( b.getBlock( 0 ) ), QString( "NiNode" ) );
	}

	void footer_roots_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "uv" );
		QTest::addColumn<int>( "uv2" );

		QTest::newRow( "Morrowind" ) << "4.0.0.2" << 0 << 0;
		QTest::newRow( "Skyrim" ) << "20.2.0.7" << 12 << 83;
	}

	//! The footer lists the blocks nothing else links to, in block order
	void footer_roots()
	{
		QFETCH( QString, version );
		QFETCH( int, uv );
		QFETCH( int, uv2 );

		auto a = make( version.toLatin1().constData(), uv, uv2 );
		QModelIndex n0 = a->insertNiBlock( "NiNode" ), n1 = a->insertNiBlock( "NiNode" ), n2 = a->insertNiBlock( "NiNode" );
		QVERIFY( n0.isValid() && n1.isValid() && n2.isValid() );

		// block 1 holds block 0: blocks 1 and 2 are the roots
		QVERIFY( a->set<int>( n1, "Num Children", 1 ) );
		QVERIFY( a->updateArray( n1, "Children" ) );
		QVERIFY( a->setLinkArray( n1, "Children", QVector<qint32>() << 0 ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 1 << 2 );

		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( b.get<int>( b.getFooter(), "Num Roots" ), 2 );
		QCOMPARE( b.getLinkArray( b.getFooter(), "Roots" ), QVector<qint32>() << 1 << 2 );
		QCOMPARE( b.getRootLinks(), QList<int>() << 1 << 2 );
	}

	//! Edits made while updates are held show up in the header and footer when they are let go
	void holdUpdates_release()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		a->holdUpdates( true );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		a->holdUpdates( false );

		QCOMPARE( a->get<int>( a->getHeader(), "Num Blocks" ), 2 );
		QCOMPARE( a->get<int>( a->getFooter(), "Num Roots" ), 2 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 1 );
	}

	// ---- saving and loading

	void state_afterSave()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QCOMPARE( a->getState(), BaseModel::Default );

		bool ok = false;
		saveBytes( *a, &ok );
		QVERIFY( ok );
		QCOMPARE( a->getState(), BaseModel::Default );
	}

	void state_duringSave()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );

		QList<int> states;
		NifModel * model = a.get();
		QObject::connect( model, &NifModel::sigProgress, [&]( int, int ) { states << int( model->getState() ); } );

		bool ok = false;
		saveBytes( *a, &ok );
		QVERIFY( ok );

		// one report per row of the model and the header's, before the first and after each
		QCOMPARE( states.count(), a->rowCount() + 1 );
		for ( int state : states )
			QCOMPARE( state, int( BaseModel::Saving ) );
	}

	//! Loading into a model that holds a file replaces it
	void load_twiceIntoSameModel()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( b.getBlockCount(), 5 );
		QVERIFY2( TestEnv::diffModels( *a, b ).isEmpty(), qPrintable( TestEnv::diffModels( *a, b ) ) );
	}

	//! A file brings its own versions, whatever the model it is loaded into was set up as: Oblivion's user versions do not stay in a Morrowind file
	void load_resetsUserVersions()
	{
		auto morrowind = make( "4.0.0.2" );
		QVERIFY( TestEnv::buildScene( *morrowind ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *morrowind, &ok );
		QVERIFY( ok );

		auto oblivion = make( "20.0.0.5", 11, 11 );	// the start-up profile of the application
		QCOMPARE( int( oblivion->getUserVersion() ), 11 );
		QVERIFY( loadBytes( *oblivion, bytes ) );
		QCOMPARE( oblivion->getVersion(), QString( "4.0.0.2" ) );
		QCOMPARE( int( oblivion->getUserVersion() ), 0 );
		QCOMPARE( int( oblivion->getUserVersion2() ), 0 );
		QVERIFY2( TestEnv::diffModels( *morrowind, *oblivion ).isEmpty(), qPrintable( TestEnv::diffModels( *morrowind, *oblivion ) ) );
	}

	void fileOffset_pointsAtBlockData_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "uv" );
		QTest::addColumn<int>( "uv2" );

		QTest::newRow( "3.1" ) << "3.1" << 0 << 0;
		QTest::newRow( "3.3.0.13" ) << "3.3.0.13" << 0 << 0;
		QTest::newRow( "4.0.0.2" ) << "4.0.0.2" << 0 << 0;
		QTest::newRow( "4.2.2.0" ) << "4.2.2.0" << 0 << 0;
		QTest::newRow( "10.0.1.0" ) << "10.0.1.0" << 0 << 0;
		QTest::newRow( "10.1.0.0" ) << "10.1.0.0" << 0 << 0;
		QTest::newRow( "10.2.0.0" ) << "10.2.0.0" << 0 << 0;
		QTest::newRow( "20.0.0.5" ) << "20.0.0.5" << 11 << 11;
	}

	//! fileOffset() of a block is where its own bytes start in the saved file: behind what the file puts in front of a block (a "Top Level Object"
	//! mark, the type name, the block number, a zero), which differs from version to version. The scene's blocks 0, 1 and 2 start with a name.
	void fileOffset_pointsAtBlockData()
	{
		QFETCH( QString, version );
		QFETCH( int, uv );
		QFETCH( int, uv2 );

		auto a = make( version.toLatin1().constData(), uv, uv2 );
		QVERIFY( TestEnv::buildScene( *a ) );
		quint32 number = a->getVersionNumber();

		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		// the blocks 0, 1 and 2 start with a name; the last block (4, a NiStringExtraData) is looked at for what is in front of it only
		const int blocks[4] = { 0, 1, 2, 4 };
		const char * types[4] = { "NiNode", "NiNode", "NiTriShape", "NiStringExtraData" };
		const char * names[4] = { "Scene Root", "Child", "Shape", nullptr };

		for ( int k = 0; k < 4; k++ ) {
			int block = blocks[k];

			// what the file has in front of the block
			QByteArray before;
			if ( number > 0x0a000000 ) {
				// 10.0.1.0 up to 10.2.0.0: a zero
				if ( number < 0x0a020000 )
					before = le32( 0 );
			} else {
				if ( number < 0x0303000d && block == 0 )	// the root of the file
					before += le32( 16 ) + "Top Level Object";

				before += le32( strlen( types[k] ) ) + types[k];
				if ( number < 0x0303000d )
					before += le32( block + 1 );
			}

			// 3.3.0.13 and up numbers nothing, below 5.0.0.1 every block names its type: only that one has a mark in front of it
			QByteArray data = names[k] ? le32( strlen( names[k] ) ) + names[k] : QByteArray();

			int offset = a->fileOffset( a->getBlock( block ) );
			QVERIFY2( offset >= before.size(), qPrintable( QString( "block %1: offset %2" ).arg( block ).arg( offset ) ) );
			QByteArray found = bytes.mid( offset - before.size(), before.size() + data.size() );
			QVERIFY2( found == before + data, qPrintable( QString( "block %1 at %2: %3, expected %4" ).arg( block ).arg( offset )
			                                             .arg( QString::fromLatin1( found.toHex( ' ' ) ), QString::fromLatin1( ( before + data ).toHex( ' ' ) ) ) ) );
		}
	}

	//! From 10.0.1.0 up to 10.1.0.x a zero precedes every block, and a file in which it is not a zero gets a warning
	void load_nonZeroBlockSeparator_data()
	{
		QTest::addColumn<QString>( "version" );

		QTest::newRow( "10.0.1.0" ) << "10.0.1.0";
		QTest::newRow( "10.1.0.0" ) << "10.1.0.0";
	}

	void load_nonZeroBlockSeparator()
	{
		QFETCH( QString, version );

		auto a = make( version.toLatin1().constData() );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		int offset = a->fileOffset( a->getBlock( 0 ) );
		QCOMPARE( bytes.mid( offset - 4, 4 ), le32( 0 ) );

		NifModel clean;
		QVERIFY( loadBytes( clean, bytes ) );
		QCOMPARE( messages( clean ), QString() );

		bytes[offset - 4] = 1;
		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QVERIFY2( messages( b ).contains( "non-zero block separator (1) preceeding block NiNode" ), qPrintable( messages( b ) ) );
	}

	//! A file of the first versions numbers its blocks itself, and links name the numbers: they need not be the positions
	void load_oldBlockNumbers()
	{
		auto a = make( "3.1" );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		// the number of block 1 is in front of its data, and Children[0] of block 0 is a link to it (both are stored plus one: 2)
		int numberAt = a->fileOffset( a->getBlock( 1 ) ) - 4;
		int linkAt = a->fileOffset( a->index( 0, 0, a->getIndex( a->getBlock( 0 ), "Children" ) ) );
		QCOMPARE( bytes.mid( numberAt, 4 ), le32( 2 ) );
		QCOMPARE( bytes.mid( linkAt, 4 ), le32( 2 ) );
		bytes.replace( numberAt, 4, le32( 12 ) );
		bytes.replace( linkAt, 4, le32( 12 ) );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( messages( b ), QString() );
		QCOMPARE( b.getLinkArray( b.getBlock( 0 ), "Children" ), QVector<qint32>() << 1 << 2 );
	}

	//! 3.3.0.13 is the first version with links that are the block positions and a footer, and the last with a type name in front of each block
	void container_3_3_0_13()
	{
		auto a = make( "3.3.0.13" );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		// no markers of the older files, a footer: one root, block 0 (the links are the positions, not the positions plus one)
		QVERIFY( !bytes.contains( "End Of File" ) );
		QVERIFY( !bytes.contains( "Top Level Object" ) );
		QCOMPARE( bytes.right( 8 ), le32( 1 ) + le32( 0 ) );
		// every block names its type (NiNode, NiNode, NiTriShape, NiTriShapeData, NiStringExtraData) in front of its data
		QCOMPARE( bytes.count( le32( 6 ) + "NiNode" ), 2 );
		QCOMPARE( bytes.count( le32( 10 ) + "NiTriShape" ), 1 );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( messages( b ), QString() );
		QCOMPARE( b.getBlockCount(), 5 );
		QVERIFY2( TestEnv::diffModels( *a, b ).isEmpty(), qPrintable( TestEnv::diffModels( *a, b ) ) );
	}

	//! A file of a version below 3.3.0.13 and one of 3.3.0.13 and up both stop between two blocks, and both say so
	void load_cutBetweenBlocks_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "uv" );
		QTest::addColumn<int>( "headerBytes" );   // bytes in front of block 1's own data: its type name and, below 3.3.0.13, its number

		QTest::newRow( "3.1" ) << "3.1" << 0 << ( 4 + 6 + 4 );
		QTest::newRow( "Oblivion" ) << "20.0.0.5" << 11 << 0;
	}

	void load_cutBetweenBlocks()
	{
		QFETCH( QString, version );
		QFETCH( int, uv );
		QFETCH( int, headerBytes );

		auto a = make( version.toLatin1().constData(), uv, uv );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		int start = a->fileOffset( a->getBlock( 1 ) ) - headerBytes;
		NifModel b;
		QVERIFY( !loadBytes( b, bytes.left( start ) ) );
		QVERIFY2( messages( b ).contains( "unexpected EOF" ), qPrintable( messages( b ) ) );
	}

	//! A block of a type that is not known is a reason to stop, in a file with a table of types and in one that names the type of every block
	void load_unknownBlockType_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "uv" );

		QTest::newRow( "3.1" ) << "3.1" << 0;
		QTest::newRow( "Oblivion" ) << "20.0.0.5" << 11;
	}

	void load_unknownBlockType()
	{
		QFETCH( QString, version );
		QFETCH( int, uv );

		auto a = make( version.toLatin1().constData(), uv, uv );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		// the first "NiNode" of the file is the type of block 0 (the header's table of types, or the block's own type name)
		int at = bytes.indexOf( "NiNode" );
		QVERIFY( at >= 0 );
		bytes[at + 5] = 'X';

		NifModel b;
		QVERIFY( !loadBytes( b, bytes ) );
		QVERIFY2( messages( b ).contains( "unknown block (NiNodX)" ), qPrintable( messages( b ) ) );
	}

	void load_blockTypeNameLength_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<quint32>( "length" );
		QTest::addColumn<QString>( "message" );
		QTest::addColumn<int>( "block" );   // the type name of which of the two NiNodes, the first (the root) or the second

		const QString notString( "does not start with a NiString" );
		const QString unknown( "encountered unknown block (" );

		// 3.3.0.13 up to 5.0.0.1: the type name of every block is a length of 2 to 80 and that many characters; the files before it say 0 to 80
		QTest::newRow( "4.0.0.2, length -1" ) << "4.0.0.2" << 0xffffffffu << notString << 0;
		QTest::newRow( "4.0.0.2, length 0" ) << "4.0.0.2" << 0u << notString << 0;
		QTest::newRow( "4.0.0.2, length 1" ) << "4.0.0.2" << 1u << notString << 0;
		QTest::newRow( "4.0.0.2, length 2" ) << "4.0.0.2" << 2u << unknown << 0;
		QTest::newRow( "4.0.0.2, length 80" ) << "4.0.0.2" << 80u << unknown << 0;
		QTest::newRow( "4.0.0.2, length 81" ) << "4.0.0.2" << 81u << notString << 0;
		QTest::newRow( "3.1, length -1" ) << "3.1" << 0xffffffffu << notString << 0;
		QTest::newRow( "3.1, length 80" ) << "3.1" << 80u << unknown << 0;
		QTest::newRow( "3.1, length 81" ) << "3.1" << 81u << notString << 0;
		// below 3.3.0.13 the first block is told from the others (it has the mark "Top Level Object" in front of its type name), and the others have
		// the same limit
		QTest::newRow( "3.1, second block, length 80" ) << "3.1" << 80u << unknown << 1;
		QTest::newRow( "3.1, second block, length 81" ) << "3.1" << 81u << notString << 1;
		QTest::newRow( "4.0.0.2, second block, length 81" ) << "4.0.0.2" << 81u << notString << 1;
	}

	//! A file that names the type of every block has to give it a length it can believe, and says so when it does not
	void load_blockTypeNameLength()
	{
		QFETCH( QString, version );
		QFETCH( quint32, length );
		QFETCH( QString, message );
		QFETCH( int, block );

		auto a = make( version.toLatin1().constData() );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		// the type name of block 0 (the first NiNode of the file) or of block 1: its length and the name
		QByteArray original = le32( 6 ) + "NiNode";
		int at = bytes.indexOf( original );
		QVERIFY( at >= 0 );
		if ( block == 1 ) {
			at = bytes.indexOf( original, at + 1 );
			QVERIFY( at >= 0 );
		}
		// as many X as the length says, none for a negative one
		QByteArray replacement = le32( length ) + QByteArray( length < 0x10000 ? int( length ) : 0, 'X' );
		bytes.replace( at, original.size(), replacement );

		NifModel b;
		QVERIFY( !loadBytes( b, bytes ) );
		QVERIFY2( messages( b ).contains( message ), qPrintable( messages( b ) ) );
	}

	//! "Ignore Block Size" off: a block of a 20.2.0.7 file ends where its size in the header says, and a file where it does not gets a warning
	void load_blockSizeCheck()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		// Block Size[0], made 3 bytes too large
		int off = a->fileOffset( a->index( 0, 0, a->getIndex( a->getHeader(), "Block Size" ) ) );
		QVERIFY( off > 0 );
		quint32 size0 = quint32( quint8( bytes[off] ) ) | ( quint32( quint8( bytes[off + 1] ) ) << 8 );
		QVERIFY( size0 > 0 && size0 < 0x10000 - 3 );
		QByteArray bad = bytes;
		bad.replace( off, 4, le32( size0 + 3 ) );

		SettingRestorer restore( "Ignore Block Size" );

		// by default the sizes are ignored: a wrong one goes unnoticed
		{
			QSettings().remove( "Ignore Block Size" );
			NifModel b;
			QVERIFY( loadBytes( b, bad ) );
			QCOMPARE( messages( b ), QString() );
			QCOMPARE( b.getBlockCount(), 5 );
		}

		// asked to check them: right sizes are no reason for a message, a wrong one is
		QSettings().setValue( "Ignore Block Size", false );
		{
			NifModel good;
			QVERIFY( loadBytes( good, bytes ) );
			QCOMPARE( messages( good ), QString() );
			QCOMPARE( good.getBlockCount(), 5 );

			NifModel b;
			loadBytes( b, bad );
			QVERIFY2( messages( b ).contains( "device position incorrect after block number 1 (NiNode)" ), qPrintable( messages( b ) ) );
		}
	}

	//! Reading just the header of a file into a model that holds another one: what it held is gone, the header is the file's
	void loadHeaderOnly_replacesWhatTheModelHolds()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QString skyrim = QDir( dir.path() ).filePath( "skyrim.nif" );
		QVERIFY( a->saveToFile( skyrim ) );

		auto o = make( "20.0.0.5", 11, 11 );
		QVERIFY( o->insertNiBlock( "NiNode" ).isValid() );
		QString oblivion = QDir( dir.path() ).filePath( "oblivion.nif" );
		QVERIFY( o->saveToFile( oblivion ) );

		NifModel m;
		QVERIFY( m.loadFromFile( skyrim ) );
		QCOMPARE( m.getBlockCount(), 5 );

		QVERIFY( m.loadHeaderOnly( oblivion ) );
		QCOMPARE( m.getBlockCount(), 0 );
		QCOMPARE( m.getVersion(), QString( "20.0.0.5" ) );
		QCOMPARE( m.get<int>( m.getHeader(), "Num Blocks" ), 1 );
		QCOMPARE( int( m.getUserVersion2() ), 11 );
	}

	//! A field is on only when its parent is: asked with the parents (what a view does to grey out fields), a field under a compound that is off
	//! is off, though its own condition holds
	void evalCondition_withParents()
	{
		// 4.0.0.2: a node has a Bounding Volume when "Has Bounding Volume" says so (it does not, by default); the volume is a Sphere when its
		// Collision Type is 0 (the default)
		auto nif = make( "4.0.0.2" );
		QModelIndex node = nif->insertNiBlock( "NiNode" );
		QVERIFY( node.isValid() );

		// the fields that are off are not found by name (getIndex), they are rows all the same
		auto child = [&]( const QModelIndex & parent, const QString & name ) {
			for ( int r = 0; r < nif->rowCount( parent ); r++ ) {
				if ( nif->itemName( nif->index( r, 0, parent ) ) == name )
					return nif->index( r, 0, parent );
			}

			return QModelIndex();
		};

		QModelIndex volume = child( node, "Bounding Volume" );
		QVERIFY( volume.isValid() );
		QModelIndex sphere = child( volume, "Sphere" );
		QVERIFY( sphere.isValid() );

		// the model keeps the answer once it has worked it out, so this is the first question about the field that counts
		const BaseModel & model = *nif;
		QVERIFY( !model.evalCondition( sphere, true ) );
		QVERIFY( !model.evalCondition( volume ) );
	}

	// ---- editing

	//! A block put in the middle moves the ones behind it, and every link keeps pointing at the block it pointed at
	void insertNiBlock_atPosition()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QStringList before = linkGraph( *a );

		QModelIndex inserted = a->insertNiBlock( "NiNode", 1 );
		QVERIFY( inserted.isValid() );
		QCOMPARE( a->getBlockNumber( inserted ), 1 );
		QCOMPARE( a->getBlockCount(), 6 );
		// the root's children were blocks 1 and 2, they are 2 and 3 now
		QCOMPARE( a->getLinkArray( a->getBlock( 0 ), "Children" ), QVector<qint32>() << 2 << 3 );
		QCOMPARE( a->getLink( a->getBlock( 3 ), "Data" ), 4 );

		QStringList after = linkGraph( *a );
		after.removeAll( "NiNode/ -> " );
		QCOMPARE( after, before );
	}

	//! Moving a block to the back
	void moveNiBlock_forward()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QStringList before = linkGraph( *a );

		a->moveNiBlock( 1, 4 );
		QCOMPARE( a->getBlockName( a->getBlock( 4 ) ), QString( "NiNode" ) );
		QCOMPARE( a->get<QString>( a->getBlock( 4 ), "Name" ), QString( "Child" ) );
		QCOMPARE( linkGraph( *a ), before );
	}

	void reorderBlocks_keepsLinks()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QStringList before = linkGraph( *a );

		// the number at a position is the position the block that is there now goes to: the root becomes block 1, the extra data block 0
		a->reorderBlocks( QVector<qint32>() << 1 << 2 << 3 << 4 << 0 );
		QCOMPARE( messages( *a ), QString() );
		QCOMPARE( a->getBlockName( a->getBlock( 0 ) ), QString( "NiStringExtraData" ) );
		QCOMPARE( a->get<QString>( a->getBlock( 1 ), "Name" ), QString( "Scene Root" ) );
		QCOMPARE( a->get<QString>( a->getBlock( 2 ), "Name" ), QString( "Child" ) );
		QCOMPARE( a->getBlockName( a->getBlock( 4 ) ), QString( "NiTriShapeData" ) );
		QCOMPARE( linkGraph( *a ), before );
	}

	//! A link to block 0 follows block 0 where it goes: here the Ptr from the skin to the skeleton root
	void moveNiBlock_rootWithPtr()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QString failed;
		QVERIFY2( TestEnv::buildRichScene( *a, &failed ), qPrintable( failed ) );

		auto graph = [&]() {
			QStringList out;
			for ( int i = 0; i < a->getBlockCount(); i++ ) {
				QModelIndex b = a->getBlock( i );
				QString key = a->getBlockName( b ) + "/" + ( a->getIndex( b, "Name" ).isValid() ? a->get<QString>( b, "Name" ) : QString() );

				QStringList children;
				for ( int c : a->getChildLinks( i ) )
					children << a->getBlockName( a->getBlock( c ) );

				QStringList parents;
				for ( int p : a->getParentLinks( i ) )
					parents << a->getBlockName( a->getBlock( p ) ) + "/" + a->get<QString>( a->getBlock( p ), "Name" );

				children.sort();
				parents.sort();
				out << key + " children: " + children.join( "," ) + " parents: " + parents.join( "," );
			}

			out.sort();
			return out;
		};

		QStringList before = graph();
		a->moveNiBlock( 0, 3 );
		QCOMPARE( a->getBlockName( a->getBlock( 3 ) ), QString( "NiNode" ) );
		QCOMPARE( graph(), before );
	}

	void removeNiBlock_outOfRange()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		int rows = a->rowCount();

		a->removeNiBlock( a->getBlockCount() );		// one past the last: that is the footer
		a->removeNiBlock( -1 );
		QCOMPARE( a->getBlockCount(), 5 );
		QCOMPARE( a->rowCount(), rows );
		QVERIFY( a->getFooter().isValid() );
	}

	//! The links a block has to others: Ref children, Ptr parents, and a Ref three levels down in an array of compounds
	void links_childAndParent()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QString failed;
		QVERIFY2( TestEnv::buildRichScene( *a, &failed ), qPrintable( failed ) );

		int skin = -1;
		for ( int i = 0; i < a->getBlockCount(); i++ ) {
			if ( a->getBlockName( a->getBlock( i ) ) == "NiSkinInstance" )
				skin = i;
		}
		QVERIFY( skin >= 0 );

		// the skin's Skeleton Root is a Ptr, a link up to the root of the scene; its Data is a Ref to the block behind it
		QCOMPARE( a->getParentLinks( skin ), QList<int>() << 0 );
		QCOMPARE( a->getChildLinks( skin ), QList<int>() << skin + 1 );
		QCOMPARE( a->getBlockName( a->getBlock( skin + 1 ) ), QString( "NiSkinData" ) );
		// a Ptr does not make the block it names a child: nothing holds block 0
		QVERIFY( a->getRootLinks().contains( 0 ) );
		QVERIFY( !a->getChildLinks( skin ).contains( 0 ) );
	}

	//! A Ref in a compound in an array of a block is still a child of the block
	void links_nestedChild()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QModelIndex seq = a->insertNiBlock( "NiControllerSequence" );
		QModelIndex interpolator = a->insertNiBlock( "NiTransformInterpolator" );
		QVERIFY( seq.isValid() && interpolator.isValid() );

		QVERIFY( a->set<int>( seq, "Num Controlled Blocks", 1 ) );
		QVERIFY( a->updateArray( seq, "Controlled Blocks" ) );
		QModelIndex row = a->index( 0, 0, a->getIndex( seq, "Controlled Blocks" ) );
		QVERIFY( row.isValid() );
		QVERIFY( a->setLink( row, "Interpolator", a->getBlockNumber( interpolator ) ) );

		QVERIFY( a->getChildLinks( a->getBlockNumber( seq ) ).contains( a->getBlockNumber( interpolator ) ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << a->getBlockNumber( seq ) );
	}

	//! Two blocks that hold each other are a loop: the model cuts it where it is made and says so, and refuses a list of the wrong length
	void links_cycleAndLength()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QModelIndex n0 = a->insertNiBlock( "NiNode" ), n1 = a->insertNiBlock( "NiNode" );
		QVERIFY( a->set<int>( n0, "Num Children", 1 ) && a->updateArray( n0, "Children" ) );
		QVERIFY( a->set<int>( n1, "Num Children", 1 ) && a->updateArray( n1, "Children" ) );
		QVERIFY( a->setLinkArray( n0, "Children", QVector<qint32>() << 1 ) );
		QCOMPARE( messages( *a ), QString() );

		QVERIFY( a->setLinkArray( n1, "Children", QVector<qint32>() << 0 ) );
		QVERIFY2( messages( *a ).contains( "infinite recursive link" ), qPrintable( messages( *a ) ) );
		QVERIFY( a->getChildLinks( 1 ).isEmpty() );
		QCOMPARE( a->getChildLinks( 0 ), QList<int>() << 1 );

		// two links for an array of one
		QVERIFY( !a->setLinkArray( n0, "Children", QVector<qint32>() << 1 << 1 ) );
	}

	//! Taking rows out of an array of links lets go of the blocks they held: a block nothing holds any more is a root, and the footer lists it
	void updateArray_shrinkingALinkArray_updatesRoots()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		// the root holds blocks 1 and 2, it is the only block nothing holds
		QCOMPARE( a->getLinkArray( a->getBlock( 0 ), "Children" ), QVector<qint32>() << 1 << 2 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 );

		QSignalSpy changed( a.get(), &NifModel::linksChanged );
		QVERIFY( a->set<int>( a->getBlock( 0 ), "Num Children", 1 ) );
		QVERIFY( a->updateArray( a->getBlock( 0 ), "Children" ) );

		QCOMPARE( a->getLinkArray( a->getBlock( 0 ), "Children" ), QVector<qint32>() << 1 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 2 );
		QCOMPARE( a->get<int>( a->getFooter(), "Num Roots" ), 2 );
		QCOMPARE( a->getLinkArray( a->getFooter(), "Roots" ), QVector<qint32>() << 0 << 2 );
		QVERIFY( changed.count() >= 1 );
	}

	//! Loading the bytes of a block into another block of the same type: the conditions of its fields are worked out from the new values, not
	//! taken from the ones the block had before (here the old block has normals and the new bytes have none)
	void loadIndex_reevaluatesConditions()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QModelIndex withNormals = a->insertNiBlock( "NiTriShapeData" );
		QModelIndex without = a->insertNiBlock( "NiTriShapeData" );
		QVERIFY( withNormals.isValid() && without.isValid() );

		for ( QModelIndex data : { withNormals, without } ) {
			QVERIFY( a->set<int>( data, "Num Vertices", 2 ) );
			QVERIFY( a->set<int>( data, "Has Vertices", 1 ) );
			QVERIFY( a->updateArray( data, "Vertices" ) );
		}

		QVERIFY( a->set<int>( withNormals, "Has Normals", 1 ) );
		QVERIFY( a->updateArray( withNormals, "Normals" ) );
		QCOMPARE( a->rowCount( a->getIndex( withNormals, "Normals" ) ), 2 );
		// the model has used the condition of the normals (it holds from here on in the first block)
		const BaseModel & model = *a;
		QVERIFY( model.evalCondition( a->getIndex( withNormals, "Normals" ) ) );

		QBuffer out;
		QVERIFY( out.open( QIODevice::WriteOnly ) );
		QVERIFY( a->saveIndex( out, without ) );
		QVERIFY( !out.data().isEmpty() );

		QBuffer in;
		in.setData( out.data() );
		QVERIFY( in.open( QIODevice::ReadOnly ) );
		QVERIFY2( a->loadIndex( in, withNormals ), qPrintable( messages( *a ) ) );

		QCOMPARE( a->get<int>( withNormals, "Has Normals" ), 0 );
		QVERIFY( !model.evalCondition( a->getIndex( withNormals, "Normals" ) ) );
		QCOMPARE( int( in.pos() ), int( out.data().size() ) );
	}

	// ---- strings

	//! Two blocks with the same name share one entry of the header's string table, and no name is the "none" index, not a table entry
	void strings_dedupeAndEmpty()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QModelIndex n0 = a->insertNiBlock( "NiNode" ), n1 = a->insertNiBlock( "NiNode" ), n2 = a->insertNiBlock( "NiNode" );
		QVERIFY( a->set<QString>( n0, "Name", "Same" ) );
		QVERIFY( a->set<QString>( n1, "Name", "Same" ) );
		QVERIFY( a->set<QString>( n2, "Name", "" ) );

		QCOMPARE( a->get<int>( a->getHeader(), "Num Strings" ), 1 );
		QCOMPARE( a->getValue( a->getIndex( n0, "Name" ) ).toCount(), 0u );
		QCOMPARE( a->getValue( a->getIndex( n1, "Name" ) ).toCount(), 0u );
		QCOMPARE( a->getValue( a->getIndex( n2, "Name" ) ).toCount(), 0xffffffffu );

		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );
		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( b.getArray<QString>( b.getHeader(), "Strings" ), QVector<QString>() << "Same" );
		QCOMPARE( b.get<QString>( b.getBlock( 0 ), "Name" ), QString( "Same" ) );
		QCOMPARE( b.get<QString>( b.getBlock( 1 ), "Name" ), QString( "Same" ) );
		QCOMPARE( b.get<QString>( b.getBlock( 2 ), "Name" ), QString() );
	}

	//! A name that is changed gets a new entry (the other blocks that use the old one keep it); the entry of a string can be replaced as well
	void strings_extraInfoReplaceMove()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QModelIndex n0 = a->insertNiBlock( "NiNode" );
		QVERIFY( a->set<QString>( n0, "Name", "First" ) );
		QCOMPARE( a->string( a->getIndex( n0, "Name" ), true ), QString( "First [0]" ) );
		QCOMPARE( a->string( a->getIndex( n0, "Name" ) ), QString( "First" ) );

		// another text for the same name is appended: the old one stays in the table
		QVERIFY( a->set<QString>( n0, "Name", "Second" ) );
		QCOMPARE( a->getArray<QString>( a->getHeader(), "Strings" ), QVector<QString>() << "First" << "Second" );
		QCOMPARE( a->get<QString>( n0, "Name" ), QString( "Second" ) );

		// replacing writes into the entry the name uses
		QModelIndex name = a->getIndex( n0, "Name" );
		QVERIFY( a->assignString( name, "Third", true ) );
		QCOMPARE( a->getArray<QString>( a->getHeader(), "Strings" ), QVector<QString>() << "First" << "Third" );
		QCOMPARE( a->get<QString>( n0, "Name" ), QString( "Third" ) );
	}

	//! The blocks of a model moved into another one take their names with them, into the other one's string table
	void strings_moveAllNiBlocks()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		auto b = make( "20.2.0.7", 12, 83 );
		QModelIndex existing = b->insertNiBlock( "NiNode" );
		QVERIFY( b->set<QString>( existing, "Name", "Already here" ) );

		a->moveAllNiBlocks( b.get(), true );
		QCOMPARE( b->getBlockCount(), 6 );
		QCOMPARE( b->get<QString>( b->getBlock( 0 ), "Name" ), QString( "Already here" ) );
		QCOMPARE( b->get<QString>( b->getBlock( 1 ), "Name" ), QString( "Scene Root" ) );
		QCOMPARE( b->get<QString>( b->getBlock( 3 ), "Name" ), QString( "Shape" ) );
		QStringList table;
		for ( const QString & entry : b->getArray<QString>( b->getHeader(), "Strings" ) )
			table << entry;
		// every text the blocks use is in the table of the target, once (the order it is added in is of no interest)
		table.sort();
		QCOMPARE( table, QStringList() << "Already here" << "Child" << "Note" << "Scene Root" << "Shape" << "hello nifskope" );
		QCOMPARE( b->get<QString>( b->getBlock( 5 ), "String Data" ), QString( "hello nifskope" ) );
	}

	// ---- arrays

	//! An array of bytes is one blob of as many bytes as its size says
	void binaryArray_size()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QModelIndex pixels = a->insertNiBlock( "NiPixelData" );
		QVERIFY( pixels.isValid() );
		QVERIFY( a->set<int>( pixels, "Num Pixels", 4 ) );
		QVERIFY( a->set<int>( pixels, "Num Faces", 1 ) );
		QVERIFY( a->updateArray( pixels, "Pixel Data" ) );

		QModelIndex array = a->getIndex( pixels, "Pixel Data" );
		QVERIFY( array.isValid() );
		QCOMPARE( a->rowCount( array ), 1 );
		QCOMPARE( a->getValue( a->index( 0, 0, array ) ).get<QByteArray>().size(), 4 );
	}

	//! A size field that does not match its array: said when the file is saved (a message box), and when the block is measured
	void save_arraySizeMismatchWarns()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QVERIFY( a->set<int>( a->getBlock( 3 ), "Num Vertices", 5 ) );	// the three rows stay

		bool ok = false;
		saveBytes( *a, &ok );
		QStringList boxes = TestEnv::takeMessageBoxes();
		QVERIFY2( boxes.join( " | " ).contains( "array size mismatch" ), qPrintable( "no warning: " + boxes.join( " | " ) ) );
	}

	void blockSize_arraySizeMismatchWarns()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QVERIFY( a->set<int>( a->getBlock( 3 ), "Num Vertices", 5 ) );
		messages( *a );

		a->blockSize( a->getBlock( 3 ) );
		QVERIFY2( messages( *a ).contains( "array size mismatch" ), qPrintable( messages( *a ) ) );
	}

	//! An array of -1 elements is invalid, one of more than 8 Mi elements is refused before anything is allocated
	void updateArray_refusesBadSizes()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QModelIndex data = a->getBlock( 3 );
		int rows = a->rowCount( a->getIndex( data, "Vertices" ) );
		QCOMPARE( rows, 3 );

		QVERIFY( a->set<int>( data, "Num Vertices", -1 ) );
		messages( *a );
		QVERIFY( !a->updateArray( data, "Vertices" ) );
		QVERIFY2( messages( *a ).contains( "Array Vertices invalid" ), qPrintable( messages( *a ) ) );
		QCOMPARE( a->rowCount( a->getIndex( data, "Vertices" ) ), 3 );

		QVERIFY( a->set<int>( data, "Num Vertices", 8 * 1024 * 1024 + 1 ) );
		QVERIFY( !a->updateArray( data, "Vertices" ) );
		QVERIFY2( messages( *a ).contains( "much too large" ), qPrintable( messages( *a ) ) );
		QCOMPARE( a->rowCount( a->getIndex( data, "Vertices" ) ), 3 );
	}

	// ---- the file

	void fileInfo_afterLoadAndRefresh()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QString path = QDir( dir.path() ).filePath( "scene.nif" );
		QVERIFY( a->saveToFile( path ) );

		NifModel b;
		QVERIFY( b.loadFromFile( path ) );
		QCOMPARE( b.getFileInfo().absoluteFilePath(), QFileInfo( path ).absoluteFilePath() );
		QCOMPARE( b.getFilename(), QString( "scene" ) );
		QCOMPARE( QDir( b.getFolder() ).absolutePath(), QDir( dir.path() ).absolutePath() );

		// the name is the file's name without the extension (and everything after its first dot)
		b.refreshFileInfo( QDir( dir.path() ).filePath( "other.name.nif" ) );
		QCOMPARE( b.getFilename(), QString( "other" ) );
		QCOMPARE( b.getFileInfo().fileName(), QString( "other.name.nif" ) );
	}

	//! Looking at a file's header to see whether a block type and version are in it
	void earlyRejection_cases()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QString path = QDir( dir.path() ).filePath( "skyrim.nif" );
		QVERIFY( a->saveToFile( path ) );

		auto o = make( "10.0.1.0" );
		QVERIFY( TestEnv::buildScene( *o ) );
		QString oldPath = QDir( dir.path() ).filePath( "ni10.nif" );
		QVERIFY( o->saveToFile( oldPath ) );

		QString junk = QDir( dir.path() ).filePath( "junk.nif" );
		{
			QFile f( junk );
			QVERIFY( f.open( QIODevice::WriteOnly ) );
			f.write( "this is not a nif" );
		}

		NifModel m;
		// no version and no block type asked for
		QVERIFY( m.earlyRejection( path, "", 0 ) );
		// a version of 0 is any version, and below 10.0.1.0 no block type is looked for
		QVERIFY( m.earlyRejection( path, "NiNode", 0 ) );
		QVERIFY( m.earlyRejection( path, "NoSuchBlock", 0 ) );
		// the type may be an ancestor of one the file has
		QVERIFY( m.earlyRejection( path, "NiNode", 0x14020007 ) );
		QVERIFY( m.earlyRejection( path, "NiAVObject", 0x14020007 ) );
		QVERIFY( m.earlyRejection( path, "NiObject", 0x14020007 ) );
		QVERIFY( !m.earlyRejection( path, "NiStringExtraData2", 0x14020007 ) );
		QVERIFY( !m.earlyRejection( path, "NoSuchBlock", 0x14020007 ) );
		// another version
		QVERIFY( !m.earlyRejection( path, "NiNode", 0x14000005 ) );
		QVERIFY( !m.earlyRejection( path, "", 0x14000005 ) );
		// a 10.0.1.0 file: its header has the types, and the version asked for is looked at from there on
		QVERIFY( m.earlyRejection( oldPath, "NiNode", 0x0a000100 ) );
		QVERIFY( !m.earlyRejection( oldPath, "NoSuchBlock", 0x0a000100 ) );
		// a file that is no NIF
		QVERIFY( !m.earlyRejection( junk, "", 0 ) );
		TestEnv::takeMessageBoxes();
		QVERIFY( !m.earlyRejection( QDir( dir.path() ).filePath( "missing.nif" ), "", 0 ) );
		TestEnv::takeMessageBoxes();
	}

	// ---- Transform

	void transform_fromBlock()
	{
		auto nif = make( "20.0.0.5", 11, 11 );
		QModelIndex node = nif->insertNiBlock( "NiNode" );
		QVERIFY( node.isValid() );
		QVERIFY( nif->set<Matrix>( node, "Rotation", TestEnv::distinctRotation() ) );
		QVERIFY( nif->set<Vector3>( node, "Translation", Vector3( 1, 2, 3 ) ) );
		QVERIFY( nif->set<float>( node, "Scale", 2.5f ) );

		QVERIFY( Transform::canConstruct( nif.get(), node ) );
		Transform t( nif.get(), node );
		for ( int r = 0; r < 3; r++ ) {
			for ( int c = 0; c < 3; c++ )
				QCOMPARE( t.rotation( r, c ), float( 1 + 3 * r + c ) );
		}
		QVERIFY( t.translation == Vector3( 1, 2, 3 ) );
		QCOMPARE( t.scale, 2.5f );

		// and back, into another node
		QModelIndex other = nif->insertNiBlock( "NiNode" );
		t.scale = 4.0f;
		t.translation = Vector3( -4, 5, -6 );
		t.writeBack( nif.get(), other );
		QCOMPARE( nif->get<float>( other, "Scale" ), 4.0f );
		QVERIFY( nif->get<Vector3>( other, "Translation" ) == Vector3( -4, 5, -6 ) );
		QCOMPARE( nif->get<Matrix>( other, "Rotation" )( 1, 0 ), 4.0f );
		// the first one is as it was
		QCOMPARE( nif->get<float>( node, "Scale" ), 2.5f );

		// a block that has no transform, no block at all, no model
		QModelIndex extra = nif->insertNiBlock( "NiStringExtraData" );
		QVERIFY( extra.isValid() );
		QVERIFY( !Transform::canConstruct( nif.get(), extra ) );
		QVERIFY( !Transform::canConstruct( nif.get(), QModelIndex() ) );
		QVERIFY( !Transform::canConstruct( nullptr, node ) );
	}

	//! The transform of a skin is in the "Skin Transform" of its data
	void transform_fromSkinData()
	{
		auto nif = make( "20.0.0.5", 11, 11 );
		QModelIndex skin = nif->insertNiBlock( "NiSkinData" );
		QVERIFY( skin.isValid() );
		QModelIndex inner = nif->getIndex( skin, "Skin Transform" );
		QVERIFY( inner.isValid() );
		QVERIFY( nif->set<Matrix>( inner, "Rotation", TestEnv::distinctRotation() ) );
		QVERIFY( nif->set<Vector3>( inner, "Translation", Vector3( 7, 8, 9 ) ) );
		QVERIFY( nif->set<float>( inner, "Scale", 0.5f ) );

		QVERIFY( Transform::canConstruct( nif.get(), skin ) );
		Transform t( nif.get(), skin );
		QCOMPARE( t.rotation( 2, 1 ), 8.0f );
		QVERIFY( t.translation == Vector3( 7, 8, 9 ) );
		QCOMPARE( t.scale, 0.5f );

		t.scale = 3.0f;
		t.writeBack( nif.get(), skin );
		QCOMPARE( nif->get<float>( inner, "Scale" ), 3.0f );
	}

	//! A compound that holds its transform in a field called "Transform" (a combined geometry of a Fallout 4 packed extra data): it is read from there
	//! and written back there
	void transform_fromTransformField()
	{
		auto nif = make( "20.2.0.7", 12, 130 );
		QModelIndex extra = nif->insertNiBlock( "BSPackedCombinedGeomDataExtra" );
		QVERIFY( extra.isValid() );
		QVERIFY( nif->set<int>( extra, "Num Data", 1 ) );
		QVERIFY( nif->updateArray( extra, "Object Data" ) );
		QModelIndex object = nif->index( 0, 0, nif->getIndex( extra, "Object Data" ) );
		QVERIFY( object.isValid() );
		QVERIFY( nif->set<int>( object, "Num Combined", 1 ) );
		QVERIFY( nif->updateArray( object, "Combined" ) );
		QModelIndex combined = nif->index( 0, 0, nif->getIndex( object, "Combined" ) );
		QVERIFY( combined.isValid() );
		QModelIndex inner = nif->getIndex( combined, "Transform" );
		QVERIFY( inner.isValid() );

		QVERIFY( nif->set<Matrix>( inner, "Rotation", TestEnv::distinctRotation() ) );
		QVERIFY( nif->set<Vector3>( inner, "Translation", Vector3( 7, 8, 9 ) ) );
		QVERIFY( nif->set<float>( inner, "Scale", 0.5f ) );

		Transform t( nif.get(), combined );
		QCOMPARE( t.rotation( 2, 1 ), 8.0f );
		QVERIFY( t.translation == Vector3( 7, 8, 9 ) );
		QCOMPARE( t.scale, 0.5f );

		t.translation = Vector3( 4, 5, 6 );
		t.scale = 3.0f;
		t.writeBack( nif.get(), combined );
		QVERIFY( nif->get<Vector3>( inner, "Translation" ) == Vector3( 4, 5, 6 ) );
		QCOMPARE( nif->get<float>( inner, "Scale" ), 3.0f );
		QCOMPARE( nif->get<Matrix>( inner, "Rotation" )( 2, 1 ), 8.0f );
	}

	//! Nothing to take a transform from: no model, no item, or one that has no transform in it
	void transform_canConstruct_noTransform()
	{
		auto nif = make( "20.0.0.5", 11, 11 );
		QModelIndex node = nif->insertNiBlock( "NiNode" );
		QVERIFY( node.isValid() );
		QVERIFY( Transform::canConstruct( nif.get(), node ) );
		QVERIFY( !Transform::canConstruct( nif.get(), QModelIndex() ) );
		QVERIFY( !Transform::canConstruct( nif.get(), nif->getHeader() ) );
		QVERIFY( !Transform::canConstruct( nullptr, QModelIndex() ) );

		// a block that is not a transform, though it has a field that says "Scale"
		QModelIndex data = nif->insertNiBlock( "NiTriShapeData" );
		QVERIFY( data.isValid() );
		QVERIFY( !Transform::canConstruct( nif.get(), data ) );
	}

	// ---- what a load leaves behind, a failed one too

	//! A model that has loaded a file and then fails to load another one does not say which file it holds
	void fileInfo_resetByFailedLoad()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QString path = QDir( dir.path() ).filePath( "scene.nif" );
		QVERIFY( a->saveToFile( path ) );
		QString junk = QDir( dir.path() ).filePath( "junk.nif" );
		{
			QFile f( junk );
			QVERIFY( f.open( QIODevice::WriteOnly ) );
			f.write( "this is not a nif" );
		}

		NifModel b;
		QVERIFY( b.loadFromFile( path ) );
		QCOMPARE( b.getFilename(), QString( "scene" ) );
		QVERIFY( !b.getFileInfo().filePath().isEmpty() );
		QVERIFY( !b.getFolder().isEmpty() );

		QVERIFY( !b.loadFromFile( junk ) );
		TestEnv::takeMessageBoxes();
		QCOMPARE( b.getFilename(), QString() );
		QCOMPARE( b.getFolder(), QString() );
		QVERIFY( b.getFileInfo().filePath().isEmpty() );
	}

	//! The name of a file is what is in front of its first dot, and the model keeps the folder it is in
	void loadFromFile_multiDotName()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QString path = QDir( dir.path() ).filePath( "my.scene.nif" );
		QVERIFY( a->saveToFile( path ) );

		NifModel b;
		QVERIFY( b.loadFromFile( path ) );
		QCOMPARE( b.getFilename(), QString( "my" ) );
		QCOMPARE( QDir( b.getFolder() ).absolutePath(), QDir( dir.path() ).absolutePath() );
	}

	void refreshFileInfo_keepsTheFolder()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		NifModel b;
		b.refreshFileInfo( QDir( dir.path() ).filePath( "other.name.nif" ) );
		QCOMPARE( QDir( b.getFolder() ).absolutePath(), QDir( dir.path() ).absolutePath() );
	}

	//! A model that loads a file is in the state "Loading" while it does, and in no state after: BaseModel does that for every kind of model
	void loadFromFile_state()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		QString path = QDir( dir.path() ).filePath( "x.bin" );
		{
			QFile f( path );
			QVERIFY( f.open( QIODevice::WriteOnly ) );
			f.write( "x" );
		}

		StubModel m;
		QVERIFY( m.loadFromFile( path ) );
		QCOMPARE( m.stateWhileLoading, int( BaseModel::Loading ) );
		QCOMPARE( m.getState(), BaseModel::Default );
	}

	//! A start-up version that nif.xml does not list is replaced by 20.0.0.5, with a message
	void newModel_unsupportedStartupVersion()
	{
		struct Restore { ~Restore() { QSettings().remove( "Settings/NIF/Startup Defaults" ); } } restore;

		{
			QSettings settings;
			settings.beginGroup( "Settings/NIF/Startup Defaults" );
			settings.setValue( "Version", "1.2.3.4" );
			settings.setValue( "User Version", 0 );
			settings.setValue( "User Version 2", 0 );
			settings.endGroup();
		}

		NifModel nif;
		QCOMPARE( nif.getVersion(), QString( "20.0.0.5" ) );
		QStringList boxes = TestEnv::takeMessageBoxes();
		QVERIFY2( boxes.join( " | " ).contains( "Unsupported 'Startup Version'" ), qPrintable( boxes.join( " | " ) ) );
	}

	//! A load that fails inside a block leaves the model reset: not in the state "Loading"
	void load_failure_resetsState()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QByteArray bytes = savedBytes( *a );
		QVERIFY( !bytes.isEmpty() );

		NifModel b;
		QVERIFY( !loadBytes( b, bytes.left( bytes.size() / 2 ) ) );
		QCOMPARE( b.getState(), BaseModel::Default );
	}

	//! A block that is larger than what is read of it (four bytes more) is skipped to its end, with one message, and the blocks after it are read from there
	void load_blockSize_skipsTrailingBytes()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QByteArray bytes = savedBytes( *a );
		QVERIFY( !bytes.isEmpty() );

		int sizeOff = a->fileOffset( a->index( 0, 0, a->getIndex( a->getHeader(), "Block Size" ) ) );
		QVERIFY( sizeOff > 0 );
		quint32 size0 = quint32( quint8( bytes[sizeOff] ) ) | ( quint32( quint8( bytes[sizeOff + 1] ) ) << 8 );
		int end0 = a->fileOffset( a->getBlock( 0 ) ) + int( size0 );

		// block 0 is four bytes longer than the model knows
		QByteArray bad = bytes;
		bad.replace( sizeOff, 4, le32( size0 + 4 ) );
		bad.insert( end0, QByteArray( 4, 'Z' ) );

		SettingRestorer restore( "Ignore Block Size" );
		QSettings().setValue( "Ignore Block Size", false );

		NifModel b;
		QVERIFY( loadBytes( b, bad ) );
		QCOMPARE( b.getBlockCount(), 5 );
		QString text = messages( b );
		QCOMPARE( text.count( "device position incorrect" ), 1 );
		QVERIFY2( text.contains( "block number 0" ), qPrintable( text ) );
		QCOMPARE( b.get<QString>( b.getBlock( 0 ), "Name" ), QString( "Scene Root" ) );
		QCOMPARE( b.get<QString>( b.getBlock( 1 ), "Name" ), QString( "Child" ) );
		QCOMPARE( b.get<QString>( b.getBlock( 2 ), "Name" ), QString( "Shape" ) );
		QCOMPARE( b.get<QString>( b.getBlock( 4 ), "Name" ), QString( "Note" ) );
	}

	//! The progress of a load goes from (0, blocks) to (blocks, blocks), that of a save from (0, rows) to (rows, rows)
	void progress_values()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );

		QList<QPair<int, int> > saving;
		QObject::connect( a.get(), &NifModel::sigProgress, [&]( int c, int m ) { saving << qMakePair( c, m ); } );
		QByteArray bytes = savedBytes( *a );
		QVERIFY( !bytes.isEmpty() );
		QVERIFY( !saving.isEmpty() );
		QCOMPARE( saving.first(), qMakePair( 0, a->rowCount() ) );
		QCOMPARE( saving.last(), qMakePair( a->rowCount(), a->rowCount() ) );

		NifModel b;
		QList<QPair<int, int> > loading;
		QObject::connect( &b, &NifModel::sigProgress, [&]( int c, int m ) { loading << qMakePair( c, m ); } );
		QVERIFY( loadBytes( b, bytes ) );
		QVERIFY( !loading.isEmpty() );
		QCOMPARE( loading.first(), qMakePair( 0, 5 ) );
		QCOMPARE( loading.last(), qMakePair( 5, 5 ) );
	}

	//! A header of a 20.2.0.7 file has Max String Length even when it has the one string only
	void updateHeader_maxStringLength_oneString()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QModelIndex iNode = a->insertNiBlock( "NiNode" );
		QVERIFY( iNode.isValid() );
		QVERIFY( a->set<QString>( iNode, "Name", "abcdef" ) );
		QCOMPARE( int( a->get<uint>( a->getHeader(), "Num Strings" ) ), 1 );

		QByteArray bytes = savedBytes( *a );
		QVERIFY( !bytes.isEmpty() );
		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( int( b.get<uint>( b.getHeader(), "Max String Length" ) ), 6 );
	}

	//! A save counts the arrays inside the rows of an array: the Num Vertices of a bone is set, the rows are there when the file is loaded
	void save_updatesNestedArrays()
	{
		auto a = make( "20.2.0.7", 12, 83 );
		QModelIndex iSkinData = a->insertNiBlock( "NiSkinData" );
		QVERIFY( iSkinData.isValid() );
		QVERIFY( a->set<int>( iSkinData, "Num Bones", 1 ) );
		QModelIndex iBones = a->getIndex( iSkinData, "Bone List" );
		QVERIFY( a->updateArray( iBones ) );
		QModelIndex iBone = a->index( 0, 0, iBones );
		QVERIFY( a->set<int>( iBone, "Num Vertices", 2 ) );

		QByteArray bytes = savedBytes( *a );
		QVERIFY( !bytes.isEmpty() );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QModelIndex iWeights = b.getIndex( b.index( 0, 0, b.getIndex( b.getBlock( 0 ), "Bone List" ) ), "Vertex Weights" );
		QCOMPARE( b.rowCount( iWeights ), 2 );
		QCOMPARE( messages( b ), QString() );
	}

	//! The header's size of a block counts the bytes of the block, and not the abstract fields of its type (a NiDataStream's Usage and Access are in the type, not in the file)
	void save_blockSize_withoutAbstractFields()
	{
		auto a = make( "20.5.0.0" );
		QModelIndex ds = a->insertNiBlock( "NiDataStream" );
		QVERIFY( ds.isValid() );
		QVERIFY( a->set<int>( ds, "Usage", 1 ) );
		QVERIFY( a->set<int>( ds, "Access", 2 ) );

		QByteArray bytes = savedBytes( *a );
		QVERIFY( !bytes.isEmpty() );

		int start = a->fileOffset( a->getBlock( 0 ) );
		int footer = 4 + 4 * a->get<int>( a->getFooter(), "Num Roots" );
		int real = bytes.size() - start - footer;
		QCOMPARE( a->get<int>( a->index( 0, 0, a->getIndex( a->getHeader(), "Block Size" ) ) ), real );
	}

	//! Updating the header tells the views before it does
	void updateHeader_announcesItself()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );

		QSignalSpy spy( a.get(), &NifModel::beginUpdateHeader );
		QVERIFY( !savedBytes( *a ).isEmpty() );
		QCOMPARE( spy.count(), 1 );
	}

	//! A save puts the roots in the footer: a file whose footer lists the wrong block comes out with the right one
	void save_correctsAStaleFooter()
	{
		auto a = make( "4.0.0.2" );
		QVERIFY( TestEnv::buildScene( *a ) );
		QByteArray bytes = savedBytes( *a );
		QVERIFY( bytes.endsWith( le32( 1 ) + le32( 0 ) ) );

		QByteArray bad = bytes;
		bad.replace( bad.size() - 4, 4, le32( 3 ) );

		NifModel b;
		QVERIFY( loadBytes( b, bad ) );
		QCOMPARE( b.getRootLinks(), QList<int>() << 0 );
		QByteArray again = savedBytes( b );
		QVERIFY2( again == bytes, qPrintable( TestEnv::diffBytes( again, bytes ) ) );
	}

	//! The version in a header line may be followed by something that is not a blank
	void headerString_noBlankAfterTheVersion()
	{
		auto loaded = make( "4.0.0.2" );
		QVERIFY( !loadBytes( *loaded, QByteArray( "Gamebryo File Format, Version 20.2.0.7(export)\n" ) ) );
		QCOMPARE( loaded->getVersion(), QString( "20.2.0.7" ) );
	}

	// ---- the order of links, the footer and the header after an edit

	//! The links of a block come in the order of its fields
	void childLinks_order()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		// the extra data of the root first, then its children
		QCOMPARE( a->getChildLinks( 0 ), QList<int>() << 4 << 1 << 2 );
	}

	//! A link set by its index is a link like the one set by name: the links, the roots and the footer follow
	void setLink_byIndex()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QModelIndex iOrphan = a->insertNiBlock( "NiNode" );
		QVERIFY( iOrphan.isValid() );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 5 );

		QVERIFY( a->setLink( a->getIndex( a->getBlock( 0 ), "Controller" ), 5 ) );
		QCOMPARE( a->getChildLinks( 0 ), QList<int>() << 4 << 5 << 1 << 2 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 );
		QCOMPARE( a->get<int>( a->getFooter(), "Num Roots" ), 1 );
	}

	//! A block inserted at the end takes the place of a link that points past the last block
	void insertNiBlock_atTheEnd_movesADanglingLink()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QVERIFY( a->setLink( a->getBlock( 0 ), "Controller", 5 ) );
		QVERIFY( a->insertNiBlock( "NiNode", 5 ).isValid() );
		QCOMPARE( a->getLink( a->getBlock( 0 ), "Controller" ), 6 );
	}

	//! After a block is removed the links and the footer describe the blocks that are left
	void removeNiBlock_updatesLinksAndFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		a->removeNiBlock( 1 );
		QCOMPARE( a->getBlockCount(), 4 );
		// the root's Children were 1 and 2: 1 is gone, 2 became 1; the extra data was 4 and became 3
		QCOMPARE( a->getChildLinks( 0 ), QList<int>() << 3 << 1 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );

		a->removeNiBlock( 0 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 2 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );
	}

	void moveNiBlock_updatesHeaderAndFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		a->moveNiBlock( 1, 4 );
		QVERIFY2( headerProblem( *a ).isEmpty(), qPrintable( headerProblem( *a ) ) );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );

		a->moveNiBlock( 0, 3 );
		QVERIFY2( headerProblem( *a ).isEmpty(), qPrintable( headerProblem( *a ) ) );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 3 );
	}

	void reorderBlocks_updatesHeaderAndFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		a->reorderBlocks( QVector<qint32>() << 1 << 2 << 3 << 4 << 0 );
		QVERIFY2( headerProblem( *a ).isEmpty(), qPrintable( headerProblem( *a ) ) );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 1 );
	}

	//! The roots are in block order, in the model and in the footer, whatever the blocks were moved or reordered into
	void roots_keepBlockOrderInTheFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		for ( int i = 0; i < 3; i++ )
			QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 1 << 2 );

		a->moveNiBlock( 0, 2 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 1 << 2 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );

		a->reorderBlocks( QVector<qint32>() << 2 << 0 << 1 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 1 << 2 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );
	}

	//! mapLinks( map ) sends the links where the map says, and the links, the roots and the footer follow
	void mapLinks_updatesLinksAndFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QMap<qint32, qint32> map;
		map.insert( 1, 2 );
		a->mapLinks( map );

		QCOMPARE( a->getLinkArray( a->getBlock( 0 ), "Children" ), QVector<qint32>() << 2 << 2 );
		QCOMPARE( a->getChildLinks( 0 ), QList<int>() << 4 << 2 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 1 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );
		QVERIFY2( headerProblem( *a ).isEmpty(), qPrintable( headerProblem( *a ) ) );
	}

	//! A block that was removed leaves the header as it was until the next update of it, which any mapLinks does
	void mapLinks_updatesTheHeader()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		a->removeNiBlock( 4 );
		a->mapLinks( QMap<qint32, qint32>() );
		QCOMPARE( a->get<int>( a->getHeader(), "Num Blocks" ), a->getBlockCount() );
	}

	//! The footer follows the links that are set, whichever way they are set
	void setLink_updatesTheFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 5 << 6 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );

		// by name
		QVERIFY( a->setLink( a->getBlock( 0 ), "Controller", 5 ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 6 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );

		// by index
		QVERIFY( a->setLink( a->getIndex( a->getBlock( 0 ), "Controller" ), 6 ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 5 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );

		// as an array
		QVERIFY( a->set<int>( a->getBlock( 5 ), "Num Children", 1 ) );
		QVERIFY( a->updateArray( a->getBlock( 5 ), "Children" ) );
		QVERIFY( a->setLinkArray( a->getBlock( 5 ), "Children", QVector<qint32>() << 6 ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 5 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );

		QVERIFY( a->setLink( a->getIndex( a->getBlock( 0 ), "Controller" ), -1 ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 5 );
		QVERIFY( a->setLinkArray( a->getBlock( 5 ), "Children", QVector<qint32>() << 4 ) );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 5 << 6 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );
	}

	//! Letting go of held updates tells the views that the links changed
	void holdUpdates_release_announcesLinks()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		a->holdUpdates( true );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );

		QSignalSpy spy( a.get(), &NifModel::linksChanged );
		a->holdUpdates( false );
		QVERIFY( spy.count() > 0 );
	}

	//! A model that is cleared forgets the updates it held back
	void clear_forgetsHeldUpdates()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		a->holdUpdates( true );
		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		a->clear();

		QSignalSpy spy( a.get(), &NifModel::linksChanged );
		a->holdUpdates( true );
		a->holdUpdates( false );
		QCOMPARE( spy.count(), 0 );
	}

	//! Every edit of the blocks tells the views that the links changed
	void linksChanged_blockEdits()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QSignalSpy spy( a.get(), &NifModel::linksChanged );

		auto emitted = [&]( const char * what ) {
			bool yes = spy.count() > 0;
			spy.clear();
			return yes ? QString() : QString( "%1 did not emit linksChanged" ).arg( what );
		};

		QVERIFY( a->insertNiBlock( "NiNode" ).isValid() );
		QVERIFY2( emitted( "insertNiBlock" ).isEmpty(), "insertNiBlock" );
		a->removeNiBlock( 5 );
		QVERIFY2( emitted( "removeNiBlock" ).isEmpty(), "removeNiBlock" );
		a->moveNiBlock( 1, 4 );
		QVERIFY2( emitted( "moveNiBlock" ).isEmpty(), "moveNiBlock" );
		a->reorderBlocks( QVector<qint32>() << 1 << 2 << 3 << 4 << 0 );
		QVERIFY2( emitted( "reorderBlocks" ).isEmpty(), "reorderBlocks" );
		a->mapLinks( QMap<qint32, qint32>() );
		QVERIFY2( emitted( "mapLinks" ).isEmpty(), "mapLinks" );
	}

	//! ... and every edit of a link
	void linksChanged_linkEdits()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QSignalSpy spy( a.get(), &NifModel::linksChanged );

		auto emitted = [&]() {
			bool yes = spy.count() > 0;
			spy.clear();
			return yes;
		};

		QVERIFY( a->setLink( a->getBlock( 0 ), "Controller", 1 ) );
		QVERIFY2( emitted(), "setLink by name" );
		QVERIFY( a->setLink( a->getIndex( a->getBlock( 0 ), "Controller" ), 2 ) );
		QVERIFY2( emitted(), "setLink by index" );
		QVERIFY( a->setLinkArray( a->getBlock( 0 ), "Extra Data List", QVector<qint32>() << 3 ) );
		QVERIFY2( emitted(), "setLinkArray" );

		QByteArray root = blockBytes( *a, 0 );
		QVERIFY( !root.isEmpty() );
		{
			QBuffer in( &root );
			QVERIFY( in.open( QIODevice::ReadOnly ) );
			QVERIFY( a->loadIndex( in, a->getBlock( 0 ) ) );
			QVERIFY2( emitted(), "loadIndex" );
		}
		{
			QBuffer in( &root );
			QVERIFY( in.open( QIODevice::ReadOnly ) );
			QVERIFY( a->loadAndMapLinks( in, a->getBlock( 0 ), QMap<qint32, qint32>() ) );
			QVERIFY2( emitted(), "loadAndMapLinks" );
		}
	}

	// ---- a block read back into a model

	//! loadIndex() of a block brings the links, the roots and the footer up to date
	void loadIndex_updatesLinksAndFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QByteArray bytes = savedBytes( *a );
		QVERIFY( !bytes.isEmpty() );
		QByteArray root = blockBytes( *a, 0 );
		QVERIFY( !root.isEmpty() );

		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		// the root holds no children, so that its children are roots
		QVERIFY( b.set<int>( b.getBlock( 0 ), "Num Children", 0 ) );
		QVERIFY( b.updateArray( b.getBlock( 0 ), "Children" ) );
		QCOMPARE( b.getRootLinks(), QList<int>() << 0 << 1 << 2 );

		QBuffer in( &root );
		QVERIFY( in.open( QIODevice::ReadOnly ) );
		QVERIFY( b.loadIndex( in, b.getBlock( 0 ) ) );
		QCOMPARE( b.getChildLinks( 0 ), QList<int>() << 4 << 1 << 2 );
		QCOMPARE( b.getRootLinks(), QList<int>() << 0 );
		QCOMPARE( b.get<int>( b.getFooter(), "Num Roots" ), 1 );
	}

	//! loadAndMapLinks() sends the links it loads where the map says
	void loadAndMapLinks_mapsTheLinks()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QByteArray root = blockBytes( *a, 0 );
		QVERIFY( !root.isEmpty() );

		// the children are 1 and 2: swapped
		QMap<qint32, qint32> swap;
		swap.insert( 1, 2 );
		swap.insert( 2, 1 );
		QBuffer in( &root );
		QVERIFY( in.open( QIODevice::ReadOnly ) );
		QVERIFY( a->loadAndMapLinks( in, a->getBlock( 0 ), swap ) );
		QCOMPARE( a->getLinkArray( a->getBlock( 0 ), "Children" ), QVector<qint32>() << 2 << 1 );
	}

	//! ... and the links, the roots and the footer follow: with 1 sent to 2 the root has the child 2 twice and block 1 is a root
	void loadAndMapLinks_updatesLinksAndFooter()
	{
		auto a = make( "20.0.0.5", 11, 11 );
		QVERIFY( TestEnv::buildScene( *a ) );
		QByteArray root = blockBytes( *a, 0 );
		QVERIFY( !root.isEmpty() );

		QMap<qint32, qint32> map;
		map.insert( 1, 2 );
		QBuffer in( &root );
		QVERIFY( in.open( QIODevice::ReadOnly ) );
		QVERIFY( a->loadAndMapLinks( in, a->getBlock( 0 ), map ) );
		QCOMPARE( a->getChildLinks( 0 ), QList<int>() << 4 << 2 );
		QCOMPARE( a->getRootLinks(), QList<int>() << 0 << 1 );
		QVERIFY2( footerProblem( *a ).isEmpty(), qPrintable( footerProblem( *a ) ) );
	}

	//! Blocks moved from a model that has a string table (20.1.0.3 and up) into one that has not keep their names
	void moveAllNiBlocks_toAModelWithoutAStringTable()
	{
		auto src = make( "20.2.0.7", 12, 83 );
		QModelIndex n0 = src->insertNiBlock( "NiNode" ), n1 = src->insertNiBlock( "NiNode" );
		QVERIFY( src->set<QString>( n0, "Name", "Alpha" ) );
		QVERIFY( src->set<QString>( n1, "Name", "Beta" ) );

		auto tgt = make( "20.0.0.5", 11, 11 );
		src->moveAllNiBlocks( tgt.get(), true );
		QCOMPARE( tgt->getBlockCount(), 2 );
		QCOMPARE( tgt->get<QString>( tgt->getBlock( 0 ), "Name" ), QString( "Alpha" ) );
		QCOMPARE( tgt->get<QString>( tgt->getBlock( 1 ), "Name" ), QString( "Beta" ) );
	}
};

REGISTER_TEST( tst_NifModel )

#include "tst_nifmodel.moc"
