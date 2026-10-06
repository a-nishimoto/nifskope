#include "testenv.h"

#include "model/kfmmodel.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QMessageBox>
#include <QSettings>


QString TestEnv::sourceDir()
{
	return QDir::cleanPath( QStringLiteral( NIFSKOPE_SOURCE_DIR ) );
}

QString TestEnv::nifXmlPath()
{
	return sourceDir() + QStringLiteral( "/build/docsys/nifxml/nif.xml" );
}

QString TestEnv::kfmXmlPath()
{
	return sourceDir() + QStringLiteral( "/build/docsys/kfmxml/kfm.xml" );
}

QString TestEnv::reloadXml()
{
	QString err = NifModel::parseXmlDescription( nifXmlPath() );

	if ( err.isEmpty() )
		err = KfmModel::parseXmlDescription( kfmXmlPath() );

	return err;
}

QList<TestEnv::Profile> TestEnv::profiles()
{
	return QList<Profile>()
		<< Profile{ "NetImmerse 3.1 (pre-footer)", "3.1", 0, 0 }
		<< Profile{ "Morrowind 4.0.0.2", "4.0.0.2", 0, 0 }
		<< Profile{ "Civ IV 4.2.2.0", "4.2.2.0", 0, 0 }
		<< Profile{ "10.0.1.0", "10.0.1.0", 0, 0 }
		<< Profile{ "10.2.0.0", "10.2.0.0", 0, 0 }
		<< Profile{ "Oblivion 20.0.0.5", "20.0.0.5", 11, 11 }
		<< Profile{ "20.1.0.3 (string table)", "20.1.0.3", 0, 0 }
		<< Profile{ "Fallout 3 20.2.0.7", "20.2.0.7", 11, 34 }
		<< Profile{ "Skyrim LE 20.2.0.7", "20.2.0.7", 12, 83 }
		<< Profile{ "Skyrim SE 20.2.0.7", "20.2.0.7", 12, 100 }
		<< Profile{ "Fallout 4 20.2.0.7", "20.2.0.7", 12, 130 };
}

std::unique_ptr<NifModel> TestEnv::makeModel( const Profile & p )
{
	{
		QSettings settings;
		settings.beginGroup( "Settings/NIF/Startup Defaults" );
		settings.setValue( "Version", QString::fromLatin1( p.version ) );
		settings.setValue( "User Version", p.userVersion );
		settings.setValue( "User Version 2", p.userVersion2 );
		settings.endGroup();
	}

	// The constructor reads the startup defaults and clear()s into a fresh header/footer
	return std::unique_ptr<NifModel>( new NifModel );
}

//! Append a block, set its name; returns the block index
static QModelIndex addBlock( NifModel & nif, const QString & type, const QString & name )
{
	QModelIndex idx = nif.insertNiBlock( type );
	if ( idx.isValid() )
		nif.set<QString>( idx, "Name", name );
	return idx;
}

bool TestEnv::buildScene( NifModel & nif )
{
	bool ok = true;

	// clear() leaves the 3-line Copyright array of pre-3.1 headers empty; give it rows so the file is writable
	if ( nif.getVersionNumber() <= 0x03010000 ) {
		ok &= nif.updateArray( nif.getHeader(), "Copyright" );
		nif.setArray<QString>( nif.getHeader(), "Copyright", QVector<QString>() << "line one" << "line two" << "line three" );
	}

	QModelIndex iRoot = addBlock( nif, "NiNode", "Scene Root" );
	QModelIndex iChild = addBlock( nif, "NiNode", "Child" );
	QModelIndex iShape = addBlock( nif, "NiTriShape", "Shape" );
	QModelIndex iData = nif.insertNiBlock( "NiTriShapeData" );
	QModelIndex iExtra = addBlock( nif, "NiStringExtraData", "Note" );

	ok &= iRoot.isValid() && iChild.isValid() && iShape.isValid() && iData.isValid() && iExtra.isValid();

	ok &= nif.set<QString>( iExtra, "String Data", "hello nifskope" );
	if ( nif.getVersionNumber() <= 0x04020200 )
		ok &= nif.set<int>( iExtra, "Bytes Remaining", 14 + 4 );

	ok &= nif.set<Vector3>( iRoot, "Translation", Vector3( 1.0f, 2.0f, 3.0f ) );
	ok &= nif.set<float>( iRoot, "Scale", 1.5f );
	ok &= nif.set<Vector3>( iShape, "Translation", Vector3( -0.5f, 0.25f, 8.0f ) );

	// Children: Child, Shape
	ok &= nif.set<int>( iRoot, "Num Children", 2 );
	ok &= nif.updateArray( iRoot, "Children" );
	ok &= nif.setLinkArray( iRoot, "Children", QVector<qint32>() << nif.getBlockNumber( iChild ) << nif.getBlockNumber( iShape ) );

	// Extra data: a chain head before 10.0.1.0, a list afterwards
	if ( nif.getVersionNumber() >= 0x0A000100 ) {
		ok &= nif.set<int>( iRoot, "Num Extra Data List", 1 );
		ok &= nif.updateArray( iRoot, "Extra Data List" );
		ok &= nif.setLinkArray( iRoot, "Extra Data List", QVector<qint32>() << nif.getBlockNumber( iExtra ) );
	} else {
		ok &= nif.setLink( iRoot, "Extra Data", nif.getBlockNumber( iExtra ) );
	}

	// Geometry
	ok &= nif.setLink( iShape, "Data", nif.getBlockNumber( iData ) );

	ok &= nif.set<int>( iData, "Num Vertices", 3 );
	ok &= nif.updateArray( iData, "Vertices" );
	nif.setArray<Vector3>( iData, "Vertices", QVector<Vector3>()
		<< Vector3( 0.0f, 0.0f, 0.0f ) << Vector3( 1.0f, 0.0f, 0.0f ) << Vector3( 0.0f, 1.0f, 0.0f ) );
	ok &= nif.set<int>( iData, "Num Triangles", 1 );
	ok &= nif.set<int>( iData, "Num Triangle Points", 3 );
	if ( nif.getVersionNumber() >= 0x0A010000 )
		ok &= nif.set<int>( iData, "Has Triangles", 1 );
	ok &= nif.updateArray( iData, "Triangles" );
	nif.setArray<Triangle>( iData, "Triangles", QVector<Triangle>() << Triangle( 0, 1, 2 ) );

	return ok;
}

namespace {

//! Collects the names of the edits a scene builder asked for and the model refused
class Edits
{
public:
	void operator()( const char * what, bool accepted )
	{
		if ( !accepted )
			failed << QString::fromLatin1( what );
	}

	//! True if every edit was accepted; otherwise names the refused edits in *out
	bool finish( QString * out ) const
	{
		if ( out )
			*out = failed.join( QStringLiteral( ", " ) );

		return failed.isEmpty();
	}

private:
	QStringList failed;
};

//! Give an array item its rows (the row count comes from the array's size field, which the caller has set) and fill
//! them. False if there is no such array, or if it did not end up with one row per value.
template <typename T> bool fillArray( NifModel & nif, const QModelIndex & array, const QVector<T> & values )
{
	if ( !array.isValid() || !nif.updateArray( array ) )
		return false;

	nif.setArray<T>( array, values );
	return nif.getArray<T>( array ).count() == values.count();
}

//! Give an array of compounds its rows; true if there are `rows` of them
bool makeRows( NifModel & nif, const QModelIndex & array, int rows )
{
	return array.isValid() && nif.updateArray( array ) && nif.rowCount( array ) == rows;
}

//! Insert a block and set its name (false if the type is unknown or has no name)
QModelIndex addNamed( NifModel & nif, Edits & edit, const char * type, const char * name )
{
	QModelIndex idx = nif.insertNiBlock( QString::fromLatin1( type ) );
	edit( type, idx.isValid() && nif.set<QString>( idx, "Name", QString::fromLatin1( name ) ) );
	return idx;
}

//! Insert an extra data block. Extra data has a name only from 10.0.1.0 (nif.xml: NiExtraData.Name ver1 10.0.1.0)
QModelIndex addExtra( NifModel & nif, Edits & edit, const char * type, const char * name )
{
	if ( nif.getVersionNumber() >= 0x0A000100 )
		return addNamed( nif, edit, type, name );

	QModelIndex idx = nif.insertNiBlock( QString::fromLatin1( type ) );
	edit( type, idx.isValid() );
	return idx;
}

//! The nine strings of the string-heavy extra data: empty, one character, spaces and punctuation, a backslash path,
//! and 300 characters (the longest are far above the 253 a LineString may hold, and cross the 255 of a ShortString)
QVector<QString> manyStrings()
{
	QString longText;
	for ( int i = 0; i < 30; i++ )
		longText += QStringLiteral( "0123456789" );

	return QVector<QString>() << QString() << QStringLiteral( "a" ) << QStringLiteral( "two words" )
		<< QStringLiteral( "Spaces, punctuation: (x) [y] {z}; 100% #1 'q' \"d\"" )
		<< QStringLiteral( "textures\\actors\\character\\body_d.dds" ) << longText;
}

//! A NiStringsExtraData (a SizedString array) holding manyStrings()
QModelIndex addStringsBlock( NifModel & nif, Edits & edit )
{
	QModelIndex iStrings = addExtra( nif, edit, "NiStringsExtraData", "Strings" );
	QVector<QString> strings = manyStrings();
	edit( "Num Strings", nif.set<int>( iStrings, "Num Strings", strings.count() ) );
	edit( "Strings Data", fillArray<QString>( nif, nif.getIndex( iStrings, "Data" ), strings ) );
	return iStrings;
}

//! A NiTextKeyExtraData with two keys. The key value is a string inside a compound inside an array: from 20.1.0.3
//! it has to find its way into the header's string table.
QModelIndex addTextKeysBlock( NifModel & nif, Edits & edit )
{
	QModelIndex iKeys = addExtra( nif, edit, "NiTextKeyExtraData", "Text Keys" );
	edit( "Num Text Keys", nif.set<int>( iKeys, "Num Text Keys", 2 ) );

	QModelIndex iKeyArray = nif.getIndex( iKeys, "Text Keys" );
	edit( "Text Key rows", makeRows( nif, iKeyArray, 2 ) );
	if ( nif.rowCount( iKeyArray ) == 2 ) {
		edit( "Text Key 0 Time", nif.set<float>( nif.index( 0, 0, iKeyArray ), "Time", 0.5f ) );
		edit( "Text Key 0 Value", nif.set<QString>( nif.index( 0, 0, iKeyArray ), "Value", QStringLiteral( "start: walk" ) ) );
		edit( "Text Key 1 Time", nif.set<float>( nif.index( 1, 0, iKeyArray ), "Time", 2.25f ) );
		edit( "Text Key 1 Value", nif.set<QString>( nif.index( 1, 0, iKeyArray ), "Value", QStringLiteral( "end" ) ) );
	}

	return iKeys;
}

//! A NiBinaryExtraData holding bytes that are not text: NUL, bytes >= 0x80, CR and LF
QModelIndex addBinaryBlock( NifModel & nif, Edits & edit )
{
	QModelIndex iBinary = addExtra( nif, edit, "NiBinaryExtraData", "Binary" );
	edit( "Binary Data", nif.set<QByteArray>( iBinary, "Binary Data", QByteArray::fromHex( "00ff807f0a0d4e494600" ) ) );
	return iBinary;
}

//! Put the given extra data blocks (after the ones the root already has) on the root: the list from 10.0.1.0, the
//! linked chain (NiExtraData.Next Extra Data, up to 4.2.2.0) before
void linkExtras( NifModel & nif, Edits & edit, const QModelIndex & iRoot, const QVector<qint32> & chain )
{
	if ( nif.getVersionNumber() >= 0x0A000100 ) {
		edit( "Num Extra Data List", nif.set<int>( iRoot, "Num Extra Data List", chain.count() ) );
		edit( "Extra Data List", nif.updateArray( iRoot, "Extra Data List" ) && nif.setLinkArray( iRoot, "Extra Data List", chain ) );
	} else {
		edit( "Extra Data", nif.setLink( iRoot, "Extra Data", chain.value( 0 ) ) );

		for ( int i = 1; i < chain.count(); i++ )
			edit( "Next Extra Data", nif.setLink( nif.getBlock( chain[i - 1] ), "Next Extra Data", chain[i] ) );
	}
}

//! Four texture paths (one empty) for a BSShaderTextureSet
QVector<QString> texturePaths( const char * stem )
{
	QString s = QString::fromLatin1( stem );
	return QVector<QString>() << QStringLiteral( "textures\\" ) + s + QStringLiteral( "_d.dds" )
		<< QStringLiteral( "textures\\" ) + s + QStringLiteral( "_n.dds" ) << QString()
		<< QStringLiteral( "textures\\" ) + s + QStringLiteral( "_s.dds" );
}

//! A byte as the stream stores a ByteVector3 component (the inverse of what NifIStream does) and as a ByteColor4 one
float fromByteVector( int b ) { return float( ( double( b ) / 255.0 ) * 2.0 - 1.0 ); }
float fromByteColor( int b ) { return float( double( b ) / 255.0 ); }

}

bool TestEnv::buildRichScene( NifModel & nif, QString * failed )
{
	Edits edit;
	edit( "buildScene", buildScene( nif ) );

	const quint32 v = nif.getVersionNumber();
	const quint32 uv = nif.getUserVersion();
	const quint32 uv2 = nif.getUserVersion2();

	QModelIndex iRoot = nif.getBlock( 0 );
	QModelIndex iShape = nif.getBlock( 2 );
	QModelIndex iData = nif.getBlock( 3 );
	QModelIndex iNote = nif.getBlock( 4 );
	if ( !iRoot.isValid() || !iShape.isValid() || !iData.isValid() || !iNote.isValid() ) {
		if ( failed )
			*failed = QStringLiteral( "buildScene did not leave blocks 0..4" );

		return false;
	}

	// ---- transforms: a rotation with nine different elements (an identity matrix hides a transposed one)
	edit( "root Rotation", nif.set<Matrix>( iRoot, "Rotation", TestEnv::distinctRotation() ) );
	edit( "shape Scale", nif.set<float>( iShape, "Scale", 0.5f ) );

	if ( v <= 0x04020200 )   // NiAVObject.Velocity: nif.xml ver2 4.2.2.0
		edit( "root Velocity", nif.set<Vector3>( iRoot, "Velocity", Vector3( -1.0f, 0.5f, 8.0f ) ) );

	// ---- geometry: normals, tangents, vertex colours, one UV set, one match group (3 vertices from buildScene)
	// nif.xml, NiGeometryData: the UV count lives in Num UV Sets (up to 4.2.2.0), Vector Flags (from 10.0.1.0, but
	// not on Bethesda 20.2.0.7 files) or BS Vector Flags (20.2.0.7 with User Version 2 > 0); bit 12 of the two
	// flag fields (0x1000) says that tangents and bitangents follow, which they do from 10.1.0.0.
	const bool bsFlags = v == 0x14020007 && uv2 > 0;
	const bool vectorFlags = v >= 0x0A000100 && !bsFlags;
	const bool tangents = v >= 0x0A010000;
	const int hasTangents = tangents ? 0x1000 : 0;

	if ( v <= 0x04020200 )
		edit( "Num UV Sets", nif.set<int>( iData, "Num UV Sets", 1 ) );
	if ( v <= 0x04000002 )
		edit( "Has UV", nif.set<int>( iData, "Has UV", 1 ) );
	if ( vectorFlags )
		edit( "Vector Flags", nif.set<int>( iData, "Vector Flags", 1 | hasTangents ) );
	if ( bsFlags )
		edit( "BS Vector Flags", nif.set<int>( iData, "BS Vector Flags", 1 | hasTangents ) );

	edit( "Has Normals", nif.set<int>( iData, "Has Normals", 1 ) );
	edit( "Normals", fillArray<Vector3>( nif, nif.getIndex( iData, "Normals" ),
		QVector<Vector3>() << Vector3( 0.0f, 0.0f, 1.0f ) << Vector3( 0.0f, 1.0f, 0.0f ) << Vector3( 1.0f, 0.0f, 0.0f ) ) );

	if ( tangents ) {
		edit( "Tangents", fillArray<Vector3>( nif, nif.getIndex( iData, "Tangents" ),
			QVector<Vector3>() << Vector3( 1.0f, 0.0f, 0.0f ) << Vector3( 0.0f, 0.0f, 1.0f ) << Vector3( 0.0f, 1.0f, 0.0f ) ) );
		edit( "Bitangents", fillArray<Vector3>( nif, nif.getIndex( iData, "Bitangents" ),
			QVector<Vector3>() << Vector3( 0.0f, 1.0f, 0.0f ) << Vector3( 1.0f, 0.0f, 0.0f ) << Vector3( 0.0f, 0.0f, 1.0f ) ) );
	}

	edit( "Center", nif.set<Vector3>( iData, "Center", Vector3( 0.25f, 0.25f, 0.0f ) ) );
	edit( "Radius", nif.set<float>( iData, "Radius", 0.75f ) );

	edit( "Has Vertex Colors", nif.set<int>( iData, "Has Vertex Colors", 1 ) );
	edit( "Vertex Colors", fillArray<Color4>( nif, nif.getIndex( iData, "Vertex Colors" ),
		QVector<Color4>() << Color4( 1.0f, 0.0f, 0.0f, 1.0f ) << Color4( 0.0f, 1.0f, 0.0f, 0.5f ) << Color4( 0.0f, 0.0f, 1.0f, 0.25f ) ) );

	// UV Sets is an array of arrays: one set (from the flags above) of one TexCoord per vertex
	QModelIndex iUVSets = nif.getIndex( iData, "UV Sets" );
	edit( "UV Sets", makeRows( nif, iUVSets, 1 ) );
	if ( nif.rowCount( iUVSets ) == 1 )
		edit( "UV Set 0", fillArray<Vector2>( nif, nif.index( 0, 0, iUVSets ),
			QVector<Vector2>() << Vector2( 0.0f, 0.0f ) << Vector2( 1.0f, 0.25f ) << Vector2( 0.5f, 1.0f ) ) );

	// Match Groups (from 3.1): a compound with its own array, here one group of two vertices
	edit( "Num Match Groups", nif.set<int>( iData, "Num Match Groups", 1 ) );
	QModelIndex iGroups = nif.getIndex( iData, "Match Groups" );
	edit( "Match Groups", makeRows( nif, iGroups, 1 ) );
	if ( nif.rowCount( iGroups ) == 1 ) {
		QModelIndex iGroup = nif.index( 0, 0, iGroups );
		edit( "Match Group Num Vertices", nif.set<int>( iGroup, "Num Vertices", 2 ) );
		edit( "Match Group Vertex Indices", fillArray<int>( nif, nif.getIndex( iGroup, "Vertex Indices" ), QVector<int>() << 2 << 1 ) );
	}

	// ---- extra data with many strings and bytes; the root keeps buildScene's "Note" and gets these after it
	// (one statement per block: the order they are inserted in is the order of their block numbers)
	QVector<qint32> chain;
	chain << nif.getBlockNumber( iNote );
	chain << nif.getBlockNumber( addStringsBlock( nif, edit ) );
	chain << nif.getBlockNumber( addTextKeysBlock( nif, edit ) );
	chain << nif.getBlockNumber( addBinaryBlock( nif, edit ) );
	linkExtras( nif, edit, iRoot, chain );

	// ---- material, texture and shader blocks, as the profile's games use them. NiAVObject.Properties exists up to
	// User Version 2 34; Skyrim LE puts its shader and alpha property in fields of the NiTriShape (nif.xml:
	// NiGeometry.Shader Property / Alpha Property, 20.2.0.7 with User Version 12). Skyrim SE and Fallout 4 draw with
	// BSTriShape, so their shader block is built by buildBSTriShapeScene().
	QVector<qint32> properties;

	if ( uv2 <= 34 ) {
		QModelIndex iMaterial = addNamed( nif, edit, "NiMaterialProperty", "Material" );
		QModelIndex iAlpha = addNamed( nif, edit, "NiAlphaProperty", "Alpha" );

		if ( v <= 0x0A000102 )   // NiMaterialProperty.Flags: ver2 10.0.1.2
			edit( "material Flags", nif.set<int>( iMaterial, "Flags", 0x0123 ) );
		if ( uv2 < 26 ) {        // Ambient / Diffuse Color have User Version 2 < 26
			edit( "material Ambient Color", nif.set<Color3>( iMaterial, "Ambient Color", Color3( 0.125f, 0.25f, 0.5f ) ) );
			edit( "material Diffuse Color", nif.set<Color3>( iMaterial, "Diffuse Color", Color3( 0.25f, 0.5f, 0.75f ) ) );
		}
		edit( "material Specular Color", nif.set<Color3>( iMaterial, "Specular Color", Color3( 0.5f, 0.25f, 0.125f ) ) );
		edit( "material Emissive Color", nif.set<Color3>( iMaterial, "Emissive Color", Color3( 0.0f, 0.5f, 0.0f ) ) );
		edit( "material Glossiness", nif.set<float>( iMaterial, "Glossiness", 33.0f ) );
		edit( "material Alpha", nif.set<float>( iMaterial, "Alpha", 0.75f ) );
		if ( uv2 > 21 )          // Emissive Mult has User Version 2 > 21
			edit( "material Emissive Mult", nif.set<float>( iMaterial, "Emissive Mult", 1.5f ) );

		edit( "alpha Flags", nif.set<int>( iAlpha, "Flags", 0x01ED ) );
		edit( "alpha Threshold", nif.set<int>( iAlpha, "Threshold", 100 ) );

		properties << nif.getBlockNumber( iMaterial ) << nif.getBlockNumber( iAlpha );

		if ( uv2 == 34 ) {
			// Fallout 3: BSShaderPPLightingProperty with a BSShaderTextureSet
			QModelIndex iShader = addNamed( nif, edit, "BSShaderPPLightingProperty", "Shader PP" );
			QModelIndex iTextures = nif.insertNiBlock( "BSShaderTextureSet" );
			edit( "BSShaderTextureSet", iTextures.isValid() );

			edit( "shader Environment Map Scale", nif.set<float>( iShader, "Environment Map Scale", 1.25f ) );
			edit( "shader Parallax Max Passes", nif.set<float>( iShader, "Parallax Max Passes", 3.0f ) );
			edit( "shader Texture Set", nif.setLink( iShader, "Texture Set", nif.getBlockNumber( iTextures ) ) );

			QVector<QString> paths = texturePaths( "fo3\\wall" );
			edit( "Num Textures", nif.set<int>( iTextures, "Num Textures", paths.count() ) );
			edit( "Textures", fillArray<QString>( nif, nif.getIndex( iTextures, "Textures" ), paths ) );

			properties << nif.getBlockNumber( iShader );
		} else if ( v >= 0x0303000D ) {
			// Before 3.3.0.13 a texture slot points to a NiImage instead of a NiSourceTexture, so only newer files get one
			QModelIndex iTexturing = addNamed( nif, edit, "NiTexturingProperty", "Texturing" );
			QModelIndex iSource = addNamed( nif, edit, "NiSourceTexture", "Texture File" );

			if ( v <= 0x14010001 )   // NiTexturingProperty.Apply Mode: 3.3.0.13 to 20.1.0.1
				edit( "texturing Apply Mode", nif.set<int>( iTexturing, "Apply Mode", 1 ) );

			edit( "Has Base Texture", nif.set<int>( iTexturing, "Has Base Texture", 1 ) );
			QModelIndex iBase = nif.getIndex( iTexturing, "Base Texture" );
			edit( "Base Texture", iBase.isValid() );
			edit( "Base Texture Source", nif.setLink( iBase, "Source", nif.getBlockNumber( iSource ) ) );
			if ( v <= 0x14000005 ) {   // TexDesc: Clamp Mode, Filter Mode and UV Set end at 20.0.0.5
				edit( "Base Texture Clamp Mode", nif.set<int>( iBase, "Clamp Mode", 1 ) );
				edit( "Base Texture Filter Mode", nif.set<int>( iBase, "Filter Mode", 6 ) );
				edit( "Base Texture UV Set", nif.set<int>( iBase, "UV Set", 1 ) );
			}
			if ( v >= 0x0A010000 ) {   // TexDesc.Has Texture Transform: ver1 10.1.0.0
				edit( "Has Texture Transform", nif.set<int>( iBase, "Has Texture Transform", 1 ) );
				edit( "Texture Translation", nif.set<Vector2>( iBase, "Translation", Vector2( 0.25f, 0.5f ) ) );
				edit( "Texture Scale", nif.set<Vector2>( iBase, "Scale", Vector2( 2.0f, 3.0f ) ) );
				edit( "Texture Rotation", nif.set<float>( iBase, "Rotation", 0.5f ) );
				edit( "Texture Transform Method", nif.set<int>( iBase, "Transform Method", 2 ) );
				edit( "Texture Center", nif.set<Vector2>( iBase, "Center", Vector2( 0.5f, 0.5f ) ) );
			}

			// File Name is a FilePath: a SizedString before 20.1.0.3, an index into the header's strings from then on
			edit( "texture File Name", nif.set<QString>( iSource, "File Name", QStringLiteral( "textures\\base_d.dds" ) ) );

			properties << nif.getBlockNumber( iTexturing );
		}

		edit( "Num Properties", nif.set<int>( iShape, "Num Properties", properties.count() ) );
		edit( "Properties", nif.updateArray( iShape, "Properties" ) && nif.setLinkArray( iShape, "Properties", properties ) );
	} else if ( uv == 12 && uv2 < 100 ) {
		// Skyrim LE
		QModelIndex iShader = addNamed( nif, edit, "BSLightingShaderProperty", "Lighting Shader" );
		QModelIndex iTextures = nif.insertNiBlock( "BSShaderTextureSet" );
		QModelIndex iAlpha = addNamed( nif, edit, "NiAlphaProperty", "Alpha" );
		edit( "BSShaderTextureSet", iTextures.isValid() );

		edit( "Skyrim Shader Type", nif.set<int>( iShader, "Skyrim Shader Type", 3 ) );
		edit( "shader UV Offset", nif.set<Vector2>( iShader, "UV Offset", Vector2( 0.125f, 0.25f ) ) );
		edit( "shader UV Scale", nif.set<Vector2>( iShader, "UV Scale", Vector2( 2.0f, 0.5f ) ) );
		edit( "shader Texture Set", nif.setLink( iShader, "Texture Set", nif.getBlockNumber( iTextures ) ) );
		edit( "shader Emissive Color", nif.set<Color3>( iShader, "Emissive Color", Color3( 0.5f, 0.25f, 0.125f ) ) );
		edit( "shader Emissive Multiple", nif.set<float>( iShader, "Emissive Multiple", 1.5f ) );
		edit( "shader Alpha", nif.set<float>( iShader, "Alpha", 0.75f ) );
		edit( "shader Glossiness", nif.set<float>( iShader, "Glossiness", 33.0f ) );
		edit( "shader Specular Color", nif.set<Color3>( iShader, "Specular Color", Color3( 1.0f, 0.5f, 0.25f ) ) );
		edit( "shader Specular Strength", nif.set<float>( iShader, "Specular Strength", 2.0f ) );

		QVector<QString> paths = texturePaths( "skyrim\\wall" );
		edit( "Num Textures", nif.set<int>( iTextures, "Num Textures", paths.count() ) );
		edit( "Textures", fillArray<QString>( nif, nif.getIndex( iTextures, "Textures" ), paths ) );

		edit( "alpha Flags", nif.set<int>( iAlpha, "Flags", 0x01ED ) );
		edit( "alpha Threshold", nif.set<int>( iAlpha, "Threshold", 100 ) );

		edit( "Shader Property", nif.setLink( iShape, "Shader Property", nif.getBlockNumber( iShader ) ) );
		edit( "Alpha Property", nif.setLink( iShape, "Alpha Property", nif.getBlockNumber( iAlpha ) ) );

		// Material CRC: NiGeometryData, 20.2.0.7 with User Version 12 only
		edit( "Material CRC", nif.set<int>( iData, "Material CRC", 0x12345678 ) );
	}

	// ---- a skin: Ptr links (Skeleton Root, Bones) and an array of compounds with an argument (Bone List).
	// Ptr is stored with the same 1-based shift as Ref before 3.3.0.13; nothing older has a NiSkinInstance here.
	if ( v >= 0x0303000D ) {
		QModelIndex iSkin = nif.insertNiBlock( "NiSkinInstance" );
		QModelIndex iSkinData = nif.insertNiBlock( "NiSkinData" );
		edit( "NiSkinInstance", iSkin.isValid() && iSkinData.isValid() );

		edit( "skin Data", nif.setLink( iSkin, "Data", nif.getBlockNumber( iSkinData ) ) );
		edit( "skin Skeleton Root", nif.setLink( iSkin, "Skeleton Root", nif.getBlockNumber( iRoot ) ) );
		edit( "skin Num Bones", nif.set<int>( iSkin, "Num Bones", 1 ) );
		edit( "skin Bones", nif.updateArray( iSkin, "Bones" ) && nif.setLinkArray( iSkin, "Bones", QVector<qint32>() << nif.getBlockNumber( iRoot ) ) );

		edit( "skin data Num Bones", nif.set<int>( iSkinData, "Num Bones", 1 ) );
		QModelIndex iBones = nif.getIndex( iSkinData, "Bone List" );
		edit( "Bone List", makeRows( nif, iBones, 1 ) );
		if ( nif.rowCount( iBones ) == 1 ) {
			QModelIndex iBone = nif.index( 0, 0, iBones );
			edit( "bone Bounding Sphere Radius", nif.set<float>( iBone, "Bounding Sphere Radius", 1.5f ) );
			edit( "bone Num Vertices", nif.set<int>( iBone, "Num Vertices", 2 ) );

			QModelIndex iWeights = nif.getIndex( iBone, "Vertex Weights" );
			edit( "Vertex Weights", makeRows( nif, iWeights, 2 ) );
			if ( nif.rowCount( iWeights ) == 2 ) {
				edit( "weight 0 Index", nif.set<int>( nif.index( 0, 0, iWeights ), "Index", 2 ) );
				edit( "weight 0 Weight", nif.set<float>( nif.index( 0, 0, iWeights ), "Weight", 0.25f ) );
				edit( "weight 1 Index", nif.set<int>( nif.index( 1, 0, iWeights ), "Index", 1 ) );
				edit( "weight 1 Weight", nif.set<float>( nif.index( 1, 0, iWeights ), "Weight", 0.75f ) );
			}
		}

		edit( "shape Skin Instance", nif.setLink( iShape, "Skin Instance", nif.getBlockNumber( iSkin ) ) );
	}

	return edit.finish( failed );
}

bool TestEnv::buildBSTriShapeScene( NifModel & nif, QString * failed )
{
	const quint32 uv2 = nif.getUserVersion2();
	const bool fo4 = uv2 == 130;

	if ( nif.getVersionNumber() != 0x14020007 || ( uv2 != 100 && !fo4 ) ) {
		if ( failed )
			*failed = QStringLiteral( "BSTriShape is only built for 20.2.0.7 with User Version 2 100 or 130" );

		return false;
	}

	Edits edit;

	QModelIndex iRoot = addNamed( nif, edit, "NiNode", "BS Root" );
	QModelIndex iShape = addNamed( nif, edit, "BSTriShape", "BS Shape" );
	QModelIndex iShader = addNamed( nif, edit, "BSLightingShaderProperty", "BS Shader" );
	QModelIndex iTextures = nif.insertNiBlock( "BSShaderTextureSet" );
	QModelIndex iAlpha = addNamed( nif, edit, "NiAlphaProperty", "BS Alpha" );
	edit( "BSShaderTextureSet", iTextures.isValid() );

	linkExtras( nif, edit, iRoot, QVector<qint32>() << nif.getBlockNumber( addStringsBlock( nif, edit ) ) );

	edit( "root Children", nif.set<int>( iRoot, "Num Children", 1 ) );
	edit( "root Children list", nif.updateArray( iRoot, "Children" ) && nif.setLinkArray( iRoot, "Children", QVector<qint32>() << nif.getBlockNumber( iShape ) ) );

	// ---- the shape: transform, bounds, links, vertex layout
	edit( "shape Translation", nif.set<Vector3>( iShape, "Translation", Vector3( -0.5f, 0.25f, 8.0f ) ) );
	edit( "shape Rotation", nif.set<Matrix>( iShape, "Rotation", TestEnv::distinctRotation() ) );
	edit( "shape Scale", nif.set<float>( iShape, "Scale", 0.5f ) );

	QModelIndex iBound = nif.getIndex( iShape, "Bounding Sphere" );
	edit( "Bounding Sphere", iBound.isValid() );
	edit( "Bounding Sphere Center", nif.set<Vector3>( iBound, "Center", Vector3( 0.5f, -1.0f, 2.0f ) ) );
	edit( "Bounding Sphere Radius", nif.set<float>( iBound, "Radius", 4.0f ) );

	edit( "Shader Property", nif.setLink( iShape, "Shader Property", nif.getBlockNumber( iShader ) ) );
	edit( "Alpha Property", nif.setLink( iShape, "Alpha Property", nif.getBlockNumber( iAlpha ) ) );

	// Vertex layout: position, UV, normal, tangent and colour. SSE stores 3 floats + the bitangent X float (16 bytes),
	// Fallout 4 3 half floats + a half float bitangent X (8 bytes); UV 2 halves, normal and tangent 3 bytes + 1 byte of
	// the bitangent each, colour 4 bytes. 32 and 24 bytes per vertex.
	const int vertices = 3;
	const int vertexSize = fo4 ? 24 : 32;
	const int triangles = 1;

	BSVertexDesc desc;
	desc.SetFlag( VF_VERTEX );
	desc.SetFlag( VF_UV );
	desc.SetFlag( VF_NORMAL );
	desc.SetFlag( VF_TANGENT );
	desc.SetFlag( VF_COLORS );
	desc.ResetAttributeOffsets( uv2 );
	edit( "Vertex Desc", nif.set<BSVertexDesc>( iShape, "Vertex Desc", desc ) );
	edit( "Num Vertices", nif.set<int>( iShape, "Num Vertices", vertices ) );
	edit( "Num Triangles", nif.set<int>( iShape, "Num Triangles", triangles ) );
	edit( "Data Size", nif.set<int>( iShape, "Data Size", vertices * vertexSize + triangles * 6 ) );

	QModelIndex iVertices = nif.getIndex( iShape, "Vertex Data" );
	edit( "Vertex Data", makeRows( nif, iVertices, vertices ) );

	// every component is a value the stream stores exactly: halves of small multiples of 2^-2, bytes 0, 128, 255
	static const int bytes[3][3] = { { 255, 128, 0 }, { 128, 0, 255 }, { 0, 255, 128 } };

	for ( int i = 0; i < vertices && nif.rowCount( iVertices ) == vertices; i++ ) {
		QModelIndex iV = nif.index( i, 0, iVertices );
		const float x = 0.5f * i;

		if ( fo4 )
			edit( "Vertex", nif.set<HalfVector3>( iV, "Vertex", HalfVector3( x, -1.0f, 2.0f ) ) );
		else
			edit( "Vertex", nif.set<Vector3>( iV, "Vertex", Vector3( x, -1.0f, 2.0f ) ) );

		// float on SSE, hfloat on Fallout 4
		edit( "Bitangent X", nif.set<float>( iV, "Bitangent X", 0.25f * ( i + 1 ) ) );
		edit( "UV", nif.set<HalfVector2>( iV, "UV", HalfVector2( 0.25f * i, 1.0f - 0.25f * i ) ) );
		edit( "Normal", nif.set<ByteVector3>( iV, "Normal", ByteVector3( fromByteVector( bytes[i][0] ), fromByteVector( bytes[i][1] ), fromByteVector( bytes[i][2] ) ) ) );
		edit( "Bitangent Y", nif.set<int>( iV, "Bitangent Y", 10 + i ) );
		edit( "Tangent", nif.set<ByteVector3>( iV, "Tangent", ByteVector3( fromByteVector( bytes[i][2] ), fromByteVector( bytes[i][0] ), fromByteVector( bytes[i][1] ) ) ) );
		edit( "Bitangent Z", nif.set<int>( iV, "Bitangent Z", 20 + i ) );

		ByteColor4 color;
		color.setRGBA( fromByteColor( 255 ), fromByteColor( 0 ), fromByteColor( 51 ), fromByteColor( 255 - 85 * i ) );
		edit( "Vertex Colors", nif.set<ByteColor4>( iV, "Vertex Colors", color ) );
	}

	edit( "Triangles", fillArray<Triangle>( nif, nif.getIndex( iShape, "Triangles" ), QVector<Triangle>() << Triangle( 0, 1, 2 ) ) );

	// ---- shader, textures, alpha
	edit( "Skyrim Shader Type", nif.set<int>( iShader, "Skyrim Shader Type", 5 ) );
	edit( "shader UV Offset", nif.set<Vector2>( iShader, "UV Offset", Vector2( 0.125f, 0.25f ) ) );
	edit( "shader UV Scale", nif.set<Vector2>( iShader, "UV Scale", Vector2( 2.0f, 0.5f ) ) );
	edit( "shader Texture Set", nif.setLink( iShader, "Texture Set", nif.getBlockNumber( iTextures ) ) );
	edit( "shader Emissive Color", nif.set<Color3>( iShader, "Emissive Color", Color3( 0.5f, 0.25f, 0.125f ) ) );
	edit( "shader Emissive Multiple", nif.set<float>( iShader, "Emissive Multiple", 1.5f ) );
	edit( "shader Alpha", nif.set<float>( iShader, "Alpha", 0.75f ) );
	edit( "shader Specular Color", nif.set<Color3>( iShader, "Specular Color", Color3( 1.0f, 0.5f, 0.25f ) ) );
	edit( "shader Specular Strength", nif.set<float>( iShader, "Specular Strength", 2.0f ) );
	if ( fo4 ) {
		// Smoothness and Wet Material (a string) are Fallout 4's, Glossiness is Skyrim SE's
		edit( "shader Smoothness", nif.set<float>( iShader, "Smoothness", 0.5f ) );
		edit( "shader Wet Material", nif.set<QString>( iShader, "Wet Material", QStringLiteral( "materials\\wet\\rain.bgsm" ) ) );
	} else {
		edit( "shader Glossiness", nif.set<float>( iShader, "Glossiness", 33.0f ) );
	}

	QVector<QString> paths = texturePaths( "bs\\body" );
	edit( "Num Textures", nif.set<int>( iTextures, "Num Textures", paths.count() ) );
	edit( "Textures", fillArray<QString>( nif, nif.getIndex( iTextures, "Textures" ), paths ) );

	edit( "alpha Flags", nif.set<int>( iAlpha, "Flags", 0x01ED ) );
	edit( "alpha Threshold", nif.set<int>( iAlpha, "Threshold", 100 ) );

	return edit.finish( failed );
}

NifValue TestEnv::countValue( NifValue::Type t, quint32 n )
{
	NifValue v( t );
	if ( !v.setCount( n ) )
		qFatal( "TestEnv::countValue: setCount failed for type %d", int( t ) );

	return v;
}

NifValue TestEnv::floatValue( NifValue::Type t, float f )
{
	NifValue v( t );
	if ( !v.setFloat( f ) )
		qFatal( "TestEnv::floatValue: setFloat failed for type %d", int( t ) );

	return v;
}

NifValue TestEnv::linkValue( NifValue::Type t, int link )
{
	NifValue v( t );
	if ( !v.setLink( link ) )
		qFatal( "TestEnv::linkValue: setLink failed for type %d", int( t ) );

	return v;
}

QByteArray TestEnv::saveBytes( const BaseModel & model, bool * ok )
{
	QBuffer buf;
	buf.open( QIODevice::WriteOnly );
	bool saved = model.save( buf );
	if ( ok )
		*ok = saved;

	return buf.data();
}

bool TestEnv::loadBytes( NifModel & nif, const QByteArray & bytes )
{
	QBuffer in;
	in.setData( bytes );
	in.open( QIODevice::ReadOnly );
	return nif.load( in );
}

Matrix TestEnv::distinctRotation()
{
	Matrix m;
	for ( int r = 0; r < 3; r++ )
		for ( int c = 0; c < 3; c++ )
			m( r, c ) = float( 1 + 3 * r + c );

	return m;
}

QString TestEnv::diffBytes( const QByteArray & got, const QByteArray & expected )
{
	int n = qMin( got.size(), expected.size() );

	for ( int i = 0; i < n; i++ ) {
		if ( got[i] != expected[i] )
			return QString( "first difference at offset %1 (0x%2): got %3, expected %4 (%5 bytes got, %6 expected)" )
				.arg( i ).arg( i, 0, 16 )
				.arg( quint8( got[i] ), 2, 16, QLatin1Char( '0' ) ).arg( quint8( expected[i] ), 2, 16, QLatin1Char( '0' ) )
				.arg( got.size() ).arg( expected.size() );
	}

	if ( got.size() != expected.size() )
		return QString( "equal for the first %1 bytes, then got %2 bytes and expected %3" ).arg( n ).arg( got.size() ).arg( expected.size() );

	return QString();
}

static void diffIndex( const BaseModel & a, const BaseModel & b, const QModelIndex & ia, const QModelIndex & ib, const QString & path, QString & out )
{
	if ( !out.isEmpty() )
		return;

	if ( a.itemName( ia ) != b.itemName( ib ) || a.itemType( ia ) != b.itemType( ib ) ) {
		out = path + ": item is " + a.itemName( ia ) + "/" + a.itemType( ia ) + " vs " + b.itemName( ib ) + "/" + b.itemType( ib );
		return;
	}

	int rows = a.rowCount( ia );
	if ( rows != b.rowCount( ib ) ) {
		out = path + QString( ": %1 children vs %2" ).arg( rows ).arg( b.rowCount( ib ) );
		return;
	}

	if ( rows == 0 ) {
		NifValue va = a.getValue( ia );
		NifValue vb = b.getValue( ib );

		// NifValue::operator== is false for empty byte arrays, so also accept equal text
		if ( va.type() != vb.type() || ( !( va == vb ) && va.toString() != vb.toString() ) )
			out = path + ": " + va.toString() + " vs " + vb.toString();

		return;
	}

	for ( int r = 0; r < rows && out.isEmpty(); r++ ) {
		QModelIndex ca = a.index( r, 0, ia );
		QModelIndex cb = b.index( r, 0, ib );

		// Items whose condition is false are not stored in the file and keep whatever default they had
		bool onA = a.evalCondition( ca );
		bool onB = b.evalCondition( cb );

		if ( onA != onB )
			out = path + "/" + a.itemName( ca ) + QString( ": condition %1 vs %2" ).arg( onA ).arg( onB );
		else if ( onA )
			diffIndex( a, b, ca, cb, path + "/" + a.itemName( ca ), out );
	}
}

QString TestEnv::diffModels( const BaseModel & a, const BaseModel & b )
{
	if ( a.getVersionNumber() != b.getVersionNumber() )
		return QString( "version %1 vs %2" ).arg( a.getVersion(), b.getVersion() );

	QString out;
	diffIndex( a, b, QModelIndex(), QModelIndex(), QString(), out );
	return out;
}

QStringList TestEnv::takeMessageBoxes()
{
	QStringList texts;

	for ( QWidget * w : QApplication::topLevelWidgets() ) {
		if ( QMessageBox * box = qobject_cast<QMessageBox *>( w ) ) {
			if ( box->isVisible() ) {
				texts << box->text() + " " + box->detailedText();
				// Only hide it: Message::append() keeps the pointer and shows the same box again later
				box->close();
			}
		}
	}

	return texts;
}
