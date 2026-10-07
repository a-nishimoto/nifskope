#include "testenv.h"
#include "testregistry.h"

#include "data/niftypes.h"
#include "data/nifvalue.h"
#include "xml/nifexpr.h"

#include <QColor>
#include <QSet>
#include <QTest>

#include <initializer_list>


//! NifValue (the tagged value every model cell holds) and NifExpr (nif.xml conditions)
class tst_NifValue final : public QObject
{
	Q_OBJECT

	typedef NifValue V;

	// ---- values for the tables below

	static NifValue num( V::Type t, quint32 n ) { return TestEnv::countValue( t, n ); }
	static NifValue flt( V::Type t, float f ) { return TestEnv::floatValue( t, f ); }
	template <typename X> static NifValue of( V::Type t, const X & x ) { return TestEnv::valueOf<X>( t, x ); }
	static NifValue text( V::Type t, const char * s ) { return of<QString>( t, QString::fromLatin1( s ) ); }
	static NifValue bytes( V::Type t, const char * hex ) { return of<QByteArray>( t, QByteArray::fromHex( QByteArray( hex ) ) ); }

	//! m( r, c ) = 1 + 3 r + c, and a copy with the last element changed
	static Matrix matrixA() { return TestEnv::distinctRotation(); }
	static Matrix matrixB() { Matrix m = matrixA(); m( 2, 2 ) = 0.0f; return m; }

	//! m( r, c ) = 1 + 4 r + c, and a copy with the last element changed
	static Matrix4 matrix4A()
	{
		Matrix4 m;
		for ( int r = 0; r < 4; r++ )
			for ( int c = 0; c < 4; c++ )
				m( r, c ) = float( 1 + 4 * r + c );

		return m;
	}

	static Matrix4 matrix4B() { Matrix4 m = matrix4A(); m( 3, 3 ) = 0.0f; return m; }

	//! Vertex, UV and colour attributes at a vertex size of 12 bytes, and the same at 16
	static BSVertexDesc descA()
	{
		BSVertexDesc d;
		d.SetFlag( VertexFlags( VF_VERTEX | VF_UV | VF_COLORS ) );
		d.SetSize( 12 );
		return d;
	}

	static BSVertexDesc descB() { BSVertexDesc d = descA(); d.SetSize( 16 ); return d; }

	//! Two rows: the value against a copy of itself, and against another value of the same type
	static void equalAndDifferent( const char * name, const NifValue & a, const NifValue & other )
	{
		QTest::newRow( qPrintable( QString( "%1, same value" ).arg( name ) ) ) << a << NifValue( a ) << true;
		QTest::newRow( qPrintable( QString( "%1, other value" ).arg( name ) ) ) << a << other << false;
	}

	//! The type a data row names by number
	static V::Type typeOf( int type ) { return V::Type( type ); }

	static QString hex( quint32 n ) { return QString( "%1" ).arg( n, 0, 16 ); }

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

private slots:
	void initTestCase()
	{
		// Starts from the built-in types only. This wipes whatever nif.xml registered; classes that need the XML reload it.
		NifValue::initialize();
	}

	void typeNames()
	{
		QCOMPARE( NifValue::type( "uint" ), NifValue::tUInt );
		QCOMPARE( NifValue::type( "ushort" ), NifValue::tWord );
		QCOMPARE( NifValue::type( "Ref" ), NifValue::tLink );
		QCOMPARE( NifValue::type( "Ptr" ), NifValue::tUpLink );
		QCOMPARE( NifValue::type( "Vector3" ), NifValue::tVector3 );
		QCOMPARE( NifValue::type( "TexCoord" ), NifValue::tVector2 );
		QCOMPARE( NifValue::type( "QuaternionWXYZ" ), NifValue::tQuat );
		QCOMPARE( NifValue::type( "hkQuaternion" ), NifValue::tQuatXYZW );
		QCOMPARE( NifValue::type( "NoSuchType" ), NifValue::tNone );
	}

	void defaults()
	{
		QVERIFY( !NifValue().isValid() );
		QCOMPARE( NifValue( NifValue::tInt ).get<int>(), 0 );
		QVERIFY( NifValue( NifValue::tString ).get<QString>().isEmpty() );
		// a fresh link is the null link, and so is anything that is not a link
		QCOMPARE( NifValue( NifValue::tLink ).toLink(), -1 );
		QCOMPARE( NifValue( NifValue::tInt ).toLink(), -1 );
	}

	void setGet_scalars()
	{
		NifValue i( NifValue::tInt );
		QVERIFY( i.set<int>( -5 ) );
		QCOMPARE( i.get<int>(), -5 );
		QCOMPARE( i.toString(), QString( "-5" ) );

		NifValue u( NifValue::tUInt );
		QVERIFY( u.set<quint32>( 0xFFFFFFFFu ) );
		QCOMPARE( u.get<quint32>(), 0xFFFFFFFFu );

		NifValue b( NifValue::tByte );
		QVERIFY( b.set<quint8>( 200 ) );
		QCOMPARE( b.get<quint8>(), quint8( 200 ) );

		NifValue f( NifValue::tFloat );
		QVERIFY( f.set<float>( 0.25f ) );
		QCOMPARE( f.get<float>(), 0.25f );
		QCOMPARE( f.toString(), QString( "0.250000" ) );

		NifValue l( NifValue::tLink );
		QVERIFY( l.setLink( 7 ) );
		QCOMPARE( l.toLink(), 7 );
		QVERIFY( l.isLink() );

		// wrong-type writes are refused, not coerced
		QVERIFY( !i.set<float>( 1.0f ) );
		QVERIFY( !f.set<QString>( "x" ) );
	}

	void setGet_compound()
	{
		NifValue v3( NifValue::tVector3 );
		QVERIFY( v3.set<Vector3>( Vector3( 1, 2, 3 ) ) );
		QVERIFY( v3.get<Vector3>() == Vector3( 1, 2, 3 ) );
		QCOMPARE( v3.toString(), QString( "X 1.000000 Y 2.000000 Z 3.000000" ) );

		NifValue q( NifValue::tQuat );
		QVERIFY( q.set<Quat>( Quat( 0.5f, 0.5f, 0.5f, 0.5f ) ) );
		QVERIFY( q.get<Quat>() == Quat( 0.5f, 0.5f, 0.5f, 0.5f ) );

		NifValue c( NifValue::tColor4 );
		QVERIFY( c.set<Color4>( Color4( 1.0f, 0.0f, 0.5f, 1.0f ) ) );
		QVERIFY( c.get<Color4>() == Color4( 1.0f, 0.0f, 0.5f, 1.0f ) );
		QVERIFY( c.isColor() );

		NifValue t( NifValue::tTriangle );
		QVERIFY( t.set<Triangle>( Triangle( 4, 5, 6 ) ) );
		QVERIFY( t.get<Triangle>() == Triangle( 4, 5, 6 ) );

		NifValue s( NifValue::tSizedString );
		QVERIFY( s.set<QString>( "hello" ) );
		QCOMPARE( s.get<QString>(), QString( "hello" ) );
		QVERIFY( s.isString() );

		NifValue arr( NifValue::tByteArray );
		QVERIFY( arr.set<QByteArray>( QByteArray( "\x01\x02\x03", 3 ) ) );
		QCOMPARE( arr.get<QByteArray>(), QByteArray( "\x01\x02\x03", 3 ) );
	}

	//! Copies own their data (the union holds heap pointers for compound types)
	void copyIsDeep()
	{
		NifValue a( NifValue::tVector3 );
		a.set<Vector3>( Vector3( 1, 1, 1 ) );

		NifValue b( a );
		b.set<Vector3>( Vector3( 9, 9, 9 ) );
		QVERIFY( a.get<Vector3>() == Vector3( 1, 1, 1 ) );

		NifValue c;
		c = a;
		a.set<Vector3>( Vector3( 2, 2, 2 ) );
		QVERIFY( c.get<Vector3>() == Vector3( 1, 1, 1 ) );
		QVERIFY( !( a == c ) );

		NifValue s1( NifValue::tString ), s2;
		s1.set<QString>( "abc" );
		s2 = s1;
		s1.set<QString>( "xyz" );
		QCOMPARE( s2.get<QString>(), QString( "abc" ) );
	}

	void stringConversions()
	{
		NifValue b( NifValue::tBool );
		QVERIFY( b.setFromString( "yes" ) );
		QCOMPARE( b.toString(), QString( "yes" ) );
		QVERIFY( b.setFromString( "no" ) );
		QCOMPARE( b.toString(), QString( "no" ) );
		QVERIFY( b.setFromString( "undefined" ) );
		QCOMPARE( b.toString(), QString( "undefined" ) );

		NifValue i( NifValue::tInt );
		QVERIFY( i.setFromString( "0x10" ) );
		QCOMPARE( i.get<int>(), 16 );
		QVERIFY( !i.setFromString( "nope" ) );

		NifValue f( NifValue::tFloat );
		QVERIFY( f.setFromString( "-1.5" ) );
		QCOMPARE( f.get<float>(), -1.5f );

		NifValue v( NifValue::tVector3 );
		QVERIFY( v.setFromString( "1, 2, 3" ) );
		QVERIFY( v.get<Vector3>() == Vector3( 1, 2, 3 ) );

		NifValue ver( NifValue::tFileVersion );
		QVERIFY( ver.setFromString( "20.2.0.7" ) );
		QCOMPARE( ver.toFileVersion(), 0x14020007u );
		QVERIFY( ver.isFileVersion() );

		// no text form for matrices
		NifValue m( NifValue::tMatrix );
		QVERIFY( !m.setFromString( "1 0 0 0 1 0 0 0 1" ) );
	}

	void equality()
	{
		NifValue a( NifValue::tInt ), b( NifValue::tInt );
		a.set<int>( 3 );
		b.set<int>( 3 );
		QVERIFY( a == b );
		b.set<int>( 4 );
		QVERIFY( !( a == b ) );

		NifValue s1( NifValue::tSizedString ), s2( NifValue::tSizedString );
		s1.set<QString>( "x" );
		s2.set<QString>( "x" );
		QVERIFY( s1 == s2 );

		// via QVariant as the views use it
		QVERIFY( a.toVariant().isValid() );
		NifValue back;
		QVERIFY( back.setFromVariant( a.toVariant() ) );
		QVERIFY( back == a );
	}

	void enums()
	{
		NifValue::initialize();	// clears enums

		QVERIFY( NifValue::registerEnumType( "TestEnum", NifValue::eDefault ) );
		QVERIFY( !NifValue::registerEnumType( "TestEnum", NifValue::eDefault ) );
		QVERIFY( NifValue::registerEnumOption( "TestEnum", "TE_ONE", 1, "first" ) );
		QVERIFY( NifValue::registerEnumOption( "TestEnum", "TE_TWO", 2, "second" ) );
		QVERIFY( !NifValue::registerEnumOption( "TestEnum", "TE_DUP", 2, "duplicate value" ) );

		QCOMPARE( NifValue::enumType( "TestEnum" ), NifValue::eDefault );
		QCOMPARE( NifValue::enumType( "Nope" ), NifValue::eNone );
		QCOMPARE( NifValue::enumOptionName( "TestEnum", 2 ), QString( "TE_TWO" ) );
		QCOMPARE( NifValue::enumOptionText( "TestEnum", 1 ), QString( "first" ) );
		QCOMPARE( NifValue::enumOptionValue( "TestEnum", "TE_ONE" ), 1u );
		bool ok = true;
		NifValue::enumOptionValue( "TestEnum", "TE_MISSING", &ok );
		QVERIFY( !ok );
		QCOMPARE( NifValue::enumOptions( "TestEnum" ), QStringList() << "TE_ONE" << "TE_TWO" );

		QVERIFY( NifValue::registerAlias( "TestEnumAlias", "uint" ) );
		QCOMPARE( NifValue::type( "TestEnumAlias" ), NifValue::tUInt );
		QVERIFY( !NifValue::registerAlias( "TestEnumAlias", "uint" ) );
		QVERIFY( !NifValue::registerAlias( "Another", "NoSuchType" ) );

		// leave the type table the way XML loading expects it
		NifValue::initialize();
	}

	//! Every name nif.xml can use for a basic type, and the type it stands for
	void typeNames_all_data()
	{
		QTest::addColumn<QString>( "name" );
		QTest::addColumn<int>( "type" );

		struct Name { const char * name; V::Type type; };
		const Name names[] = {
			{ "bool", V::tBool }, { "byte", V::tByte }, { "char", V::tByte }, { "word", V::tWord }, { "short", V::tShort },
			{ "int", V::tInt }, { "Flags", V::tFlags }, { "ushort", V::tWord }, { "uint", V::tUInt }, { "ulittle32", V::tULittle32 },
			{ "Ref", V::tLink }, { "Ptr", V::tUpLink }, { "float", V::tFloat },
			{ "SizedString", V::tSizedString }, { "Text", V::tText }, { "ShortString", V::tShortString },
			{ "Color3", V::tColor3 }, { "Color4", V::tColor4 }, { "Vector4", V::tVector4 }, { "Vector3", V::tVector3 }, { "TBC", V::tVector3 },
			{ "Quaternion", V::tQuat }, { "QuaternionWXYZ", V::tQuat }, { "QuaternionXYZW", V::tQuatXYZW }, { "hkQuaternion", V::tQuatXYZW },
			{ "Matrix33", V::tMatrix }, { "Matrix44", V::tMatrix4 }, { "Vector2", V::tVector2 }, { "TexCoord", V::tVector2 },
			{ "Triangle", V::tTriangle }, { "ByteArray", V::tByteArray }, { "ByteMatrix", V::tByteMatrix }, { "FileVersion", V::tFileVersion },
			{ "HeaderString", V::tHeaderString }, { "LineString", V::tLineString }, { "StringPalette", V::tStringPalette },
			{ "StringOffset", V::tStringOffset }, { "StringIndex", V::tStringIndex }, { "BlockTypeIndex", V::tBlockTypeIndex },
			{ "char8string", V::tChar8String }, { "string", V::tString }, { "FilePath", V::tFilePath }, { "blob", V::tBlob },
			{ "hfloat", V::tHfloat }, { "HalfVector3", V::tHalfVector3 }, { "ByteVector3", V::tByteVector3 },
			{ "HalfVector2", V::tHalfVector2 }, { "HalfTexCoord", V::tHalfVector2 }, { "ByteColor4", V::tByteColor4 },
			{ "BSVertexDesc", V::tBSVertexDesc },
			// compounds of nif.xml, and spellings that are not types
			{ "Matrix22", V::tNone }, { "Color", V::tNone }, { "vector3", V::tNone }, { "", V::tNone }
		};

		for ( const Name & n : names )
			QTest::newRow( n.name[0] ? n.name : "empty name" ) << QString::fromLatin1( n.name ) << int( n.type );
	}

	void typeNames_all()
	{
		QFETCH( QString, name );
		QFETCH( int, type );

		QCOMPARE( int( V::type( name ) ), type );
	}

	//! A new string index and string offset is the "none" marker 0xffffffff, like a link is -1; the other types start at 0
	void defaults_indices()
	{
		QCOMPARE( num( V::tStringIndex, 5 ).toCount(), 5u );
		QCOMPARE( V( V::tStringIndex ).toCount(), 0xffffffffu );
		QCOMPARE( V( V::tStringOffset ).toCount(), 0xffffffffu );
		QCOMPARE( V( V::tUInt ).toCount(), 0u );
		QCOMPARE( V( V::tWord ).toCount(), 0u );
		QCOMPARE( V( V::tBool ).toCount(), 0u );
	}

	void typeDescription_escapesText()
	{
		V::initialize();
		V::setTypeDescription( "Thing", "a < b > c\nsecond line" );
		// the angle bracket that opens a tag is escaped, the line feed becomes a line break
		QCOMPARE( V::typeDescription( "Thing" ), QString( "<p><b>Thing</b></p><p>a &lt; b > c<br/>second line</p>" ) );
		QCOMPARE( V::typeDescription( "Unknown" ), QString( "<p><b>Unknown</b></p><p></p>" ) );
		V::initialize();
	}

	//! A flags enum: the value of an option is the number of its bit; names come out in bit order, three to a line
	void enumFlags_names()
	{
		V::initialize();
		QVERIFY( V::registerEnumType( "TestFlags", V::eFlags ) );
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT0", 0, "" ) );		// 0x01
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT1", 1, "" ) );		// 0x02
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT2", 2, "" ) );		// 0x04
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT5", 5, "" ) );		// 0x20

		QCOMPARE( V::enumType( "TestFlags" ), V::eFlags );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x00 ), QString() );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x01 ), QString( "F_BIT0" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x02 ), QString( "F_BIT1" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x04 ), QString( "F_BIT2" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x20 ), QString( "F_BIT5" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x05 ), QString( "F_BIT0 | F_BIT2" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x22 ), QString( "F_BIT1 | F_BIT5" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x07 ), QString( "F_BIT0 | F_BIT1 | F_BIT2" ) );

		// the fourth name starts a new line (3 options per line is the default of the Options Per Line setting)
		QCOMPARE( V::enumOptionName( "TestFlags", 0x27 ), QString( "F_BIT0 | F_BIT1 | F_BIT2 | \nF_BIT5" ) );

		// bits without an option follow as one hexadecimal number
		QCOMPARE( V::enumOptionName( "TestFlags", 0x301 ), QString( "F_BIT0 | 300" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x300 ), QString( "300" ) );
		QCOMPARE( V::enumOptionName( "TestFlags", 0x10 ), QString( "10" ) );

		// not an enum at all: no text
		QCOMPARE( V::enumOptionName( "NoSuchEnum", 3 ), QString() );
		V::initialize();
	}

	void enumFlags_values()
	{
		V::initialize();
		QVERIFY( V::registerEnumType( "TestFlags", V::eFlags ) );
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT0", 0, "" ) );
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT1", 1, "" ) );
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT2", 2, "" ) );
		QVERIFY( V::registerEnumOption( "TestFlags", "F_BIT5", 5, "" ) );

		struct Row { const char * text; quint32 value; bool ok; };
		const Row rows[] = {
			{ "F_BIT0", 0x01, true }, { "F_BIT1", 0x02, true }, { "F_BIT2", 0x04, true }, { "F_BIT5", 0x20, true },
			{ "F_BIT0 | F_BIT2", 0x05, true }, { "F_BIT1|F_BIT5", 0x22, true }, { "F_BIT5  |  F_BIT0", 0x21, true },
			// a number is taken as it is, hexadecimal with 0x
			{ "F_BIT2 | 0x10", 0x14, true }, { "F_BIT2 | 16", 0x14, true }, { "0x300", 0x300, true },
			// a word that is neither: the options that were found are still in the value, the answer is "not found"
			{ "F_BIT2 | NOPE", 0x04, false }, { "NOPE", 0x00, false }, { "NOPE | F_BIT1", 0x02, false }
		};

		for ( const Row & r : rows ) {
			bool ok = !r.ok;
			quint32 value = V::enumOptionValue( "TestFlags", QString::fromLatin1( r.text ), &ok );
			QVERIFY2( value == r.value, qPrintable( QString( "'%1' is %2, expected %3" ).arg( r.text ).arg( value, 0, 16 ).arg( r.value, 0, 16 ) ) );
			QVERIFY2( ok == r.ok, qPrintable( QString( "'%1': ok is %2" ).arg( r.text ).arg( ok ) ) );
		}

		V::initialize();
	}

	//! A new value and a copy of it are equal, a value that differs in the part the type stores is not
	void equality_types_data()
	{
		QTest::addColumn<NifValue>( "a" );
		QTest::addColumn<NifValue>( "b" );
		QTest::addColumn<bool>( "equal" );

		equalAndDifferent( "tByte", num( V::tByte, 0x7f ), num( V::tByte, 0x7e ) );
		equalAndDifferent( "tWord", num( V::tWord, 0x1234 ), num( V::tWord, 0x1235 ) );
		equalAndDifferent( "tFlags", num( V::tFlags, 0x1234 ), num( V::tFlags, 0x1235 ) );
		equalAndDifferent( "tBlockTypeIndex", num( V::tBlockTypeIndex, 0x8003 ), num( V::tBlockTypeIndex, 0x8004 ) );
		equalAndDifferent( "tShort", num( V::tShort, 0xfffe ), num( V::tShort, 0xfffd ) );
		equalAndDifferent( "tStringOffset", num( V::tStringOffset, 0x12345678 ), num( V::tStringOffset, 0x12345679 ) );
		equalAndDifferent( "tBool", num( V::tBool, 1 ), num( V::tBool, 2 ) );
		equalAndDifferent( "tUInt", num( V::tUInt, 0xdeadbeef ), num( V::tUInt, 0xdeadbeee ) );
		equalAndDifferent( "tULittle32", num( V::tULittle32, 0xdeadbeef ), num( V::tULittle32, 0xdeadbeee ) );
		equalAndDifferent( "tUInt (the upper half)", num( V::tUInt, 0x00010005 ), num( V::tUInt, 0x00020005 ) );
		equalAndDifferent( "tStringIndex", num( V::tStringIndex, 7 ), num( V::tStringIndex, 8 ) );
		equalAndDifferent( "tInt", num( V::tInt, quint32( -5 ) ), num( V::tInt, quint32( -6 ) ) );
		equalAndDifferent( "tLink", TestEnv::linkValue( V::tLink, 5 ), TestEnv::linkValue( V::tLink, 6 ) );
		equalAndDifferent( "tUpLink", TestEnv::linkValue( V::tUpLink, 5 ), TestEnv::linkValue( V::tUpLink, -1 ) );
		equalAndDifferent( "tFloat", flt( V::tFloat, 1.5f ), flt( V::tFloat, 2.5f ) );
		equalAndDifferent( "tHfloat", flt( V::tHfloat, 1.5f ), flt( V::tHfloat, 2.5f ) );

		for ( V::Type t : { V::tSizedString, V::tText, V::tShortString, V::tHeaderString, V::tLineString, V::tChar8String, V::tString } )
			equalAndDifferent( qPrintable( QString( "string type %1" ).arg( int( t ) ) ), text( t, "abc" ), text( t, "abd" ) );

		equalAndDifferent( "tColor3", of<Color3>( V::tColor3, Color3( 0.25f, 0.5f, 1.0f ) ), of<Color3>( V::tColor3, Color3( 0.25f, 0.5f, 0.75f ) ) );
		equalAndDifferent( "tColor4", of<Color4>( V::tColor4, Color4( 0.25f, 0.5f, 1.0f, 0.75f ) ), of<Color4>( V::tColor4, Color4( 0.25f, 0.5f, 1.0f, 0.5f ) ) );
		{
			ByteColor4 a, b;
			a.setRGBA( 0.25f, 0.5f, 1.0f, 0.75f );
			b.setRGBA( 0.25f, 0.5f, 1.0f, 0.5f );
			equalAndDifferent( "tByteColor4", of<ByteColor4>( V::tByteColor4, a ), of<ByteColor4>( V::tByteColor4, b ) );
		}
		equalAndDifferent( "tVector2", of<Vector2>( V::tVector2, Vector2( 1, 2 ) ), of<Vector2>( V::tVector2, Vector2( 1, 3 ) ) );
		equalAndDifferent( "tHalfVector2", of<HalfVector2>( V::tHalfVector2, HalfVector2( 1, 2 ) ), of<HalfVector2>( V::tHalfVector2, HalfVector2( 1, 3 ) ) );
		equalAndDifferent( "tVector3", of<Vector3>( V::tVector3, Vector3( 1, 2, 3 ) ), of<Vector3>( V::tVector3, Vector3( 1, 2, 4 ) ) );
		equalAndDifferent( "tHalfVector3", of<HalfVector3>( V::tHalfVector3, HalfVector3( 1, 2, 3 ) ), of<HalfVector3>( V::tHalfVector3, HalfVector3( 1, 2, 4 ) ) );
		equalAndDifferent( "tByteVector3", of<ByteVector3>( V::tByteVector3, ByteVector3( 1, 0, -1 ) ), of<ByteVector3>( V::tByteVector3, ByteVector3( 1, 0, 1 ) ) );
		equalAndDifferent( "tVector4", of<Vector4>( V::tVector4, Vector4( 1, 2, 3, 4 ) ), of<Vector4>( V::tVector4, Vector4( 1, 2, 3, 5 ) ) );
		equalAndDifferent( "tQuat", of<Quat>( V::tQuat, Quat( 1, 2, 3, 4 ) ), of<Quat>( V::tQuat, Quat( 1, 2, 3, 5 ) ) );
		equalAndDifferent( "tQuatXYZW", of<Quat>( V::tQuatXYZW, Quat( 1, 2, 3, 4 ) ), of<Quat>( V::tQuatXYZW, Quat( 1, 2, 3, 5 ) ) );
		equalAndDifferent( "tTriangle", of<Triangle>( V::tTriangle, Triangle( 1, 2, 3 ) ), of<Triangle>( V::tTriangle, Triangle( 1, 3, 2 ) ) );
		equalAndDifferent( "tMatrix", of<Matrix>( V::tMatrix, matrixA() ), of<Matrix>( V::tMatrix, matrixB() ) );
		equalAndDifferent( "tMatrix4", of<Matrix4>( V::tMatrix4, matrix4A() ), of<Matrix4>( V::tMatrix4, matrix4B() ) );
		equalAndDifferent( "tBSVertexDesc", of<BSVertexDesc>( V::tBSVertexDesc, descA() ), of<BSVertexDesc>( V::tBSVertexDesc, descB() ) );
		equalAndDifferent( "tByteArray", bytes( V::tByteArray, "01 02 03" ), bytes( V::tByteArray, "01 02 04" ) );
		equalAndDifferent( "tStringPalette", bytes( V::tStringPalette, "61 00 62 00" ), bytes( V::tStringPalette, "61 00 63 00" ) );
		equalAndDifferent( "tBlob", bytes( V::tBlob, "aa bb" ), bytes( V::tBlob, "aa bc" ) );

		// byte arrays: two empty arrays that were stored are equal, a null array (a value nothing was set in) is equal to nothing
		QTest::newRow( "tByteArray, two empty arrays" ) << bytes( V::tByteArray, "" ) << bytes( V::tByteArray, "" ) << true;
		QTest::newRow( "tByteArray, null and null" ) << V( V::tByteArray ) << V( V::tByteArray ) << false;
		QTest::newRow( "tByteArray, null and empty" ) << V( V::tByteArray ) << bytes( V::tByteArray, "" ) << false;
		QTest::newRow( "tByteArray, empty and null" ) << bytes( V::tByteArray, "" ) << V( V::tByteArray ) << false;
		QTest::newRow( "tNone and tNone" ) << V() << V() << false;
	}

	void equality_types()
	{
		QFETCH( NifValue, a );
		QFETCH( NifValue, b );
		QFETCH( bool, equal );

		QVERIFY2( ( a == b ) == equal, equal ? "the values are not equal" : "the values are equal" );
	}

	//! tStringOffset is a 32-bit number, but operator== compares its lower 16 bits (nifvalue.cpp, it is listed with the 16-bit types)
	void equality_stringOffsetUses32Bits()
	{
		NifValue a = num( V::tStringOffset, 0x00010005 );
		NifValue b = num( V::tStringOffset, 0x00020005 );

		QEXPECT_FAIL( "", "NifValue::operator== compares a tStringOffset as 16 bits (nifvalue.cpp); fix then remove", Continue );
		QVERIFY( !( a == b ) );
	}

	void setFromVariant()
	{
		NifValue s( V::tSizedString );
		QVERIFY( s.setFromVariant( QVariant( QString( "abc" ) ) ) );
		QCOMPARE( s.get<QString>(), QString( "abc" ) );

		// a text for a value that is not a string is refused
		NifValue i = num( V::tInt, 7 );
		QVERIFY( !i.setFromVariant( QVariant( QString( "9" ) ) ) );
		QCOMPARE( i.toCount(), 7u );

		// a NifValue in a QVariant replaces the value, type included
		NifValue target( V::tFloat );
		QVERIFY( target.setFromVariant( i.toVariant() ) );
		QCOMPARE( int( target.type() ), int( V::tInt ) );
		QCOMPARE( target.toCount(), 7u );

		// anything else is refused
		QVERIFY( !target.setFromVariant( QVariant( 3 ) ) );
	}

	void setFromString_count_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<bool>( "ok" );
		QTest::addColumn<quint32>( "count" );

		auto row = []( V::Type t, const char * text, bool ok, quint32 count ) {
			QTest::newRow( qPrintable( QString( "type %1 '%2'" ).arg( int( t ) ).arg( text ) ) ) << int( t ) << QString::fromLatin1( text ) << ok << count;
		};

		// a bool is words, or a number
		row( V::tBool, "yes", true, 1 );
		row( V::tBool, "true", true, 1 );
		row( V::tBool, "no", true, 0 );
		row( V::tBool, "false", true, 0 );
		row( V::tBool, "undefined", true, 2 );
		row( V::tBool, "1", true, 1 );
		row( V::tBool, "0x2", true, 2 );
		row( V::tBool, "maybe", false, 0 );

		// a byte: decimal or hexadecimal
		row( V::tByte, "200", true, 200 );
		row( V::tByte, "0x10", true, 16 );
		row( V::tByte, "0xff", true, 255 );
		row( V::tByte, "abc", false, 0 );
		row( V::tByte, "", false, 0 );

		// the 16-bit types
		for ( V::Type t : { V::tWord, V::tFlags, V::tBlockTypeIndex, V::tShort, V::tStringOffset } ) {
			row( t, "1234", true, 1234 );
			row( t, "0x10", true, 16 );
			row( t, "0x7fff", true, 0x7fff );
			row( t, "xyz", false, 0 );
		}

		// short is signed
		row( V::tShort, "-2", true, 0xfffe );
		row( V::tShort, "-32768", true, 0x8000 );

		row( V::tInt, "-5", true, quint32( -5 ) );
		row( V::tInt, "0x10", true, 16 );
		row( V::tInt, "2147483647", true, 0x7fffffff );
		row( V::tInt, "nope", false, 0 );

		for ( V::Type t : { V::tUInt, V::tULittle32 } ) {
			row( t, "4000000000", true, 4000000000u );
			row( t, "0x10", true, 16 );
			row( t, "0xFFFFFFFF", true, 0xffffffffu );
			row( t, "-1", false, 0 );
		}

		// a string index: decimal, and it holds the largest value (0xffffffff is "none")
		row( V::tStringIndex, "7", true, 7 );
		row( V::tStringIndex, "4294967295", true, 0xffffffffu );
		row( V::tStringIndex, "-1", false, 0 );
	}

	void setFromString_count()
	{
		QFETCH( int, type );
		QFETCH( QString, text );
		QFETCH( bool, ok );
		QFETCH( quint32, count );

		NifValue v( typeOf( type ) );
		QCOMPARE( v.setFromString( text ), ok );
		if ( ok )
			QCOMPARE( hex( v.toCount() ), hex( count ) );
	}

	//! The 16-bit types are read with toShort(): the numbers from 32768 on are refused, though a Word, Flags or BlockTypeIndex is unsigned
	void setFromString_unsigned16_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<quint32>( "count" );

		QTest::newRow( "tWord 40000" ) << int( V::tWord ) << "40000" << 40000u;
		QTest::newRow( "tWord 65535" ) << int( V::tWord ) << "65535" << 65535u;
		QTest::newRow( "tWord 0xFFFF" ) << int( V::tWord ) << "0xFFFF" << 65535u;
		QTest::newRow( "tFlags 32768" ) << int( V::tFlags ) << "32768" << 32768u;
		QTest::newRow( "tBlockTypeIndex 0x8003" ) << int( V::tBlockTypeIndex ) << "0x8003" << 0x8003u;
	}

	void setFromString_unsigned16()
	{
		QFETCH( int, type );
		QFETCH( QString, text );
		QFETCH( quint32, count );

		NifValue v( typeOf( type ) );
		bool read = v.setFromString( text ) && v.toCount() == count;

		QEXPECT_FAIL( "", "NifValue::setFromString() reads a Word, Flags and BlockTypeIndex with toShort(), so 32768 and up is refused (nifvalue.cpp); fix then remove", Continue );
		QVERIFY( read );
	}

	void setFromString_other_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<bool>( "ok" );
		QTest::addColumn<QString>( "result" );   // the value as text, from the type's own toString(); empty if the call fails

		auto row = []( V::Type t, const char * text, bool ok, const char * result ) {
			QTest::newRow( qPrintable( QString( "type %1 '%2'" ).arg( int( t ) ).arg( text ) ) ) << int( t ) << QString::fromLatin1( text ) << ok << QString::fromLatin1( result );
		};

		row( V::tLink, "-1", true, "-1" );
		row( V::tLink, "5", true, "5" );
		row( V::tUpLink, "-1", true, "-1" );
		row( V::tLink, "x", false, "" );
		row( V::tFloat, "-1.5", true, "-1.500000" );
		row( V::tFloat, "abc", false, "" );
		row( V::tHfloat, "0.5", true, "0.5000" );
		row( V::tHfloat, "0.0625", true, "0.0625" );
		row( V::tHfloat, "abc", false, "" );
		row( V::tFileVersion, "20.2.0.7", true, "20.2.0.7" );
		row( V::tFileVersion, "4.0.0.2", true, "4.0.0.2" );
		row( V::tFileVersion, "garbage", false, "" );

		for ( V::Type t : { V::tSizedString, V::tText, V::tShortString, V::tHeaderString, V::tLineString, V::tChar8String, V::tString } )
			row( t, "some text", true, "some text" );

		row( V::tVector2, "1.5, -2", true, "X 1.500000 Y -2.000000" );
		row( V::tVector3, "1, 2, 3", true, "X 1.000000 Y 2.000000 Z 3.000000" );
		row( V::tVector4, "1, 2, 3, 4", true, "X 1.000000 Y 2.000000 Z 3.000000 W 4.000000" );
		// the Quat text is x, y, z, w; the value is w, x, y, z
		row( V::tQuat, "1, 2, 3, 4", true, "" );
		row( V::tQuatXYZW, "1, 2, 3, 4", true, "" );

		// no text form
		for ( V::Type t : { V::tMatrix, V::tMatrix4, V::tTriangle, V::tByteArray, V::tByteMatrix, V::tStringPalette, V::tBlob } )
			row( t, "1 2 3", false, "" );
	}

	void setFromString_other()
	{
		QFETCH( int, type );
		QFETCH( QString, text );
		QFETCH( bool, ok );
		QFETCH( QString, result );

		NifValue v( typeOf( type ) );
		QCOMPARE( v.setFromString( text ), ok );

		if ( ok && !result.isEmpty() )
			QCOMPARE( v.toString(), result );
	}

	void setFromString_quat()
	{
		for ( V::Type t : { V::tQuat, V::tQuatXYZW } ) {
			NifValue q( t );
			QVERIFY( q.setFromString( "1, 2, 3, 4" ) );
			// x y z w in the text
			Quat got = q.get<Quat>();
			QCOMPARE( got[0], 4.0f );	// w
			QCOMPARE( got[1], 1.0f );	// x
			QCOMPARE( got[2], 2.0f );	// y
			QCOMPARE( got[3], 3.0f );	// z
		}
	}

	void setFromString_vectors()
	{
		NifValue v4( V::tVector4 );
		QVERIFY( v4.setFromString( "1, 2, 3, 4" ) );
		QVERIFY( v4.get<Vector4>() == Vector4( 1, 2, 3, 4 ) );

		NifValue v2( V::tVector2 );
		QVERIFY( v2.setFromString( "7, 8" ) );
		QVERIFY( v2.get<Vector2>() == Vector2( 7, 8 ) );
	}

	//! A colour is read from the text QColor takes, so a colour name and #rrggbb both work; every channel has its own value
	void setFromString_colors()
	{
		NifValue c3( V::tColor3 );
		QVERIFY( c3.setFromString( "#ff8040" ) );
		QCOMPARE( c3.get<Color3>().red(), 1.0f );
		QCOMPARE( c3.get<Color3>().green(), 128.0f / 255.0f );
		QCOMPARE( c3.get<Color3>().blue(), 64.0f / 255.0f );

		for ( V::Type t : { V::tColor4, V::tByteColor4 } ) {
			// get<Color4>() is for tColor4 only, a tByteColor4 gives a ByteColor4
			auto colorOf = [t]( const NifValue & v ) { return t == V::tColor4 ? v.get<Color4>() : Color4( v.get<ByteColor4>() ); };

			NifValue c4( t );
			QVERIFY( c4.setFromString( "#ff8040" ) );
			Color4 c = colorOf( c4 );
			QCOMPARE( c.red(), 1.0f );
			QCOMPARE( c.green(), 128.0f / 255.0f );
			QCOMPARE( c.blue(), 64.0f / 255.0f );
			QCOMPARE( c.alpha(), 1.0f );

			QVERIFY( c4.setFromString( "blue" ) );
			QCOMPARE( colorOf( c4 ).red(), 0.0f );
			QCOMPARE( colorOf( c4 ).blue(), 1.0f );
		}
	}

	//! The text of every kind of value, as a column shows it
	void toString_types_data()
	{
		QTest::addColumn<NifValue>( "value" );
		QTest::addColumn<QString>( "expected" );

		auto row = []( const char * name, const NifValue & v, const QString & expected ) { QTest::newRow( name ) << v << expected; };

		row( "tBool 0", num( V::tBool, 0 ), "no" );
		row( "tBool 1", num( V::tBool, 1 ), "yes" );
		row( "tBool 2", num( V::tBool, 2 ), "undefined" );
		row( "tBool 3", num( V::tBool, 3 ), "yes" );
		row( "tByte", num( V::tByte, 200 ), "200" );
		row( "tWord", num( V::tWord, 0xbeef ), "48879" );
		row( "tFlags", num( V::tFlags, 0x1234 ), "4660" );
		row( "tBlockTypeIndex", num( V::tBlockTypeIndex, 0x8003 ), "32771" );
		row( "tStringOffset (more than 16 bits)", num( V::tStringOffset, 0x12345678 ), "305419896" );
		row( "tUInt (more than 16 bits)", num( V::tUInt, 0x12345678 ), "305419896" );
		row( "tUInt largest", num( V::tUInt, 0xffffffff ), "4294967295" );
		row( "tULittle32 (more than 16 bits)", num( V::tULittle32, 0xdeadbeef ), "3735928559" );
		row( "tStringIndex (more than 16 bits)", num( V::tStringIndex, 0x12345678 ), "305419896" );
		row( "tStringIndex none", num( V::tStringIndex, 0xffffffff ), "4294967295" );
		row( "tShort -2", num( V::tShort, 0xfffe ), "-2" );
		row( "tShort 32767", num( V::tShort, 0x7fff ), "32767" );
		row( "tShort -32768", num( V::tShort, 0x8000 ), "-32768" );
		row( "tInt", num( V::tInt, quint32( -5 ) ), "-5" );
		row( "tInt smallest", num( V::tInt, 0x80000000 ), "-2147483648" );
		row( "tLink null", TestEnv::linkValue( V::tLink, -1 ), "-1" );
		row( "tLink", TestEnv::linkValue( V::tLink, 7 ), "7" );
		row( "tUpLink null", TestEnv::linkValue( V::tUpLink, -1 ), "-1" );
		row( "tFloat", flt( V::tFloat, 0.25f ), "0.250000" );
		row( "tFloat largest", flt( V::tFloat, 3.4028235e+38f ), "<float_max>" );
		row( "tHfloat 4 decimals", flt( V::tHfloat, 0.0625f ), "0.0625" );
		row( "tHfloat rounded", flt( V::tHfloat, 0.099975586f ), "0.1000" );

		for ( V::Type t : { V::tSizedString, V::tText, V::tShortString, V::tHeaderString, V::tLineString, V::tChar8String, V::tString } )
			row( qPrintable( QString( "string type %1" ).arg( int( t ) ) ), text( t, "some text" ), "some text" );

		// colours: #rrggbb from the 8-bit values (truncated: 0.25 is 63.75), HDR colours as numbers; one channel above 1 is enough
		row( "tColor3", of<Color3>( V::tColor3, Color3( 1.0f, 0.5f, 0.25f ) ), "#ff7f3f" );
		row( "tColor3 white", of<Color3>( V::tColor3, Color3( 1.0f, 1.0f, 1.0f ) ), "#ffffff" );
		row( "tColor3 HDR red", of<Color3>( V::tColor3, Color3( 2.0f, 0.5f, 0.5f ) ), "R 2.000 G 0.500 B 0.500" );
		row( "tColor3 HDR green", of<Color3>( V::tColor3, Color3( 0.5f, 2.0f, 0.5f ) ), "R 0.500 G 2.000 B 0.500" );
		row( "tColor3 HDR blue", of<Color3>( V::tColor3, Color3( 0.5f, 0.5f, 2.0f ) ), "R 0.500 G 0.500 B 2.000" );
		row( "tColor4", of<Color4>( V::tColor4, Color4( 1.0f, 0.5f, 0.25f, 0.75f ) ), "#ff7f3fbf" );
		row( "tColor4 opaque white", of<Color4>( V::tColor4, Color4( 1.0f, 1.0f, 1.0f, 1.0f ) ), "#ffffffff" );
		row( "tColor4 HDR red", of<Color4>( V::tColor4, Color4( 2.0f, 0.5f, 0.5f, 0.5f ) ), "R 2.000 G 0.500 B 0.500 A 0.500" );
		row( "tColor4 HDR green", of<Color4>( V::tColor4, Color4( 0.5f, 2.0f, 0.5f, 0.5f ) ), "R 0.500 G 2.000 B 0.500 A 0.500" );
		row( "tColor4 HDR blue", of<Color4>( V::tColor4, Color4( 0.5f, 0.5f, 2.0f, 0.5f ) ), "R 0.500 G 0.500 B 2.000 A 0.500" );
		row( "tColor4 HDR alpha", of<Color4>( V::tColor4, Color4( 0.5f, 0.5f, 0.5f, 2.0f ) ), "R 0.500 G 0.500 B 0.500 A 2.000" );
		{
			ByteColor4 c;
			c.setRGBA( 1.0f, 0.5f, 0.25f, 0.75f );
			row( "tByteColor4", of<ByteColor4>( V::tByteColor4, c ), "#ff7f3fbf" );
		}

		row( "tVector2", of<Vector2>( V::tVector2, Vector2( 1, 2 ) ), "X 1.000000 Y 2.000000" );
		row( "tHalfVector2", of<HalfVector2>( V::tHalfVector2, HalfVector2( 0.5f, -2 ) ), "X 0.500000 Y -2.000000" );
		row( "tVector3", of<Vector3>( V::tVector3, Vector3( 1, 2, 3 ) ), "X 1.000000 Y 2.000000 Z 3.000000" );
		row( "tHalfVector3", of<HalfVector3>( V::tHalfVector3, HalfVector3( 0.5f, -2, 4 ) ), "X 0.500000 Y -2.000000 Z 4.000000" );
		row( "tByteVector3", of<ByteVector3>( V::tByteVector3, ByteVector3( 1, -1, 1 ) ), "X 1.000000 Y -1.000000 Z 1.000000" );
		row( "tVector4", of<Vector4>( V::tVector4, Vector4( 1, 2, 3, 4 ) ), "X 1.000000 Y 2.000000 Z 3.000000 W 4.000000" );

		row( "tByteArray", bytes( V::tByteArray, "01 02 03" ), "3 bytes" );
		row( "tBlob", bytes( V::tBlob, "aa bb cc dd" ), "4 bytes" );
		row( "tStringPalette", bytes( V::tStringPalette, "61 62 63 00 64 65 00" ), "abc|de|" );
		{
			NifValue version( V::tFileVersion );
			version.setFileVersion( 0x14020007 );
			row( "tFileVersion", version, "20.2.0.7" );
		}
		{
			// 2 rows of 3 bytes (the dimensions are in the text: 6 bytes, [2 x 3])
			NifValue matrix( V::tByteMatrix );
			*matrix.get<ByteMatrix *>() = ByteMatrix( 2, 3 );
			row( "tByteMatrix 2 x 3", matrix, "6 bytes  [2 x 3]" );
		}
		row( "tTriangle", of<Triangle>( V::tTriangle, Triangle( 1, 2, 3 ) ), "1 2 3" );
		row( "tBSVertexDesc", of<BSVertexDesc>( V::tBSVertexDesc, descA() ), "Vertex | UVs | Colors" );
		row( "tNone", V(), "" );
	}

	void toString_types()
	{
		QFETCH( NifValue, value );
		QFETCH( QString, expected );

		QCOMPARE( value.toString(), expected );
	}

	//! A rotation as text: yaw, pitch and roll in degrees with two decimals; in brackets when the angles are not the only ones (gimbal lock)
	void toString_rotations()
	{
		// R = Rx(30 deg) Ry(20 deg) Rz(10 deg) with numpy (python3), and the quaternion of it
		Matrix r = matrixOf( { 0.925416589f, -0.163175911f, 0.342020154f, 0.31879577f, 0.823172927f, -0.469846308f, -0.204874128f, 0.543838143f, 0.813797653f } );
		Quat q( 0.94371438f, 0.268535823f, 0.144878119f, 0.127679437f );
		QCOMPARE( of<Matrix>( V::tMatrix, r ).toString(), QString( "Y 30.00 P 20.00 R 10.00" ) );
		QCOMPARE( of<Quat>( V::tQuat, q ).toString(), QString( "Y 30.00 P 20.00 R 10.00" ) );
		QCOMPARE( of<Quat>( V::tQuatXYZW, q ).toString(), QString( "Y 30.00 P 20.00 R 10.00" ) );

		// Rx(30 deg) Ry(90 deg): at 90 degrees of pitch yaw and roll cannot be told apart, the roll is 0 and the text is in brackets
		Matrix lock = matrixOf( { 0, 0, 1, 0.5f, 0.866025404f, 0, -0.866025404f, 0.5f, 0 } );
		QCOMPARE( of<Matrix>( V::tMatrix, lock ).toString(), QString( "(Y 30.00 P 90.00 R 0.00)" ) );
	}

	//! A 4x4 matrix as translation, rotation and scale: T( 1, 2, 3 ), Rx(30) Ry(20) Rz(10) and a scale of ( 2, 3, 4 ); each row of the
	//! rotation scaled by its own factor and the translation in the last row, computed with numpy (python3)
	void toString_matrix4()
	{
		Matrix4 m = matrix4Of( { 1.85083318f, 0.956387341f, -0.819496512f, 0.0f,
		                         -0.326351821f, 2.4695189f, 2.17535257f, 0.0f,
		                         0.684040308f, -1.40953898f, 3.25519061f, 0.0f,
		                         1.0f, 2.0f, 3.0f, 1.0f } );
		QCOMPARE( of<Matrix4>( V::tMatrix4, m ).toString(),
		          QString( "Trans( X 1.000 Y 2.000 Z 3.000 ) Rot( Y 30.000 P 20.000 R 10.000 ) Scale( X 2.000 Y 3.000 Z 4.000 )" ) );
	}

	void toColor_data()
	{
		QTest::addColumn<NifValue>( "value" );
		QTest::addColumn<bool>( "valid" );
		QTest::addColumn<int>( "red" );
		QTest::addColumn<int>( "green" );
		QTest::addColumn<int>( "blue" );
		QTest::addColumn<int>( "alpha" );

		// the channels are 0 or 1, so every value is exact in 8 bits
		QTest::newRow( "tColor3 red" ) << of<Color3>( V::tColor3, Color3( 1, 0, 0 ) ) << true << 255 << 0 << 0 << 255;
		QTest::newRow( "tColor3 green" ) << of<Color3>( V::tColor3, Color3( 0, 1, 0 ) ) << true << 0 << 255 << 0 << 255;
		QTest::newRow( "tColor3 blue" ) << of<Color3>( V::tColor3, Color3( 0, 0, 1 ) ) << true << 0 << 0 << 255 << 255;
		QTest::newRow( "tColor4 red, transparent" ) << of<Color4>( V::tColor4, Color4( 1, 0, 0, 0 ) ) << true << 255 << 0 << 0 << 0;
		QTest::newRow( "tColor4 green" ) << of<Color4>( V::tColor4, Color4( 0, 1, 0, 1 ) ) << true << 0 << 255 << 0 << 255;
		QTest::newRow( "tColor4 blue" ) << of<Color4>( V::tColor4, Color4( 0, 0, 1, 1 ) ) << true << 0 << 0 << 255 << 255;
		{
			ByteColor4 c;
			c.setRGBA( 0, 1, 0, 0 );
			QTest::newRow( "tByteColor4 green, transparent" ) << of<ByteColor4>( V::tByteColor4, c ) << true << 0 << 255 << 0 << 0;
		}
		QTest::newRow( "tInt" ) << num( V::tInt, 0xff00ff ) << false << 0 << 0 << 0 << 0;
		QTest::newRow( "tVector3" ) << of<Vector3>( V::tVector3, Vector3( 1, 0, 0 ) ) << false << 0 << 0 << 0 << 0;
		QTest::newRow( "tNone" ) << V() << false << 0 << 0 << 0 << 0;
	}

	void toColor()
	{
		QFETCH( NifValue, value );
		QFETCH( bool, valid );
		QFETCH( int, red );
		QFETCH( int, green );
		QFETCH( int, blue );
		QFETCH( int, alpha );

		QColor c = value.toColor();
		QCOMPARE( c.isValid(), valid );
		if ( valid ) {
			QCOMPARE( c.red(), red );
			QCOMPARE( c.green(), green );
			QCOMPARE( c.blue(), blue );
			QCOMPARE( c.alpha(), alpha );
		}
	}

	//! A half float vector hands out its floats as a Vector2 / Vector3, the others do not
	void get_halfVectorsAsVectors()
	{
		NifValue h2 = of<HalfVector2>( V::tHalfVector2, HalfVector2( 0.5f, -2.0f ) );
		QVERIFY( h2.get<Vector2>() == Vector2( 0.5f, -2.0f ) );
		QVERIFY( h2.isHalfVector2() );
		QVERIFY( !h2.isVector2() );

		NifValue h3 = of<HalfVector3>( V::tHalfVector3, HalfVector3( 0.5f, -2.0f, 4.0f ) );
		QVERIFY( h3.get<Vector3>() == Vector3( 0.5f, -2.0f, 4.0f ) );
		QVERIFY( h3.isHalfVector3() );
		QVERIFY( !h3.isVector3() );

		// not the same type: nothing
		QVERIFY( of<Vector3>( V::tVector3, Vector3( 1, 2, 3 ) ).get<Vector2>() == Vector2() );
		QVERIFY( of<Vector2>( V::tVector2, Vector2( 1, 2 ) ).get<Vector2>() == Vector2( 1, 2 ) );
	}

	//! toCount() is the number of a count type, and also the 32 bits of a float (a union in the value): 0 for everything else
	void toCount_countsAndFloats()
	{
		QCOMPARE( num( V::tUInt, 0xdeadbeef ).toCount(), 0xdeadbeefu );
		QCOMPARE( hex( flt( V::tFloat, 1.0f ).toCount() ), hex( 0x3f800000 ) );
		QCOMPARE( hex( flt( V::tHfloat, -2.0f ).toCount() ), hex( 0xc0000000 ) );
		QCOMPARE( TestEnv::linkValue( V::tLink, 5 ).toCount(), 0u );
		QCOMPARE( text( V::tSizedString, "5" ).toCount(), 0u );
		QCOMPARE( V().toCount(), 0u );
	}

	void typePredicates_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QString>( "predicates" );   // every is...() that is true for the type, space separated

		QTest::newRow( "tBool" ) << int( V::tBool ) << "count";
		QTest::newRow( "tUInt" ) << int( V::tUInt ) << "count";
		QTest::newRow( "tFlags" ) << int( V::tFlags ) << "count flags";
		QTest::newRow( "tFloat" ) << int( V::tFloat ) << "float";
		QTest::newRow( "tHfloat" ) << int( V::tHfloat ) << "float";
		QTest::newRow( "tLink" ) << int( V::tLink ) << "link";
		QTest::newRow( "tUpLink" ) << int( V::tUpLink ) << "link";
		QTest::newRow( "tSizedString" ) << int( V::tSizedString ) << "string";
		QTest::newRow( "tChar8String" ) << int( V::tChar8String ) << "string";
		QTest::newRow( "tString" ) << int( V::tString ) << "string";
		QTest::newRow( "tFilePath" ) << int( V::tFilePath ) << "";
		QTest::newRow( "tColor3" ) << int( V::tColor3 ) << "color";
		QTest::newRow( "tColor4" ) << int( V::tColor4 ) << "color";
		QTest::newRow( "tByteColor4" ) << int( V::tByteColor4 ) << "color";
		QTest::newRow( "tVector2" ) << int( V::tVector2 ) << "vector2";
		QTest::newRow( "tHalfVector2" ) << int( V::tHalfVector2 ) << "halfvector2";
		QTest::newRow( "tVector3" ) << int( V::tVector3 ) << "vector3";
		QTest::newRow( "tHalfVector3" ) << int( V::tHalfVector3 ) << "halfvector3";
		QTest::newRow( "tByteVector3" ) << int( V::tByteVector3 ) << "bytevector3";
		QTest::newRow( "tVector4" ) << int( V::tVector4 ) << "vector4";
		QTest::newRow( "tQuat" ) << int( V::tQuat ) << "quat";
		QTest::newRow( "tQuatXYZW" ) << int( V::tQuatXYZW ) << "quat";
		QTest::newRow( "tMatrix" ) << int( V::tMatrix ) << "matrix";
		QTest::newRow( "tMatrix4" ) << int( V::tMatrix4 ) << "matrix4";
		QTest::newRow( "tTriangle" ) << int( V::tTriangle ) << "triangle";
		QTest::newRow( "tByteArray" ) << int( V::tByteArray ) << "bytearray";
		QTest::newRow( "tStringPalette" ) << int( V::tStringPalette ) << "bytearray";
		QTest::newRow( "tBlob" ) << int( V::tBlob ) << "bytearray";
		QTest::newRow( "tByteMatrix" ) << int( V::tByteMatrix ) << "bytematrix";
		QTest::newRow( "tFileVersion" ) << int( V::tFileVersion ) << "fileversion";
		QTest::newRow( "tBSVertexDesc" ) << int( V::tBSVertexDesc ) << "";
	}

	void typePredicates()
	{
		QFETCH( int, type );
		QFETCH( QString, predicates );

		// a value that has no data of the type behind it: the predicates look at the type only
		NifValue v( typeOf( type ) );
		QStringList got;
		if ( v.isCount() ) got << "count";
		if ( v.isFlags() ) got << "flags";
		if ( v.isFloat() ) got << "float";
		if ( v.isLink() ) got << "link";
		if ( v.isString() ) got << "string";
		if ( v.isColor() ) got << "color";
		if ( v.isVector2() ) got << "vector2";
		if ( v.isHalfVector2() ) got << "halfvector2";
		if ( v.isVector3() ) got << "vector3";
		if ( v.isHalfVector3() ) got << "halfvector3";
		if ( v.isByteVector3() ) got << "bytevector3";
		if ( v.isVector4() ) got << "vector4";
		if ( v.isQuat() ) got << "quat";
		if ( v.isMatrix() ) got << "matrix";
		if ( v.isMatrix4() ) got << "matrix4";
		if ( v.isTriangle() ) got << "triangle";
		if ( v.isByteArray() ) got << "bytearray";
		if ( v.isByteMatrix() ) got << "bytematrix";
		if ( v.isFileVersion() ) got << "fileversion";

		QCOMPARE( got.join( ' ' ), predicates );
		QCOMPARE( v.isValid(), true );
		QCOMPARE( NifValue::isLink( typeOf( type ) ), predicates == "link" );
	}

	// ---- the description of an enum, and its options as names and numbers

	//! What a view shows of an enum: its name and what it is stored as, its text, and its options in a table: value, name, text
	void typeDescription_enum()
	{
		V::initialize();
		QVERIFY( V::registerEnumType( "TstDescribed", V::eDefault ) );
		QVERIFY( V::registerAlias( "TstDescribed", "uint" ) );
		V::setTypeDescription( "TstDescribed", "what it is" );
		QVERIFY( V::registerEnumOption( "TstDescribed", "TD_FIRST", 3, "the first" ) );
		QVERIFY( V::registerEnumOption( "TstDescribed", "TD_SECOND", 7, "the second" ) );

		QString expected = "<p><b>TstDescribed (uint)</b><p>what it is</p>"
		                   "<table><tr><td><table>"
		                   "<tr><td><p style='white-space:pre'>3 TD_FIRST</p></td><td><p style='white-space:pre'>the first</p></td></tr>"
		                   "<tr><td><p style='white-space:pre'>7 TD_SECOND</p></td><td><p style='white-space:pre'>the second</p></td></tr>"
		                   "</table></td></tr></table>";

		// the text is made the first time it is asked for, and kept
		QCOMPARE( V::typeDescription( "TstDescribed" ), expected );
		QCOMPARE( V::typeDescription( "TstDescribed" ), expected );
		V::initialize();
	}

	//! The options are in columns of 32
	void typeDescription_enum_columns()
	{
		V::initialize();
		QVERIFY( V::registerEnumType( "TstColumns", V::eDefault ) );
		for ( int i = 0; i < 33; i++ )
			QVERIFY( V::registerEnumOption( "TstColumns", QString( "TC_%1" ).arg( i ), quint32( i ), QString() ) );

		QStringList columns = V::typeDescription( "TstColumns" ).split( "</table></td><td><table>" );
		QCOMPARE( columns.count(), 2 );
		// the first column has the row of the outer table too
		QCOMPARE( columns[0].count( "<tr>" ), 1 + 32 );
		QCOMPARE( columns[1].count( "<tr>" ), 1 );
		QVERIFY( columns[0].contains( ">31 TC_31<" ) );
		QVERIFY( columns[1].contains( ">32 TC_32<" ) );
		V::initialize();
	}

	void enumOptionName_unknownValues()
	{
		V::initialize();
		QVERIFY( V::registerEnumType( "TstPlain", V::eDefault ) );
		QVERIFY( V::registerEnumOption( "TstPlain", "TP_ONE", 1, "" ) );
		QCOMPARE( V::enumOptionName( "TstPlain", 1 ), QString( "TP_ONE" ) );
		// a number that is no option is written as the number, in decimal
		QCOMPARE( V::enumOptionName( "TstPlain", 26 ), QString( "26" ) );
		QCOMPARE( V::enumOptionName( "TstPlain", 0 ), QString( "0" ) );
		// an enum that is not there has no names
		QCOMPARE( V::enumOptionName( "TstNoSuchEnum", 1 ), QString() );
		V::initialize();
	}

	void enumOptionValue_flagsAndUnknownOptions()
	{
		V::initialize();
		QVERIFY( V::registerEnumType( "TstFlags", V::eFlags ) );
		QVERIFY( V::registerEnumOption( "TstFlags", "TF_A", 0, "" ) );		// bit 0
		QVERIFY( V::registerEnumOption( "TstFlags", "TF_B", 2, "" ) );		// bit 2
		QVERIFY( V::registerEnumType( "TstPlain", V::eDefault ) );
		QVERIFY( V::registerEnumOption( "TstPlain", "TP_ONE", 1, "" ) );

		bool ok = false;
		QCOMPARE( V::enumOptionValue( "TstFlags", "TF_A | TF_B", &ok ), 5u );
		QVERIFY( ok );
		// a bar with nothing beside it is no option, at either end or in the middle
		ok = false;
		QCOMPARE( V::enumOptionValue( "TstFlags", "TF_A | | TF_B", &ok ), 5u );
		QVERIFY( ok );
		ok = false;
		QCOMPARE( V::enumOptionValue( "TstFlags", "TF_A |", &ok ), 1u );
		QVERIFY( ok );
		ok = false;
		QCOMPARE( V::enumOptionValue( "TstFlags", "| TF_B", &ok ), 4u );
		QVERIFY( ok );
		ok = false;
		QCOMPARE( V::enumOptionValue( "TstFlags", QString(), &ok ), 0u );
		QVERIFY( ok );
		// bits that no option has are given as a number
		ok = false;
		QCOMPARE( V::enumOptionValue( "TstFlags", "TF_A | 0x10", &ok ), 0x11u );
		QVERIFY( ok );

		// an option that is not there: 0, and not ok
		ok = true;
		QCOMPARE( V::enumOptionValue( "TstFlags", "TF_C", &ok ), 0u );
		QVERIFY( !ok );
		ok = true;
		QCOMPARE( V::enumOptionValue( "TstPlain", "TP_MISSING", &ok ), 0u );
		QVERIFY( !ok );
		ok = true;
		QCOMPARE( V::enumOptionValue( "TstNoSuchEnum", "x", &ok ), 0u );
		QVERIFY( !ok );
		QCOMPARE( V::enumOptionValue( "TstPlain", "TP_MISSING" ), 0u );
		V::initialize();
	}

	// ---- a value's data is freed when it changes type, and what the types answer to

	//! Changing the type, copying a value of another type over it, and clearing it let go of the data it had
	void changeType_freesTheOldData()
	{
		QByteArray shared( 1000, 'x' );

		V a( V::tByteArray );
		QVERIFY( a.set<QByteArray>( shared ) );
		QVERIFY( !shared.isDetached() );		// the value holds the same bytes
		a.changeType( V::tInt );
		QVERIFY( shared.isDetached() );

		V b( V::tByteArray );
		QVERIFY( b.set<QByteArray>( shared ) );
		QVERIFY( !shared.isDetached() );
		b = V( V::tFloat );
		QVERIFY( shared.isDetached() );

		V c( V::tByteArray );
		QVERIFY( c.set<QByteArray>( shared ) );
		QVERIFY( !shared.isDetached() );
		c.clear();
		QVERIFY( shared.isDetached() );
	}

	void setFromString_strings_keepBlanks()
	{
		for ( V::Type t : { V::tString, V::tSizedString, V::tText, V::tShortString, V::tHeaderString, V::tLineString, V::tChar8String } ) {
			V v( t );
			QVERIFY( v.setFromString( "  two words  " ) );
			QCOMPARE( v.get<QString>(), QString( "  two words  " ) );
		}
	}

	//! A byte read from a text is the whole count: what the value held before (a count of 16 bits or more) does not stay in the bits above it
	void setFromString_byte_clearsTheBitsAbove()
	{
		V v( V::tByte );
		QVERIFY( v.setCount( 0xabcd ) );
		QVERIFY( v.setFromString( "5" ) );
		QCOMPARE( hex( v.toCount() ), hex( 5 ) );
	}

	//! Each channel is two hex digits, the small ones with a leading 0
	void toString_color3_padsTheChannels()
	{
		V v( V::tColor3 );
		QVERIFY( v.set<Color3>( Color3( 0.02f, 0.03f, 0.04f ) ) );
		// 0.02 * 255 = 5.1, 0.03 * 255 = 7.65, 0.04 * 255 = 10.2: 05 07 0a
		QCOMPARE( v.toString(), QString( "#05070a" ) );
		QVERIFY( v.set<Color3>( Color3( 0.0f, 1.0f, 0.5f ) ) );
		QCOMPARE( v.toString(), QString( "#00ff7f" ) );
	}

	//! operator< is there for the metatype system, and is never true
	void lessThan_isNeverTrue()
	{
		V a( V::tInt ), b( V::tInt );
		QVERIFY( a.setCount( 1 ) && b.setCount( 2 ) );
		QVERIFY( !( a < b ) );
		QVERIFY( !( b < a ) );
		QVERIFY( !( a < a ) );
	}

	void toFloat_toFileVersion_ofOtherTypes()
	{
		// what is not a float is 0.0, and what is not a file version is 0
		QCOMPARE( num( V::tInt, 7 ).toFloat(), 0.0f );
		QCOMPARE( num( V::tUInt, 0x3f800000u ).toFloat(), 0.0f );
		QCOMPARE( text( V::tString, "x" ).toFloat(), 0.0f );
		QCOMPARE( hex( num( V::tInt, 7 ).toFileVersion() ), hex( 0 ) );
		QCOMPARE( hex( flt( V::tFloat, 2.5f ).toFileVersion() ), hex( 0 ) );
		QCOMPARE( hex( text( V::tString, "x" ).toFileVersion() ), hex( 0 ) );

		V version( V::tFileVersion );
		QVERIFY( version.setFileVersion( 0x14020007 ) );
		QCOMPARE( hex( version.toFileVersion() ), hex( 0x14020007 ) );
		QCOMPARE( version.toFloat(), 0.0f );
	}

	void get_matrix4_halfVector3_byteArrayPointer()
	{
		V m( V::tMatrix4 );
		QVERIFY( m.set<Matrix4>( matrix4A() ) );
		QVERIFY( m.get<Matrix4>() == matrix4A() );
		QVERIFY( !( m.get<Matrix4>() == Matrix4() ) );
		// a Matrix33 is another type
		QVERIFY( V( V::tMatrix ).get<Matrix4>() == Matrix4() );

		V h( V::tHalfVector3 );
		QVERIFY( h.set<HalfVector3>( HalfVector3( 0.5f, -1.0f, 2.0f ) ) );
		QVERIFY( h.get<HalfVector3>() == HalfVector3( 0.5f, -1.0f, 2.0f ) );
		QVERIFY( V( V::tVector3 ).get<HalfVector3>() == HalfVector3() );

		// a pointer to the bytes of the value: what is written through it is in the value
		V b( V::tByteArray );
		QByteArray * bytes = b.get<QByteArray *>();
		QVERIFY( bytes != nullptr );
		*bytes = "abc";
		QCOMPARE( b.get<QByteArray>(), QByteArray( "abc" ) );
		V blob( V::tBlob );
		QVERIFY( blob.get<QByteArray *>() != nullptr );
		V palette( V::tStringPalette );
		QVERIFY( palette.get<QByteArray *>() != nullptr );
		QVERIFY( num( V::tInt, 1 ).get<QByteArray *>() == nullptr );
	}

	void set_bool()
	{
		V b( V::tBool );
		QVERIFY( b.set<bool>( true ) );
		QCOMPARE( b.toCount(), 1u );
		QVERIFY( b.set<bool>( false ) );
		QCOMPARE( b.toCount(), 0u );
		// a bool is set to a count type and only to those
		QVERIFY( !V( V::tFloat ).set<bool>( true ) );
		QVERIFY( !V( V::tString ).set<bool>( true ) );
	}

	//! ask<T>() says whether the value is of a type that T stands for: the count types for bool, int and short, the float types for float,
	//! tVector3 only for Vector3 (the half and byte vectors have their own), the types of a kind (strings, byte arrays, quaternions) for the others
	void ask_everyType()
	{
		auto set = []( std::initializer_list<V::Type> types ) {
			QSet<int> s;
			for ( V::Type t : types )
				s << int( t );
			return s;
		};

		const QSet<int> counts = set( { V::tBool, V::tByte, V::tWord, V::tFlags, V::tStringOffset, V::tStringIndex, V::tBlockTypeIndex, V::tInt, V::tShort, V::tULittle32, V::tUInt } );
		const QSet<int> floats = set( { V::tFloat, V::tHfloat } );
		const QSet<int> strings = set( { V::tSizedString, V::tText, V::tShortString, V::tHeaderString, V::tLineString, V::tChar8String, V::tString } );
		const QSet<int> byteArrays = set( { V::tByteArray, V::tStringPalette, V::tBlob } );
		const QSet<int> quats = set( { V::tQuat, V::tQuatXYZW } );

		// 17 is no type (the string types are numbered from 14 to 20 for the tests of a range, which makes it one)
		QList<int> types;
		for ( int t = 0; t <= 43; t++ ) {
			if ( t != 17 )
				types << t;
		}
		types << int( V::tNone );

		for ( int t : types ) {
			V v( typeOf( t ) );
			auto check = [&]( const char * what, bool got, bool expected ) {
				QVERIFY2( got == expected, qPrintable( QString( "type %1 asked for %2: %3, expected %4" ).arg( t ).arg( what ).arg( got ).arg( expected ) ) );
			};

			check( "bool", v.ask<bool>(), counts.contains( t ) );
			check( "int", v.ask<int>(), counts.contains( t ) );
			check( "short", v.ask<short>(), counts.contains( t ) );
			check( "float", v.ask<float>(), floats.contains( t ) );
			check( "Matrix", v.ask<Matrix>(), t == V::tMatrix );
			check( "Matrix4", v.ask<Matrix4>(), t == V::tMatrix4 );
			check( "Quat", v.ask<Quat>(), quats.contains( t ) );
			check( "Vector4", v.ask<Vector4>(), t == V::tVector4 );
			check( "Vector3", v.ask<Vector3>(), t == V::tVector3 );
			check( "HalfVector3", v.ask<HalfVector3>(), t == V::tHalfVector3 );
			check( "ByteVector3", v.ask<ByteVector3>(), t == V::tByteVector3 );
			check( "Vector2", v.ask<Vector2>(), t == V::tVector2 );
			check( "HalfVector2", v.ask<HalfVector2>(), t == V::tHalfVector2 );
			check( "Color3", v.ask<Color3>(), t == V::tColor3 );
			check( "ByteColor4", v.ask<ByteColor4>(), t == V::tByteColor4 );
			check( "Color4", v.ask<Color4>(), t == V::tColor4 );
			check( "Triangle", v.ask<Triangle>(), t == V::tTriangle );
			check( "QString", v.ask<QString>(), strings.contains( t ) );
			check( "QByteArray", v.ask<QByteArray>(), byteArrays.contains( t ) );
		}
	}
};

REGISTER_TEST( tst_NifValue )


//! Evaluates a NifExpr with a name -> number table, like NifModelEval does with header fields
struct MapEval
{
	QHash<QString, quint32> names;

	QVariant operator()( const QVariant & v ) const
	{
		if ( v.type() == QVariant::String )
			return QVariant( names.value( v.toString(), 0 ) );
		return v;
	}
};

//! Hands names back as text (a field that holds a string) instead of as numbers
struct TextEval
{
	QHash<QString, QString> names;

	QVariant operator()( const QVariant & v ) const
	{
		if ( v.type() == QVariant::String )
			return QVariant( names.value( v.toString() ) );
		return v;
	}
};

/*! NifExpr, the evaluator behind the cond, vercond and arr1/arr2 attributes of nif.xml. The tables list the expected answers
 *  by hand (python3 for the version numbers), one operator or one shape per group of rows, and every comparison is tried
 *  below, at and above its boundary, so changing one operator or making one off-by-one in the parser or the evaluator
 *  fails the rows that use it. The modelContext_ tests at the end evaluate real nif.xml expressions on the items of a model.
 */
class tst_NifExpr final : public QObject
{
	Q_OBJECT

	// NifExpr throws a C string for unbalanced parentheses. typedef: QVERIFY_EXCEPTION_THROWN expands to catch (const T &),
	// and a literal "const char *" gives a duplicate const that GCC rejects
	typedef const char * CString;

	static bool eval( const QString & expr, const MapEval & env ) { return NifExpr( expr ).evaluateBool( env ); }

	//! The expression as a 32-bit unsigned number (a bool is 0 or 1), which is what the arithmetic operators work in
	static quint32 value( const QString & expr, const MapEval & env ) { return NifExpr( expr ).evaluateValue( env ).toUInt(); }

	//! A header version number from its four parts, 20.2.0.7 is ver( 20, 2, 0, 7 ) = 0x14020007
	static quint32 ver( int a, int b, int c, int d )
	{
		return ( quint32( a ) << 24 ) | ( quint32( b ) << 16 ) | ( quint32( c ) << 8 ) | quint32( d );
	}

	//! "Version=..;User Version=..;User Version 2=.." for a header
	static QString header( quint32 version, quint32 userVersion, quint32 userVersion2 )
	{
		return QString( "Version=%1;User Version=%2;User Version 2=%3" ).arg( version ).arg( userVersion ).arg( userVersion2 );
	}

	//! The names every row can use. vars ("Name=5;Other Name=0x10") adds names or replaces their values.
	static MapEval makeEnv( const QString & vars = QString() )
	{
		MapEval env;
		env.names["A"] = 5;
		env.names["B"] = 3;
		env.names["C"] = 2;
		env.names["Zero"] = 0;
		env.names["Big"] = 0xFFFFFFFFu;
		env.names["Flags"] = 0x1004;
		env.names["Version"] = ver( 20, 2, 0, 7 );
		env.names["User Version"] = 12;
		env.names["User Version 2"] = 83;

		for ( const QString & assignment : vars.split( ';', Qt::SkipEmptyParts ) ) {
			bool ok = false;
			quint32 v = assignment.section( '=', 1 ).toUInt( &ok, 0 );
			if ( !ok )
				qFatal( "bad variable in a test row: %s", qPrintable( assignment ) );
			env.names[assignment.section( '=', 0, 0 )] = v;
		}

		return env;
	}

	//! What a comparison answers when the left operand is below, equal to or above the right one
	struct Relation
	{
		const char * op;
		bool less, equal, greater;

		bool expects( int relation ) const { return relation < 0 ? less : ( relation == 0 ? equal : greater ); }
	};

	static QList<Relation> relations()
	{
		return QList<Relation>()
			<< Relation{ "==", false, true, false }
			<< Relation{ "!=", true, false, true }
			<< Relation{ ">=", false, true, true }
			<< Relation{ ">", false, false, true }
			<< Relation{ "<=", true, true, false }
			<< Relation{ "<", true, false, false };
	}

	// Row helpers: tables of (expression, variables, expected). note replaces the variables in the row name.
	static void addBoolColumns()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<QString>( "vars" );
		QTest::addColumn<bool>( "expected" );
	}

	static void boolRow( const QString & expr, const QString & vars, bool expected, const QString & note = QString() )
	{
		QString what = note.isEmpty() ? vars : note;
		QTest::newRow( qPrintable( what.isEmpty() ? expr : expr + " [" + what + "]" ) ) << expr << vars << expected;
	}

	static void addValueColumns()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<QString>( "vars" );
		QTest::addColumn<quint32>( "expected" );
	}

	static void valueRow( const QString & expr, const QString & vars, quint32 expected )
	{
		QTest::newRow( qPrintable( vars.isEmpty() ? expr : expr + " [" + vars + "]" ) ) << expr << vars << expected;
	}

	void checkBool()
	{
		QFETCH( QString, expr );
		QFETCH( QString, vars );
		QFETCH( bool, expected );

		QCOMPARE( eval( expr, makeEnv( vars ) ), expected );
	}

	void checkValue()
	{
		QFETCH( QString, expr );
		QFETCH( QString, vars );
		QFETCH( quint32, expected );

		QCOMPARE( value( expr, makeEnv( vars ) ), expected );
	}

private slots:
	void initTestCase()
	{
		// Only the model tests at the end need nif.xml; reloading is cheap
		QString err = TestEnv::reloadXml();
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
	}

	void cleanup()
	{
		// Nothing here may pop up a QMessageBox (the app's way of reporting load/save problems)
		QStringList boxes = TestEnv::takeMessageBoxes();
		QVERIFY2( boxes.isEmpty(), qPrintable( boxes.join( " | " ) ) );
	}

	void versionConditions_data()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<quint32>( "version" );
		QTest::addColumn<quint32>( "userVersion" );
		QTest::addColumn<bool>( "expected" );

		// Spelled the way nif.xml does it (compare the Header compound)
		QString f4 = "(Version == 20.2.0.7) && (User Version >= 12)";
		QTest::newRow( "and, true" ) << f4 << 0x14020007u << 12u << true;
		QTest::newRow( "and, false right" ) << f4 << 0x14020007u << 11u << false;
		QTest::newRow( "and, false left" ) << f4 << 0x14000005u << 12u << false;
		QTest::newRow( ">= version" ) << "Version >= 10.0.1.2" << 0x0A000102u << 0u << true;
		QTest::newRow( ">= version, below" ) << "Version >= 10.0.1.2" << 0x0A000100u << 0u << false;
		QTest::newRow( "<= version" ) << "Version <= 20.0.0.4" << 0x14000005u << 0u << false;
		QTest::newRow( "or" ) << "(User Version == 3) || (User Version == 12)" << 0u << 12u << true;
		QTest::newRow( "not equal" ) << "User Version != 11" << 0u << 12u << true;
		QTest::newRow( "negation" ) << "!User Version" << 0u << 0u << true;
		QTest::newRow( "negation, set" ) << "!User Version" << 0u << 5u << false;
		QTest::newRow( "hex literal" ) << "User Version == 0xC" << 0u << 12u << true;
		QTest::newRow( "nested groups" )
			<< "((Version == 20.2.0.7) || (Version == 20.0.0.5)) && (User Version >= 3)" << 0x14000005u << 11u << true;
		QTest::newRow( "empty is nop" ) << "" << 0u << 0u << false;
	}

	void versionConditions()
	{
		QFETCH( QString, expr );
		QFETCH( quint32, version );
		QFETCH( quint32, userVersion );
		QFETCH( bool, expected );

		MapEval env;
		env.names["Version"] = version;
		env.names["User Version"] = userVersion;

		QCOMPARE( eval( expr, env ), expected );
	}

	void arithmeticAndBits()
	{
		MapEval env;
		env.names["Num Vertices"] = 3;
		env.names["Flags"] = 0x1000;

		QCOMPARE( NifExpr( "Num Vertices * 3" ).evaluateUInt( env ), 9 );
		QCOMPARE( NifExpr( "Num Vertices + 4" ).evaluateUInt( env ), 7 );
		QCOMPARE( NifExpr( "Num Vertices - 1" ).evaluateUInt( env ), 2 );
		QCOMPARE( NifExpr( "Flags & 4096" ).evaluateUInt( env ), 4096 );
		QCOMPARE( NifExpr( "Flags | 1" ).evaluateUInt( env ), 4097 );
		QCOMPARE( NifExpr( "(Flags & 4096) != 0" ).evaluateBool( env ), true );
	}

	void toString_roundTrip()
	{
		NifExpr e( "(Version >= 10.0.1.2) && (User Version == 11)" );
		// parsing the printed form gives the same truth table
		NifExpr again( e.toString() );

		MapEval env;
		for ( quint32 v : { 0x0A000100u, 0x0A000102u, 0x14000005u } ) {
			for ( quint32 uv : { 0u, 11u, 12u } ) {
				env.names["Version"] = v;
				env.names["User Version"] = uv;
				QCOMPARE( again.evaluateBool( env ), e.evaluateBool( env ) );
			}
		}
	}

	void unbalancedParentheses_throw()
	{
		// nifxml.cpp relies on well-formed input; malformed expressions throw a C string rather than failing softly
		QVERIFY_EXCEPTION_THROWN( NifExpr( "(Version == 20.2.0.7" ), CString );
	}

	//! The six comparisons on names and on decimal, hex and unsigned operands, left below, equal to and above the right operand
	void comparisons_data()
	{
		addBoolColumns();

		for ( const Relation & r : relations() ) {
			QString op = r.op;

			// A is 4, 5 and 6 against 5, so a <= that became < or a > that became >= fails on the equal row
			for ( int a = 4; a <= 6; a++ ) {
				QString vars = QString( "A=%1" ).arg( a );
				int relation = a - 5;

				boolRow( "A " + op + " 5", vars, r.expects( relation ) );
				boolRow( "5 " + op + " A", vars, r.expects( -relation ) );	// the literal on the left mirrors the relation
				boolRow( "A " + op + " B", vars + ";B=5", r.expects( relation ) );
				boolRow( "A " + op + " 0x5", vars, r.expects( relation ) );
			}

			// 32-bit unsigned: 0xFFFFFFFF is above 1, not -1 below it
			boolRow( "Big " + op + " 1", QString(), r.expects( 1 ) );
			boolRow( "Zero " + op + " Big", QString(), r.expects( -1 ) );
		}
	}

	void comparisons() { checkBool(); }

	//! Version literals ("20.2.0.7") against a header version one step below, at and above them, in every byte of the number
	void versionLiterals_data()
	{
		addBoolColumns();

		// header version a.b.c.d and how it compares with the literal (-1 below, 0 equal, 1 above)
		struct Probe { int a, b, c, d, relation; };

		auto literal = [&]( const QString & text, const QList<Probe> & probes, bool mirrored ) {
			for ( const Probe & p : probes ) {
				QString vars = QString( "Version=%1" ).arg( ver( p.a, p.b, p.c, p.d ) );
				QString note = QString( "Version is %1.%2.%3.%4" ).arg( p.a ).arg( p.b ).arg( p.c ).arg( p.d );

				for ( const Relation & r : relations() ) {
					QString op = r.op;
					boolRow( "Version " + op + " " + text, vars, r.expects( p.relation ), note );
					if ( mirrored )
						boolRow( text + " " + op + " Version", vars, r.expects( -p.relation ), note );
				}
			}
		};

		// every byte of the version: a smaller low byte must not hide a bigger high byte, and the other way round
		literal( "20.2.0.7", QList<Probe>()
			<< Probe{ 20, 2, 0, 6, -1 } << Probe{ 20, 2, 0, 7, 0 } << Probe{ 20, 2, 0, 8, 1 }
			<< Probe{ 20, 2, 0, 0, -1 } << Probe{ 20, 2, 1, 0, 1 }
			<< Probe{ 20, 1, 255, 255, -1 } << Probe{ 20, 3, 0, 0, 1 }
			<< Probe{ 19, 255, 255, 255, -1 } << Probe{ 21, 0, 0, 0, 1 }, true );
		// the boundaries other conditions in nif.xml use: a two digit and a three digit last part, and the first 4.0 version
		literal( "3.3.0.13", QList<Probe>() << Probe{ 3, 3, 0, 12, -1 } << Probe{ 3, 3, 0, 13, 0 } << Probe{ 3, 3, 0, 14, 1 }, false );
		literal( "4.0.0.2", QList<Probe>() << Probe{ 4, 0, 0, 1, -1 } << Probe{ 4, 0, 0, 2, 0 } << Probe{ 4, 0, 0, 3, 1 }, false );
		literal( "10.1.0.114", QList<Probe>() << Probe{ 10, 1, 0, 113, -1 } << Probe{ 10, 1, 0, 114, 0 } << Probe{ 10, 1, 0, 115, 1 }, false );
	}

	void versionLiterals() { checkBool(); }

	//! How a bare number is read: decimal, hex (0x..), or a dotted version, and what each of them is worth
	void literals_data()
	{
		addValueColumns();

		// computed with python3: (a << 24) | (b << 16) | (c << 8) | d
		valueRow( "20.2.0.7", QString(), 335675399 );	// 0x14020007
		valueRow( "20.0.0.5", QString(), 335544325 );	// 0x14000005
		valueRow( "4.0.0.2", QString(), 67108866 );		// 0x04000002
		valueRow( "3.3.0.13", QString(), 50528269 );	// 0x0303000D
		valueRow( "10.1.0.114", QString(), 167837810 );	// 0x0A010072
		valueRow( "1.2.3.4", QString(), 16909060 );		// 0x01020304: bytes in order, not reversed

		valueRow( "10", QString(), 10 );
		valueRow( "010", QString(), 10 );				// decimal, not octal
		valueRow( "7", QString(), 7 );
		valueRow( "0", QString(), 0 );
		valueRow( "0x10", QString(), 16 );
		valueRow( "0xFF", QString(), 255 );
		valueRow( "0Xa", QString(), 10 );
		valueRow( "0xFFFFFFFF", QString(), 4294967295u );

		// the same number three ways is the same number
		valueRow( "20.2.0.7 == 0x14020007", QString(), 1 );
		valueRow( "20.2.0.7 == 335675399", QString(), 1 );
		valueRow( "0x14020007 == 335675399", QString(), 1 );
		// and a hex number is not read as decimal digits
		valueRow( "0x10 == 16", QString(), 1 );
		valueRow( "10 == 0x10", QString(), 0 );
		valueRow( "0x10 == 10", QString(), 0 );
	}

	void literals() { checkValue(); }

	//! + - * / & | on names and literals: every operator gives a different answer for 12 and 10
	void arithmetic_data()
	{
		addValueColumns();

		// 12 and 10 are 0b1100 and 0b1010: sum 22, difference 2, product 120, quotient 1, and 8, or 14
		valueRow( "12 + 10", QString(), 22 );
		valueRow( "A + B", "A=12;B=10", 22 );
		valueRow( "12 - 10", QString(), 2 );
		valueRow( "A - B", "A=12;B=10", 2 );
		valueRow( "12 * 10", QString(), 120 );
		valueRow( "A * B", "A=12;B=10", 120 );
		valueRow( "12 / 10", QString(), 1 );
		valueRow( "A / B", "A=12;B=10", 1 );
		valueRow( "12 & 10", QString(), 8 );
		valueRow( "A & B", "A=12;B=10", 8 );
		valueRow( "12 | 10", QString(), 14 );
		valueRow( "A | B", "A=12;B=10", 14 );

		// the left operand is the left operand
		valueRow( "10 - 12", QString(), 4294967294u );
		valueRow( "B - A", "A=12;B=10", 4294967294u );
		valueRow( "10 / 12", QString(), 0 );
		valueRow( "B / A", "A=12;B=10", 0 );
		valueRow( "100 / 7", QString(), 14 );
		valueRow( "9 / 2", QString(), 4 );				// truncated, not rounded
		valueRow( "7 / 1", QString(), 7 );
		valueRow( "Zero / A", QString(), 0 );
		valueRow( "A / C", QString(), 2 );
		valueRow( "A * 0", QString(), 0 );
		valueRow( "0xF0 & 0x3C", QString(), 0x30 );
		valueRow( "0xF0 | 0x0F", QString(), 0xFF );

		// unsigned 32-bit: there is no negative number
		valueRow( "Zero - 1", QString(), 4294967295u );
		valueRow( "Big + 1", QString(), 0 );
		valueRow( "Big - 1", QString(), 4294967294u );
		valueRow( "Big * 2", QString(), 4294967294u );
		valueRow( "0xFFFFFFFF & 0xFF00", QString(), 0xFF00 );

		// flag fields, with Flags = 0x1004
		valueRow( "Flags & 4096", QString(), 4096 );
		valueRow( "Flags & 4", QString(), 4 );
		valueRow( "Flags & 2", QString(), 0 );
		valueRow( "Flags | 1", QString(), 0x1005 );
		valueRow( "(Flags & 4096) | (Flags & 4)", QString(), 0x1004 );
		valueRow( "(Flags & 4096) | (Flags & 2)", QString(), 0x1000 );
	}

	void arithmetic() { checkValue(); }

	//! && and || on truth values and on numbers, ! on names and groups
	void logical_data()
	{
		addBoolColumns();

		for ( int a = 0; a <= 1; a++ ) {
			for ( int b = 0; b <= 1; b++ ) {
				QString vars = QString( "A=%1;B=%2" ).arg( a ).arg( b );
				boolRow( "A && B", vars, a && b );
				boolRow( "A || B", vars, a || b );
			}
		}

		// true numbers that share no bit: & would give 0 where && gives true
		boolRow( "A && B", "A=2;B=1", true );
		boolRow( "A && B", "A=2;B=4", true );
		boolRow( "(A && B) == 1", "A=2;B=1", true );
		boolRow( "(A || B) == 1", "A=2;B=1", true );		// a bool, not 2 | 1
		boolRow( "(A || B) == 1", "A=0;B=0", false );

		// comparisons are bools: they combine with && || == != and with the numbers 0 and 1
		boolRow( "(A > B) && (B > C)", QString(), true );
		boolRow( "(A > B) && (C > B)", QString(), false );
		boolRow( "(C > B) && (A > B)", QString(), false );
		boolRow( "(C > B) || (B > C)", QString(), true );
		boolRow( "(C > B) || (B > A)", QString(), false );
		boolRow( "(A > B) == (B > C)", QString(), true );
		boolRow( "(A > B) == (C > B)", QString(), false );
		boolRow( "(A > B) != (C > B)", QString(), true );
		boolRow( "(A > B) == 1", QString(), true );
		boolRow( "(A > B) == 2", QString(), false );			// 1 is not 2, whatever a bool says
		boolRow( "(C > B) == 0", QString(), true );
		boolRow( "(A > B) == 0", QString(), false );
	}

	void logical() { checkBool(); }

	void negation_data()
	{
		addBoolColumns();

		boolRow( "!A", QString(), false );
		boolRow( "!Zero", QString(), true );
		boolRow( "! A", QString(), false );
		boolRow( "  !Zero", QString(), true );
		boolRow( "!!A", QString(), true );
		boolRow( "!!Zero", QString(), false );
		boolRow( "!!!A", QString(), false );
		boolRow( "!User Version 2", "User Version 2=0", true );
		boolRow( "!User Version 2", QString(), false );

		boolRow( "!(A == 5)", QString(), false );
		boolRow( "!(A == 4)", QString(), true );
		boolRow( "!(A > B)", QString(), false );
		boolRow( "!(A < B)", QString(), true );
		boolRow( "!(A <= 5)", QString(), false );
		boolRow( "!(A >= 6)", QString(), true );
		boolRow( "!!(A == 5)", QString(), true );
		boolRow( "!(Version == 20.2.0.7)", QString(), false );
		boolRow( "!(Version >= 20.2.0.7)", QString(), false );
		boolRow( "!(Version < 20.2.0.7)", QString(), true );

		// the shape nif.xml uses most: Vector Flags exists unless a Bethesda 20.2.0.7 file has a User Version 2
		QString f = "!((Version == 20.2.0.7) && (User Version 2 > 0))";
		boolRow( f, header( ver( 20, 2, 0, 7 ), 12, 83 ), false, "Skyrim" );
		boolRow( f, header( ver( 20, 2, 0, 7 ), 12, 1 ), false, "User Version 2 is 1" );
		boolRow( f, header( ver( 20, 2, 0, 7 ), 12, 0 ), true, "User Version 2 is 0" );
		boolRow( f, header( ver( 20, 0, 0, 5 ), 11, 11 ), true, "Oblivion" );
		boolRow( f, header( ver( 20, 2, 0, 6 ), 12, 83 ), true, "one version before 20.2.0.7" );
		boolRow( "!((Version == 20.2.0.7) && (User Version 2 > 100))", header( ver( 20, 2, 0, 7 ), 12, 83 ), true, "User Version 2 is 83" );
		boolRow( "!((Version == 20.2.0.7) && (User Version 2 > 100))", header( ver( 20, 2, 0, 7 ), 12, 101 ), false, "User Version 2 is 101" );
	}

	void negation() { checkBool(); }

	//! Parentheses: around a whole expression, around either operand, nested
	void grouping_data()
	{
		addValueColumns();

		valueRow( "(A)", QString(), 5 );
		valueRow( "((A))", QString(), 5 );
		valueRow( "(((A)))", QString(), 5 );
		valueRow( "(A) == 5", QString(), 1 );
		valueRow( "A == (5)", QString(), 1 );
		valueRow( "(A) == (5)", QString(), 1 );
		valueRow( "(A == 5)", QString(), 1 );
		valueRow( "((A == 5))", QString(), 1 );
		valueRow( "( A == 5 )", QString(), 1 );
		valueRow( "(A == 4)", QString(), 0 );

		// grouping decides what is calculated first
		valueRow( "(A + B) * C", QString(), 16 );
		valueRow( "A * (B + C)", QString(), 25 );
		valueRow( "(A + B) * (B + C)", QString(), 40 );
		valueRow( "((A + B) * C) + 1", QString(), 17 );
		valueRow( "A * (B + (C + 1))", QString(), 30 );
		valueRow( "(A - B) - C", QString(), 0 );
		valueRow( "A - (B - C)", QString(), 4 );
		valueRow( "(8 - 4) - 2", QString(), 2 );
		valueRow( "8 - (4 - 2)", QString(), 6 );
		valueRow( "(12 / 4) / 3", QString(), 1 );
		valueRow( "12 / (4 / 3)", QString(), 12 );
		valueRow( "(1 + 2) * 3", QString(), 9 );
		valueRow( "1 + (2 * 3)", QString(), 7 );
		valueRow( "(2 * 3) + 1", QString(), 7 );
		valueRow( "2 * (3 + 1)", QString(), 8 );
		valueRow( "(A & B) | C", QString(), 3 );		// (5 & 3) | 2
		valueRow( "A & (B | C)", QString(), 1 );		// 5 & (3 | 2)
		valueRow( "((A == 5) && (B == 3)) && (C == 2)", QString(), 1 );
		valueRow( "(A == 5) && ((B == 3) && (C == 1))", QString(), 0 );
	}

	void grouping() { checkValue(); }

	//! Three and more operands joined by the same operator
	void chains_data()
	{
		addValueColumns();

		valueRow( "(A == 5) && (B == 3) && (C == 2)", QString(), 1 );
		valueRow( "(A == 5) && (B == 3) && (C == 1)", QString(), 0 );
		valueRow( "(A == 4) && (B == 3) && (C == 2)", QString(), 0 );
		valueRow( "(A == 5) && (B == 4) && (C == 2)", QString(), 0 );
		valueRow( "(A == 4) || (B == 4) || (C == 2)", QString(), 1 );
		valueRow( "(A == 4) || (B == 4) || (C == 1)", QString(), 0 );
		valueRow( "(A == 5) || (B == 4) || (C == 1)", QString(), 1 );
		valueRow( "A + B + C", QString(), 10 );
		valueRow( "A * B * C", QString(), 30 );
		valueRow( "1 + 2 + 3 + 4", QString(), 10 );
		valueRow( "A | B | C", "A=1;B=2;C=4", 7 );
		valueRow( "A & B & C", "A=7;B=6;C=3", 2 );
		valueRow( "Tri Count LOD0 + Tri Count LOD1 + Tri Count LOD2", "Tri Count LOD0=3;Tri Count LOD1=5;Tri Count LOD2=9", 17 );
	}

	void chains() { checkValue(); }

	/*! NifExpr has no operator precedence and no associativity rule: the text is split at its first operator and the rest is
	 *  parsed on its own, so every chain groups to the right. nif.xml parenthesises everything that matters (the shipped
	 *  conditions give the conventional answer), so this is pinned, not fixed. If NifExpr ever gets real precedence these
	 *  rows change, the comment on each says what C would give.
	 */
	void precedence_flatAndRightAssociative_data()
	{
		addValueColumns();

		// subtraction and division are not associative
		valueRow( "A - B - C", QString(), 4 );				// 5 - (3 - 2), C gives 0
		valueRow( "8 - 4 - 2", QString(), 6 );				// 8 - (4 - 2), C gives 2
		valueRow( "12 / 4 / 3", QString(), 12 );			// 12 / (4 / 3), C gives 1
		// no * before +
		valueRow( "A * B + C", QString(), 25 );				// 5 * (3 + 2), C gives 17
		valueRow( "2 * 3 + 1", QString(), 8 );				// 2 * (3 + 1), C gives 7
		valueRow( "1 + 2 * 3", QString(), 7 );				// 1 + (2 * 3), the same as C
		valueRow( "A + B * C", QString(), 11 );				// 5 + (3 * 2), the same as C
		// nor & before |
		valueRow( "A & B | C", QString(), 1 );				// 5 & (3 | 2), C gives 3
		valueRow( "A | B & C", QString(), 7 );				// 5 | (3 & 2), the same as C
		// arithmetic before comparison is also only by grouping
		valueRow( "A + B == C", QString(), 5 );				// 5 + (3 == 2), C gives 0
		valueRow( "A == B + C", QString(), 1 );				// 5 == (3 + 2), the same as C
		// no && before ||
		valueRow( "A || Zero && Zero", QString(), 1 );		// A || (0 && 0), the same as C
		valueRow( "Zero && Zero || A", QString(), 0 );		// 0 && (0 || 5), C gives 1
		valueRow( "Zero && B || A", QString(), 0 );			// 0 && (3 || 5), C gives 1
		// comparisons are operands of the operator after them
		valueRow( "A == 5 && B == 3", QString(), 0 );		// 5 == (5 && (3 == 3)) = 5 == 1, C gives 1
		valueRow( "A == 5 && B == 4", QString(), 0 );		// 5 == (5 && (3 == 4)) = 5 == 0, the same as C
		// ! takes everything to its right
		valueRow( "!(A == 5) && (B == 4)", QString(), 1 );	// !((5 == 5) && (3 == 4)), C gives 0
		valueRow( "!(A == 5) && (B == 3)", QString(), 0 );	// !((5 == 5) && (3 == 3)), the same as C
		valueRow( "!A == 0", QString(), 1 );				// !(5 == 0), the same as C
	}

	void precedence_flatAndRightAssociative() { checkValue(); }

	void names_data()
	{
		addValueColumns();

		// names may contain spaces and digits, and the operator splits them off
		valueRow( "User Version", QString(), 12 );
		valueRow( "User Version 2", QString(), 83 );
		valueRow( "User Version 2 > User Version", QString(), 1 );
		valueRow( "User Version == 12", QString(), 1 );
		valueRow( "User Version 2 == 83", QString(), 1 );
		valueRow( "Tri Count LOD0 * 2", "Tri Count LOD0=7", 14 );
		valueRow( "Num Pixels * Num Faces", "Num Pixels=16;Num Faces=6", 96 );

		// a name nobody knows is 0, whatever it looks like
		valueRow( "Unknown Name", QString(), 0 );
		valueRow( "Unknown Name == 0", QString(), 1 );
		valueRow( "Unknown Name > 0", QString(), 0 );
		valueRow( "Unknown Name >= 0", QString(), 1 );
		valueRow( "!Unknown Name", QString(), 1 );
		valueRow( "Unknown Name + 3", QString(), 3 );
		valueRow( "Unknown Name == Another Unknown", QString(), 1 );
		valueRow( "A == Unknown Name", QString(), 0 );
		valueRow( "A != Unknown Name", QString(), 1 );

		// blanks around operands and operators do not matter
		valueRow( "  A   ==   5  ", QString(), 1 );
		valueRow( "A    +    B", QString(), 8 );
		valueRow( "(  A  )  ==  5", QString(), 1 );
		valueRow( "  (A == 5)  &&  (B == 3)  ", QString(), 1 );
	}

	void names() { checkValue(); }

	/*! nifexpr.cpp (rstartpos = oendpos + 1) assumes exactly one character between the operator and the right operand, so
	 *  without a space the first character of the operand is dropped and A==5 compares A with nothing. nif.xml has one such
	 *  condition, (Flags & 2)!=0 on the Bone Bounds of NiSkinningMeshModifier, which is therefore true for every Flags.
	 *  The rows that are wrong today are expected failures: once the parser is fixed they pass unexpectedly, which fails the
	 *  test. Then remove the QEXPECT_FAIL below (and the brokenToday column).
	 */
	void noSpaceAfterOperator_data()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<QString>( "vars" );
		QTest::addColumn<bool>( "expected" );
		QTest::addColumn<bool>( "brokenToday" );

		// with a space they are right
		QTest::newRow( "A == 5" ) << "A == 5" << "" << true << false;
		QTest::newRow( "(A & 4) == 4" ) << "(A & 4) == 4" << "" << true << false;
		QTest::newRow( "(Flags & 2) != 0, bit clear, with spaces" ) << "(Flags & 2) != 0" << "Flags=0" << false << false;

		// without one they are not
		QTest::newRow( "A==5" ) << "A==5" << "" << true << true;
		QTest::newRow( "(A)==5" ) << "(A)==5" << "" << true << true;
		QTest::newRow( "(A&4)==4" ) << "(A&4)==4" << "" << true << true;
		QTest::newRow( "A!=5" ) << "A!=5" << "" << false << true;
		QTest::newRow( "A>=6" ) << "A>=6" << "" << false << true;
		QTest::newRow( "(A == 5)&&(B == 3)" ) << "(A == 5)&&(B == 3)" << "" << true << true;

		// the real one: Bone Bounds are read only if bit 1 of Flags (RECOMPUTE_BOUNDS) is set, today they always are
		QTest::newRow( "(Flags & 2)!=0, Flags 0" ) << "(Flags & 2)!=0" << "Flags=0" << false << true;
		QTest::newRow( "(Flags & 2)!=0, Flags 1" ) << "(Flags & 2)!=0" << "Flags=1" << false << true;
		QTest::newRow( "(Flags & 2)!=0, Flags 2" ) << "(Flags & 2)!=0" << "Flags=2" << true << false;
		QTest::newRow( "(Flags & 2)!=0, Flags 3" ) << "(Flags & 2)!=0" << "Flags=3" << true << false;
	}

	void noSpaceAfterOperator()
	{
		QFETCH( QString, expr );
		QFETCH( QString, vars );
		QFETCH( bool, expected );
		QFETCH( bool, brokenToday );

		if ( brokenToday )
			QEXPECT_FAIL( "", "nifexpr.cpp: the first character of the right operand is dropped when no space follows the operator", Continue );
		QCOMPARE( eval( expr, makeEnv( vars ) ), expected );
	}

	void malformed_throws_data()
	{
		QTest::addColumn<QString>( "expr" );

		// every shape of a ( without its )
		QTest::newRow( "open group" ) << "(A == 5";
		QTest::newRow( "open group, nested" ) << "((A == 5)";
		QTest::newRow( "open group after &&" ) << "(A == 5) && (B == 3";
		QTest::newRow( "open group as right operand" ) << "A && (B == 3";
		QTest::newRow( "open group after !" ) << "!(A == 5";
		QTest::newRow( "open group, version" ) << "(Version == 20.2.0.7";
		QTest::newRow( "lone (" ) << "(";
	}

	void malformed_throws()
	{
		QFETCH( QString, expr );

		QVERIFY_EXCEPTION_THROWN( NifExpr e( expr ), CString );
	}

	/*! Other malformed input is not reported, it evaluates to something. Pinned as it is today, like the missing precedence:
	 *  there are no such expressions in nif.xml, and a stricter parser would make these rows fail.
	 */
	void malformed_degradesQuietly_data()
	{
		addValueColumns();

		// a ) without a ( is ignored or ends up in the right operand
		valueRow( "A == 5)", QString(), 0 );
		valueRow( "(A == 5))", QString(), 1 );
		// an operator without an operand: the missing operand is nothing, which is 0 in arithmetic and false in logic
		valueRow( "A ==", QString(), 0 );
		valueRow( "A &&", QString(), 0 );
		valueRow( "== 5", QString(), 0 );
		valueRow( "&&", QString(), 0 );
		valueRow( "A +", QString(), 5 );
		valueRow( "!", QString(), 1 );					// not (nothing)
		valueRow( "()", QString(), 0 );
		// words without an operator are one (unknown) name
		valueRow( "A B", QString(), 0 );
		valueRow( "A = 5", QString(), 0 );
		// there are no fractional literals: 1.5 is a name like any other
		valueRow( "1.5", QString(), 0 );
	}

	void malformed_degradesQuietly()
	{
		QFETCH( QString, expr );
		QFETCH( QString, vars );
		QFETCH( quint32, expected );

		try {
			QCOMPARE( value( expr, makeEnv( vars ) ), expected );
		} catch ( CString message ) {
			QFAIL( qPrintable( QString( "threw: " ) + message ) );
		}
	}

	//! The printed form: every operator, nesting, and numbers as their value. Wrong text would also change what the printed form parses to.
	void toString_data()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<QString>( "expected" );

		for ( const char * op : { "!=", "==", ">=", "<=", ">", "<", "&", "|", "+", "-", "/", "*", "&&", "||" } )
			QTest::newRow( qPrintable( QString( "A %1 B" ).arg( op ) ) ) << QString( "A %1 B" ).arg( op ) << QString( "(A %1 B)" ).arg( op );

		QTest::newRow( "name" ) << "A" << "A";
		QTest::newRow( "name with spaces" ) << "User Version 2" << "User Version 2";
		QTest::newRow( "empty" ) << "" << "";
		QTest::newRow( "group around a name" ) << "(A)" << "A";
		QTest::newRow( "group around an expression" ) << "((A == 5))" << "(A == 5)";
		QTest::newRow( "not" ) << "!A" << "!A";
		QTest::newRow( "not not" ) << "!!A" << "!!A";
		QTest::newRow( "not of a group" ) << "!(A == 5)" << "!(A == 5)";
		QTest::newRow( "two groups" ) << "(A == 5) && (B == 3)" << "((A == 5) && (B == 3))";
		QTest::newRow( "group first" ) << "(A + B) * C" << "((A + B) * C)";
		QTest::newRow( "right associative chain" ) << "A + B + C" << "(A + (B + C))";
		QTest::newRow( "version literal is its number" ) << "Version >= 20.2.0.7" << "(Version >= 335675399)";
		QTest::newRow( "hex literal is its number" ) << "A == 0x5" << "(A == 5)";
		QTest::newRow( "lone hex literal" ) << "0x10" << "16";
		QTest::newRow( "hex literal is unsigned" ) << "0xFFFFFFFF" << "4294967295";
		QTest::newRow( "nif.xml condition" )
			<< "!((Version == 20.2.0.7) && (User Version 2 > 0))" << "!((Version == 335675399) && (User Version 2 > 0))";
	}

	void toString()
	{
		QFETCH( QString, expr );
		QFETCH( QString, expected );

		QCOMPARE( NifExpr( expr ).toString(), expected );
	}

	//! Conditions copied from nif.xml, on the values a file can have
	void nifXmlConditions_data()
	{
		addBoolColumns();

		// the header's User Version 2: 20.2.0.7 and 20.0.0.5, or an old file with User Version at most 11, and a User Version of 3 or more
		QString uv2 = "((Version == 20.2.0.7) || (Version == 20.0.0.5) || ((Version >= 10.0.1.2) && (Version <= 20.0.0.4) && (User Version <= 11))) && (User Version >= 3)";
		boolRow( uv2, header( ver( 20, 2, 0, 7 ), 12, 0 ), true, "20.2.0.7, User Version 12" );
		boolRow( uv2, header( ver( 20, 2, 0, 7 ), 3, 0 ), true, "20.2.0.7, User Version 3" );
		boolRow( uv2, header( ver( 20, 2, 0, 7 ), 2, 0 ), false, "20.2.0.7, User Version 2" );
		boolRow( uv2, header( ver( 20, 0, 0, 5 ), 11, 0 ), true, "20.0.0.5, User Version 11" );
		boolRow( uv2, header( ver( 20, 0, 0, 5 ), 2, 0 ), false, "20.0.0.5, User Version 2" );
		boolRow( uv2, header( ver( 20, 1, 0, 3 ), 11, 0 ), false, "20.1.0.3, User Version 11" );
		boolRow( uv2, header( ver( 20, 0, 0, 4 ), 11, 0 ), true, "20.0.0.4, User Version 11" );
		boolRow( uv2, header( ver( 20, 0, 0, 4 ), 12, 0 ), false, "20.0.0.4, User Version 12" );
		boolRow( uv2, header( ver( 10, 0, 1, 2 ), 11, 0 ), true, "10.0.1.2, User Version 11" );
		boolRow( uv2, header( ver( 10, 0, 1, 2 ), 3, 0 ), true, "10.0.1.2, User Version 3" );
		boolRow( uv2, header( ver( 10, 0, 1, 1 ), 11, 0 ), false, "10.0.1.1, User Version 11" );
		boolRow( uv2, header( ver( 10, 0, 1, 2 ), 12, 0 ), false, "10.0.1.2, User Version 12" );

		// bit tests on ARG, the vertex flags a BSVertexData is read with, and the key type of a Key
		QString arg = "((ARG & 16) != 0) && ((ARG & 16384) == 0)";
		boolRow( arg, "ARG=16", true );
		boolRow( arg, "ARG=0x4010", false );
		boolRow( arg, "ARG=0x4000", false );
		boolRow( arg, "ARG=0", false );
		boolRow( arg, "ARG=0x110", true );
		QString arg2 = "((ARG & 16) != 0) && (ARG & 256) == 0";
		boolRow( arg2, "ARG=16", true );
		boolRow( arg2, "ARG=0x110", false );
		boolRow( arg2, "ARG=256", false );
		boolRow( "ARG == 2", "ARG=2", true );
		boolRow( "ARG == 2", "ARG=3", false );
		boolRow( "ARG != 4", "ARG=4", false );
		boolRow( "ARG != 4", "ARG=5", true );

		// NiGeometryData tangents: needs normals and a bit 12 in either of the two flag fields
		QString tangents = "(Has Normals) && ((Vector Flags | BS Vector Flags) & 4096)";
		boolRow( tangents, "Has Normals=1;Vector Flags=0x1000", true );
		boolRow( tangents, "Has Normals=1;BS Vector Flags=0x1000", true );
		boolRow( tangents, "Has Normals=1;Vector Flags=0x0FFF", false );
		boolRow( tangents, "Has Normals=1;Vector Flags=0x0FFF;BS Vector Flags=0x1000", true );
		boolRow( tangents, "Has Normals=0;Vector Flags=0x1000", false );
		boolRow( tangents, "Has Normals=1", false );

		boolRow( "(Has Faces) && (Num Strips != 0)", "Has Faces=1;Num Strips=2", true );
		boolRow( "(Has Faces) && (Num Strips != 0)", "Has Faces=1;Num Strips=0", false );
		boolRow( "(Has Faces) && (Num Strips != 0)", "Has Faces=0;Num Strips=2", false );
		boolRow( "(Has Faces) && (Num Strips == 0)", "Has Faces=1;Num Strips=0", true );
		boolRow( "Num Keys != 0", "Num Keys=1", true );
		boolRow( "Num Keys != 0", "Num Keys=0", false );
		boolRow( "User Version 2 > 34", "User Version 2=35", true );
		boolRow( "User Version 2 > 34", "User Version 2=34", false );
		boolRow( "User Version 2 == 130", "User Version 2=130", true );
		boolRow( "User Version 2 == 130", "User Version 2=100", false );
		boolRow( "(Flags & 4096) != 0", "Flags=0x1000", true );
		boolRow( "(Flags & 4096) != 0", "Flags=0xEFFF", false );
	}

	void nifXmlConditions() { checkBool(); }

	//! Array sizes (arr1, arr2) copied from nif.xml
	void nifXmlArraySizes_data()
	{
		addValueColumns();

		valueRow( "Data Size / Vertex Size", "Data Size=96;Vertex Size=12", 8 );
		valueRow( "Data Size / Vertex Size", "Data Size=100;Vertex Size=12", 8 );	// truncated
		valueRow( "Num Pivots / 2", "Num Pivots=8", 4 );
		valueRow( "MOPP Data Size - 1", "MOPP Data Size=100", 99 );
		valueRow( "Num Pixels * Num Faces", "Num Pixels=16;Num Faces=6", 96 );
		valueRow( "Tri Count LOD0 + Tri Count LOD1 + Tri Count LOD2", "Tri Count LOD0=3;Tri Count LOD1=5;Tri Count LOD2=9", 17 );

		// UV Sets: the low 6 bits of Num UV Sets (up to 4.2.2.0), of Vector Flags (from 10.0.1.0) or the low bit of BS Vector Flags
		QString uvSets = "((Num UV Sets & 63) | (Vector Flags & 63) | (BS Vector Flags & 1))";
		valueRow( uvSets, "Num UV Sets=0x4003", 3 );
		valueRow( uvSets, "Num UV Sets=0x40", 0 );
		valueRow( uvSets, "Vector Flags=0x1002", 2 );
		valueRow( uvSets, "BS Vector Flags=0x0201", 1 );
		valueRow( uvSets, "BS Vector Flags=0x0200", 0 );
		valueRow( uvSets, "Num UV Sets=0x4001;Vector Flags=0x1002;BS Vector Flags=1", 3 );	// 1 | 2 | 1
		valueRow( uvSets, "Num UV Sets=0;Vector Flags=0", 0 );
	}

	void nifXmlArraySizes() { checkValue(); }

	//! A functor that answers with text: == and != convert the text to the type of the other operand before comparing
	void textOperands_data()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<QString>( "a" );
		QTest::addColumn<QString>( "b" );
		QTest::addColumn<bool>( "expected" );

		QTest::newRow( "number as text ==" ) << "A == 5" << "5" << "" << true;
		QTest::newRow( "padded number as text ==" ) << "A == 5" << "05" << "" << true;
		QTest::newRow( "padded number as text !=" ) << "A != 5" << "05" << "" << false;
		QTest::newRow( "padded number on the right" ) << "5 == A" << "05" << "" << true;
		QTest::newRow( "other number ==" ) << "A == 5" << "6" << "" << false;
		QTest::newRow( "other number !=" ) << "A != 5" << "6" << "" << true;
		QTest::newRow( "padded number, hex literal" ) << "A == 0x5" << "05" << "" << true;
		QTest::newRow( "text and text, equal" ) << "A == B" << "x" << "x" << true;
		QTest::newRow( "text and text, different" ) << "A == B" << "x" << "y" << false;
		QTest::newRow( "text and text, !=" ) << "A != B" << "x" << "y" << true;
		QTest::newRow( "text and text are compared as text" ) << "A == B" << "05" << "5" << false;
	}

	void textOperands()
	{
		QFETCH( QString, expr );
		QFETCH( QString, a );
		QFETCH( QString, b );
		QFETCH( bool, expected );

		TextEval env;
		env.names["A"] = a;
		env.names["B"] = b;

		QCOMPARE( NifExpr( expr ).evaluateBool( env ), expected );
	}

	//! A name that stands for text is true when the text is: it is not read as a number first
	void textName_asBool_data()
	{
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<bool>( "expected" );

		QTest::newRow( "words" ) << "abc" << true;
		QTest::newRow( "a number" ) << "7" << true;
		QTest::newRow( "a number with a sign" ) << "-1" << true;
		QTest::newRow( "empty" ) << "" << false;
		QTest::newRow( "zero" ) << "0" << false;
	}

	void textName_asBool()
	{
		QFETCH( QString, text );
		QFETCH( bool, expected );

		TextEval env;
		env.names["Name"] = text;

		QCOMPARE( NifExpr( "Name" ).evaluateBool( env ), expected );
		QCOMPARE( NifExpr( "!Name" ).evaluateBool( env ), !expected );
	}

	//! A decimal number is printed as the number it is. One that does not fit in an int is stored as an int (nifexpr.cpp converts the
	//! decimal literal to QVariant::Int) and comes out as the negative number it wraps around to
	void toString_decimalAboveIntMax()
	{
		QCOMPARE( NifExpr( "A != 2147483647" ).toString(), QString( "(A != 2147483647)" ) );

		QString printed = NifExpr( "A != 4294967295" ).toString();
		QEXPECT_FAIL( "", "NifExpr stores a decimal literal as an int, so 4294967295 prints as -1 (nifexpr.cpp); fix then remove", Continue );
		QCOMPARE( printed, QString( "(A != 4294967295)" ) );
	}

	//! Names resolved by BaseModelEval, the evaluator the model uses for cond and arr1, on the items of a real block
	void modelContext_siblingFields_data()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<quint32>( "expected" );

		// the scene of TestEnv::buildScene(): the NiTriShapeData has 3 vertices, 1 triangle and 3 triangle points
		QTest::newRow( "Num Vertices" ) << "Num Vertices" << 3u;
		QTest::newRow( "Num Triangles" ) << "Num Triangles" << 1u;
		QTest::newRow( "Num Triangle Points" ) << "Num Triangle Points" << 3u;
		QTest::newRow( "Num Triangles * 3" ) << "Num Triangles * 3" << 3u;
		QTest::newRow( "points are three per triangle" ) << "Num Triangle Points == (Num Triangles * 3)" << 1u;
		QTest::newRow( "Num Vertices + Num Triangles" ) << "Num Vertices + Num Triangles" << 4u;
		QTest::newRow( "Num Vertices - Num Triangles" ) << "Num Vertices - Num Triangles" << 2u;
		QTest::newRow( "Num Triangles - Num Vertices" ) << "Num Triangles - Num Vertices" << 4294967294u;
		QTest::newRow( "Num Vertices / Num Triangles" ) << "Num Vertices / Num Triangles" << 3u;
		QTest::newRow( "Num Triangle Points / Num Vertices" ) << "Num Triangle Points / Num Vertices" << 1u;
		QTest::newRow( "Num Vertices & Num Triangles" ) << "Num Vertices & Num Triangles" << 1u;
		QTest::newRow( "Num Vertices | 4" ) << "Num Vertices | 4" << 7u;
		QTest::newRow( "Num Vertices > Num Triangles" ) << "Num Vertices > Num Triangles" << 1u;
		QTest::newRow( "Num Vertices < Num Triangles" ) << "Num Vertices < Num Triangles" << 0u;
		QTest::newRow( "Num Vertices >= 3" ) << "Num Vertices >= 3" << 1u;
		QTest::newRow( "Num Vertices > 3" ) << "Num Vertices > 3" << 0u;
		QTest::newRow( "Num Vertices <= 3" ) << "Num Vertices <= 3" << 1u;
		QTest::newRow( "Num Vertices < 3" ) << "Num Vertices < 3" << 0u;
		QTest::newRow( "Num Triangles != 1" ) << "Num Triangles != 1" << 0u;
		QTest::newRow( "a name the block does not have" ) << "No Such Field" << 0u;
		QTest::newRow( "not a name the block does not have" ) << "!No Such Field" << 1u;
		QTest::newRow( "a name the block does not have is 0" ) << "Num Vertices == No Such Field" << 0u;
	}

	void modelContext_siblingFields()
	{
		QFETCH( QString, expr );
		QFETCH( quint32, expected );

		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		auto nif = TestEnv::makeModel( p );
		QVERIFY( TestEnv::buildScene( *nif ) );

		// 0 root, 1 child, 2 shape, 3 data, 4 extra; names are looked up next to the item, so ask as the data's Vertices array
		QModelIndex iVertices = nif->getIndex( nif->getBlock( 3 ), "Vertices" );
		QVERIFY( iVertices.isValid() );
		BaseModelEval env( nif.get(), static_cast<NifItem *>( iVertices.internalPointer() ) );

		QCOMPARE( NifExpr( expr ).evaluateValue( env ).toUInt(), expected );
	}

	//! The header's Version, User Version and User Version 2 as NifModelEval (the evaluator of vercond) reads them
	void modelContext_header_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<bool>( "expected" );

		// Skyrim: 20.2.0.7, User Version 12, User Version 2 83
		QString skyrim = "20.2.0.7";
		QTest::newRow( "Skyrim == 20.2.0.7" ) << skyrim << 12 << 83 << "Version == 20.2.0.7" << true;
		QTest::newRow( "Skyrim >= 20.2.0.7" ) << skyrim << 12 << 83 << "Version >= 20.2.0.7" << true;
		QTest::newRow( "Skyrim > 20.2.0.7" ) << skyrim << 12 << 83 << "Version > 20.2.0.7" << false;
		QTest::newRow( "Skyrim <= 20.2.0.7" ) << skyrim << 12 << 83 << "Version <= 20.2.0.7" << true;
		QTest::newRow( "Skyrim < 20.2.0.7" ) << skyrim << 12 << 83 << "Version < 20.2.0.7" << false;
		QTest::newRow( "Skyrim != 20.2.0.7" ) << skyrim << 12 << 83 << "Version != 20.2.0.7" << false;
		QTest::newRow( "Skyrim User Version" ) << skyrim << 12 << 83 << "User Version == 12" << true;
		QTest::newRow( "Skyrim User Version 2" ) << skyrim << 12 << 83 << "User Version 2 == 83" << true;
		QTest::newRow( "Skyrim User Version 2 >= 83" ) << skyrim << 12 << 83 << "User Version 2 >= 83" << true;
		QTest::newRow( "Skyrim User Version 2 > 83" ) << skyrim << 12 << 83 << "User Version 2 > 83" << false;
		QTest::newRow( "Skyrim Bethesda condition" ) << skyrim << 12 << 83 << "(Version == 20.2.0.7) && (User Version 2 > 0)" << true;
		QTest::newRow( "User Version 2 is 1" ) << skyrim << 12 << 1 << "User Version 2 > 0" << true;
		QTest::newRow( "User Version 2 is 0" ) << skyrim << 12 << 0 << "User Version 2 > 0" << false;
		QTest::newRow( "User Version 2 is 0, ==" ) << skyrim << 12 << 0 << "User Version 2 == 0" << true;

		// Oblivion: 20.0.0.5, 11, 11
		QString oblivion = "20.0.0.5";
		QTest::newRow( "Oblivion == 20.0.0.5" ) << oblivion << 11 << 11 << "Version == 20.0.0.5" << true;
		QTest::newRow( "Oblivion >= 20.0.0.5" ) << oblivion << 11 << 11 << "Version >= 20.0.0.5" << true;
		QTest::newRow( "Oblivion > 20.0.0.5" ) << oblivion << 11 << 11 << "Version > 20.0.0.5" << false;
		QTest::newRow( "Oblivion < 20.0.0.5" ) << oblivion << 11 << 11 << "Version < 20.0.0.5" << false;
		QTest::newRow( "Oblivion <= 20.0.0.4" ) << oblivion << 11 << 11 << "Version <= 20.0.0.4" << false;
		QTest::newRow( "Oblivion < 20.2.0.7" ) << oblivion << 11 << 11 << "Version < 20.2.0.7" << true;
		QTest::newRow( "Oblivion User Version 2" ) << oblivion << 11 << 11 << "User Version 2 == 11" << true;
		QTest::newRow( "Oblivion Bethesda condition" ) << oblivion << 11 << 11 << "(Version == 20.2.0.7) && (User Version 2 > 0)" << false;

		// Morrowind: 4.0.0.2 has neither user version
		QString morrowind = "4.0.0.2";
		QTest::newRow( "Morrowind == 4.0.0.2" ) << morrowind << 0 << 0 << "Version == 4.0.0.2" << true;
		QTest::newRow( "Morrowind <= 4.0.0.2" ) << morrowind << 0 << 0 << "Version <= 4.0.0.2" << true;
		QTest::newRow( "Morrowind < 4.0.0.2" ) << morrowind << 0 << 0 << "Version < 4.0.0.2" << false;
		QTest::newRow( "Morrowind >= 4.0.0.2" ) << morrowind << 0 << 0 << "Version >= 4.0.0.2" << true;
		QTest::newRow( "Morrowind > 4.0.0.1" ) << morrowind << 0 << 0 << "Version > 4.0.0.1" << true;
		QTest::newRow( "Morrowind has no User Version" ) << morrowind << 0 << 0 << "User Version == 0" << true;
		QTest::newRow( "Morrowind has no User Version 2" ) << morrowind << 0 << 0 << "User Version 2 == 0" << true;
	}

	void modelContext_header()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QFETCH( QString, expr );
		QFETCH( bool, expected );

		QByteArray versionText = version.toLatin1();
		TestEnv::Profile p{ "", versionText.constData(), userVersion, userVersion2 };
		auto nif = TestEnv::makeModel( p );

		NifModelEval env( nif.get(), static_cast<NifItem *>( nif->getHeader().internalPointer() ) );

		QCOMPARE( NifExpr( expr ).evaluateBool( env ), expected );
	}

	//! NiGeometryData has Vector Flags (ver1 10.0.1.0, vercond !((Version == 20.2.0.7) && (User Version 2 > 0))) or BS Vector Flags
	//! (vercond (Version == 20.2.0.7) && (User Version 2 > 0)): the model decides from the header which of the two is there
	void modelContext_vectorFlagFields_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );
		QTest::addColumn<bool>( "hasVectorFlags" );
		QTest::addColumn<bool>( "hasBSVectorFlags" );

		QTest::newRow( "4.2.2.0" ) << "4.2.2.0" << 0 << 0 << false << false;
		QTest::newRow( "10.0.1.0" ) << "10.0.1.0" << 0 << 0 << true << false;
		QTest::newRow( "10.2.0.0" ) << "10.2.0.0" << 0 << 0 << true << false;
		QTest::newRow( "Oblivion 20.0.0.5, User Version 2 is 11" ) << "20.0.0.5" << 11 << 11 << true << false;
		QTest::newRow( "20.1.0.3" ) << "20.1.0.3" << 0 << 0 << true << false;
		QTest::newRow( "20.2.0.7 without user versions" ) << "20.2.0.7" << 0 << 0 << true << false;
		QTest::newRow( "20.2.0.7, User Version 2 is 0" ) << "20.2.0.7" << 12 << 0 << true << false;
		QTest::newRow( "20.2.0.7, User Version 2 is 1" ) << "20.2.0.7" << 12 << 1 << false << true;
		QTest::newRow( "Fallout 3" ) << "20.2.0.7" << 11 << 34 << false << true;
		QTest::newRow( "Skyrim LE" ) << "20.2.0.7" << 12 << 83 << false << true;
		QTest::newRow( "Skyrim SE" ) << "20.2.0.7" << 12 << 100 << false << true;
		QTest::newRow( "Fallout 4" ) << "20.2.0.7" << 12 << 130 << false << true;
	}

	void modelContext_vectorFlagFields()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QFETCH( bool, hasVectorFlags );
		QFETCH( bool, hasBSVectorFlags );

		QByteArray versionText = version.toLatin1();
		TestEnv::Profile p{ "", versionText.constData(), userVersion, userVersion2 };
		auto nif = TestEnv::makeModel( p );

		QModelIndex iData = nif->insertNiBlock( "NiTriShapeData" );
		QVERIFY( iData.isValid() );

		QCOMPARE( nif->getIndex( iData, "Vector Flags" ).isValid(), hasVectorFlags );
		QCOMPARE( nif->getIndex( iData, "BS Vector Flags" ).isValid(), hasBSVectorFlags );
	}

	//! UV Sets has arr1 ((Num UV Sets & 63) | (Vector Flags & 63) | (BS Vector Flags & 1)): with the one flag field the version has
	//! set, the array gets that many rows
	void modelContext_uvSetCount_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );
		QTest::addColumn<QString>( "field" );
		QTest::addColumn<int>( "flags" );
		QTest::addColumn<int>( "rows" );

		QTest::newRow( "4.2.2.0, 3 sets" ) << "4.2.2.0" << 0 << 0 << "Num UV Sets" << 0x4003 << 3;
		QTest::newRow( "4.2.2.0, bit 6 only" ) << "4.2.2.0" << 0 << 0 << "Num UV Sets" << 0x40 << 0;
		QTest::newRow( "10.2.0.0, 2 sets" ) << "10.2.0.0" << 0 << 0 << "Vector Flags" << 0x1002 << 2;
		QTest::newRow( "Oblivion, 4 sets" ) << "20.0.0.5" << 11 << 11 << "Vector Flags" << 0x1004 << 4;
		QTest::newRow( "20.2.0.7, User Version 2 is 0, 3 sets" ) << "20.2.0.7" << 12 << 0 << "Vector Flags" << 0x1003 << 3;
		QTest::newRow( "Skyrim, one set" ) << "20.2.0.7" << 12 << 83 << "BS Vector Flags" << 0x0201 << 1;
		QTest::newRow( "Skyrim, no set" ) << "20.2.0.7" << 12 << 83 << "BS Vector Flags" << 0x0200 << 0;
	}

	void modelContext_uvSetCount()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QFETCH( QString, field );
		QFETCH( int, flags );
		QFETCH( int, rows );

		QByteArray versionText = version.toLatin1();
		TestEnv::Profile p{ "", versionText.constData(), userVersion, userVersion2 };
		auto nif = TestEnv::makeModel( p );

		QModelIndex iData = nif->insertNiBlock( "NiTriShapeData" );
		QVERIFY( iData.isValid() );

		// the field exists for this version (set<> refuses a field that is not there)
		QVERIFY( nif->set<int>( iData, field, flags ) );
		QVERIFY( nif->updateArray( iData, "UV Sets" ) );

		QModelIndex iUVSets = nif->getIndex( iData, "UV Sets" );
		QVERIFY( iUVSets.isValid() );
		QCOMPARE( nif->rowCount( iUVSets ), rows );
	}

	// ---- what the parser takes for a number, and what it takes for a name

	//! A name that has a hex number or a version number in it is a name: only the whole text is a number
	void names_withNumbersInside_data()
	{
		addValueColumns();

		// the names are 9 in every row: a number would be 16 (0x10) or 0x01020300
		for ( const char * name : { "0x10G", "G0x10", "x1.2.3.4", "1.2.3.4x", "0x10 0x10", "v1.2.3.4.5" } )
			valueRow( name, QString( "%1=9" ).arg( name ), 9 );

		// ... and the numbers themselves are numbers
		valueRow( "0x10", "0x10=9", 16 );
		valueRow( "1.2.3.4", "1.2.3.4=9", 0x01020304 );
	}

	void names_withNumbersInside() { checkValue(); }

	//! The text of the exception for a group that is not closed
	void malformed_exceptionText()
	{
		try {
			NifExpr e( "(A == 5" );
			QFAIL( "no exception" );
		} catch ( CString message ) {
			QCOMPARE( QString::fromLatin1( message ), QString( "expression syntax error (non-matching brackets?)" ) );
		}
	}

	//! An expression made from a part of a text: the characters from the first position to the last, both included
	void rangeConstructor()
	{
		MapEval env;
		env.names["A"] = 5;

		QString text = "xx(A == 5)xx";
		QVERIFY( NifExpr( text, 2, 9 ).evaluateBool( env ) );
		QCOMPARE( NifExpr( text, 2, 9 ).toString(), QString( "(A == 5)" ) );
		// one character less on either side is another expression
		QCOMPARE( NifExpr( text, 3, 8 ).toString(), QString( "(A == 5)" ) );
		QCOMPARE( NifExpr( "A == 5 && B", 0, 5 ).toString(), QString( "(A == 5)" ) );
		QCOMPARE( NifExpr( "A == 5 && B", 0, 4 ).toString(), QString( "(A == )" ) );
	}

	//! An expression that was not given a text has none to evaluate: no value, and nothing that is true
	void defaultConstructed()
	{
		NifExpr none;
		MapEval env;
		QVERIFY( !none.evaluateBool( env ) );
		QVERIFY( !none.evaluateValue( env ).isValid() );
		QCOMPARE( none.toString(), QString() );
	}

	//! The arithmetic and bit operators answer with an unsigned 32-bit number, in every operand they get
	void arithmetic_resultIsUnsigned_data()
	{
		QTest::addColumn<QString>( "expr" );

		for ( const char * op : { "+", "-", "*", "/", "&", "|" } ) {
			QTest::newRow( qPrintable( QString( "A %1 B" ).arg( op ) ) ) << QString( "A %1 B" ).arg( op );
			QTest::newRow( qPrintable( QString( "12 %1 10" ).arg( op ) ) ) << QString( "12 %1 10" ).arg( op );
			QTest::newRow( qPrintable( QString( "Big %1 C" ).arg( op ) ) ) << QString( "Big %1 C" ).arg( op );
		}
	}

	void arithmetic_resultIsUnsigned()
	{
		QFETCH( QString, expr );

		QVariant v = NifExpr( expr ).evaluateValue( makeEnv() );
		QVERIFY2( v.type() == QVariant::UInt, qPrintable( QString( "the answer is a %1" ).arg( v.typeName() ) ) );
	}

	//! A division of numbers above 2^31 is done on unsigned numbers
	void arithmetic_divisionAbove2To31_data()
	{
		addValueColumns();

		valueRow( "Big / 2", QString(), 2147483647u );
		valueRow( "Big / C", QString(), 2147483647u );
		valueRow( "0x80000000 / 2", QString(), 0x40000000u );
		valueRow( "Big / Big", QString(), 1u );
		valueRow( "A / Big", QString(), 0u );
		valueRow( "Zero - A", QString(), 4294967291u );
		valueRow( "(Zero - A) / C", QString(), 2147483645u );
		valueRow( "(Zero - A) & 0xFFFF", QString(), 0xFFFBu );
		valueRow( "(Zero - A) | 1", QString(), 4294967291u );
		valueRow( "(Zero - A) * C", QString(), 4294967286u );
	}

	void arithmetic_divisionAbove2To31() { checkValue(); }

	//! && and || on names that stand for text: a text is true when it is not empty (and not "0"), whatever number it would be
	void textOperands_andOr_data()
	{
		QTest::addColumn<QString>( "expr" );
		QTest::addColumn<QString>( "a" );
		QTest::addColumn<QString>( "b" );
		QTest::addColumn<bool>( "expected" );

		QTest::newRow( "words && words" ) << "A && B" << "yes" << "yes" << true;
		QTest::newRow( "words && empty" ) << "A && B" << "yes" << "" << false;
		QTest::newRow( "empty && words" ) << "A && B" << "" << "yes" << false;
		QTest::newRow( "words || empty" ) << "A || B" << "yes" << "" << true;
		QTest::newRow( "empty || words" ) << "A || B" << "" << "yes" << true;
		QTest::newRow( "empty || empty" ) << "A || B" << "" << "" << false;
		QTest::newRow( "zero text && words" ) << "A && B" << "0" << "yes" << false;
	}

	void textOperands_andOr()
	{
		QFETCH( QString, expr );
		QFETCH( QString, a );
		QFETCH( QString, b );
		QFETCH( bool, expected );

		TextEval env;
		env.names["A"] = a;
		env.names["B"] = b;
		QCOMPARE( NifExpr( expr ).evaluateBool( env ), expected );
	}
};

REGISTER_TEST( tst_NifExpr )

#include "tst_nifvalue.moc"
