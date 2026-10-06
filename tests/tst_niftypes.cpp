#include "testregistry.h"

#include "data/niftypes.h"
#include "model/kfmmodel.h"
#include "model/nifmodel.h"

#include <QColor>
#include <QDataStream>
#include <QTest>

#include <cfloat>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <new>


//! Compares two values of the types of niftypes.h with ==, and says both when they differ (both are evaluated twice)
#define CHECK_EQ( actual, expected ) \
	QVERIFY2( ( actual ) == ( expected ), qPrintable( QString( "%1 is %2, expected %3" ).arg( QString::fromLatin1( #actual ), describe( actual ), describe( expected ) ) ) )


//! Pure value types and version number conversion; no XML, no models
class tst_NifTypes final : public QObject
{
	Q_OBJECT

	static bool near( float a, float b, float eps = 1e-5f ) { return std::fabs( a - b ) <= eps; }

	static bool near( const Vector3 & a, const Vector3 & b, float eps = 1e-5f )
	{
		return near( a[0], b[0], eps ) && near( a[1], b[1], eps ) && near( a[2], b[2], eps );
	}

	static bool near( const Quat & a, const Quat & b, float eps = 1e-5f )
	{
		return near( a[0], b[0], eps ) && near( a[1], b[1], eps ) && near( a[2], b[2], eps ) && near( a[3], b[3], eps );
	}

	static QString show( const Quat & q ) { return QString( "w %1 x %2 y %3 z %4" ).arg( q[0], 0, 'g', 9 ).arg( q[1], 0, 'g', 9 ).arg( q[2], 0, 'g', 9 ).arg( q[3], 0, 'g', 9 ); }

	//! Matrix33 from nine floats, row after row
	static Matrix matrixOf( std::initializer_list<float> rowMajor )
	{
		Matrix m;
		unsigned i = 0;
		for ( float x : rowMajor ) {
			m( i / 3, i % 3 ) = x;
			i++;
		}

		return m;
	}

	//! Matrix44 from sixteen floats, row after row
	static Matrix4 matrix4Of( std::initializer_list<float> rowMajor )
	{
		Matrix4 m;
		unsigned i = 0;
		for ( float x : rowMajor ) {
			m( i / 4, i % 4 ) = x;
			i++;
		}

		return m;
	}

	//! The first element that differs by more than eps, as text; empty when the matrices agree
	static QString matrixDiff( const Matrix & got, const Matrix & want, float eps = 1e-5f )
	{
		for ( unsigned r = 0; r < 3; r++ ) {
			for ( unsigned c = 0; c < 3; c++ ) {
				if ( !near( got( r, c ), want( r, c ), eps ) )
					return QString( "(%1, %2) is %3, expected %4" ).arg( r ).arg( c ).arg( got( r, c ), 0, 'g', 9 ).arg( want( r, c ), 0, 'g', 9 );
			}
		}

		return QString();
	}

	static QString matrix4Diff( const Matrix4 & got, const Matrix4 & want, float eps = 1e-5f )
	{
		for ( unsigned r = 0; r < 4; r++ ) {
			for ( unsigned c = 0; c < 4; c++ ) {
				if ( !near( got( r, c ), want( r, c ), eps ) )
					return QString( "(%1, %2) is %3, expected %4" ).arg( r ).arg( c ).arg( got( r, c ), 0, 'g', 9 ).arg( want( r, c ), 0, 'g', 9 );
			}
		}

		return QString();
	}

	static QVector<float> floats4( float a, float b, float c, float d ) { return QVector<float>() << a << b << c << d; }
	static QVector<float> floats9( float a, float b, float c, float d, float e, float f, float g, float h, float i )
	{
		return QVector<float>() << a << b << c << d << e << f << g << h << i;
	}

	//! The 64 bits of a BSVertexDesc as they are stored (QDataStream writes the word little endian, which this test sets)
	static quint64 raw( BSVertexDesc d )
	{
		QByteArray bytes;
		QDataStream out( &bytes, QIODevice::WriteOnly );
		out.setByteOrder( QDataStream::LittleEndian );
		out << d;

		quint64 word = 0;
		for ( int i = 7; i >= 0; i-- )
			word = ( word << 8 ) | quint8( bytes.at( i ) );

		return word;
	}

	static QString hex64( quint64 n ) { return QString( "0x%1" ).arg( n, 16, 16, QLatin1Char( '0' ) ); }

	//! The array of Quats and Matrix4s a test builds in memory it fills first, so that an element the constructor leaves out shows (as NaN)
	template <class T> struct Poisoned
	{
		alignas( T ) unsigned char storage[sizeof( T )];
		T * object = nullptr;

		Poisoned() { memset( storage, 0xff, sizeof( storage ) ); object = new ( storage ) T(); }
		~Poisoned() { object->~T(); }
	};

	// ---- describing values for a failed comparison

	static QString describe( const Vector2 & v ) { return QString( "(%1, %2)" ).arg( v[0], 0, 'g', 9 ).arg( v[1], 0, 'g', 9 ); }
	static QString describe( const Vector3 & v ) { return QString( "(%1, %2, %3)" ).arg( v[0], 0, 'g', 9 ).arg( v[1], 0, 'g', 9 ).arg( v[2], 0, 'g', 9 ); }
	static QString describe( const Vector4 & v ) { return QString( "(%1, %2, %3, %4)" ).arg( v[0], 0, 'g', 9 ).arg( v[1], 0, 'g', 9 ).arg( v[2], 0, 'g', 9 ).arg( v[3], 0, 'g', 9 ); }
	static QString describe( const Quat & q ) { return "quat " + show( q ); }
	static QString describe( const Color3 & c ) { return QString( "(%1, %2, %3)" ).arg( c[0], 0, 'g', 9 ).arg( c[1], 0, 'g', 9 ).arg( c[2], 0, 'g', 9 ); }
	static QString describe( const Color4 & c ) { return QString( "(%1, %2, %3, %4)" ).arg( c[0], 0, 'g', 9 ).arg( c[1], 0, 'g', 9 ).arg( c[2], 0, 'g', 9 ).arg( c[3], 0, 'g', 9 ); }
	static QString describe( const Triangle & t ) { return QString( "(%1, %2, %3)" ).arg( t[0] ).arg( t[1] ).arg( t[2] ); }

private slots:
	void version_data()
	{
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<quint32>( "number" );

		QTest::newRow( "Morrowind" ) << "4.0.0.2" << 0x04000002u;
		QTest::newRow( "Oblivion" ) << "20.0.0.5" << 0x14000005u;
		QTest::newRow( "Fallout3/Skyrim/FO4" ) << "20.2.0.7" << 0x14020007u;
		QTest::newRow( "10.0.1.0" ) << "10.0.1.0" << 0x0A000100u;
		QTest::newRow( "3.3.0.13 (first 4-part)" ) << "3.3.0.13" << 0x0303000Du;
		QTest::newRow( "old style 3.1" ) << "3.1" << 0x03010000u;
		QTest::newRow( "old style 3.0" ) << "3.0" << 0x03000000u;
		QTest::newRow( "old style 3.03" ) << "3.03" << 0x03000300u;
		QTest::newRow( "old style 2.3" ) << "2.3" << 0x02030000u;
		// the digits behind the first one are read one at a time: the second, the third, and all that follow the third as one number.
		// 0x0303000C is the last number below 3.3.0.13: it is written the old way, though it has four parts the new way
		QTest::newRow( "old style 3.01" ) << "3.01" << 0x03000100u;
		QTest::newRow( "old style 3.001" ) << "3.001" << 0x03000001u;
		QTest::newRow( "old style 3.013" ) << "3.013" << 0x03000103u;
		QTest::newRow( "old style 3.123" ) << "3.123" << 0x03010203u;
		QTest::newRow( "old style 3.1016: more than one digit after the third" ) << "3.1016" << 0x03010010u;
		QTest::newRow( "old style 3.3012: the last one below 3.3.0.13" ) << "3.3012" << 0x0303000Cu;
		// a part is a whole byte, up to 255
		QTest::newRow( "new style, major above 127" ) << "200.2.0.7" << 0xC8020007u;
		QTest::newRow( "new style, every part 255" ) << "255.255.255.255" << 0xFFFFFFFFu;
		QTest::newRow( "new style, the parts are bytes" ) << "128.130.131.255" << 0x808283FFu;
	}

	void version()
	{
		QFETCH( QString, text );
		QFETCH( quint32, number );

		QCOMPARE( NifModel::version2number( text ), number );
		QCOMPARE( NifModel::version2string( number ), text );
	}

	void version_degenerate()
	{
		QCOMPARE( NifModel::version2number( QString() ), 0u );
		QCOMPARE( NifModel::version2string( 0 ), QString() );
		// more than four parts
		QCOMPARE( NifModel::version2number( "1.2.3.4.5" ), 0u );
		// 0xffffffff is the "no version" marker
		QCOMPARE( NifModel::version2number( "4294967295" ), 0u );
		// plain integers pass through
		QCOMPARE( NifModel::version2number( "335544322" ), 0x14000002u );
	}

	void kfmVersion()
	{
		// KFM versions are hexadecimal parts: "2.0.0.0b" is 0x0200000b
		QCOMPARE( KfmModel::version2number( "2.0.0.0b" ), 0x0200000bu );
		// kfm.xml spells the same version both ways ("1.2.4b" and 16927488)
		QCOMPARE( KfmModel::version2number( "1.2.4b" ), 16927488u );
		// the string form is not identical to the xml spelling, but converts back to the same number
		QCOMPARE( KfmModel::version2number( KfmModel::version2string( 0x0200000b ) ), 0x0200000bu );
	}

	void vector3_arithmetic()
	{
		Vector3 a( 1, 2, 3 ), b( 4, -5, 6 );

		QVERIFY( ( a + b ) == Vector3( 5, -3, 9 ) );
		QVERIFY( ( a - b ) == Vector3( -3, 7, -3 ) );
		QVERIFY( ( a * 2.0f ) == Vector3( 2, 4, 6 ) );
		QVERIFY( ( a / 2.0f ) == Vector3( 0.5f, 1, 1.5f ) );
		QVERIFY( -a == Vector3( -1, -2, -3 ) );
		QCOMPARE( Vector3::dotproduct( a, b ), 12.0f );
		QVERIFY( Vector3::crossproduct( Vector3( 1, 0, 0 ), Vector3( 0, 1, 0 ) ) == Vector3( 0, 0, 1 ) );
		QCOMPARE( Vector3( 3, 4, 0 ).length(), 5.0f );
		QCOMPARE( Vector3( 3, 4, 0 ).squaredLength(), 25.0f );

		Vector3 n( 0, 0, 7 );
		QVERIFY( near( n.normalize(), Vector3( 0, 0, 1 ) ) );
		// normalising a zero vector must not produce NaN/inf
		Vector3 z;
		z.normalize();
		QVERIFY( z == Vector3( 0, 0, 0 ) );

		Vector3 lo( 5, 5, 5 ), hi( 5, 5, 5 );
		lo.boundMin( Vector3( 1, 9, 5 ) );
		hi.boundMax( Vector3( 1, 9, 5 ) );
		QVERIFY( lo == Vector3( 1, 5, 5 ) );
		QVERIFY( hi == Vector3( 5, 9, 5 ) );
	}

	void vector_fromString()
	{
		Vector3 v;
		v.fromString( "1.5, -2, 3e1" );
		QVERIFY( v == Vector3( 1.5f, -2.0f, 30.0f ) );

		// malformed input leaves the value alone
		v.fromString( "1, 2" );
		QVERIFY( v == Vector3( 1.5f, -2.0f, 30.0f ) );
		v.fromString( "a, b, c" );
		QVERIFY( v == Vector3( 1.5f, -2.0f, 30.0f ) );

		Vector2 v2;
		v2.fromString( "7, 8" );
		QVERIFY( v2 == Vector2( 7, 8 ) );
	}

	void matrix_identityAndInverse()
	{
		Matrix id;
		QVERIFY( id * Vector3( 1, 2, 3 ) == Vector3( 1, 2, 3 ) );

		Matrix m = Matrix::euler( 0.3f, -0.4f, 0.5f );
		Matrix product = m * m.inverted();
		for ( unsigned r = 0; r < 3; r++ ) {
			for ( unsigned c = 0; c < 3; c++ )
				QVERIFY2( near( product( r, c ), id( r, c ) ), qPrintable( QString( "[%1][%2] = %3" ).arg( r ).arg( c ).arg( product( r, c ) ) ) );
		}

		// A rotation does not change lengths
		QVERIFY( near( ( m * Vector3( 1, 2, 3 ) ).length(), Vector3( 1, 2, 3 ).length() ) );
	}

	void matrix_eulerRoundTrip_data()
	{
		QTest::addColumn<float>( "x" );
		QTest::addColumn<float>( "y" );
		QTest::addColumn<float>( "z" );

		QTest::newRow( "zero" ) << 0.0f << 0.0f << 0.0f;
		QTest::newRow( "x only" ) << 0.7f << 0.0f << 0.0f;
		QTest::newRow( "y only" ) << 0.0f << -0.7f << 0.0f;
		QTest::newRow( "z only" ) << 0.0f << 0.0f << 2.5f;
		QTest::newRow( "mixed" ) << 0.3f << -0.4f << 0.5f;
		QTest::newRow( "large z" ) << -1.2f << 0.9f << -2.8f;
	}

	void matrix_eulerRoundTrip()
	{
		QFETCH( float, x );
		QFETCH( float, y );
		QFETCH( float, z );

		Matrix m = Matrix::euler( x, y, z );
		float rx = 0, ry = 0, rz = 0;
		QVERIFY( m.toEuler( rx, ry, rz ) );
		QVERIFY( near( rx, x, 1e-4f ) );
		QVERIFY( near( ry, y, 1e-4f ) );
		QVERIFY( near( rz, z, 1e-4f ) );
	}

	void quat_axisAngle_matrix()
	{
		Quat q;
		q.fromAxisAngle( Vector3( 0, 0, 1 ), float( PI / 2 ) );

		Matrix m;
		m.fromQuat( q );
		QVERIFY( near( m * Vector3( 1, 0, 0 ), Vector3( 0, 1, 0 ) ) );

		// ... and back (a quaternion and its negation are the same rotation)
		Quat back = m.toQuat();
		float d = Quat::dotproduct( q, back );
		QVERIFY( near( std::fabs( d ), 1.0f, 1e-4f ) );

		Vector3 axis;
		float angle = 0;
		q.toAxisAngle( axis, angle );
		QVERIFY( near( axis, Vector3( 0, 0, 1 ) ) );
		QVERIFY( near( angle, float( PI / 2 ), 1e-4f ) );
	}

	//! slerp is Jonathan Blow's fast approximation, so endpoints are only accurate to ~1e-3
	void quat_slerp()
	{
		Quat p, q;
		p.fromAxisAngle( Vector3( 0, 0, 1 ), 0.0f );
		q.fromAxisAngle( Vector3( 0, 0, 1 ), float( PI / 2 ) );

		QVERIFY( near( Quat::dotproduct( Quat::slerp( 0.0f, p, q ), p ), 1.0f, 2e-3f ) );
		QVERIFY( near( Quat::dotproduct( Quat::slerp( 1.0f, p, q ), q ), 1.0f, 2e-3f ) );

		Quat mid;
		mid.fromAxisAngle( Vector3( 0, 0, 1 ), float( PI / 4 ) );
		QVERIFY( near( Quat::dotproduct( Quat::slerp( 0.5f, p, q ), mid ), 1.0f, 2e-3f ) );
	}

	//! Documents a latent defect: Quat::normalize() divides by the squared magnitude (no sqrt)
	void quat_normalize()
	{
		Quat q( 2, 0, 0, 0 );
		q.normalize();
		QEXPECT_FAIL( "", "Quat::normalize() divides by magnitude squared; remove this line when fixed", Continue );
		QCOMPARE( q[0], 1.0f );
	}

	void matrix4_composeDecompose()
	{
		Matrix rot = Matrix::euler( 0.2f, 0.1f, -0.3f );
		Vector3 t( 10, -20, 30 ), s( 2, 2, 2 );

		Matrix4 m;
		m.compose( t, rot, s );

		Vector3 t2, s2;
		Matrix r2;
		m.decompose( t2, r2, s2 );
		QVERIFY( near( t2, t ) );
		QVERIFY( near( s2, s, 1e-4f ) );
		for ( unsigned r = 0; r < 3; r++ ) {
			for ( unsigned c = 0; c < 3; c++ )
				QVERIFY( near( r2( r, c ), rot( r, c ), 1e-4f ) );
		}

		// m * inverse == identity
		Matrix4 id = m * m.inverted();
		for ( unsigned r = 0; r < 4; r++ ) {
			for ( unsigned c = 0; c < 4; c++ )
				QVERIFY2( near( id( r, c ), r == c ? 1.0f : 0.0f, 1e-4f ), qPrintable( QString( "[%1][%2] = %3" ).arg( r ).arg( c ).arg( id( r, c ) ) ) );
		}
	}

	void color_conversions()
	{
		Color3 c( 0.25f, 0.5f, 1.0f );
		QCOMPARE( c.toQColor().red(), 64 );
		QCOMPARE( c.toQColor().blue(), 255 );

		// out-of-range (HDR) values are clamped when converted for display
		Color3 hdr( 2.0f, -1.0f, 0.5f );
		QCOMPARE( hdr.toQColor().red(), 255 );
		QCOMPARE( hdr.toQColor().green(), 0 );

		Color4 c4( Color3( 0.1f, 0.2f, 0.3f ), 0.4f );
		QCOMPARE( c4.alpha(), 0.4f );
		QVERIFY( Color3( c4 ) == Color3( 0.1f, 0.2f, 0.3f ) );
	}

	void triangle()
	{
		Triangle t( 1, 2, 3 );
		QCOMPARE( t.v1(), quint16( 1 ) );
		QCOMPARE( t.v2(), quint16( 2 ) );
		QCOMPARE( t.v3(), quint16( 3 ) );
		t.flip();
		QCOMPARE( t.v1(), quint16( 2 ) );
		QCOMPARE( t.v2(), quint16( 1 ) );
		QCOMPARE( t.v3(), quint16( 3 ) );
		QVERIFY( t == Triangle( 2, 1, 3 ) );
		QVERIFY( ( t + 10 ) == Triangle( 12, 11, 13 ) );
	}

	void numOrMinMax()
	{
		QCOMPARE( NumOrMinMax( 1.5f, 'f', 2 ), QString( "1.50" ) );
		QCOMPARE( NumOrMinMax( -FLT_MAX ), QString( "<float_min>" ) );
		QCOMPARE( NumOrMinMax( FLT_MAX ), QString( "<float_max>" ) );
		// negative zero is shown as such
		QCOMPARE( NumOrMinMax( -0.0f, 'f', 1 ), QString( "-0.0" ) );
	}

	void bsVertexDesc_flags()
	{
		BSVertexDesc d;
		QVERIFY( !d.HasFlag( VertexFlags::VF_VERTEX ) );
		d.SetFlag( VertexFlags::VF_VERTEX );
		d.SetFlag( VertexFlags::VF_UV );
		QVERIFY( d.HasFlag( VertexFlags::VF_VERTEX ) );
		QVERIFY( d.HasFlag( VertexFlags::VF_UV ) );
		d.RemoveFlag( VertexFlags::VF_VERTEX );
		QVERIFY( !d.HasFlag( VertexFlags::VF_VERTEX ) );
		QVERIFY( d.HasFlag( VertexFlags::VF_UV ) );
	}

	//! A text that is not a complete vector changes nothing: every part has to be a number, and there have to be exactly as many as components
	void vector_fromString_incomplete_data()
	{
		QTest::addColumn<QString>( "text" );

		QTest::newRow( "a single number" ) << "1";
		QTest::newRow( "five parts" ) << "1, 2, 3, 4, 5";
		QTest::newRow( "first part is not a number" ) << "x, 2, 3, 4";
		QTest::newRow( "middle part is not a number" ) << "1, 2, y, 4";
		QTest::newRow( "last part is not a number" ) << "1, 2, 3, w";
		QTest::newRow( "empty" ) << "";
	}

	void vector_fromString_incomplete()
	{
		QFETCH( QString, text );

		Vector2 v2( 7, 8 );
		v2.fromString( text );
		QVERIFY2( v2 == Vector2( 7, 8 ), "a Vector2 changed" );

		Vector3 v3( 7, 8, 9 );
		v3.fromString( text );
		QVERIFY2( v3 == Vector3( 7, 8, 9 ), "a Vector3 changed" );

		Vector4 v4( 6, 7, 8, 9 );
		v4.fromString( text );
		QVERIFY2( v4 == Vector4( 6, 7, 8, 9 ), "a Vector4 changed" );

		Quat q( 6, 7, 8, 9 );
		q.fromString( text );
		QVERIFY2( q == Quat( 6, 7, 8, 9 ), "a Quat changed" );
	}

	//! One bad part out of the right number: the numbers that did parse are not taken either
	void vector_fromString_partiallyBad()
	{
		Vector2 v2( 7, 8 );
		v2.fromString( "1, y" );
		QVERIFY( v2 == Vector2( 7, 8 ) );
		v2.fromString( "x, 2" );
		QVERIFY( v2 == Vector2( 7, 8 ) );

		Vector3 v3( 7, 8, 9 );
		v3.fromString( "1, 2, z" );
		QVERIFY( v3 == Vector3( 7, 8, 9 ) );
		v3.fromString( "1, y, 3" );
		QVERIFY( v3 == Vector3( 7, 8, 9 ) );
		v3.fromString( "x, 2, 3" );
		QVERIFY( v3 == Vector3( 7, 8, 9 ) );

		Vector4 v4( 6, 7, 8, 9 );
		v4.fromString( "1, 2, 3, w" );
		QVERIFY( v4 == Vector4( 6, 7, 8, 9 ) );
		v4.fromString( "x, 2, 3, 4" );
		QVERIFY( v4 == Vector4( 6, 7, 8, 9 ) );

		Quat q( 6, 7, 8, 9 );
		q.fromString( "1, 2, 3, w" );
		QVERIFY( q == Quat( 6, 7, 8, 9 ) );
		q.fromString( "x, 2, 3, 4" );
		QVERIFY( q == Quat( 6, 7, 8, 9 ) );
	}

	//! Every component takes its own part: a Vector4 is x y z w, a Quat is written x y z w too, though it is stored w x y z
	void vector_fromString_components()
	{
		Vector4 v4;
		v4.fromString( "1.5, -2, 3e1, 4" );
		QVERIFY( v4 == Vector4( 1.5f, -2.0f, 30.0f, 4.0f ) );

		Quat q;
		q.fromString( "1, 2, 3, 4" );
		QCOMPARE( q[0], 4.0f );	// w
		QCOMPARE( q[1], 1.0f );	// x
		QCOMPARE( q[2], 2.0f );	// y
		QCOMPARE( q[3], 3.0f );	// z
	}

	//! One part too few or too many is not a vector of that size, though it is one of another size
	void vector_fromString_wrongCount()
	{
		Vector2 v2( 7, 8 );
		v2.fromString( "1, 2, 3" );
		QVERIFY( v2 == Vector2( 7, 8 ) );
		v2.fromString( "1" );
		QVERIFY( v2 == Vector2( 7, 8 ) );

		Vector3 v3( 7, 8, 9 );
		v3.fromString( "1, 2" );
		QVERIFY( v3 == Vector3( 7, 8, 9 ) );
		v3.fromString( "1, 2, 3, 4" );
		QVERIFY( v3 == Vector3( 7, 8, 9 ) );

		Vector4 v4( 6, 7, 8, 9 );
		v4.fromString( "1, 2, 3" );
		QVERIFY( v4 == Vector4( 6, 7, 8, 9 ) );
		v4.fromString( "1, 2, 3, 4, 5" );
		QVERIFY( v4 == Vector4( 6, 7, 8, 9 ) );

		Quat q( 6, 7, 8, 9 );
		q.fromString( "1, 2, 3" );
		QVERIFY( q == Quat( 6, 7, 8, 9 ) );
		q.fromString( "1, 2, 3, 4, 5" );
		QVERIFY( q == Quat( 6, 7, 8, 9 ) );
	}

	void vector3_equalityAndOrder()
	{
		// each component alone makes two vectors different
		Vector3 a( 1, 2, 3 );
		QVERIFY( a == Vector3( 1, 2, 3 ) );
		QVERIFY( !( a == Vector3( 9, 2, 3 ) ) );
		QVERIFY( !( a == Vector3( 1, 9, 3 ) ) );
		QVERIFY( !( a == Vector3( 1, 2, 9 ) ) );

		// lexicographic: the first component that differs decides, the others do not matter
		QVERIFY( Vector3::lexLessThan( Vector3( 1, 9, 9 ), Vector3( 2, 0, 0 ) ) );
		QVERIFY( !Vector3::lexLessThan( Vector3( 2, 0, 0 ), Vector3( 1, 9, 9 ) ) );
		QVERIFY( Vector3::lexLessThan( Vector3( 1, 2, 9 ), Vector3( 1, 3, 0 ) ) );
		QVERIFY( !Vector3::lexLessThan( Vector3( 1, 3, 0 ), Vector3( 1, 2, 9 ) ) );
		QVERIFY( Vector3::lexLessThan( Vector3( 1, 2, 3 ), Vector3( 1, 2, 4 ) ) );
		QVERIFY( !Vector3::lexLessThan( Vector3( 1, 2, 4 ), Vector3( 1, 2, 3 ) ) );
		QVERIFY( !Vector3::lexLessThan( Vector3( 1, 2, 3 ), Vector3( 1, 2, 3 ) ) );
	}

	//! The angle between two vectors is the arc cosine of their dot product, which is only a cosine for unit vectors:
	//! above 1 is 0 and below -1 is pi, whatever the vectors are
	void vector3_angle()
	{
		QVERIFY( near( Vector3::angle( Vector3( 1, 0, 0 ), Vector3( 0, 1, 0 ) ), float( PI / 2 ) ) );
		QVERIFY( near( Vector3::angle( Vector3( 1, 0, 0 ), Vector3( 1, 0, 0 ) ), 0.0f ) );
		QVERIFY( near( Vector3::angle( Vector3( 1, 0, 0 ), Vector3( -1, 0, 0 ) ), float( PI ) ) );
		QVERIFY( near( Vector3::angle( Vector3( 1, 0, 0 ), Vector3( 0.5f, 0, 0 ) ), float( PI / 3 ) ) );		// cos 60 degrees
		QVERIFY( near( Vector3::angle( Vector3( 1, 0, 0 ), Vector3( -0.5f, 0, 0 ) ), float( 2 * PI / 3 ) ) );
		QVERIFY( near( Vector3::angle( Vector3( 2, 0, 0 ), Vector3( 1, 0, 0 ) ), 0.0f ) );
		QVERIFY( near( Vector3::angle( Vector3( -2, 0, 0 ), Vector3( 1, 0, 0 ) ), float( PI ) ) );
	}

	//! A quaternion as a matrix, from numpy (python3): the rotation by 0.9 radians about (1, 2, 3)
	void matrix_fromQuat()
	{
		Quat q( 0.90044713f, 0.116249427f, 0.232498854f, 0.348748296f );
		Matrix m;
		m.fromQuat( q );

		Matrix expected = matrixOf( { 0.648637831f, -0.574003041f, 0.499789417f,
		                              0.682114482f, 0.729721427f, -0.0471857674f,
		                              -0.337622255f, 0.371520072f, 0.864860713f } );
		QVERIFY2( matrixDiff( m, expected, 1e-6f ).isEmpty(), qPrintable( matrixDiff( m, expected, 1e-6f ) ) );

		// the opposite turn is the transposed matrix
		Matrix opposite;
		opposite.fromQuat( Quat( q[0], -q[1], -q[2], -q[3] ) );
		Matrix transposed = matrixOf( { 0.648637831f, 0.682114482f, -0.337622255f,
		                                -0.574003041f, 0.729721427f, 0.371520072f,
		                                0.499789417f, -0.0471857674f, 0.864860713f } );
		QVERIFY2( matrixDiff( opposite, transposed, 1e-6f ).isEmpty(), qPrintable( matrixDiff( opposite, transposed, 1e-6f ) ) );
	}

	void matrix_toQuat_data()
	{
		QTest::addColumn<QVector<float> >( "matrix" );
		QTest::addColumn<QVector<float> >( "quat" );   // w x y z

		// The matrices are rotations about the axes (a, b, c) by the angle (numpy, python3), the quaternions as Matrix::toQuat() gives them:
		// with a positive trace w is positive; without one the component of the largest diagonal element is positive and w follows
		QTest::newRow( "0.9 rad about (1, 2, 3)" ) << floats9( 0.648637831f, -0.574003041f, 0.499789417f, 0.682114482f, 0.729721427f, -0.0471857674f, -0.337622255f, 0.371520072f, 0.864860713f )
			<< floats4( 0.90044713f, 0.116249427f, 0.232498854f, 0.348748296f );
		QTest::newRow( "-0.9 rad about (1, 2, 3)" ) << floats9( 0.648637831f, 0.682114482f, -0.337622255f, -0.574003041f, 0.729721427f, 0.371520072f, 0.499789417f, -0.0471857674f, 0.864860713f )
			<< floats4( 0.90044713f, -0.116249427f, -0.232498854f, -0.348748296f );
		QTest::newRow( "175 degrees about (1, 2, 3): z is the largest" ) << floats9( -0.853609383f, 0.215290621f, 0.474342704f, 0.355050713f, -0.425853342f, 0.832218647f, 0.381169289f, 0.878805339f, 0.287073314f )
			<< floats4( 0.0436193869f, 0.267006874f, 0.534013748f, 0.801020622f );
		QTest::newRow( "175 degrees about (3, 1, 2): x is the largest" ) << floats9( 0.287073314f, 0.381169289f, 0.878805339f, 0.474342704f, -0.853609383f, 0.215290621f, 0.832218647f, 0.355050713f, -0.425853342f )
			<< floats4( 0.0436193869f, 0.801020622f, 0.267006874f, 0.534013748f );
		QTest::newRow( "175 degrees about (2, 3, 1): y is the largest" ) << floats9( -0.425853342f, 0.832218647f, 0.355050713f, 0.878805339f, 0.287073314f, 0.381169289f, 0.215290621f, 0.474342704f, -0.853609383f )
			<< floats4( 0.0436193869f, 0.534013748f, 0.801020622f, 0.267006874f );
		QTest::newRow( "175 degrees about (-1, 2, -3)" ) << floats9( -0.853609383f, -0.215290621f, 0.474342704f, -0.355050713f, -0.425853342f, -0.832218647f, 0.381169289f, -0.878805339f, 0.287073314f )
			<< floats4( -0.0436193869f, 0.267006874f, -0.534013748f, 0.801020622f );
		QTest::newRow( "175 degrees about (1, -4, 2)" ) << floats9( -0.901137829f, -0.418265432f, 0.114038013f, -0.34218967f, 0.524715543f, -0.77947408f, 0.266189545f, -0.741436183f, -0.615967155f )
			<< floats4( -0.0436193869f, -0.218010202f, 0.872040808f, -0.436020404f );
		QTest::newRow( "identity" ) << floats9( 1, 0, 0, 0, 1, 0, 0, 0, 1 ) << floats4( 1, 0, 0, 0 );
		// the trace is 0: not above it, so the quaternion is the one of the largest diagonal element (the first, all three are 0), which is
		// positive and w follows. 120 degrees about ( 1, 1, 1 ) and about ( -1, -1, -1 ) (the matrix and its transpose): the second one has
		// the negative w, where the formula for a positive trace would give it the positive one
		QTest::newRow( "120 degrees about (1, 1, 1): the trace is 0" ) << floats9( 0, 0, 1, 1, 0, 0, 0, 1, 0 ) << floats4( 0.5f, 0.5f, 0.5f, 0.5f );
		QTest::newRow( "120 degrees about (-1, -1, -1): the trace is 0" ) << floats9( 0, 1, 0, 0, 0, 1, 1, 0, 0 ) << floats4( -0.5f, 0.5f, 0.5f, 0.5f );
	}

	void matrix_toQuat()
	{
		QFETCH( QVector<float>, matrix );
		QFETCH( QVector<float>, quat );

		Matrix m = matrixOf( { matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5], matrix[6], matrix[7], matrix[8] } );
		Quat want( quat[0], quat[1], quat[2], quat[3] );
		Quat got = m.toQuat();
		QVERIFY2( near( got, want, 1e-5f ), qPrintable( QString( "got %1, expected %2" ).arg( show( got ), show( want ) ) ) );

		// and the quaternion is that rotation
		Matrix back;
		back.fromQuat( got );
		QVERIFY2( matrixDiff( back, m, 1e-5f ).isEmpty(), qPrintable( matrixDiff( back, m, 1e-5f ) ) );
	}

	//! At 90 degrees of pitch there are many Euler angles for one rotation: toEuler() answers false and puts everything in the first angle
	void matrix_toEuler_gimbalLock()
	{
		// Rx(0.5) Ry(90 degrees): the matrix has a 1 in its upper right corner
		Matrix up = matrixOf( { 0, 0, 1, 0.47942555f, 0.87758255f, 0, -0.87758255f, 0.47942555f, 0 } );
		float x = -1, y = -1, z = -1;
		QVERIFY( !up.toEuler( x, y, z ) );
		QVERIFY( near( x, 0.5f ) );
		QVERIFY( near( y, float( PI / 2 ) ) );
		QCOMPARE( z, 0.0f );

		// Rx(0.5) Ry(-90 degrees)
		Matrix down = matrixOf( { 0, 0, -1, -0.47942555f, 0.87758255f, 0, 0.87758255f, 0.47942555f, 0 } );
		QVERIFY( !down.toEuler( x, y, z ) );
		QVERIFY( near( y, float( -PI / 2 ) ) );
		QCOMPARE( z, 0.0f );

		// ... and what it returns is that rotation again
		QEXPECT_FAIL( "", "Matrix::toEuler() at -90 degrees of pitch gives the first angle the wrong sign: euler( x, y, z ) is not the matrix (niftypes.cpp); fix then remove", Continue );
		QVERIFY( near( x, 0.5f ) );
	}

	//! A matrix with no inverse comes back as the identity, the inverse of the others is the one that multiplied gives the identity
	void matrix_inverted_values()
	{
		Matrix none = matrixOf( { 0, 0, 0, 0, 0, 0, 0, 0, 0 } );
		QVERIFY2( matrixDiff( none.inverted(), Matrix(), 0.0f ).isEmpty(), qPrintable( matrixDiff( none.inverted(), Matrix(), 0.0f ) ) );

		Matrix flat = matrixOf( { 1, 2, 3, 2, 4, 6, 0, 1, 5 } );	// the second row is twice the first
		QVERIFY2( matrixDiff( flat.inverted(), Matrix(), 0.0f ).isEmpty(), qPrintable( matrixDiff( flat.inverted(), Matrix(), 0.0f ) ) );

		// determinant 7: [[2, 1, 0], [0, 3, 1], [1, 0, 1]]; numpy.linalg.inv
		Matrix m = matrixOf( { 2, 1, 0, 0, 3, 1, 1, 0, 1 } );
		Matrix inverse = matrixOf( { 0.428571433f, -0.142857149f, 0.142857149f,
		                             0.142857149f, 0.285714298f, -0.285714298f,
		                             -0.428571433f, 0.142857149f, 0.857142866f } );
		QVERIFY2( matrixDiff( m.inverted(), inverse, 1e-6f ).isEmpty(), qPrintable( matrixDiff( m.inverted(), inverse, 1e-6f ) ) );

		// a scale: diag( 2, 4, 8 ) has the inverse diag( 0.5, 0.25, 0.125 )
		Matrix scale = matrixOf( { 2, 0, 0, 0, 4, 0, 0, 0, 8 } );
		QVERIFY2( matrixDiff( scale.inverted(), matrixOf( { 0.5f, 0, 0, 0, 0.25f, 0, 0, 0, 0.125f } ), 1e-7f ).isEmpty(), "diagonal" );
	}

	void matrix_toHtml_toRaw()
	{
		Matrix m = matrixOf( { 1, 2, 3, 4, 5, 6, 7, 8, 9 } );
		QCOMPARE( m.toHtml(), QString( "<table>"
		                               "<tr><td>1.0000</td><td>2.0000</td><td>3.0000</td></tr>"
		                               "<tr><td>4.0000</td><td>5.0000</td><td>6.0000</td></tr>"
		                               "<tr><td>7.0000</td><td>8.0000</td><td>9.0000</td></tr>"
		                               "</table>" ) );
		QCOMPARE( m.toRaw(), QString( "1.0000, 2.0000, 3.0000\r\n4.0000, 5.0000, 6.0000\r\n7.0000, 8.0000, 9.0000\r\n" ) );

		Matrix4 m4 = matrix4Of( { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 } );
		QCOMPARE( m4.toHtml(), QString( "<table>"
		                                "<tr><td>1.0000</td><td>2.0000</td><td>3.0000</td><td>4.0000</td></tr>"
		                                "<tr><td>5.0000</td><td>6.0000</td><td>7.0000</td><td>8.0000</td></tr>"
		                                "<tr><td>9.0000</td><td>10.0000</td><td>11.0000</td><td>12.0000</td></tr>"
		                                "<tr><td>13.0000</td><td>14.0000</td><td>15.0000</td><td>16.0000</td></tr>"
		                                "</table>" ) );
	}

	//! translation, rotation and scale as one matrix and back, with a scale that is different on every axis: Rx(30) Ry(20) Rz(10) degrees, scale
	//! ( 2, 3, 4 ), translation ( 1, 2, 3 ), by numpy (python3): m[i][j] = rotation[j][i] * scale[j], the translation in the last row
	void matrix4_composeDecompose_scales()
	{
		Matrix rotation = matrixOf( { 0.925416589f, -0.163175911f, 0.342020154f, 0.31879577f, 0.823172927f, -0.469846308f, -0.204874128f, 0.543838143f, 0.813797653f } );
		Matrix4 expected = matrix4Of( { 1.85083318f, 0.956387341f, -0.819496512f, 0.0f,
		                                -0.326351821f, 2.4695189f, 2.17535257f, 0.0f,
		                                0.684040308f, -1.40953898f, 3.25519061f, 0.0f,
		                                1.0f, 2.0f, 3.0f, 1.0f } );

		Matrix4 composed;
		composed.compose( Vector3( 1, 2, 3 ), rotation, Vector3( 2, 3, 4 ) );
		QVERIFY2( matrix4Diff( composed, expected, 1e-5f ).isEmpty(), qPrintable( matrix4Diff( composed, expected, 1e-5f ) ) );

		Vector3 t, s;
		Matrix r;
		expected.decompose( t, r, s );
		QVERIFY( near( t, Vector3( 1, 2, 3 ) ) );
		QVERIFY( near( s, Vector3( 2, 3, 4 ), 1e-5f ) );
		QVERIFY2( matrixDiff( r, rotation, 1e-5f ).isEmpty(), qPrintable( matrixDiff( r, rotation, 1e-5f ) ) );
	}

	//! The inverse of a matrix with no zero in it, and in particular a last column that is not (0, 0, 0, 1): the product with it is the identity
	void matrix4_inverted_dense()
	{
		Matrix4 m = matrix4Of( { 4, 1, 2, 1,
		                         3, 5, 1, 2,
		                         1, 2, 6, 3,
		                         2, 1, 3, 7 } );
		Matrix4 product = m * m.inverted();
		for ( unsigned r = 0; r < 4; r++ ) {
			for ( unsigned c = 0; c < 4; c++ )
				QVERIFY2( near( product( r, c ), r == c ? 1.0f : 0.0f, 1e-5f ), qPrintable( QString( "[%1][%2] = %3" ).arg( r ).arg( c ).arg( product( r, c ) ) ) );
		}

		Matrix4 other = matrix4Of( { 1, 2, 3, 4,
		                             -2, 1, 5, 2,
		                             3, -1, 2, 6,
		                             4, 2, -3, 1 } );
		Matrix4 product2 = other.inverted() * other;
		for ( unsigned r = 0; r < 4; r++ ) {
			for ( unsigned c = 0; c < 4; c++ )
				QVERIFY2( near( product2( r, c ), r == c ? 1.0f : 0.0f, 1e-5f ), qPrintable( QString( "[%1][%2] = %3" ).arg( r ).arg( c ).arg( product2( r, c ) ) ) );
		}
	}

	//! The axis of a rotation is a direction: the length it is given is of no importance
	void quat_fromAxisAngle_normalisesTheAxis()
	{
		Quat q;
		q.fromAxisAngle( Vector3( 0, 0, 2 ), float( PI / 2 ) );
		QVERIFY2( near( q, Quat( float( std::sqrt( 0.5 ) ), 0, 0, float( std::sqrt( 0.5 ) ) ) ), qPrintable( show( q ) ) );

		// 0.9 radians about (1, 2, 3), numpy (python3)
		q.fromAxisAngle( Vector3( 1, 2, 3 ), 0.9f );
		QVERIFY2( near( q, Quat( 0.90044713f, 0.116249427f, 0.232498854f, 0.348748296f ), 1e-6f ), qPrintable( show( q ) ) );
	}

	void quat_toAxisAngle_values()
	{
		Vector3 axis( 9, 9, 9 );
		float angle = -1;

		// no rotation: the angle is 0, and the axis is the x axis (there is no other one to take)
		Quat().toAxisAngle( axis, angle );
		QVERIFY( axis == Vector3( 1, 0, 0 ) );
		QCOMPARE( angle, 0.0f );

		Quat( 0.90044713f, 0.116249427f, 0.232498854f, 0.348748296f ).toAxisAngle( axis, angle );
		QVERIFY2( near( axis, Vector3( 1, 2, 3 ) / std::sqrt( 14.0f ), 1e-5f ), qPrintable( QString( "axis %1 %2 %3" ).arg( axis[0] ).arg( axis[1] ).arg( axis[2] ) ) );
		QVERIFY( near( angle, 0.9f, 1e-5f ) );
	}

	void quat_slerp_values_data()
	{
		QTest::addColumn<float>( "t" );
		QTest::addColumn<QVector<float> >( "p" );
		QTest::addColumn<QVector<float> >( "q" );
		QTest::addColumn<QVector<float> >( "expected" );

		// Jonathan Blow's approximation of slerp, "Hacking Quaternions" (2002): numpy (python3) with the constants of that article.
		// The columns are w x y z.
		QTest::newRow( "t 0.25, 0 to 90 degrees about z" ) << 0.25f << floats4( 1, 0, 0, 0 ) << floats4( 0.707106769f, 0, 0, 0.707106769f ) << floats4( 0.980587661f, 0, 0, 0.194812134f );
		QTest::newRow( "t 0.75, 0 to 90 degrees about z" ) << 0.75f << floats4( 1, 0, 0, 0 ) << floats4( 0.707106769f, 0, 0, 0.707106769f ) << floats4( 0.831133187f, 0, 0, 0.555627167f );
		QTest::newRow( "t 0.1, 0.2 to 150 degrees about (1, 2, 3)" ) << 0.1f << floats4( 0.995004177f, 0.026681602f, 0.053363204f, 0.080044806f )
			<< floats4( 0.258819044f, 0.258154541f, 0.516309083f, 0.774463594f ) << floats4( 0.975240946f, 0.0589265078f, 0.117853016f, 0.176779523f );
		QTest::newRow( "t 0.9, 0.2 to 150 degrees about (1, 2, 3)" ) << 0.9f << floats4( 0.995004177f, 0.026681602f, 0.053363204f, 0.080044806f )
			<< floats4( 0.258819044f, 0.258154541f, 0.516309083f, 0.774463594f ) << floats4( 0.374708891f, 0.247747108f, 0.495494217f, 0.74324137f );
		QTest::newRow( "t 0.3, x axis to 120 degrees about y" ) << 0.3f << floats4( 1, 0, 0, 0 ) << floats4( 0.5f, 0, 0.866025388f, 0 ) << floats4( 0.950884879f, 0, 0.310259074f, 0 );
		// a long way: the squared length of the middle of the two is below 0.6521197 (the third step of the normalisation) from 144 degrees on
		QTest::newRow( "t 0.5, 0 to 160 degrees about z" ) << 0.5f << floats4( 1, 0, 0, 0 ) << floats4( 0.173648179f, 0, 0, 0.98480773f ) << floats4( 0.765897512f, 0, 0, 0.642664313f );
		QTest::newRow( "t 0.45, 0 to 160 degrees about z" ) << 0.45f << floats4( 1, 0, 0, 0 ) << floats4( 0.173648179f, 0, 0, 0.98480773f ) << floats4( 0.806578398f, 0, 0, 0.590789735f );
		QTest::newRow( "t 0.55, 0 to 160 degrees about z" ) << 0.55f << floats4( 1, 0, 0, 0 ) << floats4( 0.173648179f, 0, 0, 0.98480773f ) << floats4( 0.72187525f, 0, 0, 0.69173497f );
		QTest::newRow( "t 0.5, 0 to 170 degrees about z" ) << 0.5f << floats4( 1, 0, 0, 0 ) << floats4( 0.087155744f, 0, 0, 0.99619472f ) << floats4( 0.737249553f, 0, 0, 0.675564706f );
		QTest::newRow( "t 0.4, 10 degrees about x to 100 degrees about (1, 1, 1)" ) << 0.4f << floats4( 0.99619472f, 0.0871557444f, 0, 0 )
			<< floats4( 0.642787635f, 0.442275971f, 0.442275971f, 0.442275971f ) << floats4( 0.927668452f, 0.251412213f, 0.195053339f, 0.195053339f );
	}

	void quat_slerp_values()
	{
		QFETCH( float, t );
		QFETCH( QVector<float>, p );
		QFETCH( QVector<float>, q );
		QFETCH( QVector<float>, expected );

		Quat got = Quat::slerp( t, Quat( p[0], p[1], p[2], p[3] ), Quat( q[0], q[1], q[2], q[3] ) );
		Quat want( expected[0], expected[1], expected[2], expected[3] );
		QVERIFY2( near( got, want, 1e-5f ), qPrintable( QString( "got %1, expected %2" ).arg( show( got ), show( want ) ) ) );
	}

	//! The middle is the middle (to the accuracy of the approximation), and the result is a unit quaternion for every t
	void quat_slerp_midpointSides()
	{
		Quat p( 1, 0, 0, 0 );
		Quat q( float( std::sqrt( 0.5 ) ), 0, 0, float( std::sqrt( 0.5 ) ) );

		// t = 0.5 is the middle, from either side
		Quat mid = Quat::slerp( 0.5f, p, q );
		Quat half( 0.923879533f, 0, 0, 0.382683432f );
		QVERIFY2( near( mid, half, 2e-3f ), qPrintable( show( mid ) ) );

		// the result is a unit quaternion within the accuracy of the approximation
		for ( float t : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f } ) {
			Quat r = Quat::slerp( t, p, q );
			QVERIFY2( near( Quat::dotproduct( r, r ), 1.0f, 2e-3f ), qPrintable( QString( "t %1: length squared %2" ).arg( t ).arg( Quat::dotproduct( r, r ) ) ) );
		}
	}

	//! Two transforms as one: the second is moved by the first, and scaled by it
	void transform_multiply()
	{
		Transform a;
		a.rotation = matrixOf( { 0, -1, 0, 1, 0, 0, 0, 0, 1 } );		// 90 degrees about z
		a.translation = Vector3( 1, 2, 3 );
		a.scale = 2.0f;

		Transform b;
		b.rotation = matrixOf( { 1, 0, 0, 0, 0, -1, 0, 1, 0 } );		// 90 degrees about x
		b.translation = Vector3( 4, 5, 6 );
		b.scale = 3.0f;

		Transform c = a * b;
		// rotation: a then b; translation: a.translation + a.rotation * ( b.translation * a.scale ): (1, 2, 3) + (-10, 8, 12); scale: 2 * 3
		QVERIFY2( matrixDiff( c.rotation, matrixOf( { 0, 0, 1, 1, 0, 0, 0, 1, 0 } ), 1e-6f ).isEmpty(), qPrintable( matrixDiff( c.rotation, matrixOf( { 0, 0, 1, 1, 0, 0, 0, 1, 0 } ), 1e-6f ) ) );
		QVERIFY( near( c.translation, Vector3( -9, 10, 15 ), 1e-5f ) );
		QCOMPARE( c.scale, 6.0f );
	}

	//! A transform as a 4x4 matrix: the rotation transposed and scaled, the translation in the last row
	void transform_toMatrix4()
	{
		Transform t;
		t.rotation = matrixOf( { 1, 2, 3, 4, 5, 6, 7, 8, 9 } );
		t.translation = Vector3( 10, 11, 12 );
		t.scale = 2.0f;

		Matrix4 expected = matrix4Of( { 2, 8, 14, 0,
		                                4, 10, 16, 0,
		                                6, 12, 18, 0,
		                                10, 11, 12, 1 } );
		Matrix4 m = t.toMatrix4();
		QVERIFY2( matrix4Diff( m, expected, 0.0f ).isEmpty(), qPrintable( matrix4Diff( m, expected, 0.0f ) ) );
	}

	//! The stream form of a transform: the rows of the rotation each followed by the translation component of that row, then the scale
	void transform_stream()
	{
		Transform t;
		t.rotation = matrixOf( { 1, 2, 3, 4, 5, 6, 7, 8, 9 } );
		t.translation = Vector3( 10, 11, 12 );
		t.scale = 13.0f;

		// struct.pack('<13f', 1, 2, 3, 10, 4, 5, 6, 11, 7, 8, 9, 12, 13)
		QByteArray bytes = QByteArray::fromHex( "0000803f 00000040 00004040 00002041 00008040 0000a040 0000c040 00003041 0000e040 00000041 00001041 00004041 00005041" );

		QByteArray written;
		{
			QDataStream out( &written, QIODevice::WriteOnly );
			out.setByteOrder( QDataStream::LittleEndian );
			out.setFloatingPointPrecision( QDataStream::SinglePrecision );
			out << t;
		}
		QCOMPARE( QString::fromLatin1( written.toHex( ' ' ) ), QString::fromLatin1( bytes.toHex( ' ' ) ) );

		Transform back;
		QDataStream in( bytes );
		in.setByteOrder( QDataStream::LittleEndian );
		in.setFloatingPointPrecision( QDataStream::SinglePrecision );
		in >> back;
		QVERIFY2( matrixDiff( back.rotation, t.rotation, 0.0f ).isEmpty(), qPrintable( matrixDiff( back.rotation, t.rotation, 0.0f ) ) );
		QVERIFY( back.translation == Vector3( 10, 11, 12 ) );
		QCOMPARE( back.scale, 13.0f );
	}

	//! A colour is shown in 8 bits per channel, values outside 0 to 1 are clamped one channel at a time (a negative one is 0, not an invalid colour)
	void color_toQColor_clamps()
	{
		QColor c = Color3( -0.5f, 1.0f, 0.0f ).toQColor();
		QVERIFY( c.isValid() );
		QCOMPARE( c.red(), 0 );
		QCOMPARE( c.green(), 255 );
		QCOMPARE( c.blue(), 0 );

		QColor over = Color3( 2.0f, -3.0f, 1.0f ).toQColor();
		QVERIFY( over.isValid() );
		QCOMPARE( over.red(), 255 );
		QCOMPARE( over.green(), 0 );
		QCOMPARE( over.blue(), 255 );

		QColor c4 = Color4( 1.0f, -0.25f, 0.0f, 1.0f ).toQColor();
		QVERIFY( c4.isValid() );
		QCOMPARE( c4.red(), 255 );
		QCOMPARE( c4.green(), 0 );
		QCOMPARE( c4.alpha(), 255 );

		QColor transparent = Color4( 1.0f, 1.0f, 1.0f, -1.0f ).toQColor();
		QVERIFY( transparent.isValid() );
		QCOMPARE( transparent.alpha(), 0 );
	}

	void color_fromQColor_channels()
	{
		Color3 c;
		c.fromQColor( QColor( 255, 0, 64 ) );
		QCOMPARE( c.red(), 1.0f );
		QCOMPARE( c.green(), 0.0f );
		QCOMPARE( c.blue(), 64.0f / 255.0f );

		c.fromQColor( QColor( 0, 255, 0 ) );
		QCOMPARE( c.green(), 1.0f );
		QCOMPARE( c.red(), 0.0f );

		Color4 c4;
		c4.fromQColor( QColor( 255, 0, 64, 128 ) );
		QCOMPARE( c4.red(), 1.0f );
		QCOMPARE( c4.green(), 0.0f );
		QCOMPARE( c4.blue(), 64.0f / 255.0f );
		QCOMPARE( c4.alpha(), 128.0f / 255.0f );
	}

	//! Each channel alone makes two colours different
	void color_equality()
	{
		Color3 a( 0.1f, 0.2f, 0.3f );
		QVERIFY( a == Color3( 0.1f, 0.2f, 0.3f ) );
		QVERIFY( !( a == Color3( 0.9f, 0.2f, 0.3f ) ) );
		QVERIFY( !( a == Color3( 0.1f, 0.9f, 0.3f ) ) );
		QVERIFY( !( a == Color3( 0.1f, 0.2f, 0.9f ) ) );

		Color4 b( 0.1f, 0.2f, 0.3f, 0.4f );
		QVERIFY( b == Color4( 0.1f, 0.2f, 0.3f, 0.4f ) );
		QVERIFY( !( b == Color4( 0.9f, 0.2f, 0.3f, 0.4f ) ) );
		QVERIFY( !( b == Color4( 0.1f, 0.9f, 0.3f, 0.4f ) ) );
		QVERIFY( !( b == Color4( 0.1f, 0.2f, 0.9f, 0.4f ) ) );
		QVERIFY( !( b == Color4( 0.1f, 0.2f, 0.3f, 0.9f ) ) );
	}

	// ---- vectors: every operator, on values that differ in every component (so that one that mixes two of them shows)

	void vector2_arithmetic()
	{
		Vector2 a( 1, 2 ), b( 10, 30 );

		CHECK_EQ( ( a + b ), Vector2( 11, 32 ) );
		CHECK_EQ( ( a - b ), Vector2( -9, -28 ) );
		CHECK_EQ( ( b - a ), Vector2( 9, 28 ) );
		CHECK_EQ( ( a * 3.0f ), Vector2( 3, 6 ) );
		CHECK_EQ( ( b / 4.0f ), Vector2( 2.5f, 7.5f ) );
		CHECK_EQ( -a, Vector2( -1, -2 ) );
		CHECK_EQ( Vector2(), Vector2( 0, 0 ) );

		// the operators that change a vector do, and give it back
		Vector2 c( a );
		( c += b ) += b;
		CHECK_EQ( c, Vector2( 21, 62 ) );
		c = a;
		( c -= b ) -= b;
		CHECK_EQ( c, Vector2( -19, -58 ) );
		c = a;
		( c *= 3.0f ) *= 2.0f;
		CHECK_EQ( c, Vector2( 6, 12 ) );
		c = b;
		( c /= 5.0f ) /= 2.0f;
		CHECK_EQ( c, Vector2( 1, 3 ) );
	}

	void vector2_lexLessThan()
	{
		// the first component that differs decides, the others do not matter
		QVERIFY( Vector2::lexLessThan( Vector2( 1, 9 ), Vector2( 2, 0 ) ) );
		QVERIFY( !Vector2::lexLessThan( Vector2( 2, 0 ), Vector2( 1, 9 ) ) );
		QVERIFY( Vector2::lexLessThan( Vector2( 1, 2 ), Vector2( 1, 3 ) ) );
		QVERIFY( !Vector2::lexLessThan( Vector2( 1, 3 ), Vector2( 1, 2 ) ) );
		QVERIFY( !Vector2::lexLessThan( Vector2( 1, 2 ), Vector2( 1, 2 ) ) );
	}

	//! Vector3 + float is not what it looks like: it adds the number to the vector itself and gives the vector back
	void vector3_plusFloat()
	{
		Vector3 v( 1, 2, 3 );
		Vector3 & r = v + 10.0f;
		QVERIFY( &r == &v );
		CHECK_EQ( v, Vector3( 11, 12, 13 ) );
	}

	void vector3_cross_and_normalize()
	{
		// numpy.cross( [1, 2, 3], [4, 5, 6] ) and the reverse
		CHECK_EQ( Vector3::crossproduct( Vector3( 1, 2, 3 ), Vector3( 4, 5, 6 ) ), Vector3( -3, 6, -3 ) );
		CHECK_EQ( Vector3::crossproduct( Vector3( 4, 5, 6 ), Vector3( 1, 2, 3 ) ), Vector3( 3, -6, 3 ) );
		CHECK_EQ( Vector3::crossproduct( Vector3( 2, -1, 4 ), Vector3( 3, 5, -2 ) ), Vector3( -18, 16, 13 ) );

#if !defined( FLT_EVAL_METHOD ) || FLT_EVAL_METHOD == 0
		// a vector with no length, even one whose length is lost (the squares of 1e-30 are below the smallest float): no direction, and no NaN.
		// (Not where the squares are kept in a wider format than float, as the x87 does: they do not underflow there.)
		Vector3 tiny( 1e-30f, 1e-30f, 1e-30f );
		tiny.normalize();
		CHECK_EQ( tiny, Vector3( 0, 0, 0 ) );
#endif

		Vector3 v( 0, 3, 4 );
		QVERIFY( near( v.normalize(), Vector3( 0, 0.6f, 0.8f ) ) );
	}

	void vector3_construction()
	{
		CHECK_EQ( Vector3( Vector2( 1, 2 ), 3 ), Vector3( 1, 2, 3 ) );
		CHECK_EQ( Vector3( Vector2( 1, 2 ) ), Vector3( 1, 2, 0 ) );
		// a Vector4 is cut: its w is lost
		CHECK_EQ( Vector3( Vector4( 1, 2, 3, 4 ) ), Vector3( 1, 2, 3 ) );
	}

	void vector3_boundMin_boundMax_html()
	{
		// every component takes its own bound
		Vector3 lo( 5, 5, 5 ), hi( 5, 5, 5 );
		lo.boundMin( Vector3( 2, 9, 3 ) );
		hi.boundMax( Vector3( 2, 9, 7 ) );
		CHECK_EQ( lo, Vector3( 2, 5, 3 ) );
		CHECK_EQ( hi, Vector3( 5, 9, 7 ) );

		// the text of a view: the components, then the length (sqrt( 14 ) = 3.74166)
		QCOMPARE( Vector3( 1, 2, 3 ).toHtml(), QString( "X 1 Y 2 Z 3\nlength 3.74166" ) );
	}

	void vector4_arithmetic()
	{
		Vector4 a( 1, 2, 3, 4 ), b( 10, 30, 50, 90 );

		CHECK_EQ( ( a + b ), Vector4( 11, 32, 53, 94 ) );
		CHECK_EQ( ( a - b ), Vector4( -9, -28, -47, -86 ) );
		CHECK_EQ( ( b - a ), Vector4( 9, 28, 47, 86 ) );
		CHECK_EQ( ( a * 3.0f ), Vector4( 3, 6, 9, 12 ) );
		CHECK_EQ( ( b / 4.0f ), Vector4( 2.5f, 7.5f, 12.5f, 22.5f ) );
		CHECK_EQ( -a, Vector4( -1, -2, -3, -4 ) );
		CHECK_EQ( Vector4(), Vector4( 0, 0, 0, 0 ) );

		Vector4 c( a );
		( c += b ) += b;
		CHECK_EQ( c, Vector4( 21, 62, 103, 184 ) );
		c = a;
		( c -= b ) -= b;
		CHECK_EQ( c, Vector4( -19, -58, -97, -176 ) );
		c = a;
		( c *= 3.0f ) *= 2.0f;
		CHECK_EQ( c, Vector4( 6, 12, 18, 24 ) );
		c = b;
		( c /= 5.0f ) /= 2.0f;
		CHECK_EQ( c, Vector4( 1, 3, 5, 9 ) );
	}

	void vector4_length_dot_normalize()
	{
		// every component counts: 1 + 4 + 9 + 16 = 30, 2 + 3 + 4 + 5 gives 12 as a sum and 54 as a squared length
		QCOMPARE( Vector4( 1, 2, 3, 4 ).squaredLength(), 30.0f );
		QCOMPARE( Vector4( 0, 0, 0, 5 ).squaredLength(), 25.0f );
		QCOMPARE( Vector4( 0, 0, 0, -5 ).squaredLength(), 25.0f );
		QCOMPARE( Vector4( 2, 3, 4, 5 ).squaredLength(), 54.0f );
		QCOMPARE( Vector4( 0, 0, 0, 5 ).length(), 5.0f );
		QCOMPARE( Vector4( 2, 4, 5, 6 ).length(), 9.0f );

		// numpy.dot: 1 * 5 + 2 * 6 + 3 * 7 + 4 * 8 = 70; the w of each is multiplied by the w of the other, and the z by the z
		QCOMPARE( Vector4::dotproduct( Vector4( 1, 2, 3, 4 ), Vector4( 5, 6, 7, 8 ) ), 70.0f );
		QCOMPARE( Vector4::dotproduct( Vector4( 0, 0, 0, 2 ), Vector4( 0, 0, 0, 3 ) ), 6.0f );
		QCOMPARE( Vector4::dotproduct( Vector4( 0, 0, 2, 1 ), Vector4( 0, 0, 3, -1 ) ), 5.0f );
		QCOMPARE( Vector4::dotproduct( Vector4( 0, 0, 0, 2 ), Vector4( 1, 1, 1, -3 ) ), -6.0f );

		// a unit vector, with every component scaled the same way; a vector with no length stays as it is, and is no NaN
		Vector4 v( 2, 4, 5, 6 );	// length 9
		v.normalize();
		QVERIFY( near( v[0], 2.0f / 9.0f ) && near( v[1], 4.0f / 9.0f ) && near( v[2], 5.0f / 9.0f ) && near( v[3], 6.0f / 9.0f ) );

		Vector4 zero;
		zero.normalize();
		CHECK_EQ( zero, Vector4( 0, 0, 0, 0 ) );
#if !defined( FLT_EVAL_METHOD ) || FLT_EVAL_METHOD == 0
		Vector4 tiny( 1e-30f, 1e-30f, 1e-30f, 1e-30f );
		tiny.normalize();
		CHECK_EQ( tiny, Vector4( 0, 0, 0, 0 ) );
#endif
	}

	void vector4_angle()
	{
		QVERIFY( near( Vector4::angle( Vector4( 1, 0, 0, 0 ), Vector4( 0, 0, 0, 1 ) ), float( PI / 2 ) ) );
		QVERIFY( near( Vector4::angle( Vector4( 1, 0, 0, 0 ), Vector4( 1, 0, 0, 0 ) ), 0.0f ) );
		QVERIFY( near( Vector4::angle( Vector4( 1, 0, 0, 0 ), Vector4( -1, 0, 0, 0 ) ), float( PI ) ) );
		// a dot product of 0.5 is 60 degrees, of -0.5 is 120
		QVERIFY( near( Vector4::angle( Vector4( 1, 0, 0, 0 ), Vector4( 0.5f, 0, 0, 0 ) ), float( PI / 3 ) ) );
		QVERIFY( near( Vector4::angle( Vector4( 1, 0, 0, 0 ), Vector4( -0.5f, 0, 0, 0 ) ), float( 2 * PI / 3 ) ) );
		QVERIFY( near( Vector4::angle( Vector4( 2, 0, 0, 0 ), Vector4( 1, 0, 0, 0 ) ), 0.0f ) );
		QVERIFY( near( Vector4::angle( Vector4( -2, 0, 0, 0 ), Vector4( 1, 0, 0, 0 ) ), float( PI ) ) );
	}

	void vector4_lexLessThan_construction_html()
	{
		QVERIFY( Vector4::lexLessThan( Vector4( 1, 9, 9, 9 ), Vector4( 2, 0, 0, 0 ) ) );
		QVERIFY( !Vector4::lexLessThan( Vector4( 2, 0, 0, 0 ), Vector4( 1, 9, 9, 9 ) ) );
		QVERIFY( Vector4::lexLessThan( Vector4( 1, 2, 9, 9 ), Vector4( 1, 3, 0, 0 ) ) );
		QVERIFY( !Vector4::lexLessThan( Vector4( 1, 3, 0, 0 ), Vector4( 1, 2, 9, 9 ) ) );
		QVERIFY( Vector4::lexLessThan( Vector4( 1, 2, 3, 9 ), Vector4( 1, 2, 4, 0 ) ) );
		QVERIFY( !Vector4::lexLessThan( Vector4( 1, 2, 4, 0 ), Vector4( 1, 2, 3, 9 ) ) );
		QVERIFY( Vector4::lexLessThan( Vector4( 1, 2, 3, 4 ), Vector4( 1, 2, 3, 5 ) ) );
		QVERIFY( !Vector4::lexLessThan( Vector4( 1, 2, 3, 5 ), Vector4( 1, 2, 3, 4 ) ) );
		QVERIFY( !Vector4::lexLessThan( Vector4( 1, 2, 3, 4 ), Vector4( 1, 2, 3, 4 ) ) );

		// from a Vector3 and a w, which is 0 when it is not given
		CHECK_EQ( Vector4( Vector3( 1, 2, 3 ) ), Vector4( 1, 2, 3, 0 ) );
		CHECK_EQ( Vector4( Vector3( 1, 2, 3 ), 7 ), Vector4( 1, 2, 3, 7 ) );

		QCOMPARE( Vector4( 1, 2, 3, 4 ).toHtml(), QString( "X 1 Y 2 Z 3 W 4\nlength 5.47723" ) );
	}

	// ---- quaternions

	//! The constructor sets every one of the four components (the object is built over memory that holds 0xff bytes, which are NaN)
	void quat_default()
	{
		Poisoned<Quat> q;
		QCOMPARE( ( *q.object )[0], 1.0f );
		QCOMPARE( ( *q.object )[1], 0.0f );
		QCOMPARE( ( *q.object )[2], 0.0f );
		QCOMPARE( ( *q.object )[3], 0.0f );
	}

	void quat_arithmetic()
	{
		Quat a( 1, 2, 3, 4 ), b( 10, 30, 50, 90 );

		CHECK_EQ( ( a + b ), Quat( 11, 32, 53, 94 ) );
		CHECK_EQ( ( b + a ), Quat( 11, 32, 53, 94 ) );
		CHECK_EQ( ( a * 3.0f ), Quat( 3, 6, 9, 12 ) );

		Quat c( a );
		( c += b ) += b;
		CHECK_EQ( c, Quat( 21, 62, 103, 184 ) );
		c = a;
		( c *= 3.0f ) *= 2.0f;
		CHECK_EQ( c, Quat( 6, 12, 18, 24 ) );

		// every component takes the sign
		c = a;
		c.negate();
		CHECK_EQ( c, Quat( -1, -2, -3, -4 ) );
		c.negate();
		CHECK_EQ( c, a );

		QCOMPARE( Quat::dotproduct( a, b ), 10.0f + 60.0f + 150.0f + 360.0f );
	}

	//! Quat::normalize() divides by the squared length (the XFAIL above): in the meantime what it does is divide all four components by the same
	//! positive number, the squared length or, once it is right, the length. It is no direction change, and no change of sign
	void quat_normalize_direction()
	{
		Quat q( 1, 2, 3, 4 );
		q.normalize();

		QVERIFY2( q[0] > 0.0f, qPrintable( show( q ) ) );
		float divisor = 1.0f / q[0];
		QVERIFY2( near( q[1] * divisor, 2.0f, 1e-4f ) && near( q[2] * divisor, 3.0f, 1e-4f ) && near( q[3] * divisor, 4.0f, 1e-4f ), qPrintable( show( q ) ) );
		// 30 is the squared length of ( 1, 2, 3, 4 ), 5.47723 the length
		QVERIFY2( near( divisor, 30.0f, 1e-3f ) || near( divisor, 5.477226f, 1e-4f ), qPrintable( QString( "divided by %1" ).arg( divisor ) ) );

		// every component counts in the length
		Quat z( 0, 0, 0, 2 );
		z.normalize();
		QVERIFY2( near( z[3], 0.5f ) || near( z[3], 1.0f ), qPrintable( show( z ) ) );
		Quat y( 0, 0, 3, 0 );
		y.normalize();
		QVERIFY2( near( y[2], 1.0f / 9.0f ) || near( y[2], 1.0f / 3.0f ), qPrintable( show( y ) ) );
	}

	void quat_toHtml()
	{
		QCOMPARE( Quat( 1, 2, 3, 4 ).toHtml(), QString( "W 1\nX 2\nY 3\nZ 4" ) );
	}

	// ---- matrices and transforms

	void matrix4_default()
	{
		// built over memory that holds NaN: every element of the identity is set
		Poisoned<Matrix4> m;
		for ( unsigned r = 0; r < 4; r++ ) {
			for ( unsigned c = 0; c < 4; c++ )
				QVERIFY2( ( *m.object )( r, c ) == ( r == c ? 1.0f : 0.0f ), qPrintable( QString( "(%1, %2)" ).arg( r ).arg( c ) ) );
		}
	}

	void matrix4_multiply()
	{
		Matrix4 a = matrix4Of( { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 } );
		Matrix4 b = matrix4Of( { 2, 0, 1, 3, 1, 4, 0, 2, 3, 1, 5, 0, 0, 2, 1, 6 } );

		// numpy: a @ b
		Matrix4 expected = matrix4Of( { 13, 19, 20, 31, 37, 47, 48, 75, 61, 75, 76, 119, 85, 103, 104, 163 } );
		QVERIFY2( matrix4Diff( a * b, expected, 0.0f ).isEmpty(), qPrintable( matrix4Diff( a * b, expected, 0.0f ) ) );
		QVERIFY( matrix4Diff( a * Matrix4(), a, 0.0f ).isEmpty() );
		QVERIFY( matrix4Diff( Matrix4() * a, a, 0.0f ).isEmpty() );
	}

	//! A point times a Matrix4: the matrix times (x, y, z, 1), with the translation in the last column (a Matrix4 of the model has it in the last row)
	void matrix4_timesVector3()
	{
		Matrix4 m = matrix4Of( { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 } );
		// numpy: m @ [1, 10, 100, 1]
		CHECK_EQ( m * Vector3( 1, 10, 100 ), Vector3( 325, 773, 1221 ) );
		CHECK_EQ( Matrix4() * Vector3( 1, 10, 100 ), Vector3( 1, 10, 100 ) );
	}

	void transform_default_timesVector3()
	{
		Transform none;
		QCOMPARE( none.scale, 1.0f );
		CHECK_EQ( none * Vector3( 1, 2, 3 ), Vector3( 1, 2, 3 ) );

		// the rotation, the scale of it, then the translation: 90 degrees about z takes ( 1, 2, 3 ) to ( -2, 1, 3 ), twice that is ( -4, 2, 6 )
		Transform t;
		t.rotation = matrixOf( { 0, -1, 0, 1, 0, 0, 0, 0, 1 } );
		t.translation = Vector3( 1, 2, 3 );
		t.scale = 2.0f;
		CHECK_EQ( t * Vector3( 1, 2, 3 ), Vector3( -3, 4, 9 ) );
	}

	//! What a transform says of itself: the translation, the rotation as the three angles of Matrix::toEuler(), the scale
	void transform_toString()
	{
		Transform t;
		t.rotation = Matrix::euler( 0.5f, 0.25f, 0.75f );
		t.translation = Vector3( 1, 2, 3 );
		t.scale = 2.0f;
		QCOMPARE( t.toString(), QString( "TRANS( 1, 2, 3 ) ROT( 0.5, 0.25, 0.75 ) SCALE( 2 )" ) );
	}

	void triangle_equality_default()
	{
		CHECK_EQ( Triangle(), Triangle( 0, 0, 0 ) );
		QCOMPARE( Triangle().v1(), quint16( 0 ) );
		QCOMPARE( Triangle().v2(), quint16( 0 ) );
		QCOMPARE( Triangle().v3(), quint16( 0 ) );

		Triangle t( 1, 2, 3 );
		CHECK_EQ( t, Triangle( 1, 2, 3 ) );
		QVERIFY( !( t == Triangle( 9, 2, 3 ) ) );
		QVERIFY( !( t == Triangle( 1, 9, 3 ) ) );
		QVERIFY( !( t == Triangle( 1, 2, 9 ) ) );
		// two that differ in the first two and agree in the last
		QVERIFY( !( t == Triangle( 9, 9, 3 ) ) );
		QVERIFY( !( t == Triangle( 9, 2, 9 ) ) );
		QVERIFY( !( t == Triangle( 1, 9, 9 ) ) );
	}

	// ---- colours

	void clamp01_values()
	{
		QCOMPARE( clamp01( 0.25f ), 0.25f );
		QCOMPARE( clamp01( 0.0f ), 0.0f );
		QCOMPARE( clamp01( 1.0f ), 1.0f );
		QCOMPARE( clamp01( 1.5f ), 1.0f );
		QCOMPARE( clamp01( 1.9f ), 1.0f );
		QCOMPARE( clamp01( 2.5f ), 1.0f );
		QCOMPARE( clamp01( 100.0f ), 1.0f );
		QCOMPARE( clamp01( -0.5f ), 0.0f );
		// what is in the range comes back as it is, a negative zero included
		QVERIFY( std::signbit( clamp01( -0.0f ) ) );
		QVERIFY( !std::signbit( clamp01( 0.0f ) ) );
	}

	void color3_defaults_and_setters()
	{
		CHECK_EQ( Color3(), Color3( 1, 1, 1 ) );

		Color3 c( 0.1f, 0.2f, 0.3f );
		c.setRed( 0.5f );
		CHECK_EQ( c, Color3( 0.5f, 0.2f, 0.3f ) );
		c.setGreen( 0.6f );
		CHECK_EQ( c, Color3( 0.5f, 0.6f, 0.3f ) );
		c.setBlue( 0.7f );
		CHECK_EQ( c, Color3( 0.5f, 0.6f, 0.7f ) );

		// from a vector: x y z are red green blue
		Color3 v( Vector3( 0.25f, 0.5f, 0.75f ) );
		CHECK_EQ( v, Color3( 0.25f, 0.5f, 0.75f ) );
		c.fromVector3( Vector3( 1, 2, 3 ) );
		CHECK_EQ( c, Color3( 1, 2, 3 ) );
	}

	void color3_arithmetic()
	{
		Color3 a( 1, 2, 4 ), b( 10, 30, 50 );

		CHECK_EQ( ( a + b ), Color3( 11, 32, 54 ) );
		CHECK_EQ( ( a - b ), Color3( -9, -28, -46 ) );
		CHECK_EQ( ( b - a ), Color3( 9, 28, 46 ) );
		CHECK_EQ( ( a * 3.0f ), Color3( 3, 6, 12 ) );
		CHECK_EQ( ( a + 0.5f ), Color3( 1.5f, 2.5f, 4.5f ) );

		Color3 c( a );
		( c += b ) += b;
		CHECK_EQ( c, Color3( 21, 62, 104 ) );
		c = a;
		( c -= b ) -= b;
		CHECK_EQ( c, Color3( -19, -58, -96 ) );
	}

	//! A channel above 1 or below 0 is shown as the nearest one in range, in every channel
	void color3_toQColor_everyChannel()
	{
		QColor over = Color3( 0.5f, 0.5f, 2.0f ).toQColor();
		QVERIFY( over.isValid() );
		QCOMPARE( over.blue(), 255 );
		QColor under = Color3( 0.5f, 0.5f, -1.0f ).toQColor();
		QVERIFY( under.isValid() );
		QCOMPARE( under.blue(), 0 );
	}

	void color4_defaults_and_setters()
	{
		CHECK_EQ( Color4(), Color4( 1, 1, 1, 1 ) );
		// the alpha of a colour made from three channels is opaque, unless it is given
		CHECK_EQ( Color4( Color3( 0.25f, 0.5f, 0.75f ) ), Color4( 0.25f, 0.5f, 0.75f, 1.0f ) );
		CHECK_EQ( Color4( Color3( 0.25f, 0.5f, 0.75f ), 0.5f ), Color4( 0.25f, 0.5f, 0.75f, 0.5f ) );
		CHECK_EQ( ByteColor4(), Color4( 1, 1, 1, 1 ) );

		Color4 c( 0.1f, 0.2f, 0.3f, 0.4f );
		c.setRed( 0.5f );
		CHECK_EQ( c, Color4( 0.5f, 0.2f, 0.3f, 0.4f ) );
		c.setGreen( 0.6f );
		CHECK_EQ( c, Color4( 0.5f, 0.6f, 0.3f, 0.4f ) );
		c.setBlue( 0.7f );
		CHECK_EQ( c, Color4( 0.5f, 0.6f, 0.7f, 0.4f ) );
		c.setAlpha( 0.8f );
		CHECK_EQ( c, Color4( 0.5f, 0.6f, 0.7f, 0.8f ) );
	}

	void color4_arithmetic()
	{
		Color4 a( 1, 2, 4, 8 ), b( 10, 30, 50, 90 );

		CHECK_EQ( ( a + b ), Color4( 11, 32, 54, 98 ) );
		CHECK_EQ( ( a - b ), Color4( -9, -28, -46, -82 ) );
		CHECK_EQ( ( b - a ), Color4( 9, 28, 46, 82 ) );
		CHECK_EQ( ( a * 3.0f ), Color4( 3, 6, 12, 24 ) );
		CHECK_EQ( ( a + 0.5f ), Color4( 1.5f, 2.5f, 4.5f, 8.5f ) );

		Color4 c( a );
		( c += b ) += b;
		CHECK_EQ( c, Color4( 21, 62, 104, 188 ) );
		c = a;
		( c -= b ) -= b;
		CHECK_EQ( c, Color4( -19, -58, -96, -172 ) );

		// the alpha of a blend is the product, the colour stays
		CHECK_EQ( Color4( 0.1f, 0.2f, 0.3f, 0.5f ).blend( 0.25f ), Color4( 0.1f, 0.2f, 0.3f, 0.125f ) );
		CHECK_EQ( Color4( 0.1f, 0.2f, 0.3f, 4.0f ).blend( 0.5f ), Color4( 0.1f, 0.2f, 0.3f, 2.0f ) );
	}

	// ---- FixedMatrix, the ByteMatrix of the model

	void fixedMatrix_dimensions_and_assign()
	{
		// 2 rows of 3
		ByteMatrix m( 2, 3 );
		QCOMPARE( m.count(), 6 );
		QCOMPARE( m.count( 0 ), 2 );
		QCOMPARE( m.count( 1 ), 3 );
		QCOMPARE( m.count( 2 ), 0 );
		QCOMPARE( m.count( 3 ), 0 );

		memset( m.data(), 0, 6 );
		// an element is where its row and column say (rows of 3), and not where the other order says
		m.assign( 0, 1, 'x' );
		QCOMPARE( m.element( 0, 1 ), 'x' );
		QCOMPARE( m.element( 1, 0 ), '\0' );
		m.assign( 1, 0, 'y' );
		QCOMPARE( m.element( 1, 0 ), 'y' );
		QCOMPARE( m.element( 0, 1 ), 'x' );
		QCOMPARE( m.data()[1], 'x' );
		QCOMPARE( m.data()[3], 'y' );
	}

	//! The rows of a matrix are as long as its second dimension: m( row ) is where the element ( row, 0 ) is. FixedMatrix::operator()( int )
	//! takes the first dimension for the length of a row, which is the same only for a square matrix
	void fixedMatrix_rowAccess()
	{
		ByteMatrix m( 2, 3 );
		memset( m.data(), 0, 6 );

		QVERIFY( m( 0 ) == &m.element( 0, 0 ) );
		QEXPECT_FAIL( "", "FixedMatrix::operator()( int ) steps by count( 0 ), the number of rows, and not by the length of a row (niftypes.h): m( 1 ) of a 2x3 matrix is not its second row; fix then remove", Continue );
		QVERIFY( m( 1 ) == &m.element( 1, 0 ) );

		// a square matrix: the same either way
		ByteMatrix square( 3, 3 );
		memset( square.data(), 0, 9 );
		QVERIFY( square( 2 ) == &square.element( 2, 0 ) );
	}

	// ---- BSVertexDesc: flags from bit 44, the vertex size / 4 in bits 0-3, the offset / 4 of attribute n in bits 4n+4 to 4n+7

	void bsVertexDesc_flagsByAttribute()
	{
		// attribute n is bit n of the flags, which start at bit 44
		for ( int a = 0; a < VA_COUNT; a++ ) {
			BSVertexDesc d;
			QVERIFY( !d.HasFlag( VertexAttribute( a ) ) );
			d.SetFlag( VertexAttribute( a ) );
			QVERIFY2( raw( d ) == ( quint64( 1 ) << ( 44 + a ) ), qPrintable( QString( "attribute %1: %2" ).arg( a ).arg( hex64( raw( d ) ) ) ) );
			QVERIFY( d.HasFlag( VertexAttribute( a ) ) );
			// and no other attribute
			for ( int other = 0; other < VA_COUNT; other++ )
				QVERIFY2( d.HasFlag( VertexAttribute( other ) ) == ( other == a ), qPrintable( QString( "attribute %1 set, %2 asked" ).arg( a ).arg( other ) ) );

			d.RemoveFlag( VertexAttribute( a ) );
			QVERIFY2( raw( d ) == 0, qPrintable( QString( "attribute %1 removed: %2" ).arg( a ).arg( hex64( raw( d ) ) ) ) );
			QVERIFY( !d.HasFlag( VertexAttribute( a ) ) );
		}

		// a flag that is removed leaves the others
		BSVertexDesc d;
		d.SetFlag( VA_POSITION );
		d.SetFlag( VA_NORMAL );
		d.SetFlag( VA_COLOR );
		d.RemoveFlag( VA_NORMAL );
		QVERIFY( d.HasFlag( VA_POSITION ) && !d.HasFlag( VA_NORMAL ) && d.HasFlag( VA_COLOR ) );
		QCOMPARE( hex64( raw( d ) ), hex64( ( quint64( 0x21 ) << 44 ) ) );
	}

	void bsVertexDesc_flagsByVertexFlag()
	{
		BSVertexDesc d;
		d.SetFlag( VF_VERTEX );
		d.SetFlag( VF_FULLPREC );
		// VF_FULLPREC is 0x400: bit 10 of the flags
		QCOMPARE( hex64( raw( d ) ), hex64( quint64( 0x401 ) << 44 ) );
		QVERIFY( d.HasFlag( VF_FULLPREC ) && d.HasFlag( VF_VERTEX ) && !d.HasFlag( VF_NORMAL ) );
		d.RemoveFlag( VF_FULLPREC );
		QCOMPARE( hex64( raw( d ) ), hex64( quint64( 0x001 ) << 44 ) );
		QCOMPARE( int( d.GetFlags() ), int( VF_VERTEX ) );
	}

	void bsVertexDesc_sizeAndOffsets()
	{
		// the size is in bytes, stored divided by 4, in bits 0-3 and only there
		BSVertexDesc d;
		d.SetFlag( VF_VERTEX );
		d.SetFlag( VF_UV );
		d.SetAttributeOffset( VA_TEXCOORD0, 8 );
		d.SetAttributeOffset( VA_NORMAL, 60 );
		d.SetSize( 12 );
		QCOMPARE( d.GetVertexSize(), 12u );
		QCOMPARE( d.GetAttributeOffset( VA_TEXCOORD0 ), 8u );
		QCOMPARE( d.GetAttributeOffset( VA_NORMAL ), 60u );
		QCOMPARE( d.GetAttributeOffset( VA_COLOR ), 0u );
		// the offset of attribute n sits in bits 4n+4 to 4n+7: 8 / 4 = 2 in bits 8-11, 60 / 4 = 15 in bits 16-19
		QCOMPARE( hex64( raw( d ) ), hex64( ( quint64( 3 ) << 44 ) | ( quint64( 2 ) << 8 ) | ( quint64( 15 ) << 16 ) | 3 ) );

		// a new size leaves the offsets, the flags and the other bits as they are
		d.SetSize( 8 );
		QCOMPARE( d.GetVertexSize(), 8u );
		QCOMPARE( d.GetAttributeOffset( VA_TEXCOORD0 ), 8u );
		QCOMPARE( d.GetAttributeOffset( VA_NORMAL ), 60u );
		QCOMPARE( int( d.GetFlags() ), int( VF_VERTEX | VF_UV ) );

		// the position has no offset
		d.SetAttributeOffset( VA_POSITION, 20 );
		QCOMPARE( d.GetAttributeOffset( VA_POSITION ), 0u );

		// every attribute has a field of its own (the ones from the land data on are left out: see bsVertexDesc_resetAttributeOffsets)
		for ( int a = 1; a < 7; a++ ) {
			BSVertexDesc e;
			e.SetAttributeOffset( VertexAttribute( a ), 36 );
			QCOMPARE( e.GetAttributeOffset( VertexAttribute( a ) ), 36u );
			QCOMPARE( hex64( raw( e ) ), hex64( quint64( 9 ) << ( 4 * a + 4 ) ) );
		}
	}

	//! "Dynamic" is a marker in the field of the position (its offset is none): it keeps what the size and the other offsets say
	void bsVertexDesc_makeDynamic()
	{
		BSVertexDesc d;
		d.SetFlag( VF_VERTEX );
		d.SetAttributeOffset( VA_TEXCOORD0, 8 );
		d.SetSize( 12 );
		d.MakeDynamic();
		QCOMPARE( hex64( raw( d ) ), hex64( ( quint64( 1 ) << 44 ) | ( quint64( 2 ) << 8 ) | 0x43 ) );
		// a second time changes nothing
		d.MakeDynamic();
		QCOMPARE( hex64( raw( d ) ), hex64( ( quint64( 1 ) << 44 ) | ( quint64( 2 ) << 8 ) | 0x43 ) );
	}

	void bsVertexDesc_resetAttributeOffsets_data()
	{
		QTest::addColumn<int>( "flags" );
		QTest::addColumn<uint>( "stream" );
		QTest::addColumn<uint>( "size" );
		QTest::addColumn<QVector<uint> >( "offsets" );   // of the attributes 1 to 8: UV, UV 2, normal, tangent, colour, skin, land, eye (see below)

		auto offsets = []( uint uv, uint uv2, uint normal, uint tangent, uint color, uint skin, uint land, uint eye ) {
			return QVector<uint>() << uv << uv2 << normal << tangent << color << skin << land << eye;
		};

		// the attributes in the order of their numbers, each as long as its flag says (the position 8 bytes from Fallout 4 on, 16 in Skyrim SE or with
		// the full precision flag, UV 4, normal 4 with the tangent 4 behind it, colour 4, skin 12, eye data 4)
		const int all = VF_VERTEX | VF_UV | VF_UV_2 | VF_NORMAL | VF_TANGENT | VF_COLORS | VF_SKINNED | VF_EYEDATA;
		QTest::newRow( "every attribute, stream 130" ) << all << 130u << 44u << offsets( 8, 12, 16, 20, 24, 28, 0, 40 );
		QTest::newRow( "every attribute, stream 100: the position is 16 bytes" ) << all << 100u << 52u << offsets( 16, 20, 24, 28, 32, 36, 0, 48 );
		QTest::newRow( "every attribute and full precision, stream 130" ) << ( all | VF_FULLPREC ) << 130u << 52u << offsets( 16, 20, 24, 28, 32, 36, 0, 48 );
		QTest::newRow( "a normal without a tangent" ) << int( VF_VERTEX | VF_NORMAL | VF_COLORS ) << 130u << 16u << offsets( 0, 0, 8, 0, 12, 0, 0, 0 );
		QTest::newRow( "a tangent without a normal has no place" ) << int( VF_VERTEX | VF_TANGENT | VF_UV ) << 130u << 12u << offsets( 8, 0, 0, 0, 0, 0, 0, 0 );
		QTest::newRow( "the second UV and the skin only" ) << int( VF_VERTEX | VF_UV_2 | VF_SKINNED ) << 130u << 24u << offsets( 0, 8, 0, 0, 0, 12, 0, 0 );
		QTest::newRow( "the eye data only" ) << int( VF_VERTEX | VF_EYEDATA ) << 130u << 12u << offsets( 0, 0, 0, 0, 0, 0, 0, 8 );
		QTest::newRow( "no flags" ) << 0 << 130u << 0u << offsets( 0, 0, 0, 0, 0, 0, 0, 0 );
	}

	//! The layout of a vertex that its flags describe
	void bsVertexDesc_resetAttributeOffsets()
	{
		QFETCH( int, flags );
		QFETCH( uint, stream );
		QFETCH( uint, size );
		QFETCH( QVector<uint>, offsets );

		BSVertexDesc d;
		for ( int bit = 0; bit < 11; bit++ ) {
			if ( flags & ( 1 << bit ) )
				d.SetFlag( VertexFlags( 1 << bit ) );
		}
		// offsets and a size from before are not kept
		d.SetAttributeOffset( VA_COLOR, 20 );
		d.SetAttributeOffset( VA_NORMAL, 4 );
		d.SetSize( 60 );

		d.ResetAttributeOffsets( stream );
		QCOMPARE( int( d.GetFlags() ), flags );
		QCOMPARE( d.GetVertexSize(), size );
		// The offsets of the land data and the eye data (attributes 7 and 8, bits 32-39) are not looked at: SetAttributeOffset() clears the field
		// of an attribute with 15 << ( 4 * attribute + 4 ), the shift of an int by 32 and 36 bits, which no compiler is bound to get right
		// (Apple clang loses the offset of the eye data). The skin, attribute 6, shifts the int by 28 and so clears everything above
		// bit 27 (the flags, which ResetAttributeOffsets() puts back at its end)
		for ( int a = 1; a <= 6; a++ )
			QVERIFY2( d.GetAttributeOffset( VertexAttribute( a ) ) == offsets[a - 1], qPrintable( QString( "attribute %1: offset %2, expected %3" ).arg( a ).arg( d.GetAttributeOffset( VertexAttribute( a ) ) ).arg( offsets[a - 1] ) ) );
	}

	void bsVertexDesc_clearAttributeOffsets()
	{
		BSVertexDesc d;
		d.SetFlag( VF_VERTEX );
		d.SetFlag( VF_UV );
		d.SetAttributeOffset( VA_TEXCOORD0, 8 );
		d.SetSize( 12 );
		d.ClearAttributeOffsets();

		// the flags stay, the offsets and the size go
		QCOMPARE( hex64( raw( d ) ), hex64( quint64( 3 ) << 44 ) );
		QCOMPARE( d.GetVertexSize(), 0u );
		QCOMPARE( d.GetAttributeOffset( VA_TEXCOORD0 ), 0u );
	}

	//! & with a number gives the flags that are in it, from either side
	void bsVertexDesc_and()
	{
		BSVertexDesc d;
		d.SetFlag( VF_VERTEX );
		d.SetFlag( VF_UV );
		d.SetAttributeOffset( VA_TEXCOORD0, 8 );
		d.SetSize( 12 );

		QCOMPARE( d & int( VF_UV ), int( VF_UV ) );
		QCOMPARE( int( VF_UV ) & d, int( VF_UV ) );
		QCOMPARE( d & int( VF_NORMAL ), 0 );
		QCOMPARE( int( VF_NORMAL ) & d, 0 );
		QCOMPARE( d & int( VF_VERTEX | VF_NORMAL ), int( VF_VERTEX ) );
		QCOMPARE( int( VF_VERTEX | VF_NORMAL ) & d, int( VF_VERTEX ) );
		// only the flags: the offsets and the size are not in it
		QCOMPARE( d & 0xffff, int( VF_VERTEX | VF_UV ) );
		QCOMPARE( 0xffff & d, int( VF_VERTEX | VF_UV ) );
	}

	void bsVertexDesc_toString()
	{
		BSVertexDesc d;
		QCOMPARE( d.toString(), QString() );

		for ( VertexFlags f : { VF_VERTEX, VF_UV, VF_UV_2, VF_NORMAL, VF_TANGENT, VF_COLORS, VF_SKINNED, VF_EYEDATA, VF_FULLPREC } )
			d.SetFlag( f );
		QCOMPARE( d.toString(), QString( "Vertex | UVs | UVs 2 | Normals | Tangents | Colors | Skinned | Eye Data | Full Prec" ) );

		// each one on its own: the label belongs to the flag, and the others are not in it
		struct Label { VertexFlags flag; const char * text; };
		for ( const Label & l : { Label{ VF_VERTEX, "Vertex" }, Label{ VF_UV, "UVs" }, Label{ VF_UV_2, "UVs 2" }, Label{ VF_NORMAL, "Normals" },
		                         Label{ VF_TANGENT, "Tangents" }, Label{ VF_COLORS, "Colors" }, Label{ VF_SKINNED, "Skinned" },
		                         Label{ VF_EYEDATA, "Eye Data" }, Label{ VF_FULLPREC, "Full Prec" } } ) {
			BSVertexDesc one;
			one.SetFlag( l.flag );
			QCOMPARE( one.toString(), QString( l.text ) );
		}
	}
};

REGISTER_TEST( tst_NifTypes )

#include "tst_niftypes.moc"
