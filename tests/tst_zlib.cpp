#include "testregistry.h"

#include "zlib/zlib.h"

#include <QTest>


//! Defined in lib/fsengine/bsa.cpp (no header declares it): inflates zlib or gzip data, as BSA archives store it
QByteArray gUncompress( const QByteArray & data, const int size );


//! lib/zlib as consumed by lib/fsengine (BSA decompression). Guards the zlib submodule bump.
class tst_Zlib final : public QObject
{
	Q_OBJECT

	//! Deterministic test data: runs, a counter and noise from a fixed LCG, so it compresses unevenly
	static QByteArray pattern( int size )
	{
		QByteArray out;
		out.reserve( size );
		quint32 lcg = 12345;
		for ( int i = 0; i < size; i++ ) {
			lcg = lcg * 1664525u + 1013904223u;
			switch ( ( i / 4096 ) % 3 ) {
			case 0: out.append( char( 'a' ) ); break;
			case 1: out.append( char( i & 0xFF ) ); break;
			default: out.append( char( lcg >> 24 ) ); break;
			}
		}
		return out;
	}

	static QByteArray deflateBytes( const QByteArray & in, int level, int windowBits )
	{
		z_stream z = {};
		if ( deflateInit2( &z, level, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY ) != Z_OK )
			return QByteArray();

		QByteArray out( int( deflateBound( &z, uLong( in.size() ) ) ), 0 );
		z.next_in = reinterpret_cast<Bytef *>( const_cast<char *>( in.constData() ) );
		z.avail_in = uInt( in.size() );
		z.next_out = reinterpret_cast<Bytef *>( out.data() );
		z.avail_out = uInt( out.size() );
		int rc = deflate( &z, Z_FINISH );
		out.resize( int( z.total_out ) );
		deflateEnd( &z );
		return rc == Z_STREAM_END ? out : QByteArray();
	}

private slots:
	//! The runtime library must be the bundled one, not a system/Qt copy that happens to be first on the link line
	void linkedLibraryMatchesBundledHeader()
	{
		QCOMPARE( QString::fromLatin1( zlibVersion() ), QString::fromLatin1( ZLIB_VERSION ) );
	}

	//! The oldest zlib the project accepts (1.2.8 was the floor before the bump to 1.3.2). Raise it in the same
	//! commit that bumps lib/zlib.
	void minimumVersion()
	{
		const int minMajor = 1, minMinor = 3, minPatch = 2;

		QStringList parts = QString::fromLatin1( zlibVersion() ).split( '.' );
		QVERIFY( parts.count() >= 3 );
		int v = parts[0].toInt() * 10000 + parts[1].toInt() * 100 + parts[2].toInt();
		QVERIFY2( v >= minMajor * 10000 + minMinor * 100 + minPatch, zlibVersion() );
	}

	void checksums()
	{
		const Bytef * s = reinterpret_cast<const Bytef *>( "123456789" );
		QCOMPARE( quint32( crc32( crc32( 0L, Z_NULL, 0 ), s, 9 ) ), 0xCBF43926u );
		QCOMPARE( quint32( adler32( adler32( 0L, Z_NULL, 0 ), s, 9 ) ), 0x091E01DEu );
	}

	void roundTrip_data()
	{
		QTest::addColumn<int>( "size" );
		QTest::addColumn<int>( "level" );

		for ( int level : { 1, 6, 9 } ) {
			for ( int size : { 1, 100, 4095, 4096, 100000, 1 << 20 } )
				QTest::newRow( qPrintable( QString( "%1 bytes, level %2" ).arg( size ).arg( level ) ) ) << size << level;
		}
	}

	void roundTrip()
	{
		QFETCH( int, size );
		QFETCH( int, level );

		QByteArray original = pattern( size );

		uLongf bound = compressBound( uLong( original.size() ) );
		QByteArray packed( int( bound ), 0 );
		QCOMPARE( compress2( reinterpret_cast<Bytef *>( packed.data() ), &bound,
			reinterpret_cast<const Bytef *>( original.constData() ), uLong( original.size() ), level ), Z_OK );
		packed.resize( int( bound ) );

		QByteArray plain( original.size(), 0 );
		uLongf plainLen = uLongf( plain.size() );
		QCOMPARE( uncompress( reinterpret_cast<Bytef *>( plain.data() ), &plainLen,
			reinterpret_cast<const Bytef *>( packed.constData() ), uLong( packed.size() ) ), Z_OK );
		QCOMPARE( int( plainLen ), original.size() );
		QVERIFY( plain == original );

		// the path BSA extraction uses
		QVERIFY( gUncompress( packed, packed.size() ) == original );
	}

	//! inflateInit2( 15 + 32 ) in bsa.cpp auto-detects the header: zlib, gzip, and raw deflate is not accepted
	void gUncompress_gzipAndZlib()
	{
		QByteArray original = pattern( 50000 );

		QByteArray zlibStream = deflateBytes( original, 6, 15 );
		QByteArray gzipStream = deflateBytes( original, 6, 15 + 16 );
		QVERIFY( !zlibStream.isEmpty() && !gzipStream.isEmpty() );
		QVERIFY( zlibStream != gzipStream );

		QVERIFY( gUncompress( zlibStream, zlibStream.size() ) == original );
		QVERIFY( gUncompress( gzipStream, gzipStream.size() ) == original );
	}

	void gUncompress_rejectsCorruptData()
	{
		QByteArray original = pattern( 50000 );
		QByteArray packed = deflateBytes( original, 6, 15 );
		QVERIFY( packed.size() > 100 );

		// flip bytes in the middle of the deflate stream: either inflate errors out or the adler32 check fails
		QByteArray bad = packed;
		for ( int i = 40; i < 60; i++ )
			bad[i] = char( ~bad[i] );

		QByteArray out = gUncompress( bad, bad.size() );
		QVERIFY( out.isEmpty() || out != original );

		// too short to be a stream at all
		QTest::ignoreMessage( QtWarningMsg, "gUncompress: Input data is truncated" );
		QVERIFY( gUncompress( QByteArray( "abc" ), 3 ).isEmpty() );
	}
};

REGISTER_TEST( tst_Zlib )

#include "tst_zlib.moc"
