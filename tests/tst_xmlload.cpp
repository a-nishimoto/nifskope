#include "testenv.h"
#include "testregistry.h"

#include "data/nifvalue.h"
#include "model/kfmmodel.h"
#include "model/nifmodel.h"

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include <initializer_list>


//! nif.xml / kfm.xml (the build/docsys submodule) parse and describe what the models expect
class tst_XmlLoad final : public QObject
{
	Q_OBJECT

	static QByteArray readAll( const QString & path )
	{
		QFile f( path );
		return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray();
	}

	//! Writes text to a temporary .xml file the parser can open
	static QString writeTemp( QTemporaryFile & f, const QByteArray & text )
	{
		f.setFileTemplate( QDir::temp().filePath( "nifskope-test-XXXXXX.xml" ) );
		if ( !f.open() || f.write( text ) != text.size() )
			return QString();
		f.close();
		return f.fileName();
	}

private slots:
	void initTestCase()
	{
		QVERIFY2( QFile::exists( TestEnv::nifXmlPath() ),
			qPrintable( "missing " + TestEnv::nifXmlPath() + " - run: git submodule update --init --recursive" ) );
		QVERIFY2( QFile::exists( TestEnv::kfmXmlPath() ),
			qPrintable( "missing " + TestEnv::kfmXmlPath() + " - run: git submodule update --init --recursive" ) );
	}

	//! Failure tests below clear the tables, so every test starts from a loaded state
	void init()
	{
		QString err = TestEnv::reloadXml();
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
	}

	void nifXml_parses()
	{
		QString err = NifModel::parseXmlDescription( TestEnv::nifXmlPath() );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		// Sanity floors, not exact counts: nif.xml grows with every submodule bump
		QVERIFY( NifModel::allNiBlocks().count() > 400 );
		QVERIFY( NifModel::isNiBlock( "NiNode" ) );
		QVERIFY( NifModel::isNiBlock( "BSTriShape" ) );
		QVERIFY( !NifModel::isNiBlock( "NiObjectNET" ) );
		QVERIFY( NifModel::isAncestor( "NiObjectNET" ) );
		QVERIFY( NifModel::isCompound( "Header" ) );
		QVERIFY( NifModel::isCompound( "Footer" ) );
		QVERIFY( NifModel::isCompound( "ControlledBlock" ) );
		QVERIFY( !NifModel::isNiBlock( "NoSuchBlock" ) );

		NifModel nif;
		QVERIFY( nif.inherits( "NiTriShape", "NiAVObject" ) );
		QVERIFY( nif.inherits( "NiNode", "NiObject" ) );
		QVERIFY( !nif.inherits( "NiNode", "NiTriShape" ) );
	}

	//! Every <version> listed in nif.xml must be understood by version2number() and be marked supported
	void nifXml_versionList()
	{
		QString xml = QString::fromUtf8( readAll( TestEnv::nifXmlPath() ) );
		QRegularExpressionMatchIterator it = QRegularExpression( "<version num=\"([^\"]+)\"" ).globalMatch( xml );

		int n = 0;
		while ( it.hasNext() ) {
			QString text = it.next().captured( 1 );
			quint32 v = NifModel::version2number( text );
			QVERIFY2( v != 0, qPrintable( text ) );
			QVERIFY2( NifModel::isVersionSupported( v ), qPrintable( text ) );
			QCOMPARE( NifModel::version2string( v ), text );
			n++;
		}

		QVERIFY( n > 20 );
		QVERIFY( !NifModel::isVersionSupported( 0x99999999 ) );
	}

	void nifXml_enumsAndBasicTypes()
	{
		QCOMPARE( NifValue::type( "Vector3" ), NifValue::tVector3 );
		QCOMPARE( NifValue::enumType( "ConsistencyType" ), NifValue::eDefault );
		QCOMPARE( NifValue::enumOptionValue( "ConsistencyType", "CT_MUTABLE" ), 0u );
		QCOMPARE( NifValue::enumType( "VertexFlags" ), NifValue::eFlags );
	}

	void nifXml_missingFile()
	{
		QString err = NifModel::parseXmlDescription( TestEnv::sourceDir() + "/no/such/nif.xml" );
		QVERIFY( !err.isEmpty() );
		// a failed parse leaves nothing half-loaded
		QVERIFY( !NifModel::isNiBlock( "NiNode" ) );
	}

	void nifXml_malformed_data()
	{
		QTest::addColumn<QByteArray>( "xml" );

		QTest::newRow( "mismatched tags" )
			<< QByteArray( "<?xml version=\"1.0\"?><niftoolsxml><compound name=\"A\"><add name=\"x\" type=\"int\"></compound></niftoolsxml>" );
		QTest::newRow( "not xml" ) << QByteArray( "this is not xml at all" );
		QTest::newRow( "unknown type" )
			<< QByteArray( "<?xml version=\"1.0\"?><niftoolsxml><compound name=\"A\"><add name=\"x\" type=\"NoSuchType\"/></compound></niftoolsxml>" );
	}

	void nifXml_malformed()
	{
		QFETCH( QByteArray, xml );

		QTemporaryFile f;
		QString path = writeTemp( f, xml );
		QVERIFY( !path.isEmpty() );

		QVERIFY( !NifModel::parseXmlDescription( path ).isEmpty() );
		QVERIFY( !NifModel::isNiBlock( "NiNode" ) );
	}

	void kfmXml_parses()
	{
		QString err = KfmModel::parseXmlDescription( TestEnv::kfmXmlPath() );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		QVERIFY( KfmModel::isCompound( "Kfm" ) );
		QVERIFY( KfmModel::isCompound( "Animation" ) );
		QVERIFY( KfmModel::isVersionSupported( 0x0200000b ) );
		QVERIFY( KfmModel::isVersionSupported( KfmModel::version2number( "1.2.4b" ) ) );
		QVERIFY( !KfmModel::isVersionSupported( 0x99999999 ) );
	}

	void kfmXml_missingFile()
	{
		QVERIFY( !KfmModel::parseXmlDescription( TestEnv::sourceDir() + "/no/such/kfm.xml" ).isEmpty() );
		QVERIFY( !KfmModel::isCompound( "Kfm" ) );
	}
};

REGISTER_TEST( tst_XmlLoad )


/*! Schema documents written for the test and parsed by the real loaders (NifModel::parseXmlDescription() and
 *  KfmModel::parseXmlDescription()): what they register, what they say when they are wrong.
 *
 *  The error text is the whole error interface of the parsers: loadXML() shows it as the details of "Error loading XML".
 *  A semantic error carries the line on which the start tag that caused it ends (or, for the checks after the last
 *  element, the line after the last line break); a document that is not well-formed always reads "Syntax error".
 */
class tst_XmlSchema final : public QObject
{
	Q_OBJECT

	QTemporaryDir dir;
	int serial = 0;

	//! A schema document: the XML declaration, the DOCTYPE and the root start tag are lines 1 to 3, the lines of
	//! the body follow from line 4, then the end tag of the root
	static QByteArray schemaDoc( std::initializer_list<const char *> body )
	{
		QByteArray xml( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml version=\"0.9.0.0\">\n" );
		for ( const char * line : body ) {
			xml += line;
			xml += '\n';
		}
		return xml + "</niftoolsxml>\n";
	}

	//! Writes a document to a file of its own, so that the loaders read it the way they read nif.xml
	QString write( const QByteArray & xml )
	{
		QString path = dir.filePath( QString( "schema%1.xml" ).arg( serial++ ) );
		QFile f( path );
		if ( !f.open( QIODevice::WriteOnly ) || f.write( xml ) != xml.size() )
			return QString();

		return path;
	}

	//! Parses a document as nif.xml; the error text, empty on success
	QString loadNif( const QByteArray & xml )
	{
		QString path = write( xml );
		return path.isEmpty() ? QString( "the test could not write its file" ) : NifModel::parseXmlDescription( path );
	}

	//! Parses a document as kfm.xml
	QString loadKfm( const QByteArray & xml )
	{
		QString path = write( xml );
		return path.isEmpty() ? QString( "the test could not write its file" ) : KfmModel::parseXmlDescription( path );
	}

	//! The error text is "<NIF or KFM> XML parse error (line N): <message>"; a negative line accepts any line
	static void verifyError( const QString & err, const QString & parser, int line, const QString & message )
	{
		if ( line < 0 ) {
			QCOMPARE( err.section( " (line ", 0, 0 ), parser + " XML parse error" );
			QVERIFY2( err.endsWith( "): " + message ), qPrintable( err ) );
		} else {
			QCOMPARE( err, QString( "%1 XML parse error (line %2): %3" ).arg( parser ).arg( line ).arg( message ) );
		}
	}

	//! Control characters of a text made visible, so that a row of expected values reads on one line
	static QString visible( QString text )
	{
		return text.replace( '\r', "\\r" ).replace( '\n', "\\n" ).replace( '\t', "\\t" );
	}

	//! The rows of a branch of a model, one line each, the rows of a compound indented below it. Only the attributes
	//! a row has are named: this is what the parsed tables make of the model
	static QString describeRows( const BaseModel & model, const QModelIndex & parent, int depth = 0 )
	{
		QString out;

		for ( int r = 0; r < model.rowCount( parent ); r++ ) {
			QModelIndex i = model.index( r, 0, parent );
			QString line = QString( depth * 2, ' ' ) + model.itemName( i ) + ": " + model.itemType( i );

			const QList<QPair<QString, QString>> attributes = {
				{ "template", model.itemTmplt( i ) },
				{ "arg", model.itemArg( i ) },
				{ "arr1", model.itemArr1( i ) },
				{ "arr2", model.itemArr2( i ) },
				{ "cond", model.itemCond( i ) },
				{ "vercond", model.data( model.index( r, BaseModel::VerCondCol, parent ) ).toString() },
				{ "text", model.itemText( i ) },
				{ "value", model.data( model.index( r, BaseModel::ValueCol, parent ) ).toString() },
			};
			for ( const auto & attribute : attributes ) {
				if ( !attribute.second.isEmpty() )
					line += QString( " %1={%2}" ).arg( attribute.first, visible( attribute.second ) );
			}

			if ( model.itemVer1( i ) )
				line += QString( " ver1=0x%1" ).arg( model.itemVer1( i ), 8, 16, QChar( '0' ) );
			if ( model.itemVer2( i ) )
				line += QString( " ver2=0x%1" ).arg( model.itemVer2( i ), 8, 16, QChar( '0' ) );

			out += line + "\n" + describeRows( model, i, depth + 1 );
		}

		return out;
	}

	//! QCOMPARE shortens long texts: a table of rows is shown whole
	static void compareRows( const QString & actual, const QString & expected )
	{
		QVERIFY2( actual == expected, qPrintable( "\n--- actual\n" + actual + "--- expected\n" + expected ) );
	}

	//! The text of a document in UTF-16 with a byte order mark
	static QByteArray utf16( const QString & text, bool bigEndian )
	{
		QByteArray bytes = bigEndian ? QByteArray( "\xfe\xff", 2 ) : QByteArray( "\xff\xfe", 2 );
		for ( QChar c : text ) {
			char hi = char( c.unicode() >> 8 ), lo = char( c.unicode() & 0xff );
			bytes += bigEndian ? hi : lo;
			bytes += bigEndian ? lo : hi;
		}
		return bytes;
	}

private slots:
	void initTestCase()
	{
		QVERIFY( dir.isValid() );
	}

	//! The tables are process-wide: leave the real ones behind for the classes that run after this one
	void cleanupTestCase()
	{
		QString err = TestEnv::reloadXml();
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
	}

	void nif_errors_data()
	{
		QTest::addColumn<QByteArray>( "xml" );
		QTest::addColumn<int>( "line" );
		QTest::addColumn<QString>( "message" );

		QTest::newRow( "unknown element in the root" ) << schemaDoc( { "<foo/>" } ) << 4 << "error unknown element 'foo'";
		QTest::newRow( "root is not niftoolsxml" ) << QByteArray( "<?xml version=\"1.0\"?>\n<foo/>\n" ) << 2 << "error unknown element 'foo'";
		QTest::newRow( "root is a known element" ) << QByteArray( "<?xml version=\"1.0\"?>\n<compound name=\"A\"/>\n" ) << 2 << "this is not a niftoolsxml file";
		QTest::newRow( "root inside the root" ) << schemaDoc( { "<niftoolsxml/>" } ) << 4 << "expected basic, enum, compound, niobject or version got niftoolsxml instead";
		QTest::newRow( "add under the root" ) << schemaDoc( { "<add name='a' type='int'/>" } ) << 4 << "expected basic, enum, compound, niobject or version got add instead";
		QTest::newRow( "option under the root" ) << schemaDoc( { "<option name='a' value='1'/>" } ) << 4 << "expected basic, enum, compound, niobject or version got option instead";
		QTest::newRow( "unknown element in a compound" ) << schemaDoc( { "<compound name='A'>", "<foo/>", "</compound>" } ) << 5 << "error unknown element 'foo'";
		QTest::newRow( "option in a compound" ) << schemaDoc( { "<compound name='A'>", "<option name='a' value='1'/>", "</compound>" } ) << 5 << "only add tags allowed in compound type declaration";
		QTest::newRow( "compound in a compound" ) << schemaDoc( { "<compound name='A'>", "<compound name='B'/>", "</compound>" } ) << 5 << "only add tags allowed in compound type declaration";
		QTest::newRow( "compound in a niobject" ) << schemaDoc( { "<niobject name='A'>", "<compound name='B'/>", "</niobject>" } ) << 5 << "only add tags allowed in block declaration";
		QTest::newRow( "add in an enum" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<add name='a' type='int'/>", "</enum>" } ) << 5 << "only option tags allowed in enum declaration";
		QTest::newRow( "add in bitflags" ) << schemaDoc( { "<bitflags name='A' storage='uint'>", "<add name='a' type='int'/>", "</bitflags>" } ) << 5 << "only option tags allowed in enum declaration";
		QTest::newRow( "add in an add" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>", "<add name='b' type='int'/>", "</add>", "</compound>" } ) << 6 << "error unhandled tag add";
		QTest::newRow( "option in an option" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<option name='a' value='1'>", "<option name='b' value='2'/>", "</option>", "</enum>" } ) << 6 << "error unhandled tag option";
		QTest::newRow( "add in a basic" ) << schemaDoc( { "<basic name='int'>", "<add name='a' type='int'/>", "</basic>" } ) << 5 << "error unhandled tag add";
		QTest::newRow( "basic with an unknown name" ) << schemaDoc( { "<basic name='NoSuchBasic'>text</basic>" } ) << 4 << "basic definition NoSuchBasic must have an internal NifSkope type";
		QTest::newRow( "basic without a name" ) << schemaDoc( { "<basic>text</basic>" } ) << 4 << "basic definition  must have an internal NifSkope type";
		QTest::newRow( "enum without a storage type" ) << schemaDoc( { "<enum name='A'>", "<option name='a' value='1'/>", "</enum>" } ) << 4 << "enum definition must have a name and a known storage type";
		QTest::newRow( "enum without a name" ) << schemaDoc( { "<enum storage='uint'>", "<option name='a' value='1'/>", "</enum>" } ) << 4 << "enum definition must have a name and a known storage type";
		QTest::newRow( "enum with an unknown storage type" ) << schemaDoc( { "<enum name='A' storage='NoSuchType'>", "<option name='a' value='1'/>", "</enum>" } ) << 4 << "failed to register alias NoSuchType for enum type A";
		QTest::newRow( "enum named like a type" ) << schemaDoc( { "<enum name='int' storage='uint'>", "<option name='a' value='1'/>", "</enum>" } ) << 4 << "failed to register alias uint for enum type int";
		QTest::newRow( "enum declared twice" ) << schemaDoc( { "<enum name='A' storage='uint'/>", "<enum name='A' storage='uint'/>" } ) << 5 << "failed to register alias uint for enum type A";
		QTest::newRow( "option without a name" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<option value='1'/>", "</enum>" } ) << 5 << "option defintion must have a name and a value";
		QTest::newRow( "option without a value" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<option name='a'/>", "</enum>" } ) << 5 << "option defintion must have a name and a value";
		QTest::newRow( "option value with a fraction" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<option name='a' value='1.5'/>", "</enum>" } ) << 5 << "option value error (only integers please)";
		QTest::newRow( "option value below zero" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<option name='a' value='-1'/>", "</enum>" } ) << 5 << "option value error (only integers please)";
		QTest::newRow( "option value above 32 bits" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<option name='a' value='4294967296'/>", "</enum>" } ) << 5 << "option value error (only integers please)";
		QTest::newRow( "option value declared twice" ) << schemaDoc( { "<enum name='A' storage='uint'>", "<option name='a' value='1'/>", "<option name='b' value='1'/>", "</enum>" } ) << 6 << "failed to register enum option";
		QTest::newRow( "version without num" ) << schemaDoc( { "<version>text</version>" } ) << 4 << "invalid version tag";
		QTest::newRow( "version with an empty num" ) << schemaDoc( { "<version num=''/>" } ) << 4 << "invalid version tag";
		QTest::newRow( "version that is not a number" ) << schemaDoc( { "<version num='abc'/>" } ) << 4 << "invalid version tag";
		QTest::newRow( "version zero" ) << schemaDoc( { "<version num='0'/>" } ) << 4 << "invalid version tag";
		QTest::newRow( "version with five parts" ) << schemaDoc( { "<version num='1.2.3.4.5'/>" } ) << 4 << "invalid version tag";
		QTest::newRow( "add in a version" ) << schemaDoc( { "<version num='4.0.0.2'>", "<add name='a' type='int'/>", "</version>" } ) << 5 << "mismatching end element tag for element add";
		QTest::newRow( "unknown element in a version" ) << schemaDoc( { "<version num='4.0.0.2'>", "<foo/>", "</version>" } ) << 5 << "error unknown element 'foo'";
		QTest::newRow( "compound without a name" ) << schemaDoc( { "<compound>", "<add name='a' type='int'/>", "</compound>" } ) << 4 << "compound and niblocks must have a name";
		QTest::newRow( "niobject without a name" ) << schemaDoc( { "<niobject>", "<add name='a' type='int'/>", "</niobject>" } ) << 4 << "compound and niblocks must have a name";
		QTest::newRow( "compound with an empty name" ) << schemaDoc( { "<compound name=''>", "<add name='a' type='int'/>", "</compound>" } ) << 4 << "compound and niblocks must have a name";
		QTest::newRow( "compound declared twice" ) << schemaDoc( { "<compound name='A'/>", "<compound name='A'/>" } ) << 5 << "multiple declarations of A";
		QTest::newRow( "niobject declared twice" ) << schemaDoc( { "<niobject name='A'/>", "<niobject name='A'/>" } ) << 5 << "multiple declarations of A";
		QTest::newRow( "compound, then niobject of that name" ) << schemaDoc( { "<compound name='A'/>", "<niobject name='A'/>" } ) << 5 << "multiple declarations of A";
		QTest::newRow( "niobject, then compound of that name" ) << schemaDoc( { "<niobject name='A'/>", "<compound name='A'/>" } ) << 5 << "multiple declarations of A";
		QTest::newRow( "niobject inherits a later niobject" ) << schemaDoc( { "<niobject name='A' inherit='B'/>", "<niobject name='B'/>" } ) << 4 << "forward declaration of block id B";
		QTest::newRow( "niobject inherits an unknown niobject" ) << schemaDoc( { "<niobject name='A' inherit='NoSuchBlock'/>" } ) << 4 << "forward declaration of block id NoSuchBlock";
		QTest::newRow( "niobject inherits itself" ) << schemaDoc( { "<niobject name='A' inherit='A'/>" } ) << 4 << "forward declaration of block id A";
		QTest::newRow( "niobject inherits a compound" ) << schemaDoc( { "<compound name='C'/>", "<niobject name='A' inherit='C'/>" } ) << 5 << "forward declaration of block id C";
		QTest::newRow( "add without a name" ) << schemaDoc( { "<compound name='A'>", "<add type='int'/>", "</compound>" } ) << 5 << "add needs at least name and type attributes";
		QTest::newRow( "add without a type" ) << schemaDoc( { "<compound name='A'>", "<add name='a'/>", "</compound>" } ) << 5 << "add needs at least name and type attributes";
		QTest::newRow( "add with an empty name" ) << schemaDoc( { "<compound name='A'>", "<add name='' type='int'/>", "</compound>" } ) << 5 << "add needs at least name and type attributes";
		QTest::newRow( "add without a type in a niobject" ) << schemaDoc( { "<niobject name='A'>", "<add name='a'/>", "</niobject>" } ) << 5 << "add needs at least name and type attributes";
		QTest::newRow( "compound field of an unknown type" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='NoSuchType'/>", "</compound>" } ) << 8 << "compound type A refers to unknown type NoSuchType";
		QTest::newRow( "compound field with an unknown template" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='Ref' template='NoSuchTemplate'/>", "</compound>" } ) << 8 << "compound type A refers to unknown template type NoSuchTemplate";
		QTest::newRow( "compound that contains itself" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='A'/>", "</compound>" } ) << 8 << "compound type A contains itself";
		QTest::newRow( "niobject field of an unknown type" ) << schemaDoc( { "<niobject name='A'>", "<add name='a' type='NoSuchType'/>", "</niobject>" } ) << 8 << "niobject A refers to unknown type NoSuchType";
		QTest::newRow( "niobject field with an unknown template" ) << schemaDoc( { "<niobject name='A'>", "<add name='a' type='Ref' template='NoSuchTemplate'/>", "</niobject>" } ) << 8 << "niobject A refers to unknown template type NoSuchTemplate";
		QTest::newRow( "niobject field whose type is a niobject" ) << schemaDoc( { "<niobject name='B'/>", "<niobject name='A'>", "<add name='a' type='B'/>", "</niobject>" } ) << 9 << "niobject A refers to unknown type B";
		QTest::newRow( "type with blanks around it" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type=' int '/>", "</compound>" } ) << 8 << "compound type A refers to unknown type  int ";
		QTest::newRow( "error after blank lines at the end" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='Nope'/>", "</compound>", "", "" } ) << 10 << "compound type A refers to unknown type Nope";
		QTest::newRow( "no line break after the root" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml>\n<compound name='A'>\n<add name='a' type='Nope'/>\n</compound>\n</niftoolsxml>" ) << 7 << "compound type A refers to unknown type Nope";
		QTest::newRow( "start tag over several lines" ) << schemaDoc( { "<compound", "name='A'>", "<add", "name='a'", "arg='1'", "/>", "</compound>" } ) << 9 << "add needs at least name and type attributes";
		QTest::newRow( "error after blank lines" ) << schemaDoc( { "", "", "<compound name='A'>", "", "<foo", "", "/>", "</compound>" } ) << 10 << "error unknown element 'foo'";
		QTest::newRow( "end tag with blanks" ) << schemaDoc( { "<compound name='A'  >", "<add name='a'   type='int'   />", "</compound  >", "<foo/>" } ) << 7 << "error unknown element 'foo'";
		QTest::newRow( "empty file" ) << QByteArray( "" ) << 1 << "Syntax error";
		QTest::newRow( "only blanks" ) << QByteArray( "  \n\t \n" ) << 3 << "Syntax error";
		QTest::newRow( "not xml" ) << QByteArray( "this is not xml at all" ) << 1 << "Syntax error";
		QTest::newRow( "end of file inside an element" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml>\n<compound name='A'>\n<add name='a' type='int'/>\n" ) << 6 << "Syntax error";
		QTest::newRow( "end of file inside a start tag" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml>\n<compound name='A" ) << 4 << "Syntax error";
		QTest::newRow( "end of file inside text" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml>\n<compound name='A'><add name='a' type='int'>text" ) << 4 << "Syntax error";
		QTest::newRow( "end of file inside a comment" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml>\n<!-- never closed" ) << -1 << "Syntax error";
		QTest::newRow( "end tag of another element" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'></compound>", "</compound>" } ) << 5 << "Syntax error";
		QTest::newRow( "end tag without a start tag" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml>\n</niftoolsxml>\n</foo>\n" ) << 5 << "Syntax error";
		QTest::newRow( "second root element" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml/>\n<niftoolsxml/>\n" ) << 4 << "Syntax error";
		QTest::newRow( "text before the root" ) << QByteArray( "text\n<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml/>\n" ) << 1 << "Syntax error";
		QTest::newRow( "text after the root" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml/>\ntrailing text\n" ) << 4 << "Syntax error";
		QTest::newRow( "attribute value without quotes" ) << schemaDoc( { "<compound name=A/>" } ) << 4 << "Syntax error";
		QTest::newRow( "less-than sign in an attribute value" ) << schemaDoc( { "<compound name='A<B'/>" } ) << 4 << "Syntax error";
		QTest::newRow( "attribute without a value" ) << schemaDoc( { "<compound name/>" } ) << 4 << "Syntax error";
		QTest::newRow( "entity reference without a name" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>a &; b</add>", "</compound>" } ) << 5 << "Syntax error";
		QTest::newRow( "entity reference without a semicolon" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>a &amp b</add>", "</compound>" } ) << 5 << "Syntax error";
		QTest::newRow( "character reference that is not a number" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>a &#xZZ; b</add>", "</compound>" } ) << 5 << "Syntax error";
		QTest::newRow( "double hyphen in a comment" ) << schemaDoc( { "<!-- a -- b -->", "<compound name='A'/>" } ) << 4 << "Syntax error";
		QTest::newRow( "XML declaration inside the root" ) << schemaDoc( { "<?xml version='1.0'?>", "<compound name='A'/>" } ) << 4 << "Syntax error";
		QTest::newRow( "XML declaration after a blank line" ) << QByteArray( "\n<?xml version=\"1.0\"?>\n<niftoolsxml/>\n" ) << 2 << "Syntax error";
		QTest::newRow( "XML declaration without a version" ) << QByteArray( "<?xml encoding=\"UTF-8\"?>\n<niftoolsxml/>\n" ) << 1 << "Syntax error";
		QTest::newRow( "less-than sign in text" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>a < b</add>", "</compound>" } ) << 5 << "Syntax error";
		QTest::newRow( "end of a CDATA section in text" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>a ]]> b</add>", "</compound>" } ) << 5 << "Syntax error";
		QTest::newRow( "CRLF line ends" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<!DOCTYPE niftoolsxml>\r\n<niftoolsxml>\r\n<compound name='A'>\r\n<add name='a' type='int'/>\r\n</compound>\r\n<foo/>\r\n</niftoolsxml>\r\n" ) << 7 << "error unknown element 'foo'";
		QTest::newRow( "lone CR line ends" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r<!DOCTYPE niftoolsxml>\r<niftoolsxml>\r<compound name='A'>\r<add name='a' type='int'/>\r</compound>\r<foo/>\r</niftoolsxml>\r" ) << 7 << "error unknown element 'foo'";
		QTest::newRow( "mixed line ends" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<!DOCTYPE niftoolsxml>\r\n<niftoolsxml>\r<compound name='A'>\n<add name='a' type='int'/>\r\n</compound>\r<foo/>\n</niftoolsxml>\n" ) << 7 << "error unknown element 'foo'";
		QTest::newRow( "prefixed root element" ) << QByteArray( "<?xml version=\"1.0\"?>\n<nt:niftoolsxml xmlns:nt=\"urn:x\">\n</nt:niftoolsxml>\n" ) << 2 << "error unknown element 'nt:niftoolsxml'";
		QTest::newRow( "undeclared prefix on an element" ) << schemaDoc( { "<x:compound name='A'/>" } ) << 4 << "error unknown element 'x:compound'";
		QTest::newRow( "undeclared prefix on an attribute" ) << schemaDoc( { "<compound x:name='A'/>" } ) << 4 << "compound and niblocks must have a name";
	}

	void nif_errors()
	{
		QFETCH( QByteArray, xml );
		QFETCH( int, line );
		QFETCH( QString, message );

		verifyError( loadNif( xml ), "NIF", line, message );

		// a failed parse leaves no blocks, no compounds and no versions behind
		QVERIFY( NifModel::allNiBlocks().isEmpty() );
		QVERIFY( !NifModel::isCompound( "A" ) );
		QVERIFY( !NifModel::isVersionSupported( NifModel::version2number( "4.0.0.2" ) ) );

		// and the lock the parser held is free again
		QVERIFY( NifModel::XMLlock.tryLockForRead() );
		NifModel::XMLlock.unlock();
	}

	void kfm_errors_data()
	{
		QTest::addColumn<QByteArray>( "xml" );
		QTest::addColumn<int>( "line" );
		QTest::addColumn<QString>( "message" );

		QTest::newRow( "unknown element in the root" ) << schemaDoc( { "<foo/>" } ) << 4 << "error unknown element 'foo'";
		QTest::newRow( "root is not niftoolsxml" ) << QByteArray( "<?xml version=\"1.0\"?>\n<foo/>\n" ) << 2 << "error unknown element 'foo'";
		QTest::newRow( "root is a known element" ) << QByteArray( "<?xml version=\"1.0\"?>\n<compound name=\"A\"/>\n" ) << 2 << "this is not a niftoolsxml file";
		QTest::newRow( "root inside the root" ) << schemaDoc( { "<niftoolsxml/>" } ) << 4 << "expected compound or version got niftoolsxml instead";
		QTest::newRow( "add under the root" ) << schemaDoc( { "<add name='a' type='int'/>" } ) << 4 << "expected compound or version got add instead";
		QTest::newRow( "niobject is not known" ) << schemaDoc( { "<niobject name='A'/>" } ) << 4 << "error unknown element 'niobject'";
		QTest::newRow( "version without num" ) << schemaDoc( { "<version/>" } ) << 4 << "invalid version string";
		QTest::newRow( "version that is not a number" ) << schemaDoc( { "<version num='abc'/>" } ) << 4 << "invalid version string";
		QTest::newRow( "version zero" ) << schemaDoc( { "<version num='0'/>" } ) << 4 << "invalid version string";
		QTest::newRow( "add in a version" ) << schemaDoc( { "<version num='1.0'>", "<add name='a' type='int'/>", "</version>" } ) << 5 << "version tag must not contain any sub tags";
		QTest::newRow( "unknown element in a compound" ) << schemaDoc( { "<compound name='A'>", "<foo/>", "</compound>" } ) << 5 << "error unknown element 'foo'";
		QTest::newRow( "compound in a compound" ) << schemaDoc( { "<compound name='A'>", "<compound name='B'/>", "</compound>" } ) << 5 << "only add tags allowed in compound type declaration";
		QTest::newRow( "add in an add" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>", "<add name='b' type='int'/>", "</add>", "</compound>" } ) << 6 << "error unhandled tag add in add";
		QTest::newRow( "compound without a name" ) << schemaDoc( { "<compound>", "<add name='a' type='int'/>", "</compound>" } ) << 6 << "invalid compound declaration: name is empty";
		QTest::newRow( "compound with an empty name" ) << schemaDoc( { "<compound name=''>", "<add name='a' type='int'/>", "</compound>" } ) << 6 << "invalid compound declaration: name is empty";
		QTest::newRow( "compound named like a type" ) << schemaDoc( { "<compound name='int'>", "<add name='a' type='int'/>", "</compound>" } ) << 4 << "compound int is already registered as internal type";
		QTest::newRow( "add without a name" ) << schemaDoc( { "<compound name='A'>", "<add type='int'/>", "</compound>" } ) << 5 << "add needs at least name and type attributes";
		QTest::newRow( "add without a type" ) << schemaDoc( { "<compound name='A'>", "<add name='a'/>", "</compound>" } ) << 5 << "add needs at least name and type attributes";
		QTest::newRow( "compound field of an unknown type" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='NoSuchType'/>", "</compound>" } ) << 8 << "compound type A referes to unknown type NoSuchType";
		QTest::newRow( "compound field with an unknown template" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='Ref' template='NoSuchTemplate'/>", "</compound>" } ) << 8 << "compound type A refers to unknown template type NoSuchTemplate";
		QTest::newRow( "template that is a compound" ) << schemaDoc( { "<compound name='B'/>", "<compound name='A'>", "<add name='a' type='Ref' template='B'/>", "</compound>" } ) << 9 << "compound type A refers to unknown template type B";
		QTest::newRow( "compound that contains itself" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='A'/>", "</compound>" } ) << 8 << "compound type A contains itself";
		QTest::newRow( "error after blank lines at the end" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='Nope'/>", "</compound>", "", "" } ) << 10 << "compound type A referes to unknown type Nope";
		QTest::newRow( "start tag over several lines" ) << schemaDoc( { "<compound", "name='A'>", "<add", "name='a'", "/>", "</compound>" } ) << 8 << "add needs at least name and type attributes";
		QTest::newRow( "CRLF line ends" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<!DOCTYPE niftoolsxml>\r\n<niftoolsxml>\r\n<compound name='A'>\r\n<add name='a' type='int'/>\r\n</compound>\r\n<foo/>\r\n</niftoolsxml>\r\n" ) << 7 << "error unknown element 'foo'";
		QTest::newRow( "lone CR line ends" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r<!DOCTYPE niftoolsxml>\r<niftoolsxml>\r<compound name='A'>\r<add name='a' type='int'/>\r</compound>\r<foo/>\r</niftoolsxml>\r" ) << 7 << "error unknown element 'foo'";
		QTest::newRow( "undeclared prefix on an element" ) << schemaDoc( { "<x:compound name='A'/>" } ) << 4 << "error unknown element 'x:compound'";
		QTest::newRow( "empty file" ) << QByteArray( "" ) << 1 << "Syntax error";
		QTest::newRow( "not xml" ) << QByteArray( "not xml" ) << 1 << "Syntax error";
		QTest::newRow( "end of file inside an element" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml>\n<compound name='A'>\n" ) << 5 << "Syntax error";
		QTest::newRow( "end tag of another element" ) << schemaDoc( { "<compound name='A'>", "<add name='a' type='int'></compound>", "</compound>" } ) << 5 << "Syntax error";
		QTest::newRow( "second root element" ) << QByteArray( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE niftoolsxml>\n<niftoolsxml/>\n<niftoolsxml/>\n" ) << 4 << "Syntax error";
	}

	void kfm_errors()
	{
		QFETCH( QByteArray, xml );
		QFETCH( int, line );
		QFETCH( QString, message );

		verifyError( loadKfm( xml ), "KFM", line, message );

		QVERIFY( !KfmModel::isCompound( "A" ) );
		QVERIFY( !KfmModel::isVersionSupported( KfmModel::version2number( "1.0" ) ) );

		QVERIFY( KfmModel::XMLlock.tryLockForRead() );
		KfmModel::XMLlock.unlock();
	}

	// ---- what a schema registers

	//! Every attribute of an <add>, the default values, the forward references, what the model makes of them
	void nif_rows()
	{
		QString err = loadNif( schemaDoc( {
			"<version num='20.0.0.5'/>",
			"<enum name='Color' storage='uint'>",
			"<option value='0' name='RED'/>",
			"<option value='1' name='GREEN'/>",
			"</enum>",
			"<bitflags name='Bits' storage='ushort'>",
			"<option value='0' name='ONE'/>",
			"<option value='3' name='FOUR'/>",
			"</bitflags>",
			"<niobject name='Row'>",
			"<add name='every attribute' type='int' template='TEMPLATE' arg='1' arr1='Count' arr2='Size' cond='X &gt; 1 &amp;&amp; Y' "
			"ver1='4.0.0.0' ver2='20.0.0.5' abstract='1' binary='1' vercond='Version &gt; 10' userver='12' userver2='130' default='5'>text a</add>",
			"<add name='template type' type='TEMPLATE'/>",
			"<add name='enum' type='Color' default='GREEN'/>",
			"<add name='unknown option' type='Color' default='NOSUCH'/>",
			"<add name='bit flags' type='Bits' default='ONE | FOUR'/>",
			"<add name='hexadecimal' type='uint' default='0x20'/>",
			"<add name='negative' type='int' default='-7'/>",
			"<add name='float' type='float' default='128.5'/>",
			"<add name='bool' type='bool' default='yes'/>",
			"<add name='color' type='Color3' default='#ff8000'/>",
			"<add name='link' type='Ref' default='5'/>",
			"<add name='not a number' type='ushort' default='notanumber'/>",
			"<add name='later compound' type='Later'/>",
			"<add name='later enum' type='LaterEnum' default='B'/>",
			"<add name='vertex desc' type='BSVertexDesc' arg='Vertex Desc\\Vertex Attributes'/>",
			"<add name='only a user version' type='int' userver='12'/>",
			"</niobject>",
			"<compound name='Later'><add name='x' type='int'/></compound>",
			"<enum name='LaterEnum' storage='uint'><option value='1' name='B'/></enum>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		NifModel nif;
		QModelIndex row = nif.insertNiBlock( "Row" );
		QVERIFY( row.isValid() );

		compareRows( describeRows( nif, row ), QString(
			"every attribute: int template={TEMPLATE} arg={1} arr1={Count} arr2={Size} cond={X > 1 && Y} vercond={Version > 10 && (User Version == 12) && (User Version 2 == 130)} text={text a} ver1=0x04000000 ver2=0x14000005\n"
			"template type: \n"
			"enum: Color value={GREEN}\n"
			"unknown option: Color value={RED}\n"
			"bit flags: Bits value={ONE | FOUR}\n"
			"hexadecimal: uint value={32}\n"
			"negative: int value={-7}\n"
			"float: float value={128.500000}\n"
			"bool: bool value={yes}\n"
			"color: Color3 value={#ff8000}\n"
			"link: Ref value={5 <invalid>}\n"
			"not a number: ushort value={0}\n"
			"later compound: Later\n"
			"later enum: LaterEnum\n"
			"vertex desc: BSVertexDesc arg={Vertex Desc}\n"
			"only a user version: int vercond={(User Version == 12)} value={0}\n"
		) );
	}

	//! The compounds a row can have: a row of a compound type nests the rows of the compound, except the mixins
	void nif_compoundRows()
	{
		QString err = loadNif( schemaDoc( {
			"<version num='20.0.0.5'/>",
			"<compound name='HavokFilter'><add name='Layer' type='byte'/></compound>",
			"<compound name='Pair'><add name='First' type='int'/><add name='Second' type='float'/></compound>",
			"<niobject name='Holder'>",
			"<add name='plain' type='HavokFilter'/>",
			"<add name='with a condition' type='HavokFilter' cond='Flag'/>",
			"<add name='array' type='HavokFilter' arr1='3'/>",
			"<add name='templated' type='HavokFilter' template='TEMPLATE'/>",
			"<add name='from a version' type='HavokFilter' ver1='10.0.0.0'/>",
			"<add name='for a user version' type='HavokFilter' userver='12'/>",
			"<add name='not a mixin' type='Pair'/>",
			"</niobject>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		NifModel nif;
		QModelIndex holder = nif.insertNiBlock( "Holder" );
		QVERIFY( holder.isValid() );

		compareRows( describeRows( nif, holder ), QString(
			"Layer: byte value={0}\n"
			"with a condition: HavokFilter cond={Flag}\n"
			"  Layer: byte value={0}\n"
			"array: HavokFilter arr1={3}\n"
			"templated: HavokFilter template={TEMPLATE}\n"
			"  Layer: byte value={0}\n"
			"from a version: HavokFilter ver1=0x0a000000\n"
			"  Layer: byte value={0}\n"
			"for a user version: HavokFilter vercond={(User Version == 12)}\n"
			"  Layer: byte value={0}\n"
			"not a mixin: Pair\n"
			"  First: int value={0}\n"
			"  Second: float value={0.000000}\n"
		) );
	}

	//! Ancestors, abstract blocks, fixed compounds, and the types of nif.xml that a compound or a niobject may not replace
	void nif_blocks()
	{
		QString err = loadNif( schemaDoc( {
			"<version num='20.0.0.5'/>",
			"<niobject name='Root' abstract='1'><add name='Name' type='string'/></niobject>",
			"<niobject name='Mid' inherit='Root' abstract='1' externalcond='1'><add name='Mid Value' type='int'/></niobject>",
			"<niobject name='Leaf' inherit='Mid'>",
			"Leaf block.",
			"<add name='Leaf Value' type='int'/>",
			"</niobject>",
			"<niobject name='Other' abstract='0'/>",
			"<niobject name='Odd' abstract='yes'/>",
			"<compound name='Plain'/>",
			"<compound name='Ext' externalcond='1'/>",
			"<compound name='BSVertexDataTest'/>",
			"<compound name='Color3'>",
			"Description of an internal type.",
			"<add name='Dropped' type='float'/>",
			"</compound>",
			"<niobject name='Vector3'><add name='Dropped Too' type='float'/></niobject>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		// only abstract="1" makes a block abstract
		QStringList blocks = NifModel::allNiBlocks();
		blocks.sort();
		QCOMPARE( blocks, QStringList() << "Leaf" << "Odd" << "Other" );
		QVERIFY( NifModel::isNiBlock( "Leaf" ) );
		QVERIFY( !NifModel::isNiBlock( "Mid" ) );
		QVERIFY( NifModel::isAncestor( "Mid" ) );
		QVERIFY( NifModel::isAncestor( "Root" ) );
		QVERIFY( !NifModel::isAncestor( "Leaf" ) );

		NifModel nif;
		QVERIFY( nif.inherits( "Leaf", "Mid" ) );
		QVERIFY( nif.inherits( "Leaf", "Root" ) );
		QVERIFY( !nif.inherits( "Root", "Leaf" ) );
		QVERIFY( !nif.inherits( "Other", "Root" ) );

		QModelIndex leaf = nif.insertNiBlock( "Leaf" );
		compareRows( describeRows( nif, leaf ), "Name: string\nMid Value: int value={0}\nLeaf Value: int value={0}\n" );

		// externalcond="1" and the name BSVertexData... make a compound or a block "fixed"
		QVERIFY( NifModel::isFixedCompound( "Ext" ) );
		QVERIFY( NifModel::isFixedCompound( "BSVertexDataTest" ) );
		QVERIFY( NifModel::isFixedCompound( "Mid" ) );
		QVERIFY( !NifModel::isFixedCompound( "Plain" ) );
		QVERIFY( !NifModel::isFixedCompound( "Leaf" ) );

		// a compound or a niobject named like an internal type only describes it
		QVERIFY( NifModel::isCompound( "Plain" ) );
		QVERIFY( !NifModel::isCompound( "Color3" ) );
		QVERIFY( !NifModel::isCompound( "Leaf" ) );
		QVERIFY( !NifModel::isNiBlock( "Vector3" ) );
		QCOMPARE( NifValue::typeDescription( "Color3" ), QString( "<p><b>Color3</b></p><p>Description of an internal type.</p>" ) );
	}

	//! Enums and bit flags: the values (decimal, hexadecimal, octal), the names, the descriptions, the storage type
	void nif_enums()
	{
		// named for this test: NifValue::typeDescription() keeps the text of an enum for the life of the process
		QString err = loadNif( schemaDoc( {
			"<enum name='SchemaColor' storage='uint'>",
			"Colors.",
			"<option value='0' name='RED'>The red one.</option>",
			"<option value='1' name='GREEN'>The green one.</option>",
			"<option value='0x10' name='BLUE'>Hex value.</option>",
			"<option value='010' name='OCT'>Octal value.</option>",
			"<option value='20' name='SPLIT'>in <!-- a comment --> two parts</option>",
			"</enum>",
			"<bitflags name='SchemaBits' storage='ushort'>",
			"Bit flags.",
			"<option bit='0' value='0' name='ONE'/>",
			"<option value='1' name='TWO'>Second bit.</option>",
			"<option value='3' name='FOUR'>Fourth.</option>",
			"</bitflags>",
			"<enum name='SchemaNothing' storage='byte'/>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		QCOMPARE( NifValue::enumType( "SchemaColor" ), NifValue::eDefault );
		QCOMPARE( NifValue::enumType( "SchemaBits" ), NifValue::eFlags );
		QCOMPARE( NifValue::enumType( "SchemaNothing" ), NifValue::eDefault );
		QCOMPARE( NifValue::type( "SchemaColor" ), NifValue::tUInt );
		QCOMPARE( NifValue::type( "SchemaBits" ), NifValue::tWord );
		QCOMPARE( NifValue::type( "SchemaNothing" ), NifValue::tByte );

		// in the order of their values
		QCOMPARE( NifValue::enumOptions( "SchemaColor" ), QStringList() << "RED" << "GREEN" << "OCT" << "BLUE" << "SPLIT" );
		QCOMPARE( NifValue::enumOptions( "SchemaBits" ), QStringList() << "ONE" << "TWO" << "FOUR" );
		QVERIFY( NifValue::enumOptions( "SchemaNothing" ).isEmpty() );

		QCOMPARE( NifValue::enumOptionValue( "SchemaColor", "GREEN" ), 1u );
		QCOMPARE( NifValue::enumOptionValue( "SchemaColor", "BLUE" ), 16u );
		QCOMPARE( NifValue::enumOptionValue( "SchemaColor", "OCT" ), 8u );
		QCOMPARE( NifValue::enumOptionName( "SchemaColor", 16 ), QString( "BLUE" ) );
		QCOMPARE( NifValue::enumOptionText( "SchemaColor", 1 ), QString( "The green one." ) );
		QCOMPARE( NifValue::enumOptionText( "SchemaColor", 20 ), QString( "intwo parts" ) );

		// the value of a bit flag option is the number of its bit
		QCOMPARE( NifValue::enumOptionValue( "SchemaBits", "ONE" ), 1u );
		QCOMPARE( NifValue::enumOptionValue( "SchemaBits", "TWO" ), 2u );
		QCOMPARE( NifValue::enumOptionValue( "SchemaBits", "FOUR" ), 8u );
		QCOMPARE( NifValue::enumOptionValue( "SchemaBits", "ONE | FOUR" ), 9u );
		QCOMPARE( NifValue::enumOptionText( "SchemaBits", 0 ), QString() );
		QCOMPARE( NifValue::enumOptionText( "SchemaBits", 3 ), QString( "Fourth." ) );

		QVERIFY( NifValue::typeDescription( "SchemaColor" ).contains( "Colors." ) );
		QVERIFY( NifValue::typeDescription( "SchemaBits" ).contains( "Bit flags." ) );
	}

	//! The text of a version is a description nobody reads; its num may be padded and need not have four parts
	void nif_versions()
	{
		const QStringList supported = QStringList()
			<< "1.0" << "3.03" << "4.0.0.2" << "10.1.0.0" << "20.2.0.7" << "123456" << "2.3" << "3.3.0.13" << "1.2.3" << "0.5";

		QString err = loadNif( schemaDoc( {
			"<version num='1.0'>old style</version>",
			"<version num='3.03'>old style with leading digits</version>",
			"<version num='4.0.0.2'>new style <!-- comment --> and text</version>",
			"<version num=' 10.1.0.0 '>padded</version>",
			"<version num='20.2.0.7'/>",
			"<version num='123456'>an integer</version>",
			"<version num='2.3'/>",
			"<version num='3.3.0.13'/>",
			"<version num='3.3.0.13'>declared twice</version>",
			"<version num='1.2.3'/>",
			"<version num='0.5'/>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		for ( const QString & text : supported ) {
			quint32 v = NifModel::version2number( text );
			QVERIFY2( v != 0, qPrintable( text ) );
			QVERIFY2( NifModel::isVersionSupported( v ), qPrintable( text ) );
		}

		QVERIFY( !NifModel::isVersionSupported( NifModel::version2number( "4.0.0.3" ) ) );
		QVERIFY( !NifModel::isVersionSupported( NifModel::version2number( "20.0.0.5" ) ) );
		QVERIFY( !NifModel::isVersionSupported( 0 ) );
	}

	//! The text of a compound, a basic type, an enum or a row: trimmed, joined where tags, comments and CDATA split it
	void nif_descriptions()
	{
		QString err = loadNif( schemaDoc( {
			"<version num='20.0.0.5'/>",
			"<basic name='int'>An integer.</basic>",
			"<compound name='Spaced'>",
			"       leading and trailing blanks around the compound text      ",
			"<add name='a' type='int'/>",
			"</compound>",
			"<compound name='Split'>",
			"first",
			"<add name='a' type='int'>a text</add>",
			"second",
			"<add name='b' type='int'/>",
			"third",
			"</compound>",
			"<compound name='Markup'>",
			"a &lt;b&gt; and",
			"two lines<!-- c -->",
			"<add name='a' type='int'/>",
			"</compound>",
			"<niobject name='Rows'>",
			"<add name='multi line' type='int'>",
			"    multi line",
			"    description with    inner   blanks",
			"    and a tab\there",
			"</add>",
			"<add name='blanks only' type='int'>   </add>",
			"<add name='empty' type='int'></add>",
			"<add name='no text' type='int' />",
			"</niobject>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		QCOMPARE( NifValue::typeDescription( "int" ), QString( "<p><b>int</b></p><p>An integer.</p>" ) );
		QCOMPARE( NifValue::typeDescription( "Spaced" ), QString( "<p><b>Spaced</b></p><p>leading and trailing blanks around the compound text</p>" ) );
		// the text on both sides of a child belongs to one description
		QCOMPARE( NifValue::typeDescription( "Split" ), QString( "<p><b>Split</b></p><p>firstsecondthird</p>" ) );
		// the description is HTML: "<" is escaped, a line break is <br/>
		QCOMPARE( NifValue::typeDescription( "Markup" ), QString( "<p><b>Markup</b></p><p>a &lt;b> and<br/>two lines</p>" ) );

		NifModel nif;
		QModelIndex rows = nif.insertNiBlock( "Rows" );
		QVERIFY( rows.isValid() );
		compareRows( describeRows( nif, rows ), QString(
			"multi line: int text={multi line\\n    description with    inner   blanks\\n    and a tab\\there} value={0}\n"
			"blanks only: int value={0}\n"
			"empty: int value={0}\n"
			"no text: int value={0}\n"
		) );
	}

	//! Entities, character references, comments, CDATA sections and processing instructions inside the text of a row
	void nif_markup()
	{
		QString err = loadNif( schemaDoc( {
			"<version num='20.0.0.5'/>",
			"<niobject name='Markup'>",
			"<add name='entities' type='int'>less &lt; than, greater &gt; than, amp &amp; and quotes &quot;q&quot; &apos;a&apos;</add>",
			"<add name='character references' type='int'>char refs &#65;&#x42;&#x20AC; done</add>",
			"<add name='entity only' type='int'>  &amp;  </add>",
			"<add name='escaped markup' type='int'>&lt;b&gt;bold&lt;/b&gt;</add>",
			"<add name='comment' type='int'>before <!-- inline comment --> after</add>",
			"<add name='two comments' type='int'>one <!-- c1 --> two <!-- c2 --> three</add>",
			"<add name='only a comment' type='int'><!-- only a comment --></add>",
			"<add name='cdata' type='int'>before <![CDATA[ cdata <b>text</b> & more ]]> after</add>",
			"<add name='only cdata' type='int'><![CDATA[only cdata]]></add>",
			"<add name='empty cdata' type='int'><![CDATA[]]></add>",
			"<add name='cdata inside a word' type='int'>x<![CDATA[ y ]]>z</add>",
			"<add name='processing instruction' type='int'>before <?target data?> after</add>",
			"</niobject>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		NifModel nif;
		QModelIndex markup = nif.insertNiBlock( "Markup" );
		QVERIFY( markup.isValid() );
		compareRows( describeRows( nif, markup ), QString(
			"entities: int text={less < than, greater > than, amp & and quotes \"q\" 'a'} value={0}\n"
			"character references: int text={char refs AB€ done} value={0}\n"
			"entity only: int text={&} value={0}\n"
			"escaped markup: int text={<b>bold</b>} value={0}\n"
			"comment: int text={beforeafter} value={0}\n"
			"two comments: int text={onetwothree} value={0}\n"
			"only a comment: int value={0}\n"
			"cdata: int text={beforecdata <b>text</b> & moreafter} value={0}\n"
			"only cdata: int text={only cdata} value={0}\n"
			"empty cdata: int value={0}\n"
			"cdata inside a word: int text={xyz} value={0}\n"
			"processing instruction: int text={beforeafter} value={0}\n"
		) );
	}

	//! A document that is fine, however it is written: both loaders take it as a compound named A
	void validDocuments_data()
	{
		QTest::addColumn<QByteArray>( "xml" );

		QTest::newRow( "minimal" ) << QByteArray( "<niftoolsxml><compound name='A'/></niftoolsxml>" );
		QTest::newRow( "spacing, both quotes, a name split over lines" )
			<< QByteArray( "<?xml version=\"1.0\"?>\n<niftoolsxml  >\n<compound name = \"A\" >\n<add name = 'a'\ttype\n=\n\"int\" />\n</compound  >\n</niftoolsxml\n>\n" );
		QTest::newRow( "comments, processing instructions and blanks after the root" )
			<< QByteArray( "<niftoolsxml><compound name='A'/></niftoolsxml>\n<!-- fine -->\n<?pi fine?>\n  \n" );
		QTest::newRow( "comments, processing instructions and a DOCTYPE before the root" )
			<< QByteArray( "<?xml version='1.0'?><!-- a --><?pi data?><!DOCTYPE niftoolsxml><!-- b --><niftoolsxml><compound name='A'/></niftoolsxml>" );
		QTest::newRow( "text in the root and in a version" )
			<< QByteArray( "<niftoolsxml>root text before<version num='4.0.0.2'>version text <!-- c --> more</version>root text between"
						   "<compound name='A'><add name='a' type='int'/></compound>root text after</niftoolsxml>" );
		QTest::newRow( "default namespace" ) << QByteArray( "<niftoolsxml xmlns='urn:example:nif'><compound name='A'/></niftoolsxml>" );
		QTest::newRow( "namespace declarations and prefixed attributes" )
			<< QByteArray( "<niftoolsxml xmlns:p='urn:p' xml:lang='en'><compound name='A' p:extra='1' xml:space='preserve'/></niftoolsxml>" );
		QTest::newRow( "unknown attributes" )
			<< QByteArray( "<niftoolsxml version='0.9.0.0' extra='1'><compound name='A' extra='ignored'><add name='a' type='int' unknown='x' bit='3'/></compound></niftoolsxml>" );
		QTest::newRow( "greater-than sign and entity in an attribute value" )
			<< QByteArray( "<niftoolsxml><compound name='A' extra='a &gt; b &amp; c > d'/></niftoolsxml>" );
		QTest::newRow( "character references in a name" ) << QByteArray( "<niftoolsxml><compound name='&#65;'/></niftoolsxml>" );
		QTest::newRow( "declared twice, the last one counts" )
			<< QByteArray( "<niftoolsxml><compound name='B'/><version num='4.0.0.2'/><compound name='A'/></niftoolsxml>" );
		QTest::newRow( "a description of a million characters" )
			<< ( QByteArray( "<niftoolsxml><compound name='A'>" ) + QByteArray( 1000000, 'x' ) + QByteArray( "<add name='a' type='int'/></compound></niftoolsxml>" ) );
	}

	void validDocuments()
	{
		QFETCH( QByteArray, xml );

		QString err = loadNif( xml );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( NifModel::isCompound( "A" ) );

		err = loadKfm( xml );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( KfmModel::isCompound( "A" ) );
	}

	//! The encoding comes from the byte order mark or the declaration, and names may use any character
	void encodings_data()
	{
		QTest::addColumn<QByteArray>( "xml" );

		auto document = []( const char * declaration ) {
			return QString::fromUtf8( "%1\n<niftoolsxml>\n<compound name=\"caf\xc3\xa9\">caf\xc3\xa9 \xe2\x82\xac<add name=\"a\" type=\"int\"/></compound>\n</niftoolsxml>\n" )
				.arg( QString::fromLatin1( declaration ) );
		};

		QTest::newRow( "UTF-8 declared" ) << document( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" ).toUtf8();
		QTest::newRow( "UTF-8 by default" ) << document( "<?xml version=\"1.0\"?>" ).toUtf8();
		QTest::newRow( "UTF-8 without a declaration" ) << document( "" ).toUtf8();
		QTest::newRow( "UTF-8 with a byte order mark" ) << QByteArray( "\xef\xbb\xbf" ) + document( "<?xml version=\"1.0\"?>" ).toUtf8();
		QTest::newRow( "UTF-16 little endian" ) << utf16( document( "<?xml version=\"1.0\" encoding=\"UTF-16\"?>" ), false );
		QTest::newRow( "UTF-16 big endian" ) << utf16( document( "<?xml version=\"1.0\" encoding=\"UTF-16\"?>" ), true );
	}

	void encodings()
	{
		QFETCH( QByteArray, xml );

		const QString name = QString::fromUtf8( "caf\xc3\xa9" );
		const QString description = QString::fromUtf8( "caf\xc3\xa9 \xe2\x82\xac" );

		QString err = loadNif( xml );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( NifModel::isCompound( name ) );
		QCOMPARE( NifValue::typeDescription( name ), "<p><b>" + name + "</b></p><p>" + description + "</p>" );

		err = loadKfm( xml );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( KfmModel::isCompound( name ) );
	}

	//! ISO-8859-1 is an encoding of the declaration (the byte 0xe9 is an e with an acute accent)
	void encodings_latin1()
	{
		QByteArray xml = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n<niftoolsxml><compound name=\"caf\xe9\">caf\xe9<add name=\"a\" type=\"int\"/></compound></niftoolsxml>\n";

		QString err = loadNif( xml );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( NifModel::isCompound( QString::fromUtf8( "caf\xc3\xa9" ) ) );

		err = loadKfm( xml );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( KfmModel::isCompound( QString::fromUtf8( "caf\xc3\xa9" ) ) );
	}

	//! A second parse replaces everything the first one registered
	void nif_secondParse()
	{
		QString err = loadNif( schemaDoc( {
			"<version num='4.0.0.2'/>",
			"<enum name='First' storage='uint'><option value='1' name='ONE'>one</option></enum>",
			"<compound name='A'><add name='a' type='int'/></compound>",
			"<niobject name='BlockA'/>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( NifModel::isCompound( "A" ) );
		QVERIFY( NifModel::isNiBlock( "BlockA" ) );
		QVERIFY( NifModel::isVersionSupported( NifModel::version2number( "4.0.0.2" ) ) );
		QCOMPARE( NifValue::enumOptions( "First" ), QStringList( "ONE" ) );

		err = loadNif( schemaDoc( {
			"<version num='3.3.0.13'/>",
			"<compound name='B'><add name='b' type='int'/></compound>",
			"<niobject name='BlockB'/>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QVERIFY( !NifModel::isCompound( "A" ) );
		QVERIFY( !NifModel::isNiBlock( "BlockA" ) );
		QVERIFY( !NifModel::isVersionSupported( NifModel::version2number( "4.0.0.2" ) ) );
		QVERIFY( NifValue::enumOptions( "First" ).isEmpty() );
		QVERIFY( NifModel::isCompound( "B" ) );
		QVERIFY( NifModel::isNiBlock( "BlockB" ) );
		QVERIFY( NifModel::isVersionSupported( NifModel::version2number( "3.3.0.13" ) ) );

		// a failed parse keeps nothing of either
		QVERIFY( !loadNif( "not xml" ).isEmpty() );
		QVERIFY( !NifModel::isCompound( "B" ) );
		QVERIFY( !NifModel::isNiBlock( "BlockB" ) );
		QVERIFY( !NifModel::isVersionSupported( NifModel::version2number( "3.3.0.13" ) ) );
	}

	// ---- what the XML rules say, and what the old SAX reader let through

	//! Documents that are not well-formed XML, which QXmlSimpleReader accepted because it checked less. The line is the
	//! one the reader stopped at; a negative line accepts any. (Not here: a second DOCTYPE, which QXmlStreamReader
	//! refuses only from Qt 5.15.15 on, and a byte above 0x7f in a document declared US-ASCII, which depends on the
	//! text codec)
	void notWellFormed_data()
	{
		QTest::addColumn<QByteArray>( "xml" );
		QTest::addColumn<int>( "line" );

		QTest::newRow( "attribute given twice" )
			<< schemaDoc( { "<compound name='A'>", "<add name='a' name='b' type='int'/>", "</compound>" } ) << 5;
		QTest::newRow( "entity that is not declared" )
			<< schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>text &undeclared; text</add>", "</compound>" } ) << 5;
		QTest::newRow( "entity that is not declared, no DOCTYPE" )
			<< QByteArray( "<niftoolsxml>\n<compound name='A'>&undeclared;</compound>\n</niftoolsxml>\n" ) << 2;
		QTest::newRow( "character reference beyond Unicode" )
			<< schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>&#x110000;</add>", "</compound>" } ) << 5;
		QTest::newRow( "character reference to NUL" )
			<< schemaDoc( { "<compound name='A'>", "<add name='a' type='int'>&#0;</add>", "</compound>" } ) << 5;
		QTest::newRow( "XML version that does not exist" ) << QByteArray( "<?xml version=\"9.9\"?>\n<niftoolsxml/>\n" ) << 1;
		QTest::newRow( "NUL in text" )
			<< QByteArray( "<niftoolsxml>\n<compound name='A'>nul " ) + QByteArray( 1, '\0' ) + QByteArray( " byte</compound>\n</niftoolsxml>\n" ) << 2;
		QTest::newRow( "control characters in text" )
			<< QByteArray( "<niftoolsxml>\n<compound name='A'>ctl \x01\x08\x0b\x1f end</compound>\n</niftoolsxml>\n" ) << 2;
		QTest::newRow( "byte that is not UTF-8" ) << QByteArray( "<niftoolsxml>\n<compound name='A'>\xff</compound>\n</niftoolsxml>\n" ) << -1;
		QTest::newRow( "encoding that is not known" )
			<< QByteArray( "<?xml version='1.0' encoding='no-such-encoding'?>\n<niftoolsxml/>\n" ) << -1;
		QTest::newRow( "colons in an attribute name" )
			<< schemaDoc( { "<compound name='A' a:b:c='1'/>" } ) << 4;
	}

	void notWellFormed()
	{
		QFETCH( QByteArray, xml );
		QFETCH( int, line );

		verifyError( loadNif( xml ), "NIF", line, "Syntax error" );
		verifyError( loadKfm( xml ), "KFM", line, "Syntax error" );
	}

	//! A CR LF or a CR is a line end, and is read as the LF the same document would have: the text of a description
	//! and an attribute value never has a CR. A line end inside an attribute value is a blank
	void lineEndsAreNormalized()
	{
		const QList<const char *> lines = {
			"<version num='20.0.0.5'/>",
			"<compound name='Described'>",
			"first line",
			"second line",
			"<add name='a' type='int'/>",
			"</compound>",
			"<niobject name='Row'>",
			"<add name='text' type='int' arg='line one",
			"line two\tand a tab'>",
			"    one",
			"    two",
			"</add>",
			"<add name='references' type='int' arg='a&#10;b&#9;c&#13;d'/>",
			"</niobject>",
		};
		QByteArray lf;
		for ( const char * line : lines )
			lf += QByteArray( line ) + '\n';
		lf = "<?xml version=\"1.0\"?>\n<niftoolsxml>\n" + lf + "</niftoolsxml>\n";

		QByteArray crlf = lf, cr = lf;
		crlf.replace( '\n', "\r\n" );
		cr.replace( '\n', '\r' );

		QString err = loadNif( lf );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		NifModel nif;
		QModelIndex row = nif.insertNiBlock( "Row" );
		QVERIFY( row.isValid() );
		const QString expectedRows = describeRows( nif, row );
		const QString expectedDescription = NifValue::typeDescription( "Described" );
		QCOMPARE( expectedDescription, QString( "<p><b>Described</b></p><p>first line<br/>second line</p>" ) );
		compareRows( expectedRows, QString(
			"text: int arg={line one line two and a tab} text={one\\n    two} value={0}\n"
			"references: int arg={a\\nb\\tc\\rd} value={0}\n"
		) );

		for ( const QByteArray & xml : { crlf, cr } ) {
			err = loadNif( xml );
			QVERIFY2( err.isEmpty(), qPrintable( err ) );

			NifModel other;
			row = other.insertNiBlock( "Row" );
			QVERIFY( row.isValid() );
			compareRows( describeRows( other, row ), expectedRows );
			QCOMPARE( NifValue::typeDescription( "Described" ), expectedDescription );
		}
	}

	//! A fingerprint of what NifModel makes of every block of nif.xml: all rows with their attributes and texts, and
	//! the description of every type they use
	static QByteArray blockFingerprint()
	{
		NifModel nif;
		QCryptographicHash hash( QCryptographicHash::Sha1 );
		QSet<QString> types;

		QStringList names = NifModel::allNiBlocks();
		names.sort();
		for ( const QString & name : names ) {
			QModelIndex block = nif.insertNiBlock( name );
			hash.addData( describeRows( nif, block ).toUtf8() );

			QList<QModelIndex> todo = { block };
			while ( !todo.isEmpty() ) {
				QModelIndex parent = todo.takeLast();
				for ( int r = 0; r < nif.rowCount( parent ); r++ ) {
					QModelIndex i = nif.index( r, 0, parent );
					types.insert( nif.itemType( i ) );
					todo.append( i );
				}
			}
		}

		QStringList sorted = types.values();
		sorted.sort();
		for ( const QString & type : sorted )
			hash.addData( NifValue::typeDescription( type ).toUtf8() );

		return hash.result().toHex();
	}

	//! nif.xml checked out with CR LF line ends (git's autocrlf on Windows) makes the same tables as with LF
	void nif_realFileWithCrLf()
	{
		QFile f( TestEnv::nifXmlPath() );
		QVERIFY( f.open( QIODevice::ReadOnly ) );
		// The file on disk has the line ends of the checkout: the pinned nifxml says "*.xml text", so a Windows
		// checkout has CR LF already. Both variants are made from one with LF only, and a CR left over after that
		// would be a line end of neither
		QByteArray lf = f.readAll();
		lf.replace( "\r\n", "\n" );
		QVERIFY( !lf.contains( '\r' ) );
		QByteArray crlf = lf;
		crlf.replace( '\n', "\r\n" );

		QString err = loadNif( lf );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		const QByteArray expected = blockFingerprint();

		err = loadNif( crlf );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
		QCOMPARE( blockFingerprint(), expected );
	}

	// ---- kfm.xml

	void kfm_rows()
	{
		QString err = loadKfm( schemaDoc( {
			"<version num='2.0.0.0b'/>",
			"<compound name='Pair'>",
			"<add name='First' type='int'/>",
			"<add name='Second' type='SizedString' ver2='1.2.4b'/>",
			"</compound>",
			"<compound name='Kfm' extra='ignored'>",
			"<add name='every attribute' type='int' template='TEMPLATE' arg='1' arr1='N' arr2='M' cond='X &gt; 1' ver1='1.0' ver2='2.0.0.0b' "
			"abstract='1' binary='1' vercond='Y' userver='3' default='5' unknown='x'>the text of a row is not kept</add>",
			"<add name='template type' type='TEMPLATE'/>",
			"<add name='pairs' type='Pair' arr1='Num'/>",
			"<add name='later compound' type='Later'/>",
			"</compound>",
			"<compound name='Later'><add name='x' type='int'/></compound>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		KfmModel kfm;
		QModelIndex root = kfm.getKFMroot();
		QVERIFY( root.isValid() );
		compareRows( describeRows( kfm, root ), QString(
			"every attribute: int template={TEMPLATE} arg={1} arr1={N} arr2={M} cond={X > 1} ver1=0x01000000 ver2=0x0200000b\n"
			"template type: TEMPLATE\n"
			"pairs: Pair arr1={Num}\n"
			"later compound: Later\n"
		) );
	}

	//! kfm.xml reads the same version strings, the letters are digits too
	void kfm_versions()
	{
		const QStringList supported = QStringList() << "1.0" << "1.2.4b" << "2.0.0.0b" << "123" << "1.2" << "ff.1" << "20.2.0.7";

		QString err = loadKfm( schemaDoc( {
			"<version num='1.0'/>",
			"<version num='1.2.4b'>Oblivion</version><!--16927488-->",
			"<version num=' 2.0.0.0b '>padded</version>",
			"<version num='123'/>",
			"<version num='1.2'/>",
			"<version num='ff.1'/>",
			"<version num='20.2.0.7'/>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		for ( const QString & text : supported ) {
			quint32 v = KfmModel::version2number( text );
			QVERIFY2( v != 0, qPrintable( text ) );
			QVERIFY2( KfmModel::isVersionSupported( v ), qPrintable( text ) );
		}

		QVERIFY( !KfmModel::isVersionSupported( KfmModel::version2number( "2.0.0.1" ) ) );
	}

	//! A compound that is declared again replaces the first one; text, comments and CDATA in a compound are skipped
	void kfm_declaredTwice()
	{
		QString err = loadKfm( schemaDoc( {
			"<compound name='Kfm'><add name='first' type='int'/></compound>",
			"<compound name='Kfm'>",
			"text &lt; between <!-- c --> tags <![CDATA[cdata]]>",
			"<add name='second' type='float'>description &amp; text</add>",
			"</compound>",
		} ) );
		QVERIFY2( err.isEmpty(), qPrintable( err ) );

		KfmModel kfm;
		compareRows( describeRows( kfm, kfm.getKFMroot() ), "second: float value={0.000000}\n" );
	}
};

REGISTER_TEST( tst_XmlSchema )

#include "tst_xmlload.moc"
