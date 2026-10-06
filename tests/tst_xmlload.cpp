#include "testenv.h"
#include "testregistry.h"

#include "data/nifvalue.h"
#include "model/kfmmodel.h"
#include "model/nifmodel.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTest>


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

#include "tst_xmlload.moc"
