#ifndef TESTENV_H
#define TESTENV_H

#include "model/nifmodel.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>


//! @file testenv.h Shared helpers: file locations, XML loading, model fixtures

#ifndef NIFSKOPE_SOURCE_DIR
#error NIFSKOPE_SOURCE_DIR must be defined by tests.pro
#endif

namespace TestEnv
{

//! Root of the source tree, from the compile-time define
QString sourceDir();
//! build/docsys/nifxml/nif.xml (git submodule)
QString nifXmlPath();
//! build/docsys/kfmxml/kfm.xml (git submodule)
QString kfmXmlPath();

//! (Re)parse nif.xml and kfm.xml; returns an error message, empty on success. The XML tables, NifValue's type
//! map and enum map are process-wide statics, and some tests clear or rebuild them, so every test class that
//! needs the XML calls this in initTestCase() instead of relying on what ran before it.
QString reloadXml();

//! A game/file-format profile: header version plus Bethesda user versions
struct Profile
{
	const char * name;
	const char * version;
	int userVersion;
	int userVersion2;
};

//! The versions the round trip tests run over
QList<Profile> profiles();

//! Create an empty NifModel whose header is initialised for the profile (via the startup defaults in QSettings)
std::unique_ptr<NifModel> makeModel( const Profile & profile );

//! Fill a model with a small scene graph: root NiNode, child NiNode, NiTriShape + data, NiStringExtraData.
//! Returns false if the model rejected any of the edits.
bool buildScene( NifModel & nif );

//! buildScene() plus the data buildScene() leaves out: a rotation that is not the identity, normals, tangents, vertex
//! colours, a UV set and a match group on the geometry, a Ptr link (skin), a material / texture / shader block that
//! suits the profile, and extra data with many strings. Blocks 0..4 are the ones buildScene() builds, then:
//!   5 NiStringsExtraData, 6 NiTextKeyExtraData, 7 NiBinaryExtraData,
//!   up to User Version 2 34: NiMaterialProperty, NiAlphaProperty and NiTexturingProperty + NiSourceTexture (from
//!     3.3.0.13) or, on Fallout 3, BSShaderPPLightingProperty + BSShaderTextureSet,
//!   Skyrim LE: BSLightingShaderProperty, BSShaderTextureSet, NiAlphaProperty,
//!   from 3.3.0.13: NiSkinInstance, NiSkinData (the last two blocks).
//! Fields the profile's version does not have are skipped (the conditions are written out next to each edit).
//! Returns false if the model rejected any edit, and then names the edits in *failed if it is given.
//! Skyrim SE and Fallout 4 draw with BSTriShape: their shader block is built by buildBSTriShapeScene().
bool buildRichScene( NifModel & nif, QString * failed = nullptr );

//! Skyrim SE / Fallout 4 only (User Version 2 100 and 130, version 20.2.0.7): root NiNode, a BSTriShape whose vertex
//! data uses every attribute except skinning (half floats and half / byte vectors on Fallout 4), a
//! BSLightingShaderProperty with its BSShaderTextureSet, a NiAlphaProperty and a NiStringsExtraData.
//! Returns false for any other profile or if the model rejected an edit; *failed names the edits.
bool buildBSTriShapeScene( NifModel & nif, QString * failed = nullptr );

//! The rotation m(r, c) = 1 + 3 r + c: nine different elements, so a transposed or reordered matrix shows (the identity
//! hides both). Not a rotation in the mathematical sense; the model stores any 3x3 matrix.
Matrix distinctRotation();

//! NifValues for tables of samples, set the way the model sets them. They stop the program (qFatal) when the type refuses the
//! value, so a row naming the wrong type fails loudly instead of testing a default value.
NifValue countValue( NifValue::Type t, quint32 n );
NifValue floatValue( NifValue::Type t, float f );
NifValue linkValue( NifValue::Type t, int link );

//! A value of a class type: Vector3, Quat, Triangle, QString ...
template <typename X> NifValue valueOf( NifValue::Type t, const X & x )
{
	NifValue v( t );
	if ( !v.set<X>( x ) )
		qFatal( "TestEnv::valueOf: set failed for type %d", int( t ) );

	return v;
}

//! The bytes a model saves; *ok tells whether the save worked
QByteArray saveBytes( const BaseModel & model, bool * ok = nullptr );

//! Load a model from bytes
bool loadBytes( NifModel & nif, const QByteArray & bytes );

//! Describes the first difference between two byte arrays (offset, both bytes, both sizes); empty if they are equal.
//! Lets a QVERIFY2 on a 300 byte file say where the files differ instead of dumping both.
QString diffBytes( const QByteArray & got, const QByteArray & expected );

//! Structural comparison of two models (works for NifModel and KfmModel). Compares every item whose
//! condition holds: name, type, child count and value. Returns the first difference, empty if equal
QString diffModels( const BaseModel & a, const BaseModel & b );

//! Closes every QMessageBox the code under test opened and returns their texts. The app reports many
//! errors with Message::critical()/append(), which creates a message box even when no one can see it.
QStringList takeMessageBoxes();

}


/*! A BaseModel that is neither a NifModel nor a KfmModel, with a version number the test chooses. The streams give a
 *  NifModel (and only that) the version rules of the NIF format; this is how a test asks for a version number
 *  that no NifModel or KfmModel has. load() succeeds without touching anything and tells what the state was while it ran.
 */
class StubModel final : public BaseModel
{
public:
	explicit StubModel( quint32 versionNumber = 0 ) : number( versionNumber ) {}

	quint32 number;                 //!< what getVersionNumber() says
	int stateWhileLoading = -1;     //!< BaseModel::State of the model during the last load()

	void clear() override {}
	bool load( QIODevice & ) override
	{
		stateWhileLoading = int( getState() );
		return true;
	}
	bool save( QIODevice & ) const override { return true; }
	QString getVersion() const override { return QString(); }
	quint32 getVersionNumber() const override { return number; }

protected:
	bool setItemValue( NifItem *, const NifValue & ) override { return false; }
	bool updateArrayItem( NifItem * ) override { return false; }
	QString ver2str( quint32 ) const override { return QString(); }
	quint32 str2ver( QString ) const override { return 0; }
	bool evalVersion( NifItem *, bool = false ) const override { return true; }
	bool setHeaderString( const QString & ) override { return false; }
};

#endif
