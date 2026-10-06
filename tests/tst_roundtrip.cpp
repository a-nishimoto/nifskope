#include "testenv.h"
#include "testregistry.h"

#include "message.h"
#include "model/nifmodel.h"
#include "data/nifvalue.h"
#include "io/nifstream.h"
#include "model/kfmmodel.h"

#include <QBuffer>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCodec>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>


// lib/half.h declares these two. It is not included: the test target's include path has lib/ in some build configurations
// (tests.pro) but not in all of them (CMake without the BSA test), and the half.cpp that defines them is linked in all of them.
uint32_t half_to_float( uint16_t h );
uint16_t half_from_float( uint32_t f );


//! Programmatic NIF round trips over several file versions / game profiles
class tst_NifRoundTrip final : public QObject
{
	Q_OBJECT

	static void addProfileRows()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );

		for ( const TestEnv::Profile & p : TestEnv::profiles() )
			QTest::newRow( p.name ) << QString::fromLatin1( p.version ) << p.userVersion << p.userVersion2;
	}

	static std::unique_ptr<NifModel> makeScene( const QString & version, int uv, int uv2 )
	{
		TestEnv::Profile profile{ "", nullptr, uv, uv2 };
		QByteArray v = version.toLatin1();
		profile.version = v.constData();
		auto nif = TestEnv::makeModel( profile );
		return nif;
	}

	static QByteArray saveBytes( const BaseModel & model, bool * ok = nullptr ) { return TestEnv::saveBytes( model, ok ); }

	static bool loadBytes( NifModel & nif, const QByteArray & bytes ) { return TestEnv::loadBytes( nif, bytes ); }

	//! One number per component, for comparing arrays of vectors and colours with a readable QCOMPARE
	template <typename T, int N> static QVector<float> flat( const QVector<T> & values )
	{
		QVector<float> out;
		for ( const T & v : values )
			for ( int i = 0; i < N; i++ )
				out << v[i];

		return out;
	}

	static QVector<float> floats( std::initializer_list<float> values ) { return QVector<float>( values ); }

	//! The block types buildRichScene() creates, in block order, for a profile. Written out here (not taken from the
	//! builder): up to User Version 2 34 a material, an alpha property and, per game, a texture or a shader block;
	//! Skyrim LE keeps its shader on the shape; everything from 3.3.0.13 has a skin.
	static QStringList richBlockTypes( quint32 version, int userVersion, int userVersion2 )
	{
		QStringList types;
		types << "NiNode" << "NiNode" << "NiTriShape" << "NiTriShapeData" << "NiStringExtraData"
			<< "NiStringsExtraData" << "NiTextKeyExtraData" << "NiBinaryExtraData";

		if ( userVersion2 <= 34 ) {
			types << "NiMaterialProperty" << "NiAlphaProperty";

			if ( userVersion2 == 34 )
				types << "BSShaderPPLightingProperty" << "BSShaderTextureSet";
			else if ( version >= 0x0303000D )
				types << "NiTexturingProperty" << "NiSourceTexture";
		} else if ( userVersion == 12 && userVersion2 < 100 ) {
			types << "BSLightingShaderProperty" << "BSShaderTextureSet" << "NiAlphaProperty";
		}

		if ( version >= 0x0303000D )
			types << "NiSkinInstance" << "NiSkinData";

		return types;
	}

	static QStringList blockTypes( const NifModel & nif )
	{
		QStringList types;
		for ( int i = 0; i < nif.getBlockCount(); i++ )
			types << nif.getBlockName( nif.getBlock( i ) );

		return types;
	}

	//! Rows for the profiles that draw with BSTriShape (User Version 2 100 and 130). The other profiles cannot hold it:
	//! BSTriShape and its vertex layout only exist for Skyrim SE / Fallout 4, so they are left out of the table.
	static void addBSTriShapeRows()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );

		int rows = 0;
		for ( const TestEnv::Profile & p : TestEnv::profiles() ) {
			if ( p.userVersion2 >= 100 ) {
				QTest::newRow( p.name ) << QString::fromLatin1( p.version ) << p.userVersion << p.userVersion2;
				rows++;
			}
		}

		// An empty table makes QtTest abort the whole process; fail the test instead
		if ( !rows )
			QTest::newRow( "no profile with User Version 2 >= 100" ) << QString() << 0 << 0;
	}

	//! Loads `bytes` and checks the per-block sizes the header stores from 20.2.0.5 against the real block offsets
	static void verifyBlockSizes( const QByteArray & bytes, int blocks )
	{
		NifModel nif;
		QVERIFY( loadBytes( nif, bytes ) );

		QVector<int> sizes = nif.getArray<int>( nif.getHeader(), "Block Size" );
		QCOMPARE( sizes.count(), blocks );

		int pos = nif.fileOffset( nif.getBlock( 0 ) );
		QVERIFY( pos > 0 );
		for ( int i = 0; i < blocks; i++ ) {
			int next = ( i + 1 < blocks ) ? nif.fileOffset( nif.getBlock( i + 1 ) ) : nif.fileOffset( nif.getFooter() );
			QCOMPARE( sizes[i], next - pos );
			QCOMPARE( nif.blockSize( nif.getBlock( i ) ), sizes[i] );
			pos = next;
		}

		// ... and the footer is the rest of the file
		QCOMPARE( bytes.size(), pos + nif.blockSize( nif.getFooter() ) );
	}

	// ---- whole-file golden bytes -------------------------------------------------------------------------------------
	//
	// The four files below are the same two-node scene (see buildGoldenScene()) in four container layouts. Their bytes were
	// not produced by NifSkope: they were derived with python3 from nif.xml (field order, which fields exist in a version,
	// the width of each basic type) plus the container rules nif.xml does not describe (the header line, the sized type name
	// in front of each block before 5.0.0.1, "Top Level Object" / "End Of File", the block type and string tables). The
	// scripts are in the comment above each literal; run them and compare with `python3 script.py | xxd` to check by hand.
	// If a default in nif.xml changes (the kid's Flags, a matrix that is no longer the identity, a new field), the
	// derivation has to be redone, and that is the point: the file layout is part of the format.
	// All four assume a little endian host (NifSkope writes vectors and matrices as the memory holds them).

	/*! NetImmerse 4.0.0.2 (Morrowind): no block type table, the type name in front of every block, 32 bit bools, links stored
	 *  as they are (0-based from 3.3.0.13), Flags 16 bit, a Velocity (up to 4.2.2.0), a footer with the root list.
	 *
	 *  \code{.py}
	 *  import struct
	 *
	 *  def sized(text):                        # SizedString: u32 length + bytes
	 *      b = text.encode('latin-1')
	 *      return struct.pack('<I', len(b)) + b
	 *
	 *  def floats(*f):
	 *      return struct.pack('<%df' % len(f), *f)
	 *
	 *  def ni_node(name, flags, translation, rotation, scale, velocity, children):
	 *      return (sized('NiNode')                                    # block type name, in front of every block before 5.0.0.1
	 *          + sized(name)                                          # NiObjectNET.Name: string is a SizedString before 20.1.0.3
	 *          + struct.pack('<ii', -1, -1)                           # Extra Data, Controller: Ref, stored 0-based from 3.3.0.13, -1 = none
	 *          + struct.pack('<H', flags)                             # NiAVObject.Flags: 16 bit while User Version 2 <= 26
	 *          + floats(*translation) + floats(*rotation) + floats(scale)   # Translation, Rotation (9 floats, file order), Scale
	 *          + floats(*velocity)                                    # Velocity: up to 4.2.2.0
	 *          + struct.pack('<I', 0)                                 # Num Properties (User Version 2 <= 34)
	 *          + struct.pack('<I', 0)                                 # Has Bounding Volume: a bool is 32 bit up to 4.0.0.2, nothing follows
	 *          + struct.pack('<I', len(children)) + b''.join(struct.pack('<i', c) for c in children)   # Num Children, Children
	 *          + struct.pack('<I', 0))                                # Num Effects (User Version 2 < 130)
	 *
	 *  identity = [1, 0, 0, 0, 1, 0, 0, 0, 1]
	 *  golden = (b'NetImmerse File Format, Version 4.0.0.2\n'         # Header String: HeaderString = text + LF
	 *      + struct.pack('<II', 0x04000002, 2)                        # Version, Num Blocks (Endian Type / User Version / tables come later)
	 *      + ni_node('Root', 0x1234, (1.5, -2.0, 0.25), [1, 2, 3, 4, 5, 6, 7, 8, 9], 2.5, (-1.0, 0.5, 8.0), [1])
	 *      + ni_node('Kid', 0, (0, 0, 0), identity, 1.0, (0, 0, 0), [])
	 *      + struct.pack('<II', 1, 0))                                # Footer: Num Roots, Roots[0]
	 *  \endcode
	 */
	static QByteArray goldenMorrowind()
	{
		return QByteArray::fromHex(
			"4e 65 74 49 6d 6d 65 72 73 65 20 46 69 6c 65 20"	// Header String
			"46 6f 72 6d 61 74 2c 20 56 65 72 73 69 6f 6e 20"
			"34 2e 30 2e 30 2e 32 0a"
			"02 00 00 04"	// Version
			"02 00 00 00"	// Num Blocks
			"06 00 00 00 4e 69 4e 6f 64 65"	// block 0: type name
			"04 00 00 00 52 6f 6f 74"	// block 0: Name
			"ff ff ff ff"	// block 0: Extra Data
			"ff ff ff ff"	// block 0: Controller
			"34 12"	// block 0: Flags
			"00 00 c0 3f 00 00 00 c0 00 00 80 3e"	// block 0: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 40 00 00 40 40"	// block 0: Rotation m11 m21 m31 (floats)
			"00 00 80 40 00 00 a0 40 00 00 c0 40"	// block 0: Rotation m12 m22 m32 (floats)
			"00 00 e0 40 00 00 00 41 00 00 10 41"	// block 0: Rotation m13 m23 m33 (floats)
			"00 00 20 40"	// block 0: Scale
			"00 00 80 bf 00 00 00 3f 00 00 00 41"	// block 0: Velocity (x, y, z floats)
			"00 00 00 00"	// block 0: Num Properties
			"00 00 00 00"	// block 0: Has Bounding Volume
			"01 00 00 00"	// block 0: Num Children
			"01 00 00 00"	// block 0: Children[0]
			"00 00 00 00"	// block 0: Num Effects
			"06 00 00 00 4e 69 4e 6f 64 65"	// block 1: type name
			"03 00 00 00 4b 69 64"	// block 1: Name
			"ff ff ff ff"	// block 1: Extra Data
			"ff ff ff ff"	// block 1: Controller
			"00 00"	// block 1: Flags
			"00 00 00 00 00 00 00 00 00 00 00 00"	// block 1: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 00 00 00 00 00"	// block 1: Rotation m11 m21 m31 (floats)
			"00 00 00 00 00 00 80 3f 00 00 00 00"	// block 1: Rotation m12 m22 m32 (floats)
			"00 00 00 00 00 00 00 00 00 00 80 3f"	// block 1: Rotation m13 m23 m33 (floats)
			"00 00 80 3f"	// block 1: Scale
			"00 00 00 00 00 00 00 00 00 00 00 00"	// block 1: Velocity (x, y, z floats)
			"00 00 00 00"	// block 1: Num Properties
			"00 00 00 00"	// block 1: Has Bounding Volume
			"00 00 00 00"	// block 1: Num Children
			"00 00 00 00"	// block 1: Num Effects
			"01 00 00 00"	// Footer: Num Roots
			"00 00 00 00"	// Footer: Roots[0]
		);
	}

	/*! Skyrim LE (20.2.0.7, User Version 12, User Version 2 83): the whole header of a modern file (Endian Type, user
	 *  versions, Export Info, block type table, per block sizes, string table), blocks without type names whose Name is an
	 *  index into the string table, 32 bit Flags (default 14), Collision Object, no Velocity and no Properties.
	 *
	 *  \code{.py}
	 *  import struct
	 *
	 *  def sized(text):                        # SizedString: u32 length + bytes
	 *      b = text.encode('latin-1')
	 *      return struct.pack('<I', len(b)) + b
	 *
	 *  def short(text):                        # ShortString: u8 length (counts the NUL) + bytes + NUL
	 *      b = text.encode('latin-1') + b'\0'
	 *      return bytes([len(b)]) + b
	 *
	 *  def floats(*f):
	 *      return struct.pack('<%df' % len(f), *f)
	 *
	 *  def ni_node(name_index, flags, translation, rotation, scale, children):
	 *      return (struct.pack('<I', name_index)                      # NiObjectNET.Name: index into the header's string table from 20.1.0.3
	 *          + struct.pack('<Ii', 0, -1)                            # Num Extra Data List, Controller
	 *          + struct.pack('<I', flags)                             # NiAVObject.Flags: 32 bit once User Version 2 > 26
	 *          + floats(*translation) + floats(*rotation) + floats(scale)   # no Velocity after 4.2.2.0, no Properties above User Version 2 34
	 *          + struct.pack('<i', -1)                                # Collision Object (from 10.0.1.0)
	 *          + struct.pack('<I', len(children)) + b''.join(struct.pack('<i', c) for c in children)
	 *          + struct.pack('<I', 0))                                # Num Effects (User Version 2 < 130)
	 *
	 *  identity = [1, 0, 0, 0, 1, 0, 0, 0, 1]
	 *  blocks = [ni_node(0, 0x1234, (1.5, -2.0, 0.25), [1, 2, 3, 4, 5, 6, 7, 8, 9], 2.5, [1]),
	 *            ni_node(1, 14, (0, 0, 0), identity, 1.0, [])]        # 14 = the default of NiAVObject.Flags
	 *  strings = ['Root', 'Kid']
	 *  golden = (b'Gamebryo File Format, Version 20.2.0.7\n'          # Header String
	 *      + struct.pack('<I', 0x14020007) + b'\x01'                  # Version, Endian Type (1 = little)
	 *      + struct.pack('<III', 12, len(blocks), 83)                 # User Version, Num Blocks, User Version 2
	 *      + short('Me') + short('') + short('')                      # Export Info: Author, Process Script, Export Script
	 *      + struct.pack('<H', 1) + sized('NiNode')                   # Num Block Types, Block Types
	 *      + struct.pack('<HH', 0, 0)                                 # Block Type Index per block
	 *      + b''.join(struct.pack('<I', len(b)) for b in blocks)      # Block Size per block (from 20.2.0.5)
	 *      + struct.pack('<II', len(strings), max(map(len, strings))) # Num Strings, Max String Length
	 *      + b''.join(sized(s) for s in strings)                      # Strings
	 *      + struct.pack('<I', 0)                                     # Num Groups
	 *      + b''.join(blocks)
	 *      + struct.pack('<Ii', 1, 0))                                # Footer: Num Roots, Roots[0]
	 *  \endcode
	 */
	static QByteArray goldenSkyrimLE()
	{
		return QByteArray::fromHex(
			"47 61 6d 65 62 72 79 6f 20 46 69 6c 65 20 46 6f"	// Header String
			"72 6d 61 74 2c 20 56 65 72 73 69 6f 6e 20 32 30"
			"2e 32 2e 30 2e 37 0a"
			"07 00 02 14"	// Version
			"01"	// Endian Type
			"0c 00 00 00"	// User Version
			"02 00 00 00"	// Num Blocks
			"53 00 00 00"	// User Version 2
			"03 4d 65 00"	// Export Info.Author
			"01 00"	// Export Info.Process Script
			"01 00"	// Export Info.Export Script
			"01 00"	// Num Block Types
			"06 00 00 00 4e 69 4e 6f 64 65"	// Block Types[0]
			"00 00"	// Block Type Index[0]
			"00 00"	// Block Type Index[1]
			"54 00 00 00"	// Block Size[0]
			"50 00 00 00"	// Block Size[1]
			"02 00 00 00"	// Num Strings
			"04 00 00 00"	// Max String Length
			"04 00 00 00 52 6f 6f 74"	// Strings[0]
			"03 00 00 00 4b 69 64"	// Strings[1]
			"00 00 00 00"	// Num Groups
			"00 00 00 00"	// block 0: Name
			"00 00 00 00"	// block 0: Num Extra Data List
			"ff ff ff ff"	// block 0: Controller
			"34 12 00 00"	// block 0: Flags
			"00 00 c0 3f 00 00 00 c0 00 00 80 3e"	// block 0: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 40 00 00 40 40"	// block 0: Rotation m11 m21 m31 (floats)
			"00 00 80 40 00 00 a0 40 00 00 c0 40"	// block 0: Rotation m12 m22 m32 (floats)
			"00 00 e0 40 00 00 00 41 00 00 10 41"	// block 0: Rotation m13 m23 m33 (floats)
			"00 00 20 40"	// block 0: Scale
			"ff ff ff ff"	// block 0: Collision Object
			"01 00 00 00"	// block 0: Num Children
			"01 00 00 00"	// block 0: Children[0]
			"00 00 00 00"	// block 0: Num Effects
			"01 00 00 00"	// block 1: Name
			"00 00 00 00"	// block 1: Num Extra Data List
			"ff ff ff ff"	// block 1: Controller
			"0e 00 00 00"	// block 1: Flags
			"00 00 00 00 00 00 00 00 00 00 00 00"	// block 1: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 00 00 00 00 00"	// block 1: Rotation m11 m21 m31 (floats)
			"00 00 00 00 00 00 80 3f 00 00 00 00"	// block 1: Rotation m12 m22 m32 (floats)
			"00 00 00 00 00 00 00 00 00 00 80 3f"	// block 1: Rotation m13 m23 m33 (floats)
			"00 00 80 3f"	// block 1: Scale
			"ff ff ff ff"	// block 1: Collision Object
			"00 00 00 00"	// block 1: Num Children
			"00 00 00 00"	// block 1: Num Effects
			"01 00 00 00"	// Footer: Num Roots
			"00 00 00 00"	// Footer: Roots[0]
		);
	}

	/*! NetImmerse 3.1: the oldest layout. A header line and three Copyright lines, no Version, Num Blocks or footer, "Top Level
	 *  Object" in front of the root, the type name and a 1-based block number in front of every block, links stored 1-based
	 *  (0 = none), "End Of File" at the end.
	 *
	 *  \code{.py}
	 *  import struct
	 *
	 *  def sized(text):
	 *      b = text.encode('latin-1')
	 *      return struct.pack('<I', len(b)) + b
	 *
	 *  def floats(*f):
	 *      return struct.pack('<%df' % len(f), *f)
	 *
	 *  def ni_node(number, name, flags, translation, rotation, scale, velocity, children):
	 *      return (sized('NiNode') + struct.pack('<I', number)        # block type name and block number (1-based), before 3.3.0.13
	 *          + sized(name)
	 *          + struct.pack('<ii', 0, 0)                             # Extra Data, Controller: Ref is stored 1-based before 3.3.0.13, 0 = none
	 *          + struct.pack('<H', flags)
	 *          + floats(*translation) + floats(*rotation) + floats(scale) + floats(*velocity)
	 *          + struct.pack('<I', 0) + struct.pack('<I', 0)          # Num Properties, Has Bounding Volume (32 bit bool)
	 *          + struct.pack('<I', len(children)) + b''.join(struct.pack('<i', c + 1) for c in children)   # Children, 1-based
	 *          + struct.pack('<I', 0))                                # Num Effects
	 *
	 *  identity = [1, 0, 0, 0, 1, 0, 0, 0, 1]
	 *  golden = (b'NetImmerse File Format, Version 3.1\n'             # Header String; no Version / Num Blocks yet (from 3.1.0.1)
	 *      + b'line one\nline two\nline three\n'                      # Copyright: three LineStrings (up to 3.1)
	 *      + sized('Top Level Object')                                # announces the root block (no footer yet)
	 *      + ni_node(1, 'Root', 0x1234, (1.5, -2.0, 0.25), [1, 2, 3, 4, 5, 6, 7, 8, 9], 2.5, (-1.0, 0.5, 8.0), [1])
	 *      + ni_node(2, 'Kid', 0, (0, 0, 0), identity, 1.0, (0, 0, 0), [])
	 *      + sized('End Of File'))
	 *  \endcode
	 */
	static QByteArray goldenNetImmerse31()
	{
		return QByteArray::fromHex(
			"4e 65 74 49 6d 6d 65 72 73 65 20 46 69 6c 65 20"	// Header String
			"46 6f 72 6d 61 74 2c 20 56 65 72 73 69 6f 6e 20"
			"33 2e 31 0a"
			"6c 69 6e 65 20 6f 6e 65 0a"	// Copyright[0]
			"6c 69 6e 65 20 74 77 6f 0a"	// Copyright[1]
			"6c 69 6e 65 20 74 68 72 65 65 0a"	// Copyright[2]
			"10 00 00 00 54 6f 70 20 4c 65 76 65 6c 20 4f 62"	// Top Level Object
			"6a 65 63 74"
			"06 00 00 00 4e 69 4e 6f 64 65"	// block type name
			"01 00 00 00"	// block number (1-based)
			"04 00 00 00 52 6f 6f 74"	// block 0: Name
			"00 00 00 00"	// block 0: Extra Data
			"00 00 00 00"	// block 0: Controller
			"34 12"	// block 0: Flags
			"00 00 c0 3f 00 00 00 c0 00 00 80 3e"	// block 0: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 40 00 00 40 40"	// block 0: Rotation m11 m21 m31 (floats)
			"00 00 80 40 00 00 a0 40 00 00 c0 40"	// block 0: Rotation m12 m22 m32 (floats)
			"00 00 e0 40 00 00 00 41 00 00 10 41"	// block 0: Rotation m13 m23 m33 (floats)
			"00 00 20 40"	// block 0: Scale
			"00 00 80 bf 00 00 00 3f 00 00 00 41"	// block 0: Velocity (x, y, z floats)
			"00 00 00 00"	// block 0: Num Properties
			"00 00 00 00"	// block 0: Has Bounding Volume
			"01 00 00 00"	// block 0: Num Children
			"02 00 00 00"	// block 0: Children[0]
			"00 00 00 00"	// block 0: Num Effects
			"06 00 00 00 4e 69 4e 6f 64 65"	// block type name
			"02 00 00 00"	// block number (1-based)
			"03 00 00 00 4b 69 64"	// block 1: Name
			"00 00 00 00"	// block 1: Extra Data
			"00 00 00 00"	// block 1: Controller
			"00 00"	// block 1: Flags
			"00 00 00 00 00 00 00 00 00 00 00 00"	// block 1: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 00 00 00 00 00"	// block 1: Rotation m11 m21 m31 (floats)
			"00 00 00 00 00 00 80 3f 00 00 00 00"	// block 1: Rotation m12 m22 m32 (floats)
			"00 00 00 00 00 00 00 00 00 00 80 3f"	// block 1: Rotation m13 m23 m33 (floats)
			"00 00 80 3f"	// block 1: Scale
			"00 00 00 00 00 00 00 00 00 00 00 00"	// block 1: Velocity (x, y, z floats)
			"00 00 00 00"	// block 1: Num Properties
			"00 00 00 00"	// block 1: Has Bounding Volume
			"00 00 00 00"	// block 1: Num Children
			"00 00 00 00"	// block 1: Num Effects
			"0b 00 00 00 45 6e 64 20 4f 66 20 46 69 6c 65"	// End Of File
		);
	}

	/*! NetImmerse 10.1.0.0: the block type table is in the header (no names in front of the blocks), but every block is still
	 *  preceded by four zero bytes (from 10.0.1.0 up to, not including, 10.2.0.0; nif.xml does not list them), strings are
	 *  SizedStrings, Flags 16 bit, no Endian Type, string table or block sizes yet.
	 *
	 *  \code{.py}
	 *  import struct
	 *
	 *  def sized(text):                        # SizedString: u32 length + bytes
	 *      b = text.encode('latin-1')
	 *      return struct.pack('<I', len(b)) + b
	 *
	 *  def floats(*f):
	 *      return struct.pack('<%df' % len(f), *f)
	 *
	 *  def ni_node(name, flags, translation, rotation, scale, children):
	 *      return (struct.pack('<I', 0)                               # 4 zero bytes in front of every block (10.0.1.0 up to, not including, 10.2.0.0)
	 *          + sized(name)                                          # NiObjectNET.Name: string is a SizedString before 20.1.0.3
	 *          + struct.pack('<Ii', 0, -1)                            # Num Extra Data List (from 10.0.1.0), Controller
	 *          + struct.pack('<H', flags)                             # NiAVObject.Flags: 16 bit while User Version 2 <= 26
	 *          + floats(*translation) + floats(*rotation) + floats(scale)   # no Velocity after 4.2.2.0
	 *          + struct.pack('<I', 0)                                 # Num Properties (User Version 2 <= 34)
	 *          + struct.pack('<i', -1)                                # Collision Object (from 10.0.1.0); no Has Bounding Volume after 4.2.2.0
	 *          + struct.pack('<I', len(children)) + b''.join(struct.pack('<i', c) for c in children)
	 *          + struct.pack('<I', 0))                                # Num Effects (User Version 2 < 130)
	 *
	 *  identity = [1, 0, 0, 0, 1, 0, 0, 0, 1]
	 *  golden = (b'Gamebryo File Format, Version 10.1.0.0\n'          # Header String
	 *      + struct.pack('<III', 0x0A010000, 0, 2)                    # Version, User Version (from 10.0.1.8), Num Blocks
	 *      + struct.pack('<H', 1) + sized('NiNode')                   # Num Block Types, Block Types (from 5.0.0.1)
	 *      + struct.pack('<HH', 0, 0)                                 # Block Type Index per block
	 *      + struct.pack('<I', 0)                                     # Num Groups (from 5.0.0.6)
	 *      + ni_node('Root', 0x1234, (1.5, -2.0, 0.25), [1, 2, 3, 4, 5, 6, 7, 8, 9], 2.5, [1])
	 *      + ni_node('Kid', 0, (0, 0, 0), identity, 1.0, [])
	 *      + struct.pack('<Ii', 1, 0))                                # Footer: Num Roots, Roots[0]
	 *  \endcode
	 */
	static QByteArray goldenNetImmerse10_1()
	{
		return QByteArray::fromHex(
			"47 61 6d 65 62 72 79 6f 20 46 69 6c 65 20 46 6f"	// Header String
			"72 6d 61 74 2c 20 56 65 72 73 69 6f 6e 20 31 30"
			"2e 31 2e 30 2e 30 0a"
			"00 00 01 0a"	// Version
			"00 00 00 00"	// User Version
			"02 00 00 00"	// Num Blocks
			"01 00"	// Num Block Types
			"06 00 00 00 4e 69 4e 6f 64 65"	// Block Types[0]
			"00 00"	// Block Type Index[0]
			"00 00"	// Block Type Index[1]
			"00 00 00 00"	// Num Groups
			"00 00 00 00"	// block 0: separator
			"04 00 00 00 52 6f 6f 74"	// block 0: Name
			"00 00 00 00"	// block 0: Num Extra Data List
			"ff ff ff ff"	// block 0: Controller
			"34 12"	// block 0: Flags
			"00 00 c0 3f 00 00 00 c0 00 00 80 3e"	// block 0: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 40 00 00 40 40"	// block 0: Rotation m11 m21 m31 (floats)
			"00 00 80 40 00 00 a0 40 00 00 c0 40"	// block 0: Rotation m12 m22 m32 (floats)
			"00 00 e0 40 00 00 00 41 00 00 10 41"	// block 0: Rotation m13 m23 m33 (floats)
			"00 00 20 40"	// block 0: Scale
			"00 00 00 00"	// block 0: Num Properties
			"ff ff ff ff"	// block 0: Collision Object
			"01 00 00 00"	// block 0: Num Children
			"01 00 00 00"	// block 0: Children[0]
			"00 00 00 00"	// block 0: Num Effects
			"00 00 00 00"	// block 1: separator
			"03 00 00 00 4b 69 64"	// block 1: Name
			"00 00 00 00"	// block 1: Num Extra Data List
			"ff ff ff ff"	// block 1: Controller
			"00 00"	// block 1: Flags
			"00 00 00 00 00 00 00 00 00 00 00 00"	// block 1: Translation (x, y, z floats)
			"00 00 80 3f 00 00 00 00 00 00 00 00"	// block 1: Rotation m11 m21 m31 (floats)
			"00 00 00 00 00 00 80 3f 00 00 00 00"	// block 1: Rotation m12 m22 m32 (floats)
			"00 00 00 00 00 00 00 00 00 00 80 3f"	// block 1: Rotation m13 m23 m33 (floats)
			"00 00 80 3f"	// block 1: Scale
			"00 00 00 00"	// block 1: Num Properties
			"ff ff ff ff"	// block 1: Collision Object
			"00 00 00 00"	// block 1: Num Children
			"00 00 00 00"	// block 1: Num Effects
			"01 00 00 00"	// Footer: Num Roots
			"00 00 00 00"	// Footer: Roots[0]
		);
	}

	//! The scene of the golden files: "Root" (Flags 0x1234, translation (1.5, -2, 0.25), rotation m(r, c) = 3 r + c + 1, scale
	//! 2.5, velocity (-1, 0.5, 8) where the version has one) whose only child is "Kid" (everything at its default)
	static bool buildGoldenScene( NifModel & nif )
	{
		bool ok = true;

		// clear() leaves the three Copyright lines of 3.1 headers empty; the file needs them
		if ( nif.getVersionNumber() <= 0x03010000 ) {
			ok &= nif.updateArray( nif.getHeader(), "Copyright" );
			nif.setArray<QString>( nif.getHeader(), "Copyright", QVector<QString>() << "line one" << "line two" << "line three" );
		}

		QModelIndex iRoot = nif.insertNiBlock( "NiNode" );
		QModelIndex iKid = nif.insertNiBlock( "NiNode" );
		ok &= iRoot.isValid() && iKid.isValid();

		ok &= nif.set<QString>( iRoot, "Name", "Root" );
		ok &= nif.set<QString>( iKid, "Name", "Kid" );
		ok &= nif.set<int>( iRoot, "Flags", 0x1234 );
		ok &= nif.set<Vector3>( iRoot, "Translation", Vector3( 1.5f, -2.0f, 0.25f ) );
		ok &= nif.set<Matrix>( iRoot, "Rotation", TestEnv::distinctRotation() );
		ok &= nif.set<float>( iRoot, "Scale", 2.5f );
		if ( nif.getVersionNumber() <= 0x04020200 )
			ok &= nif.set<Vector3>( iRoot, "Velocity", Vector3( -1.0f, 0.5f, 8.0f ) );
		ok &= nif.set<int>( iRoot, "Num Children", 1 );
		ok &= nif.updateArray( iRoot, "Children" );
		ok &= nif.setLinkArray( iRoot, "Children", QVector<qint32>() << 1 );

		return ok;
	}

	//! What a model loaded from a golden file must hold: the scene of buildGoldenScene(), every value spelled out here
	static void verifyGoldenScene( const NifModel & nif, quint32 version, int kidFlags )
	{
		QCOMPARE( nif.getVersionNumber(), version );
		QCOMPARE( blockTypes( nif ), QStringList() << "NiNode" << "NiNode" );
		QCOMPARE( nif.getRootLinks(), QList<int>() << 0 );

		QModelIndex iRoot = nif.getBlock( 0 );
		QModelIndex iKid = nif.getBlock( 1 );

		QCOMPARE( nif.get<QString>( iRoot, "Name" ), QString( "Root" ) );
		QCOMPARE( nif.get<int>( iRoot, "Flags" ), 0x1234 );
		QVERIFY( nif.get<Vector3>( iRoot, "Translation" ) == Vector3( 1.5f, -2.0f, 0.25f ) );
		QCOMPARE( nif.get<float>( iRoot, "Scale" ), 2.5f );
		QCOMPARE( nif.getLinkArray( iRoot, "Children" ), QVector<qint32>() << 1 );
		if ( version <= 0x04020200 )
			QVERIFY( nif.get<Vector3>( iRoot, "Velocity" ) == Vector3( -1.0f, 0.5f, 8.0f ) );

		// the nine floats of the rotation are 1..9 in file order: the fourth is m(1, 0)
		Matrix rotation = nif.get<Matrix>( iRoot, "Rotation" );
		for ( int r = 0; r < 3; r++ )
			for ( int c = 0; c < 3; c++ )
				QCOMPARE( rotation( r, c ), float( 3 * r + c + 1 ) );
		QCOMPARE( rotation( 1, 0 ), 4.0f );

		QCOMPARE( nif.get<QString>( iKid, "Name" ), QString( "Kid" ) );
		QCOMPARE( nif.get<int>( iKid, "Flags" ), kidFlags );
		QVERIFY( nif.get<Vector3>( iKid, "Translation" ) == Vector3( 0.0f, 0.0f, 0.0f ) );
		QCOMPARE( nif.get<float>( iKid, "Scale" ), 1.0f );
		QCOMPARE( nif.get<int>( iKid, "Num Children" ), 0 );

		Matrix identity = nif.get<Matrix>( iKid, "Rotation" );
		for ( int r = 0; r < 3; r++ )
			for ( int c = 0; c < 3; c++ )
				QCOMPARE( identity( r, c ), r == c ? 1.0f : 0.0f );
	}

private slots:
	void initTestCase()
	{
		QString err = TestEnv::reloadXml();
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
	}

	void cleanup()
	{
		// Nothing in these tests may pop up a QMessageBox (the app's way of reporting load/save problems)
		QStringList boxes = TestEnv::takeMessageBoxes();
		QVERIFY2( boxes.isEmpty(), qPrintable( boxes.join( " | " ) ) );
	}

	void saveLoadSave_data() { addProfileRows(); }

	//! build -> save -> load -> compare structure -> save again -> byte-identical
	void saveLoadSave()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );

		auto a = makeScene( version, userVersion, userVersion2 );
		QCOMPARE( a->getVersion(), version );
		QVERIFY( TestEnv::buildScene( *a ) );
		QCOMPARE( a->getBlockCount(), 5 );

		bool ok = false;
		QByteArray first = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY( first.size() > 100 );
		QVERIFY2( a->getMessages().isEmpty(), "messages after save" );

		NifModel b;
		QVERIFY( loadBytes( b, first ) );
		QCOMPARE( b.getMessages().count(), 0 );
		QCOMPARE( b.getBlockCount(), 5 );
		QCOMPARE( b.getVersion(), version );

		QString d = TestEnv::diffModels( *a, b );
		QVERIFY2( d.isEmpty(), qPrintable( d ) );

		QByteArray second = saveBytes( b, &ok );
		QVERIFY( ok );
		QVERIFY2( second == first, "re-saving the loaded model changed the bytes" );
	}

	void contents_data() { addProfileRows(); }

	//! Spot-check that the values we put in are the values that come out (guards the helper and the diff)
	void contents()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );

		auto a = makeScene( version, userVersion, userVersion2 );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		quint32 v = NifModel::version2number( version );
		QByteArray prefix = ( v <= 0x0A000102 ? "NetImmerse File Format, Version " : "Gamebryo File Format, Version " );
		QVERIFY2( bytes.startsWith( prefix + version.toLatin1() + "\n" ), bytes.left( 60 ).constData() );

		NifModel nif;
		QVERIFY( loadBytes( nif, bytes ) );

		QCOMPARE( nif.getVersionNumber(), v );
		QCOMPARE( (int)nif.getUserVersion(), v >= 0x0A000108 ? userVersion : 0 );
		QCOMPARE( (int)nif.getUserVersion2(), userVersion2 );

		QStringList types;
		for ( int i = 0; i < nif.getBlockCount(); i++ )
			types << nif.getBlockName( nif.getBlock( i ) );
		QCOMPARE( types, QStringList() << "NiNode" << "NiNode" << "NiTriShape" << "NiTriShapeData" << "NiStringExtraData" );

		QModelIndex iRoot = nif.getBlock( 0 );
		QModelIndex iShape = nif.getBlock( 2 );
		QModelIndex iData = nif.getBlock( 3 );
		QModelIndex iExtra = nif.getBlock( 4 );

		QCOMPARE( nif.get<QString>( iRoot, "Name" ), QString( "Scene Root" ) );
		QCOMPARE( nif.get<QString>( nif.getBlock( 1 ), "Name" ), QString( "Child" ) );
		QCOMPARE( nif.get<QString>( iShape, "Name" ), QString( "Shape" ) );
		QVERIFY( nif.get<Vector3>( iRoot, "Translation" ) == Vector3( 1.0f, 2.0f, 3.0f ) );
		QCOMPARE( nif.get<float>( iRoot, "Scale" ), 1.5f );
		QCOMPARE( nif.getLinkArray( iRoot, "Children" ), QVector<qint32>() << 1 << 2 );
		QCOMPARE( nif.getLink( iShape, "Data" ), 3 );
		QCOMPARE( nif.get<QString>( iExtra, "String Data" ), QString( "hello nifskope" ) );
		QCOMPARE( nif.getArray<Vector3>( iData, "Vertices" ).count(), 3 );
		QVERIFY( nif.getArray<Vector3>( iData, "Vertices" ).at( 1 ) == Vector3( 1.0f, 0.0f, 0.0f ) );
		QVERIFY( nif.getArray<Triangle>( iData, "Triangles" ).at( 0 ) == Triangle( 0, 1, 2 ) );

		if ( v >= 0x0A000100 )
			QCOMPARE( nif.getLinkArray( iRoot, "Extra Data List" ), QVector<qint32>() << 4 );
		else
			QCOMPARE( nif.getLink( iRoot, "Extra Data" ), 4 );

		// Only the root is unreferenced
		QCOMPARE( nif.getRootLinks(), QList<int>() << 0 );
		if ( v >= 0x0303000D )
			QCOMPARE( nif.get<int>( nif.getFooter(), "Num Roots" ), 1 );

		// Header bookkeeping written by updateHeader()
		if ( v >= 0x05000001 ) {
			QCOMPARE( nif.get<int>( nif.getHeader(), "Num Blocks" ), 5 );
			QCOMPARE( nif.get<int>( nif.getHeader(), "Num Block Types" ), 4 );
		}
		if ( v >= 0x14010001 ) {
			QVERIFY( nif.getArray<QString>( nif.getHeader(), "Strings" ).contains( "hello nifskope" ) );
			QCOMPARE( nif.get<int>( nif.getHeader(), "Max String Length" ), 14 );
		}
	}

	void blockSizes_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );

		int rows = 0;
		for ( const TestEnv::Profile & p : TestEnv::profiles() ) {
			if ( NifModel::version2number( QString::fromLatin1( p.version ) ) >= 0x14020005 ) {
				QTest::newRow( p.name ) << QString::fromLatin1( p.version ) << p.userVersion << p.userVersion2;
				rows++;
			}
		}

		// An empty table makes QtTest abort the whole process; fail this test instead
		if ( !rows )
			QTest::newRow( "no profile >= 20.2.0.5" ) << QString() << 0 << 0;
	}

	//! From 20.2.0.5 the header stores each block's size. NifSStream (sizing) must agree with NifOStream (writing).
	void blockSizes()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QVERIFY2( !version.isEmpty(), "no profile is >= 20.2.0.5" );

		auto a = makeScene( version, userVersion, userVersion2 );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		verifyBlockSizes( bytes, 5 );
	}

	void file_data() { addProfileRows(); }

	//! saveToFile()/loadFromFile() produce the same bytes as save()/load() and set the file info
	void file()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );

		auto a = makeScene( version, userVersion, userVersion2 );
		QVERIFY( TestEnv::buildScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		QString path = QDir( dir.path() ).filePath( "scene.nif" );
		QVERIFY( a->saveToFile( path ) );

		QFile f( path );
		QVERIFY( f.open( QIODevice::ReadOnly ) );
		QCOMPARE( f.readAll(), bytes );
		f.close();

		NifModel b;
		QVERIFY( b.loadFromFile( path ) );
		QCOMPARE( b.getFilename(), QString( "scene" ) );
		QCOMPARE( b.getFolder(), QFileInfo( path ).absolutePath() );
		QCOMPARE( b.getState(), BaseModel::Default );
		QVERIFY2( TestEnv::diffModels( *a, b ).isEmpty(), qPrintable( TestEnv::diffModels( *a, b ) ) );
	}

	// ---- richer scenes: TestEnv::buildRichScene() / buildBSTriShapeScene() -----------------------------------------

	void richScene_saveLoadSave_data() { addProfileRows(); }

	//! saveLoadSave() for the rich scene: UV sets, normals, tangents, vertex colours, match groups, a Ptr link, material /
	//! texture / shader blocks and extra data with many strings go through save -> load -> save in every profile
	void richScene_saveLoadSave()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );

		auto a = makeScene( version, userVersion, userVersion2 );
		QString refused;
		QVERIFY2( TestEnv::buildRichScene( *a, &refused ), qPrintable( "the model refused: " + refused ) );
		QCOMPARE( a->getBlockCount(), richBlockTypes( a->getVersionNumber(), userVersion, userVersion2 ).count() );

		bool ok = false;
		QByteArray first = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY2( a->getMessages().isEmpty(), "messages after save" );

		NifModel b;
		QVERIFY( loadBytes( b, first ) );
		QCOMPARE( b.getMessages().count(), 0 );
		QCOMPARE( blockTypes( b ), blockTypes( *a ) );

		QString d = TestEnv::diffModels( *a, b );
		QVERIFY2( d.isEmpty(), qPrintable( d ) );

		QByteArray second = saveBytes( b, &ok );
		QVERIFY( ok );
		QVERIFY2( second == first, qPrintable( "re-saving the loaded model changed the bytes: " + TestEnv::diffBytes( second, first ) ) );
	}

	void richScene_contents_data() { addProfileRows(); }

	//! Spot checks of the loaded rich scene against values written out here (not read back from the builder): a field
	//! the builder left at its default, or one the loader drops, would pass the model comparison of
	//! richScene_saveLoadSave() unnoticed, because both models would agree on the default
	void richScene_contents()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );

		auto a = makeScene( version, userVersion, userVersion2 );
		QString refused;
		QVERIFY2( TestEnv::buildRichScene( *a, &refused ), qPrintable( "the model refused: " + refused ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		NifModel nif;
		QVERIFY( loadBytes( nif, bytes ) );
		QCOMPARE( nif.getMessages().count(), 0 );

		const quint32 v = NifModel::version2number( version );
		const bool fo3 = userVersion2 == 34;
		const bool skyrimLE = userVersion == 12 && userVersion2 > 34 && userVersion2 < 100;
		const bool bsFlags = v == 0x14020007 && userVersion2 > 0;
		const int blocks = richBlockTypes( v, userVersion, userVersion2 ).count();

		QCOMPARE( blockTypes( nif ), richBlockTypes( v, userVersion, userVersion2 ) );
		QCOMPARE( nif.getRootLinks(), QList<int>() << 0 );   // every added block is reachable from the root

		QModelIndex iRoot = nif.getBlock( 0 );
		QModelIndex iShape = nif.getBlock( 2 );
		QModelIndex iData = nif.getBlock( 3 );

		// transforms: file order of the nine rotation elements is 1..9, so a transposed matrix shows
		Matrix rotation = nif.get<Matrix>( iRoot, "Rotation" );
		for ( int r = 0; r < 3; r++ )
			for ( int c = 0; c < 3; c++ )
				QCOMPARE( rotation( r, c ), float( 3 * r + c + 1 ) );
		QCOMPARE( rotation( 1, 0 ), 4.0f );
		QCOMPARE( nif.get<float>( iShape, "Scale" ), 0.5f );
		if ( v <= 0x04020200 )
			QVERIFY( nif.get<Vector3>( iRoot, "Velocity" ) == Vector3( -1.0f, 0.5f, 8.0f ) );

		// geometry flags
		if ( v <= 0x04020200 )
			QCOMPARE( nif.get<int>( iData, "Num UV Sets" ), 1 );
		if ( v <= 0x04000002 )
			QCOMPARE( nif.get<int>( iData, "Has UV" ), 1 );
		if ( v >= 0x0A000100 && !bsFlags )
			QCOMPARE( nif.get<int>( iData, "Vector Flags" ), v >= 0x0A010000 ? 0x1001 : 0x0001 );
		if ( bsFlags )
			QCOMPARE( nif.get<int>( iData, "BS Vector Flags" ), 0x1001 );

		// geometry arrays
		QCOMPARE( ( flat<Vector3, 3>( nif.getArray<Vector3>( iData, "Normals" ) ) ), floats( { 0, 0, 1, 0, 1, 0, 1, 0, 0 } ) );
		if ( v >= 0x0A010000 ) {
			QCOMPARE( ( flat<Vector3, 3>( nif.getArray<Vector3>( iData, "Tangents" ) ) ), floats( { 1, 0, 0, 0, 0, 1, 0, 1, 0 } ) );
			QCOMPARE( ( flat<Vector3, 3>( nif.getArray<Vector3>( iData, "Bitangents" ) ) ), floats( { 0, 1, 0, 1, 0, 0, 0, 0, 1 } ) );
		}
		QCOMPARE( ( flat<Color4, 4>( nif.getArray<Color4>( iData, "Vertex Colors" ) ) ), floats( { 1, 0, 0, 1, 0, 1, 0, 0.5f, 0, 0, 1, 0.25f } ) );
		QVERIFY( nif.get<Vector3>( iData, "Center" ) == Vector3( 0.25f, 0.25f, 0.0f ) );
		QCOMPARE( nif.get<float>( iData, "Radius" ), 0.75f );

		QModelIndex iUVSets = nif.getIndex( iData, "UV Sets" );
		QCOMPARE( nif.rowCount( iUVSets ), 1 );
		QCOMPARE( ( flat<Vector2, 2>( nif.getArray<Vector2>( nif.index( 0, 0, iUVSets ) ) ) ), floats( { 0, 0, 1, 0.25f, 0.5f, 1 } ) );

		QCOMPARE( nif.get<int>( iData, "Num Match Groups" ), 1 );
		QModelIndex iGroup = nif.index( 0, 0, nif.getIndex( iData, "Match Groups" ) );
		QCOMPARE( nif.get<int>( iGroup, "Num Vertices" ), 2 );
		QCOMPARE( nif.getArray<int>( iGroup, "Vertex Indices" ), QVector<int>() << 2 << 1 );

		if ( skyrimLE )
			QCOMPARE( nif.get<int>( iData, "Material CRC" ), 0x12345678 );

		// extra data: the root lists / chains "Note" (block 4) and the three blocks after it
		if ( v >= 0x0A000100 ) {
			QCOMPARE( nif.getLinkArray( iRoot, "Extra Data List" ), QVector<qint32>() << 4 << 5 << 6 << 7 );
		} else {
			QCOMPARE( nif.getLink( iRoot, "Extra Data" ), 4 );
			QCOMPARE( nif.getLink( nif.getBlock( 4 ), "Next Extra Data" ), 5 );
			QCOMPARE( nif.getLink( nif.getBlock( 5 ), "Next Extra Data" ), 6 );
			QCOMPARE( nif.getLink( nif.getBlock( 6 ), "Next Extra Data" ), 7 );
			QCOMPARE( nif.getLink( nif.getBlock( 7 ), "Next Extra Data" ), -1 );
		}

		QString longText;
		for ( int i = 0; i < 30; i++ )
			longText += "0123456789";

		QCOMPARE( nif.getArray<QString>( nif.getBlock( 5 ), "Data" ), QVector<QString>()
			<< "" << "a" << "two words" << "Spaces, punctuation: (x) [y] {z}; 100% #1 'q' \"d\""
			<< "textures\\actors\\character\\body_d.dds" << longText );

		QModelIndex iKeys = nif.getIndex( nif.getBlock( 6 ), "Text Keys" );
		QCOMPARE( nif.rowCount( iKeys ), 2 );
		QCOMPARE( nif.get<float>( nif.index( 0, 0, iKeys ), "Time" ), 0.5f );
		QCOMPARE( nif.get<QString>( nif.index( 0, 0, iKeys ), "Value" ), QString( "start: walk" ) );
		QCOMPARE( nif.get<float>( nif.index( 1, 0, iKeys ), "Time" ), 2.25f );
		QCOMPARE( nif.get<QString>( nif.index( 1, 0, iKeys ), "Value" ), QString( "end" ) );

		QCOMPARE( nif.get<QByteArray>( nif.getBlock( 7 ), "Binary Data" ), QByteArray( "\x00\xff\x80\x7f\n\rNIF\0", 10 ) );

		// material, texture and shader blocks (block numbers follow the order buildRichScene() inserts them in)
		if ( userVersion2 <= 34 ) {
			QModelIndex iMaterial = nif.getBlock( 8 );
			QModelIndex iAlpha = nif.getBlock( 9 );

			QCOMPARE( nif.get<QString>( iMaterial, "Name" ), QString( "Material" ) );
			if ( v <= 0x0A000102 )
				QCOMPARE( nif.get<int>( iMaterial, "Flags" ), 0x0123 );
			if ( userVersion2 < 26 ) {
				QVERIFY( nif.get<Color3>( iMaterial, "Ambient Color" ) == Color3( 0.125f, 0.25f, 0.5f ) );
				QVERIFY( nif.get<Color3>( iMaterial, "Diffuse Color" ) == Color3( 0.25f, 0.5f, 0.75f ) );
			}
			QVERIFY( nif.get<Color3>( iMaterial, "Specular Color" ) == Color3( 0.5f, 0.25f, 0.125f ) );
			QVERIFY( nif.get<Color3>( iMaterial, "Emissive Color" ) == Color3( 0.0f, 0.5f, 0.0f ) );
			QCOMPARE( nif.get<float>( iMaterial, "Glossiness" ), 33.0f );
			QCOMPARE( nif.get<float>( iMaterial, "Alpha" ), 0.75f );
			if ( userVersion2 > 21 )
				QCOMPARE( nif.get<float>( iMaterial, "Emissive Mult" ), 1.5f );

			QCOMPARE( nif.get<QString>( iAlpha, "Name" ), QString( "Alpha" ) );
			QCOMPARE( nif.get<int>( iAlpha, "Flags" ), 0x01ED );
			QCOMPARE( nif.get<int>( iAlpha, "Threshold" ), 100 );

			if ( fo3 ) {
				QCOMPARE( nif.getLinkArray( iShape, "Properties" ), QVector<qint32>() << 8 << 9 << 10 );

				QModelIndex iShader = nif.getBlock( 10 );
				QCOMPARE( nif.get<QString>( iShader, "Name" ), QString( "Shader PP" ) );
				QCOMPARE( nif.get<float>( iShader, "Environment Map Scale" ), 1.25f );
				QCOMPARE( nif.get<float>( iShader, "Parallax Max Passes" ), 3.0f );
				QCOMPARE( nif.getLink( iShader, "Texture Set" ), 11 );

				QCOMPARE( nif.get<int>( nif.getBlock( 11 ), "Num Textures" ), 4 );
				QCOMPARE( nif.getArray<QString>( nif.getBlock( 11 ), "Textures" ), QVector<QString>()
					<< "textures\\fo3\\wall_d.dds" << "textures\\fo3\\wall_n.dds" << "" << "textures\\fo3\\wall_s.dds" );
			} else if ( v >= 0x0303000D ) {
				QCOMPARE( nif.getLinkArray( iShape, "Properties" ), QVector<qint32>() << 8 << 9 << 10 );

				QModelIndex iTexturing = nif.getBlock( 10 );
				QModelIndex iSource = nif.getBlock( 11 );
				QCOMPARE( nif.get<QString>( iTexturing, "Name" ), QString( "Texturing" ) );
				if ( v <= 0x14010001 )
					QCOMPARE( nif.get<int>( iTexturing, "Apply Mode" ), 1 );

				QCOMPARE( nif.get<int>( iTexturing, "Has Base Texture" ), 1 );
				QModelIndex iBase = nif.getIndex( iTexturing, "Base Texture" );
				QCOMPARE( nif.getLink( iBase, "Source" ), 11 );
				if ( v <= 0x14000005 ) {
					QCOMPARE( nif.get<int>( iBase, "Clamp Mode" ), 1 );
					QCOMPARE( nif.get<int>( iBase, "Filter Mode" ), 6 );
					QCOMPARE( nif.get<int>( iBase, "UV Set" ), 1 );
				}
				if ( v >= 0x0A010000 ) {
					QCOMPARE( nif.get<int>( iBase, "Has Texture Transform" ), 1 );
					QVERIFY( nif.get<Vector2>( iBase, "Translation" ) == Vector2( 0.25f, 0.5f ) );
					QVERIFY( nif.get<Vector2>( iBase, "Scale" ) == Vector2( 2.0f, 3.0f ) );
					QCOMPARE( nif.get<float>( iBase, "Rotation" ), 0.5f );
					QCOMPARE( nif.get<int>( iBase, "Transform Method" ), 2 );
					QVERIFY( nif.get<Vector2>( iBase, "Center" ) == Vector2( 0.5f, 0.5f ) );
				}

				QCOMPARE( nif.get<QString>( iSource, "Name" ), QString( "Texture File" ) );
				QCOMPARE( nif.get<QString>( iSource, "File Name" ), QString( "textures\\base_d.dds" ) );
			} else {
				QCOMPARE( nif.getLinkArray( iShape, "Properties" ), QVector<qint32>() << 8 << 9 );
			}
		} else if ( skyrimLE ) {
			QModelIndex iShader = nif.getBlock( 8 );
			QModelIndex iTextures = nif.getBlock( 9 );
			QModelIndex iAlpha = nif.getBlock( 10 );

			QCOMPARE( nif.getLink( iShape, "Shader Property" ), 8 );
			QCOMPARE( nif.getLink( iShape, "Alpha Property" ), 10 );

			QCOMPARE( nif.get<QString>( iShader, "Name" ), QString( "Lighting Shader" ) );
			QCOMPARE( nif.get<int>( iShader, "Skyrim Shader Type" ), 3 );
			QVERIFY( nif.get<Vector2>( iShader, "UV Offset" ) == Vector2( 0.125f, 0.25f ) );
			QVERIFY( nif.get<Vector2>( iShader, "UV Scale" ) == Vector2( 2.0f, 0.5f ) );
			QCOMPARE( nif.getLink( iShader, "Texture Set" ), 9 );
			QVERIFY( nif.get<Color3>( iShader, "Emissive Color" ) == Color3( 0.5f, 0.25f, 0.125f ) );
			QCOMPARE( nif.get<float>( iShader, "Emissive Multiple" ), 1.5f );
			QCOMPARE( nif.get<float>( iShader, "Alpha" ), 0.75f );
			QCOMPARE( nif.get<float>( iShader, "Glossiness" ), 33.0f );
			QVERIFY( nif.get<Color3>( iShader, "Specular Color" ) == Color3( 1.0f, 0.5f, 0.25f ) );
			QCOMPARE( nif.get<float>( iShader, "Specular Strength" ), 2.0f );

			QCOMPARE( nif.get<int>( iTextures, "Num Textures" ), 4 );
			QCOMPARE( nif.getArray<QString>( iTextures, "Textures" ), QVector<QString>()
				<< "textures\\skyrim\\wall_d.dds" << "textures\\skyrim\\wall_n.dds" << "" << "textures\\skyrim\\wall_s.dds" );

			QCOMPARE( nif.get<QString>( iAlpha, "Name" ), QString( "Alpha" ) );
			QCOMPARE( nif.get<int>( iAlpha, "Flags" ), 0x01ED );
			QCOMPARE( nif.get<int>( iAlpha, "Threshold" ), 100 );
		}

		// skin: the last two blocks. Ptr links come back as the block numbers they were set to, 0 (the root) included
		if ( v >= 0x0303000D ) {
			QModelIndex iSkin = nif.getBlock( blocks - 2 );
			QModelIndex iSkinData = nif.getBlock( blocks - 1 );

			QCOMPARE( nif.getLink( iShape, "Skin Instance" ), blocks - 2 );
			QCOMPARE( nif.getLink( iSkin, "Data" ), blocks - 1 );
			QCOMPARE( nif.getLink( iSkin, "Skeleton Root" ), 0 );
			QCOMPARE( nif.get<int>( iSkin, "Num Bones" ), 1 );
			QCOMPARE( nif.getLinkArray( iSkin, "Bones" ), QVector<qint32>() << 0 );

			QCOMPARE( nif.get<int>( iSkinData, "Num Bones" ), 1 );
			QModelIndex iBones = nif.getIndex( iSkinData, "Bone List" );
			QCOMPARE( nif.rowCount( iBones ), 1 );
			QModelIndex iBone = nif.index( 0, 0, iBones );
			QCOMPARE( nif.get<float>( iBone, "Bounding Sphere Radius" ), 1.5f );
			QCOMPARE( nif.get<int>( iBone, "Num Vertices" ), 2 );
			QModelIndex iWeights = nif.getIndex( iBone, "Vertex Weights" );
			QCOMPARE( nif.rowCount( iWeights ), 2 );
			QCOMPARE( nif.get<int>( nif.index( 0, 0, iWeights ), "Index" ), 2 );
			QCOMPARE( nif.get<float>( nif.index( 0, 0, iWeights ), "Weight" ), 0.25f );
			QCOMPARE( nif.get<int>( nif.index( 1, 0, iWeights ), "Index" ), 1 );
			QCOMPARE( nif.get<float>( nif.index( 1, 0, iWeights ), "Weight" ), 0.75f );
		}

		// from 20.1.0.3 the header's string table holds every string once, nested ones (text keys) and file paths included
		if ( v >= 0x14010003 ) {
			QStringList want;
			want << "Scene Root" << "Child" << "Shape" << "Note" << "hello nifskope" << "Strings" << "Text Keys" << "start: walk" << "end" << "Binary";
			if ( userVersion2 <= 34 )
				want << "Material" << "Alpha";
			if ( fo3 )
				want << "Shader PP";
			else if ( userVersion2 <= 34 )
				want << "Texturing" << "Texture File" << "textures\\base_d.dds";
			if ( skyrimLE )
				want << "Lighting Shader" << "Alpha";

			// the same strings, each once (the order is not part of the contract)
			QStringList have;
			for ( const QString & s : nif.getArray<QString>( nif.getHeader(), "Strings" ) )
				have << s;
			have.sort();
			want.sort();
			QCOMPARE( have, want );
			QCOMPARE( nif.get<int>( nif.getHeader(), "Num Strings" ), want.count() );

			int longest = 0;
			for ( const QString & s : want )
				longest = qMax( longest, s.length() );
			QCOMPARE( nif.get<int>( nif.getHeader(), "Max String Length" ), longest );
		}
	}

	void richScene_blockSizes_data() { blockSizes_data(); }

	//! blockSizes() on the rich scene: every block type it adds is sized by NifSStream the way NifOStream writes it
	void richScene_blockSizes()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QVERIFY2( !version.isEmpty(), "no profile is >= 20.2.0.5" );

		auto a = makeScene( version, userVersion, userVersion2 );
		QVERIFY( TestEnv::buildRichScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		verifyBlockSizes( bytes, a->getBlockCount() );
	}

	void bsTriShape_saveLoadSave_data() { addBSTriShapeRows(); }

	//! saveLoadSave() for the BSTriShape scene of Skyrim SE and Fallout 4 (half floats, half / byte vectors, vertex desc)
	void bsTriShape_saveLoadSave()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QVERIFY2( !version.isEmpty(), "no profile with User Version 2 >= 100" );

		auto a = makeScene( version, userVersion, userVersion2 );
		QString refused;
		QVERIFY2( TestEnv::buildBSTriShapeScene( *a, &refused ), qPrintable( "the model refused: " + refused ) );

		bool ok = false;
		QByteArray first = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY2( a->getMessages().isEmpty(), "messages after save" );

		NifModel b;
		QVERIFY( loadBytes( b, first ) );
		QCOMPARE( b.getMessages().count(), 0 );
		QCOMPARE( blockTypes( b ), blockTypes( *a ) );

		QString d = TestEnv::diffModels( *a, b );
		QVERIFY2( d.isEmpty(), qPrintable( d ) );

		QByteArray second = saveBytes( b, &ok );
		QVERIFY( ok );
		QVERIFY2( second == first, qPrintable( "re-saving the loaded model changed the bytes: " + TestEnv::diffBytes( second, first ) ) );

		verifyBlockSizes( first, b.getBlockCount() );
	}

	void bsTriShape_contents_data() { addBSTriShapeRows(); }

	//! The values the BSTriShape scene was built with, read from the loaded model, and the vertex data as it lies in the
	//! file. The expected bytes were computed with python3 `struct`, not with NifSkope: `<e` for the half floats, `<f` for
	//! the floats, bytes for the rest. Vertex i (0..2) of the scene:
	//!   position (0.5*i, -1, 2), bitangent X 0.25*(i+1), UV (0.25*i, 1 - 0.25*i),
	//!   normal bytes  (255,128,0) (128,0,255) (0,255,128), bitangent Y 10+i,
	//!   tangent bytes (0,255,128) (255,128,0) (128,0,255), bitangent Z 20+i, colour bytes (255, 0, 51, 255 - 85*i),
	//!   struct.pack('<ee', ...) per half component; Fallout 4 stores position and bitangent X as halves (24 bytes per
	//!   vertex), Skyrim SE as floats (32 bytes). Followed by the triangle (0, 1, 2) as three u16.
	void bsTriShape_contents()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QVERIFY2( !version.isEmpty(), "no profile with User Version 2 >= 100" );

		const bool fo4 = userVersion2 == 130;

		auto a = makeScene( version, userVersion, userVersion2 );
		QString refused;
		QVERIFY2( TestEnv::buildBSTriShapeScene( *a, &refused ), qPrintable( "the model refused: " + refused ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		NifModel nif;
		QVERIFY( loadBytes( nif, bytes ) );
		QCOMPARE( nif.getMessages().count(), 0 );

		QCOMPARE( blockTypes( nif ), QStringList() << "NiNode" << "BSTriShape" << "BSLightingShaderProperty" << "BSShaderTextureSet"
			<< "NiAlphaProperty" << "NiStringsExtraData" );
		QCOMPARE( nif.getRootLinks(), QList<int>() << 0 );

		QModelIndex iRoot = nif.getBlock( 0 );
		QModelIndex iShape = nif.getBlock( 1 );
		QModelIndex iShader = nif.getBlock( 2 );
		QModelIndex iTextures = nif.getBlock( 3 );
		QModelIndex iAlpha = nif.getBlock( 4 );
		QModelIndex iStrings = nif.getBlock( 5 );

		// graph
		QCOMPARE( nif.getLinkArray( iRoot, "Children" ), QVector<qint32>() << 1 );
		QCOMPARE( nif.getLinkArray( iRoot, "Extra Data List" ), QVector<qint32>() << 5 );
		QCOMPARE( nif.getLink( iShape, "Shader Property" ), 2 );
		QCOMPARE( nif.getLink( iShape, "Alpha Property" ), 4 );
		QCOMPARE( nif.getLink( iShape, "Skin" ), -1 );
		QCOMPARE( nif.getLink( iShader, "Texture Set" ), 3 );

		// shape
		QVERIFY( nif.get<Vector3>( iShape, "Translation" ) == Vector3( -0.5f, 0.25f, 8.0f ) );
		Matrix rotation = nif.get<Matrix>( iShape, "Rotation" );
		for ( int r = 0; r < 3; r++ )
			for ( int c = 0; c < 3; c++ )
				QCOMPARE( rotation( r, c ), float( 3 * r + c + 1 ) );
		QCOMPARE( nif.get<float>( iShape, "Scale" ), 0.5f );
		QModelIndex iBound = nif.getIndex( iShape, "Bounding Sphere" );
		QVERIFY( nif.get<Vector3>( iBound, "Center" ) == Vector3( 0.5f, -1.0f, 2.0f ) );
		QCOMPARE( nif.get<float>( iBound, "Radius" ), 4.0f );

		// vertex layout: position, UV, normal, tangent, colour
		BSVertexDesc desc = nif.get<BSVertexDesc>( iShape, "Vertex Desc" );
		QVERIFY( desc.HasFlag( VF_VERTEX ) && desc.HasFlag( VF_UV ) && desc.HasFlag( VF_NORMAL ) && desc.HasFlag( VF_TANGENT ) && desc.HasFlag( VF_COLORS ) );
		QVERIFY( !desc.HasFlag( VF_SKINNED ) && !desc.HasFlag( VF_UV_2 ) && !desc.HasFlag( VF_EYEDATA ) );
		QCOMPARE( desc.GetVertexSize(), fo4 ? 24u : 32u );
		QCOMPARE( desc.GetAttributeOffset( VA_TEXCOORD0 ), fo4 ? 8u : 16u );
		QCOMPARE( desc.GetAttributeOffset( VA_NORMAL ), fo4 ? 12u : 20u );
		QCOMPARE( desc.GetAttributeOffset( VA_BINORMAL ), fo4 ? 16u : 24u );
		QCOMPARE( desc.GetAttributeOffset( VA_COLOR ), fo4 ? 20u : 28u );
		QCOMPARE( nif.get<int>( iShape, "Num Vertices" ), 3 );
		QCOMPARE( nif.get<int>( iShape, "Num Triangles" ), 1 );
		QCOMPARE( nif.get<int>( iShape, "Data Size" ), fo4 ? 3 * 24 + 6 : 3 * 32 + 6 );
		QCOMPARE( nif.getArray<Triangle>( iShape, "Triangles" ).count(), 1 );
		QVERIFY( nif.getArray<Triangle>( iShape, "Triangles" ).at( 0 ) == Triangle( 0, 1, 2 ) );

		// vertex data in the model: the types are the narrow ones, and the values are what was set
		QModelIndex iVertices = nif.getIndex( iShape, "Vertex Data" );
		QCOMPARE( nif.rowCount( iVertices ), 3 );

		// the row has every attribute of the description and nothing else (no Unknown Int / Short, no skinning, no eye data)
		const BaseModel & base = nif;   // (NifModel::evalCondition( NifItem * ) hides the QModelIndex overload)
		QStringList active;
		for ( int k = 0; k < nif.rowCount( nif.index( 0, 0, iVertices ) ); k++ ) {
			QModelIndex child = nif.index( k, 0, nif.index( 0, 0, iVertices ) );
			if ( base.evalCondition( child ) )
				active << nif.itemName( child );
		}
		QCOMPARE( active, QStringList() << "Vertex" << "Bitangent X" << "UV" << "Normal" << "Bitangent Y" << "Tangent" << "Bitangent Z" << "Vertex Colors" );

		static const int normalBytes[3][3] = { { 255, 128, 0 }, { 128, 0, 255 }, { 0, 255, 128 } };
		static const int tangentBytes[3][3] = { { 0, 255, 128 }, { 255, 128, 0 }, { 128, 0, 255 } };

		for ( int i = 0; i < 3; i++ ) {
			QModelIndex iV = nif.index( i, 0, iVertices );
			QString row = QString( "vertex %1" ).arg( i );

			QCOMPARE( nif.getValue( nif.getIndex( iV, "Vertex" ) ).type(), fo4 ? NifValue::tHalfVector3 : NifValue::tVector3 );
			QCOMPARE( nif.getValue( nif.getIndex( iV, "Bitangent X" ) ).type(), fo4 ? NifValue::tHfloat : NifValue::tFloat );
			QCOMPARE( nif.getValue( nif.getIndex( iV, "UV" ) ).type(), NifValue::tHalfVector2 );
			QCOMPARE( nif.getValue( nif.getIndex( iV, "Normal" ) ).type(), NifValue::tByteVector3 );
			QCOMPARE( nif.getValue( nif.getIndex( iV, "Vertex Colors" ) ).type(), NifValue::tByteColor4 );

			Vector3 position = nif.get<Vector3>( iV, "Vertex" );
			QVERIFY2( position == Vector3( 0.5f * i, -1.0f, 2.0f ), qPrintable( row + " position" ) );
			QCOMPARE( nif.get<float>( iV, "Bitangent X" ), 0.25f * ( i + 1 ) );

			HalfVector2 uv = nif.get<HalfVector2>( iV, "UV" );
			QCOMPARE( uv[0], 0.25f * i );
			QCOMPARE( uv[1], 1.0f - 0.25f * i );

			ByteVector3 normal = nif.get<ByteVector3>( iV, "Normal" );
			ByteVector3 tangent = nif.get<ByteVector3>( iV, "Tangent" );
			for ( int k = 0; k < 3; k++ ) {
				// a byte b is read as b / 255 * 2 - 1
				QVERIFY2( qAbs( normal[k] - ( normalBytes[i][k] / 255.0f * 2.0f - 1.0f ) ) < 1e-6f, qPrintable( row + " normal" ) );
				QVERIFY2( qAbs( tangent[k] - ( tangentBytes[i][k] / 255.0f * 2.0f - 1.0f ) ) < 1e-6f, qPrintable( row + " tangent" ) );
			}
			QCOMPARE( nif.get<int>( iV, "Bitangent Y" ), 10 + i );
			QCOMPARE( nif.get<int>( iV, "Bitangent Z" ), 20 + i );

			ByteColor4 color = nif.get<ByteColor4>( iV, "Vertex Colors" );
			QVERIFY2( qAbs( color[0] - 1.0f ) < 1e-6f && qAbs( color[1] ) < 1e-6f && qAbs( color[2] - 51.0f / 255.0f ) < 1e-6f
				&& qAbs( color[3] - ( 255 - 85 * i ) / 255.0f ) < 1e-6f, qPrintable( row + " colour" ) );
		}

		// the file: vertex description, Num Triangles (u32 on Fallout 4, u16 on Skyrim SE), Num Vertices, Data Size, the three
		// vertices and the triangle, one contiguous run as python wrote it. nif.xml names the five description bytes VF1..VF5
		// without saying what they hold; they are the games' vertex descriptor: VF1 = size in dwords | position offset << 4,
		// VF2 = UV 0 | UV 1 offset << 4, VF3 = normal | tangent << 4, VF4 = colour | skinning << 4, VF5 = land | eye data << 4
		// (offsets in dwords), then the attribute flags << 4 as u16 and a zero byte. SSE: size 32, UV 16, normal 20, tangent
		// 24, colour 28 bytes; Fallout 4: size 24, UV 8, normal 12, tangent 16, colour 20.
		QByteArray section = QByteArray::fromHex( fo4
			? "06 02 43 05 00 b0 03 00  01 00 00 00  03 00  4e 00 00 00"
			: "08 04 65 07 00 b0 03 00  01 00  03 00  66 00 00 00" );
		section += QByteArray::fromHex( fo4
			? "00 00 00 bc 00 40  00 34  00 00 00 3c  ff 80 00 0a  00 ff 80 14  ff 00 33 ff"    // vertex 0
			  "00 38 00 bc 00 40  00 38  00 34 00 3a  80 00 ff 0b  ff 80 00 15  ff 00 33 aa"    // vertex 1
			  "00 3c 00 bc 00 40  00 3a  00 38 00 38  00 ff 80 0c  80 00 ff 16  ff 00 33 55"    // vertex 2
			: "00 00 00 00 00 00 80 bf 00 00 00 40  00 00 80 3e  00 00 00 3c  ff 80 00 0a  00 ff 80 14  ff 00 33 ff"
			  "00 00 00 3f 00 00 80 bf 00 00 00 40  00 00 00 3f  00 34 00 3a  80 00 ff 0b  ff 80 00 15  ff 00 33 aa"
			  "00 00 80 3f 00 00 80 bf 00 00 00 40  00 00 40 3f  00 38 00 38  00 ff 80 0c  80 00 ff 16  ff 00 33 55" );
		section += QByteArray::fromHex( "00 00 01 00 02 00" );   // Triangles
		QCOMPARE( section.size(), ( fo4 ? 8 + 4 + 2 + 4 : 8 + 2 + 2 + 4 ) + ( fo4 ? 3 * 24 : 3 * 32 ) + 6 );
		QVERIFY2( bytes.contains( section ), "the vertex description, counts, vertex data and triangles are not in the file as expected" );

		// shader, textures, alpha, strings
		QCOMPARE( nif.get<QString>( iShader, "Name" ), QString( "BS Shader" ) );
		QCOMPARE( nif.get<int>( iShader, "Skyrim Shader Type" ), 5 );
		QVERIFY( nif.get<Vector2>( iShader, "UV Offset" ) == Vector2( 0.125f, 0.25f ) );
		QVERIFY( nif.get<Vector2>( iShader, "UV Scale" ) == Vector2( 2.0f, 0.5f ) );
		QVERIFY( nif.get<Color3>( iShader, "Emissive Color" ) == Color3( 0.5f, 0.25f, 0.125f ) );
		QCOMPARE( nif.get<float>( iShader, "Emissive Multiple" ), 1.5f );
		QCOMPARE( nif.get<float>( iShader, "Alpha" ), 0.75f );
		QVERIFY( nif.get<Color3>( iShader, "Specular Color" ) == Color3( 1.0f, 0.5f, 0.25f ) );
		QCOMPARE( nif.get<float>( iShader, "Specular Strength" ), 2.0f );
		if ( fo4 ) {
			QCOMPARE( nif.get<float>( iShader, "Smoothness" ), 0.5f );
			QCOMPARE( nif.get<QString>( iShader, "Wet Material" ), QString( "materials\\wet\\rain.bgsm" ) );
		} else {
			QCOMPARE( nif.get<float>( iShader, "Glossiness" ), 33.0f );
		}

		QCOMPARE( nif.get<int>( iTextures, "Num Textures" ), 4 );
		QCOMPARE( nif.getArray<QString>( iTextures, "Textures" ), QVector<QString>()
			<< "textures\\bs\\body_d.dds" << "textures\\bs\\body_n.dds" << "" << "textures\\bs\\body_s.dds" );

		QCOMPARE( nif.get<QString>( iAlpha, "Name" ), QString( "BS Alpha" ) );
		QCOMPARE( nif.get<int>( iAlpha, "Flags" ), 0x01ED );
		QCOMPARE( nif.get<int>( iAlpha, "Threshold" ), 100 );

		QCOMPARE( nif.getArray<QString>( iStrings, "Data" ).count(), 6 );
		QCOMPARE( nif.getArray<QString>( iStrings, "Data" ).at( 3 ), QString( "Spaces, punctuation: (x) [y] {z}; 100% #1 'q' \"d\"" ) );

		QStringList want;
		want << "BS Root" << "BS Shape" << "BS Shader" << "BS Alpha" << "Strings";
		if ( fo4 )
			want << "materials\\wet\\rain.bgsm";

		QStringList have;
		for ( const QString & s : nif.getArray<QString>( nif.getHeader(), "Strings" ) )
			have << s;
		have.sort();
		want.sort();
		QCOMPARE( have, want );   // each string once
	}

	//! The golden Morrowind file: saved bytes == derived bytes, the derived bytes load into the scene, and saving that
	//! model again gives the same bytes
	void golden_morrowind()
	{
		const QByteArray golden = goldenMorrowind();
		QCOMPARE( golden.size(), 275 );

		auto a = makeScene( "4.0.0.2", 0, 0 );
		QVERIFY( buildGoldenScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY2( bytes == golden, qPrintable( "saved model: " + TestEnv::diffBytes( bytes, golden ) ) );

		NifModel b;
		QVERIFY( loadBytes( b, golden ) );
		QCOMPARE( b.getMessages().count(), 0 );
		verifyGoldenScene( b, 0x04000002, 0 );
		if ( QTest::currentTestFailed() )
			return;

		QByteArray again = saveBytes( b, &ok );
		QVERIFY( ok );
		QVERIFY2( again == golden, qPrintable( "loaded golden saved again: " + TestEnv::diffBytes( again, golden ) ) );
	}

	//! The golden Skyrim LE file (see goldenSkyrimLE()); also pins the Export Info ShortStrings, the block type table, the
	//! string table and the per block sizes
	void golden_skyrimLE()
	{
		const QByteArray golden = goldenSkyrimLE();
		QCOMPARE( golden.size(), 287 );

		auto a = makeScene( "20.2.0.7", 12, 83 );
		QVERIFY( buildGoldenScene( *a ) );
		QVERIFY( a->set<QString>( a->getIndex( a->getHeader(), "Export Info" ), "Author", "Me" ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY2( bytes == golden, qPrintable( "saved model: " + TestEnv::diffBytes( bytes, golden ) ) );

		NifModel b;
		QVERIFY( loadBytes( b, golden ) );
		QCOMPARE( b.getMessages().count(), 0 );
		verifyGoldenScene( b, 0x14020007, 14 );
		if ( QTest::currentTestFailed() )
			return;

		QCOMPARE( (int)b.getUserVersion(), 12 );
		QCOMPARE( (int)b.getUserVersion2(), 83 );

		QModelIndex iHeader = b.getHeader();
		QModelIndex iExport = b.getIndex( iHeader, "Export Info" );
		// (get<QString>() answers "" for a ShortString from 20.1.0.3, so read the value itself)
		QCOMPARE( b.getValue( b.getIndex( iExport, "Author" ) ).get<QString>(), QString( "Me" ) );
		QCOMPARE( b.getValue( b.getIndex( iExport, "Process Script" ) ).get<QString>(), QString( "" ) );
		QCOMPARE( b.getValue( b.getIndex( iExport, "Export Script" ) ).get<QString>(), QString( "" ) );

		QCOMPARE( b.get<int>( iHeader, "Num Blocks" ), 2 );
		QCOMPARE( b.getArray<QString>( iHeader, "Block Types" ), QVector<QString>() << "NiNode" );
		QCOMPARE( b.getArray<int>( iHeader, "Block Type Index" ), QVector<int>() << 0 << 0 );
		QCOMPARE( b.getArray<int>( iHeader, "Block Size" ), QVector<int>() << 84 << 80 );
		QCOMPARE( b.get<int>( iHeader, "Num Strings" ), 2 );
		QCOMPARE( b.get<int>( iHeader, "Max String Length" ), 4 );
		QCOMPARE( b.getArray<QString>( iHeader, "Strings" ), QVector<QString>() << "Root" << "Kid" );
		QCOMPARE( b.get<int>( iHeader, "Num Groups" ), 0 );
		QCOMPARE( b.getLink( b.getBlock( 1 ), "Collision Object" ), -1 );
		QCOMPARE( b.getLink( b.getBlock( 1 ), "Controller" ), -1 );
		QCOMPARE( b.get<int>( b.getBlock( 1 ), "Num Extra Data List" ), 0 );

		QByteArray again = saveBytes( b, &ok );
		QVERIFY( ok );
		QVERIFY2( again == golden, qPrintable( "loaded golden saved again: " + TestEnv::diffBytes( again, golden ) ) );
	}

	//! The golden NetImmerse 3.1 file (see goldenNetImmerse31())
	void golden_ni31()
	{
		const QByteArray golden = goldenNetImmerse31();
		QCOMPARE( golden.size(), 327 );

		auto a = makeScene( "3.1", 0, 0 );
		QVERIFY( buildGoldenScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY2( bytes == golden, qPrintable( "saved model: " + TestEnv::diffBytes( bytes, golden ) ) );

		NifModel b;
		QVERIFY( loadBytes( b, golden ) );
		QCOMPARE( b.getMessages().count(), 0 );
		verifyGoldenScene( b, 0x03010000, 0 );
		if ( QTest::currentTestFailed() )
			return;

		QCOMPARE( b.getArray<QString>( b.getHeader(), "Copyright" ), QVector<QString>() << "line one" << "line two" << "line three" );

		QByteArray again = saveBytes( b, &ok );
		QVERIFY( ok );
		QVERIFY2( again == golden, qPrintable( "loaded golden saved again: " + TestEnv::diffBytes( again, golden ) ) );
	}

	//! The golden NetImmerse 10.1.0.0 file (see goldenNetImmerse10_1()): the one place the 4 byte block separator of
	//! 10.0.1.0 - 10.1.x is pinned (a save -> load -> save test cannot see it, both directions change together)
	void golden_ni10_1()
	{
		const QByteArray golden = goldenNetImmerse10_1();
		QCOMPARE( golden.size(), 262 );

		auto a = makeScene( "10.1.0.0", 0, 0 );
		QVERIFY( buildGoldenScene( *a ) );
		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY2( bytes == golden, qPrintable( "saved model: " + TestEnv::diffBytes( bytes, golden ) ) );

		NifModel b;
		QVERIFY( loadBytes( b, golden ) );
		QCOMPARE( b.getMessages().count(), 0 );   // a separator that is not zero would be reported here
		verifyGoldenScene( b, 0x0A010000, 0 );
		if ( QTest::currentTestFailed() )
			return;

		QModelIndex iHeader = b.getHeader();
		QCOMPARE( b.get<int>( iHeader, "Num Blocks" ), 2 );
		QCOMPARE( b.getArray<QString>( iHeader, "Block Types" ), QVector<QString>() << "NiNode" );
		QCOMPARE( b.getArray<int>( iHeader, "Block Type Index" ), QVector<int>() << 0 << 0 );
		QCOMPARE( b.get<int>( iHeader, "Num Groups" ), 0 );
		QCOMPARE( b.getLink( b.getBlock( 1 ), "Collision Object" ), -1 );

		QByteArray again = saveBytes( b, &ok );
		QVERIFY( ok );
		QVERIFY2( again == golden, qPrintable( "loaded golden saved again: " + TestEnv::diffBytes( again, golden ) ) );
	}

	void havokBlock_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<bool>( "separator" );

		QTest::newRow( "10.0.1.0" ) << "10.0.1.0" << true;
		QTest::newRow( "10.1.0.0" ) << "10.1.0.0" << true;
		QTest::newRow( "10.2.0.0 (no separator)" ) << "10.2.0.0" << false;
	}

	//! A bhk block (Havok) in a file that stores the 4 byte zero separator in front of every block (10.0.1.0 up to 10.1.x).
	//! FINDING: NifModel::save() writes the separator before every block (nifmodel.cpp:2041) but NifModel::load() does not
	//! read one in front of "bhk" blocks (nifmodel.cpp:1839, a workaround for Oblivion files that lack it), so everything
	//! from that block on is read four bytes early: with blocks after it the load fails, with none (as here) it ends with
	//! the message "Array Roots invalid" and wrong field values. Remove the QEXPECT_FAIL when save and load agree;
	//! 10.2.0.0 and later are not affected.
	void havokBlock()
	{
		QFETCH( QString, version );
		QFETCH( bool, separator );

		auto a = makeScene( version, 0, 0 );
		QModelIndex iRoot = a->insertNiBlock( "NiNode" );
		QModelIndex iHavok = a->insertNiBlock( "bhkCollisionObject" );
		QVERIFY( iRoot.isValid() && iHavok.isValid() );
		QVERIFY( a->setLink( iRoot, "Collision Object", a->getBlockNumber( iHavok ) ) );

		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );

		NifModel b;
		QString problem;
		QStringList messages;
		bool loaded = loadBytes( b, bytes );
		for ( const TestMessage & m : b.getMessages() )   // (getMessages() hands them out once)
			messages << QString( m );

		if ( !loaded )
			problem = "the file does not load: " + messages.join( "; " );
		else if ( !messages.isEmpty() )
			problem = "messages while loading: " + messages.join( "; " );
		else if ( blockTypes( b ) != ( QStringList() << "NiNode" << "bhkCollisionObject" ) )
			problem = "block types " + blockTypes( b ).join( "," );
		else
			problem = TestEnv::diffModels( *a, b );

		if ( separator )
			QEXPECT_FAIL( "", "save() writes a block separator before bhk blocks that load() does not expect (nifmodel.cpp:2041 vs 1839)", Abort );
		QVERIFY2( problem.isEmpty(), qPrintable( problem ) );
	}

	void golden_headerPrefix_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );
		QTest::addColumn<QByteArray>( "headerLine" );
		QTest::addColumn<QByteArray>( "fields" );

		// Per row: python3 over the fields nif.xml's Header compound lists in front of the tables: Header String (text and a
		// line feed: "NetImmerse File Format, Version" up to 10.0.1.2, "Gamebryo File Format, Version" after), Version (u32,
		// from 3.1.0.1), Endian Type (u8, 1 = little endian, from 20.0.0.3), User Version (u32, from 10.0.1.8), Num Blocks
		// (u32, from 3.1.0.1, here 2) and User Version 2 (u32, only for 20.2.0.7 and 20.0.0.5 when User Version >= 3).
		// e.g. Fallout 3:  python3 -c "import struct; print((struct.pack('<IBIII', 0x14020007, 1, 11, 2, 34)).hex(' '))"
		QTest::newRow( "Morrowind 4.0.0.2" ) << "4.0.0.2" << 0 << 0
			<< QByteArray( "NetImmerse File Format, Version 4.0.0.2" ) << QByteArray( "02 00 00 04  02 00 00 00" );
		QTest::newRow( "Civ IV 4.2.2.0" ) << "4.2.2.0" << 0 << 0
			<< QByteArray( "NetImmerse File Format, Version 4.2.2.0" ) << QByteArray( "00 02 02 04  02 00 00 00" );
		QTest::newRow( "10.0.1.0 (no user version yet)" ) << "10.0.1.0" << 0 << 0
			<< QByteArray( "NetImmerse File Format, Version 10.0.1.0" ) << QByteArray( "00 01 00 0a  02 00 00 00" );
		QTest::newRow( "10.2.0.0 (user version, no endian byte)" ) << "10.2.0.0" << 0 << 0
			<< QByteArray( "Gamebryo File Format, Version 10.2.0.0" ) << QByteArray( "00 00 02 0a  00 00 00 00  02 00 00 00" );
		QTest::newRow( "Oblivion 20.0.0.5" ) << "20.0.0.5" << 11 << 11
			<< QByteArray( "Gamebryo File Format, Version 20.0.0.5" ) << QByteArray( "05 00 00 14  01  0b 00 00 00  02 00 00 00  0b 00 00 00" );
		QTest::newRow( "20.1.0.3 (no user version 2)" ) << "20.1.0.3" << 0 << 0
			<< QByteArray( "Gamebryo File Format, Version 20.1.0.3" ) << QByteArray( "03 00 01 14  01  00 00 00 00  02 00 00 00" );
		QTest::newRow( "Fallout 3 20.2.0.7" ) << "20.2.0.7" << 11 << 34
			<< QByteArray( "Gamebryo File Format, Version 20.2.0.7" ) << QByteArray( "07 00 02 14  01  0b 00 00 00  02 00 00 00  22 00 00 00" );
		QTest::newRow( "Skyrim LE 20.2.0.7" ) << "20.2.0.7" << 12 << 83
			<< QByteArray( "Gamebryo File Format, Version 20.2.0.7" ) << QByteArray( "07 00 02 14  01  0c 00 00 00  02 00 00 00  53 00 00 00" );
		QTest::newRow( "Skyrim SE 20.2.0.7" ) << "20.2.0.7" << 12 << 100
			<< QByteArray( "Gamebryo File Format, Version 20.2.0.7" ) << QByteArray( "07 00 02 14  01  0c 00 00 00  02 00 00 00  64 00 00 00" );
		QTest::newRow( "Fallout 4 20.2.0.7" ) << "20.2.0.7" << 12 << 130
			<< QByteArray( "Gamebryo File Format, Version 20.2.0.7" ) << QByteArray( "07 00 02 14  01  0c 00 00 00  02 00 00 00  82 00 00 00" );
	}

	//! The first bytes of the file for every profile from 4.0.0.2 up: the version line and the binary version fields
	//! (3.1 has none of them: golden_ni31 pins its header). Each profile is only a different set of numbers in the same
	//! few fields, which is where a wrong version cut-off would show.
	void golden_headerPrefix()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QFETCH( QByteArray, headerLine );
		QFETCH( QByteArray, fields );

		QByteArray prefix = headerLine + "\n" + QByteArray::fromHex( fields );

		auto a = makeScene( version, userVersion, userVersion2 );
		QModelIndex iRoot = a->insertNiBlock( "NiNode" );
		QModelIndex iKid = a->insertNiBlock( "NiNode" );
		QVERIFY( iRoot.isValid() && iKid.isValid() );

		bool ok = false;
		QByteArray bytes = saveBytes( *a, &ok );
		QVERIFY( ok );
		QVERIFY( bytes.size() > prefix.size() );
		QByteArray head = bytes.left( prefix.size() );
		QVERIFY2( head == prefix, qPrintable( TestEnv::diffBytes( head, prefix ) ) );

		// and the bytes mean what they say when read back
		NifModel b;
		QVERIFY( loadBytes( b, bytes ) );
		QCOMPARE( b.getVersion(), version );
		QCOMPARE( b.getBlockCount(), 2 );
		QCOMPARE( (int)b.getUserVersion2(), userVersion2 );
	}
};

REGISTER_TEST( tst_NifRoundTrip )


namespace {

//! Header versions where the layout of a value changes (see init() in nifstream.cpp). All three streams use them.
const quint32 kBool32Last = 0x04000002;       //!< last version with 32-bit bools (4.0.0.2)
const quint32 kLinksZeroBased = 0x0303000D;   //!< first version that stores Ref/Ptr as they are; older files store index + 1 (3.3.0.13)
const quint32 kStringIndexFrom = 0x14010003;  //!< first version where string/FilePath are indices into the header's string table (20.1.0.3)


/*! A device that keeps what is written to it and makes one of the writes fail. The writes are counted from 0; fault says how the
 *  failing one goes wrong: Refuse reports an error (-1), Short takes one byte less than it was given.
 */
class FaultyWriteDevice final : public QIODevice
{
public:
	enum Fault { Refuse, Short };

	int failAt = -1;             //!< the number of the write that fails, -1: all of them work
	Fault fault = Refuse;
	int calls = 0;               //!< writes so far
	QByteArray written;

	FaultyWriteDevice() { open( QIODevice::WriteOnly ); }

	bool isSequential() const override { return true; }

protected:
	qint64 readData( char *, qint64 ) override { return -1; }

	qint64 writeData( const char * data, qint64 size ) override
	{
		int call = calls++;
		if ( call != failAt ) {
			written.append( data, int( size ) );
			return size;
		}

		if ( fault == Short && size > 0 ) {
			written.append( data, int( size - 1 ) );
			return size - 1;
		}

		return -1;
	}
};


/*! A device that hands out its bytes in the pieces it was given. A read never goes beyond the piece it is in, so a read of n
 *  bytes comes back with fewer when a piece ends inside it, though more bytes follow (what a socket does). A file or a buffer
 *  never does that: a short read there is the end of the data.
 */
class ChunkedReadDevice final : public QIODevice
{
public:
	QList<QByteArray> chunks;

	ChunkedReadDevice() { open( QIODevice::ReadOnly ); }

	bool isSequential() const override { return true; }

protected:
	qint64 readData( char * data, qint64 size ) override
	{
		if ( chunks.isEmpty() )
			return -1;

		qint64 n = qMin<qint64>( size, chunks.first().size() );
		memcpy( data, chunks.first().constData(), size_t( n ) );
		chunks.first().remove( 0, int( n ) );
		if ( chunks.first().isEmpty() )
			chunks.removeFirst();

		return n;
	}

	qint64 writeData( const char *, qint64 ) override { return -1; }
};


/*! A NifValue with its type and its data set by hand (both are protected members of NifValue): the combinations that the
 *  NifValue functions refuse to build. A tString that holds a number below 0x10000 as a string index does, a tBlob or a
 *  tFilePath whose data pointer is null does (NifValue( tFilePath ) does not allocate a QString, and does not set its pointer),
 *  a tFilePath with text does. The data of a RawValue is its own: NifValue does not free it.
 */
class RawValue final : public NifValue
{
public:
	//! A value of the type that holds the number, and no data pointer
	RawValue( Type t, quint32 number )
	{
		typ = t;
		val.data = nullptr;
		val.u32 = number;
	}

	//! A value of the type that holds the text
	RawValue( Type t, const QString & text )
	{
		typ = t;
		val.data = new QString( text );
		ownsText = true;
	}

	~RawValue()
	{
		if ( ownsText )
			delete static_cast<QString *>( val.data );

		typ = tNone;
	}

private:
	bool ownsText = false;
};


/*! Makes the local 8-bit codec of QString (toLocal8Bit(), fromLocal8Bit()) a codec of the test's choosing, and puts the
 *  old one back at the end of the scope. What the local codec is depends on the machine (UTF-8, or the ANSI code page of Windows),
 *  so a test of a function that uses it picks one.
 */
class LocalCodecScope
{
public:
	explicit LocalCodecScope( const char * name ) : previous( QTextCodec::codecForLocale() ) { QTextCodec::setCodecForLocale( QTextCodec::codecForName( name ) ); }
	~LocalCodecScope() { QTextCodec::setCodecForLocale( previous ); }

private:
	QTextCodec * previous;
};

}


/*! The three streams on single values: NifOStream writes, NifSStream measures, NifIStream reads, for every NifValue
 *  type, in header versions on both sides of each layout change. No scene and no file, so a mistake shows up as the
 *  one value and the one byte it concerns.
 *
 *  The expected bytes in the sample table were computed with python3 `struct` (the expression is in the comment on
 *  each row, e.g. `python3 -c "import struct; print(struct.pack('<f', 1.5).hex(' '))"`), not with NifSkope, so a mistake
 *  made the same way in the reader and the writer is caught too.
 */
class tst_NifStream final : public QObject
{
	Q_OBJECT

	/*! One value, the bytes it is stored as, and what reading those bytes gives back. The bytes are the same in every
	 *  version in [from(), to()], rows for the other versions say what changes. */
	struct Sample
	{
		const char * name;       //!< data row name
		NifValue value;          //!< what is written
		const char * hex;        //!< the bytes it is written as
		QByteArray bytes;        //!< hex, decoded
		NifValue backValue;      //!< what reading gives back, tNone: the value itself
		NifValue targetValue;    //!< what is read into, tNone: a default value of the type
		QByteArray trailerBytes; //!< follows the bytes in the reader's input, but is not part of the value
		quint32 fromVersion = 0;
		quint32 toVersion = 0xffffffff;
		bool canWrite = true;

		Sample( const char * n, const NifValue & v, const char * h ) : name( n ), value( v ), hex( h ), bytes( QByteArray::fromHex( QByteArray( h ) ) ) {}

		Sample & back( const NifValue & b ) { backValue = b; return *this; }
		Sample & target( const NifValue & t ) { targetValue = t; return *this; }
		Sample & trailer( const char * h ) { trailerBytes = QByteArray::fromHex( QByteArray( h ) ); return *this; }
		Sample & from( quint32 v ) { fromVersion = v; return *this; }
		Sample & to( quint32 v ) { toVersion = v; return *this; }
		//! No value to write (NifValue( tFilePath ) holds no text), the row only reads
		Sample & readOnly() { canWrite = false; return *this; }

		bool appliesTo( quint32 version ) const { return version >= fromVersion && version <= toVersion; }
		const NifValue & expected() const { return backValue.type() != NifValue::tNone ? backValue : value; }
		NifValue readTarget() const { return targetValue.type() != NifValue::tNone ? targetValue : NifValue( value.type() ); }

		//! What the reader is given: the bytes of the value, then what follows them in a file. A row that says nothing about
		//! that gets one byte that is no part of any value, so a reader that takes more than its value shows (the read would
		//! end at the end of the input otherwise, and look right)
		QByteArray input() const { return bytes + followingBytes(); }
		QByteArray followingBytes() const { return trailerBytes.isEmpty() ? QByteArray( 1, char( 0xa5 ) ) : trailerBytes; }
	};

	// ---- building values

	//! The type a data row names by number (NifValue v( NifValue::Type( type ) ) would declare a function)
	static NifValue::Type typeOf( int type ) { return NifValue::Type( type ); }

	static float fromBits( quint32 bits )
	{
		float f;
		memcpy( &f, &bits, sizeof( f ) );
		return f;
	}

	static quint32 toBits( float f )
	{
		quint32 bits;
		memcpy( &bits, &f, sizeof( bits ) );
		return bits;
	}

	static NifValue vCount( NifValue::Type t, quint32 n ) { return TestEnv::countValue( t, n ); }

	static NifValue vInt( int n ) { return vCount( NifValue::tInt, quint32( n ) ); }

	//! The 16 bit signed type, set the way the model sets it
	static NifValue vShort( qint16 n )
	{
		NifValue v( NifValue::tShort );
		if ( !v.set<qint16>( n ) )
			qFatal( "sample table: set<qint16> failed" );
		return v;
	}

	static NifValue vLink( NifValue::Type t, int link ) { return TestEnv::linkValue( t, link ); }

	static NifValue vFloat( NifValue::Type t, float f ) { return TestEnv::floatValue( t, f ); }

	static NifValue vFileVersion( quint32 version )
	{
		NifValue v( NifValue::tFileVersion );
		v.setFileVersion( version );
		return v;
	}

	//! A value of a class type: Vector3, Quat, Triangle, QString ...
	template <typename X> static NifValue vOf( NifValue::Type t, const X & x ) { return TestEnv::valueOf<X>( t, x ); }

	static NifValue vString( NifValue::Type t, const char * text ) { return vOf<QString>( t, QString::fromLatin1( text ) ); }

	static NifValue vBytes( NifValue::Type t, const char * hex ) { return vOf<QByteArray>( t, QByteArray::fromHex( QByteArray( hex ) ) ); }

	//! Matrix33 from nine floats, row after row
	static NifValue vMatrix( std::initializer_list<float> rowMajor )
	{
		Matrix m;
		unsigned i = 0;
		for ( float x : rowMajor ) {
			m( i / 3, i % 3 ) = x;
			i++;
		}
		return vOf<Matrix>( NifValue::tMatrix, m );
	}

	//! Matrix44 from sixteen floats, row after row
	static NifValue vMatrix4( std::initializer_list<float> rowMajor )
	{
		Matrix4 m;
		unsigned i = 0;
		for ( float x : rowMajor ) {
			m( i / 4, i % 4 ) = x;
			i++;
		}
		return vOf<Matrix4>( NifValue::tMatrix4, m );
	}

	static NifValue vByteColor( float r, float g, float b, float a )
	{
		ByteColor4 c;
		c.setRGBA( r, g, b, a );
		return vOf<ByteColor4>( NifValue::tByteColor4, c );
	}

	static NifValue vByteMatrix( int rows, int columns, const char * hex )
	{
		NifValue v( NifValue::tByteMatrix );
		QByteArray data = QByteArray::fromHex( QByteArray( hex ) );
		ByteMatrix * m = v.get<ByteMatrix *>();
		*m = ByteMatrix( rows, columns );
		for ( int i = 0; i < rows; i++ ) {
			for ( int j = 0; j < columns; j++ )
				( *m )( i, j ) = data.at( i * columns + j );
		}
		return v;
	}

	//! BSVertexDesc with the VertexFlags set, the vertex size and the offsets of the UV, normal and colour attributes in bytes
	static NifValue vDesc( int flags, uint size, uint uvOffset, uint normalOffset, uint colorOffset )
	{
		BSVertexDesc d;
		if ( flags )
			d.SetFlag( VertexFlags( flags ) );
		d.SetSize( size );
		if ( uvOffset )
			d.SetAttributeOffset( VA_TEXCOORD0, uvOffset );
		if ( normalOffset )
			d.SetAttributeOffset( VA_NORMAL, normalOffset );
		if ( colorOffset )
			d.SetAttributeOffset( VA_COLOR, colorOffset );
		return vOf<BSVertexDesc>( NifValue::tBSVertexDesc, d );
	}

	// ---- describing values

	//! Text that tells two values apart down to the bit: floats with their bit pattern, strings as code points
	static QString f( float x )
	{
		return QString( "%1[%2]" ).arg( double( x ), 0, 'g', 9 ).arg( toBits( x ), 8, 16, QLatin1Char( '0' ) );
	}

	static QString hex32( quint32 n ) { return QString( "%1" ).arg( n, 0, 16 ); }

	static QString show( const QString & s )
	{
		QString out = QStringLiteral( "\"" );
		for ( QChar c : s ) {
			if ( c.unicode() >= 0x20 && c.unicode() < 0x7f )
				out += c;
			else
				out += QString( "\\u%1" ).arg( c.unicode(), 4, 16, QLatin1Char( '0' ) );
		}
		return out + QStringLiteral( "\"" );
	}

	static QString hex( const QByteArray & b )
	{
		// QCOMPARE and qPrintable would flood the log with a 32 KiB string
		QByteArray shown = b.size() > 64 ? b.left( 64 ) : b;
		return QString::fromLatin1( shown.toHex( ' ' ) ) + ( b.size() > 64 ? QString( " ... (%1 bytes)" ).arg( b.size() ) : QString( " (%1 bytes)" ).arg( b.size() ) );
	}

	static QString describe( const NifValue & v )
	{
		typedef NifValue T;
		QString s = QString( "type %1: " ).arg( int( v.type() ) );

		switch ( v.type() ) {
		case T::tByte:
			return s + hex32( v.toCount() & 0xff );
		case T::tWord:
		case T::tFlags:
		case T::tBlockTypeIndex:
		case T::tShort:
			return s + hex32( v.toCount() & 0xffff );
		case T::tBool:
		case T::tStringOffset:
		case T::tStringIndex:
		case T::tInt:
		case T::tULittle32:
		case T::tUInt:
			return s + hex32( v.toCount() );
		case T::tLink:
		case T::tUpLink:
			return s + QString::number( v.toLink() );
		case T::tFloat:
		case T::tHfloat:
			return s + f( v.toFloat() );
		case T::tFileVersion:
			return s + hex32( v.toFileVersion() );
		case T::tSizedString:
		case T::tText:
		case T::tShortString:
		case T::tHeaderString:
		case T::tLineString:
		case T::tChar8String:
		case T::tString:
			return s + show( v.get<QString>() );
		case T::tVector2:
		case T::tHalfVector2:
			{
				Vector2 x = v.get<Vector2>();
				return s + f( x[0] ) + " " + f( x[1] );
			}
		case T::tVector3:
		case T::tHalfVector3:
			{
				Vector3 x = v.get<Vector3>();
				return s + f( x[0] ) + " " + f( x[1] ) + " " + f( x[2] );
			}
		case T::tByteVector3:
			{
				ByteVector3 x = v.get<ByteVector3>();
				return s + f( x[0] ) + " " + f( x[1] ) + " " + f( x[2] );
			}
		case T::tVector4:
			{
				Vector4 x = v.get<Vector4>();
				return s + f( x[0] ) + " " + f( x[1] ) + " " + f( x[2] ) + " " + f( x[3] );
			}
		case T::tColor3:
			{
				Color3 x = v.get<Color3>();
				return s + f( x[0] ) + " " + f( x[1] ) + " " + f( x[2] );
			}
		case T::tColor4:
			{
				Color4 x = v.get<Color4>();
				return s + f( x[0] ) + " " + f( x[1] ) + " " + f( x[2] ) + " " + f( x[3] );
			}
		case T::tByteColor4:
			{
				ByteColor4 x = v.get<ByteColor4>();
				return s + f( x[0] ) + " " + f( x[1] ) + " " + f( x[2] ) + " " + f( x[3] );
			}
		case T::tQuat:
		case T::tQuatXYZW:
			{
				Quat x = v.get<Quat>();
				return s + "w " + f( x[0] ) + " x " + f( x[1] ) + " y " + f( x[2] ) + " z " + f( x[3] );
			}
		case T::tMatrix:
			{
				Matrix m = v.get<Matrix>();
				for ( unsigned r = 0; r < 3; r++ ) {
					for ( unsigned c = 0; c < 3; c++ )
						s += f( m( r, c ) ) + " ";
				}
				return s;
			}
		case T::tMatrix4:
			{
				Matrix4 m = v.get<Matrix4>();
				for ( unsigned r = 0; r < 4; r++ ) {
					for ( unsigned c = 0; c < 4; c++ )
						s += f( m( r, c ) ) + " ";
				}
				return s;
			}
		case T::tTriangle:
			{
				Triangle t = v.get<Triangle>();
				return s + QString( "%1 %2 %3" ).arg( t.v1() ).arg( t.v2() ).arg( t.v3() );
			}
		case T::tBSVertexDesc:
			{
				BSVertexDesc d = v.get<BSVertexDesc>();
				s += QString( "flags %1 size %2 offsets" ).arg( int( d.GetFlags() ), 0, 16 ).arg( d.GetVertexSize() );
				for ( int a = 0; a < VA_COUNT; a++ )
					s += QString( " %1" ).arg( d.GetAttributeOffset( VertexAttribute( a ) ) );
				return s;
			}
		case T::tByteArray:
		case T::tStringPalette:
		case T::tBlob:
			return s + hex( v.get<QByteArray>() );
		case T::tByteMatrix:
			{
				ByteMatrix * m = v.get<ByteMatrix *>();
				return s + QString( "%1 x %2: " ).arg( m->count( 0 ) ).arg( m->count( 1 ) ) + hex( QByteArray( m->data(), m->count() ) );
			}
		case T::tNone:
			return s + "none";
		default:
			break;
		}

		return s + "?";
	}

	// ---- the streams

	//! The model contexts every sample runs in: a header version on both sides of each layout change, with the user
	//! versions of the games that use it (the streams do not look at them, a context per game proves that).
	static QList<TestEnv::Profile> contexts()
	{
		return QList<TestEnv::Profile>()
			<< TestEnv::Profile{ "3.1", "3.1", 0, 0 }
			<< TestEnv::Profile{ "3.3.0.13", "3.3.0.13", 0, 0 }
			<< TestEnv::Profile{ "4.0.0.2 Morrowind", "4.0.0.2", 0, 0 }
			<< TestEnv::Profile{ "4.1.0.12", "4.1.0.12", 0, 0 }
			<< TestEnv::Profile{ "10.0.1.0", "10.0.1.0", 0, 0 }
			<< TestEnv::Profile{ "20.0.0.5 Oblivion", "20.0.0.5", 11, 11 }
			<< TestEnv::Profile{ "20.1.0.3", "20.1.0.3", 0, 0 }
			<< TestEnv::Profile{ "20.2.0.7 Fallout 3", "20.2.0.7", 11, 34 }
			<< TestEnv::Profile{ "20.2.0.7 Skyrim LE", "20.2.0.7", 12, 83 }
			<< TestEnv::Profile{ "20.2.0.7 Fallout 4 (BS stream 130)", "20.2.0.7", 12, 130 };
	}

	static quint32 versionOf( const TestEnv::Profile & p ) { return NifModel::version2number( QString::fromLatin1( p.version ) ); }

	static QString where( const Sample & s, const TestEnv::Profile & p ) { return QString( "%1 @ %2" ).arg( s.name, p.name ); }

	//! A model per context, kept for all tests: the streams take nothing from it but the version. The header string read
	//! changes the version of its model, so that one is read with a model of its own.
	QHash<QString, std::shared_ptr<NifModel>> models;

	std::shared_ptr<NifModel> modelFor( const TestEnv::Profile & p, bool own = false )
	{
		if ( own )
			return std::shared_ptr<NifModel>( TestEnv::makeModel( p ).release() );

		QString key = QString( "%1/%2/%3" ).arg( p.version ).arg( p.userVersion ).arg( p.userVersion2 );
		if ( !models.contains( key ) )
			models.insert( key, std::shared_ptr<NifModel>( TestEnv::makeModel( p ).release() ) );

		return models.value( key );
	}

	//! Write through NifOStream with a model of the context's version; size is what NifSStream says about the same value
	bool streamWrite( const NifValue & v, const TestEnv::Profile & p, QByteArray & bytes, int * size = nullptr )
	{
		auto nif = modelFor( p );
		QBuffer buf;
		buf.open( QIODevice::WriteOnly );
		NifOStream os( nif.get(), &buf );
		bool ok = os.write( v );
		if ( size )
			*size = NifSStream( nif.get() ).size( v );
		bytes = buf.data();
		return ok;
	}

	//! Read through NifIStream with a model of the context's version; used is how many bytes it took
	bool streamRead( NifValue & v, const QByteArray & bytes, const TestEnv::Profile & p, qint64 & used )
	{
		auto nif = modelFor( p, v.type() == NifValue::tHeaderString );
		QBuffer buf;
		buf.setData( bytes );
		buf.open( QIODevice::ReadOnly );
		NifIStream is( nif.get(), &buf );
		bool ok = is.read( v );
		used = buf.pos();
		return ok;
	}

	//! Types whose size is fixed, so that every shorter input has to fail
	static bool isFixedWidth( NifValue::Type t )
	{
		switch ( t ) {
		case NifValue::tSizedString:
		case NifValue::tText:
		case NifValue::tShortString:
		case NifValue::tHeaderString:
		case NifValue::tLineString:
		case NifValue::tChar8String:
		case NifValue::tByteArray:
		case NifValue::tStringPalette:
		case NifValue::tByteMatrix:
		case NifValue::tString:
		case NifValue::tFilePath:
		case NifValue::tBlob:
		case NifValue::tNone:
			return false;
		default:
			return true;
		}
	}

	// ---- the sample table

	//! Every row is a value, the bytes it is stored as in the versions the row applies to, and what reading gives back
	static const QList<Sample> & samples()
	{
		static const QList<Sample> table = []() {
			typedef NifValue T;
			QList<Sample> t;

		// tBool: a byte, but 32 bits up to and including 4.0.0.2 (the stream flag bool32bit); 2 is the "undefined" state
		// struct.pack('<I', 0)
		t << Sample( "tBool false, 32-bit", vCount( T::tBool, 0 ), "00 00 00 00" ).to( kBool32Last );
		// struct.pack('<I', 1)
		t << Sample( "tBool true, 32-bit", vCount( T::tBool, 1 ), "01 00 00 00" ).to( kBool32Last );
		// struct.pack('<I', 2)
		t << Sample( "tBool undefined (2), 32-bit", vCount( T::tBool, 2 ), "02 00 00 00" ).to( kBool32Last );
		// struct.pack('<I', 0x100)
		t << Sample( "tBool 0x100, 32-bit", vCount( T::tBool, 0x100 ), "00 01 00 00" ).to( kBool32Last );
		// struct.pack('<B', 0)
		t << Sample( "tBool false, 8-bit", vCount( T::tBool, 0 ), "00" ).from( kBool32Last + 1 );
		// struct.pack('<B', 1)
		t << Sample( "tBool true, 8-bit", vCount( T::tBool, 1 ), "01" ).from( kBool32Last + 1 );
		// struct.pack('<B', 2)
		t << Sample( "tBool undefined (2), 8-bit", vCount( T::tBool, 2 ), "02" ).from( kBool32Last + 1 );

		// tByte
		// struct.pack('<B', 0x00)
		t << Sample( "tByte 0x00", vCount( T::tByte, 0x00 ), "00" );
		// struct.pack('<B', 0x01)
		t << Sample( "tByte 0x01", vCount( T::tByte, 0x01 ), "01" );
		// struct.pack('<B', 0x7f)
		t << Sample( "tByte 0x7f", vCount( T::tByte, 0x7f ), "7f" );
		// struct.pack('<B', 0x80)
		t << Sample( "tByte 0x80", vCount( T::tByte, 0x80 ), "80" );
		// struct.pack('<B', 0xff)
		t << Sample( "tByte 0xff", vCount( T::tByte, 0xff ), "ff" );

		// tWord
		// struct.pack('<H', 0x0000)
		t << Sample( "tWord 0x0000", vCount( T::tWord, 0x0000 ), "00 00" );
		// struct.pack('<H', 0x0001)
		t << Sample( "tWord 0x0001", vCount( T::tWord, 0x0001 ), "01 00" );
		// struct.pack('<H', 0x7fff)
		t << Sample( "tWord 0x7fff", vCount( T::tWord, 0x7fff ), "ff 7f" );
		// struct.pack('<H', 0x8000)
		t << Sample( "tWord 0x8000", vCount( T::tWord, 0x8000 ), "00 80" );
		// struct.pack('<H', 0xbeef)
		t << Sample( "tWord 0xbeef", vCount( T::tWord, 0xbeef ), "ef be" );
		// struct.pack('<H', 0xffff)
		t << Sample( "tWord 0xffff", vCount( T::tWord, 0xffff ), "ff ff" );

		// tFlags
		// struct.pack('<H', 0x0000)
		t << Sample( "tFlags 0x0000", vCount( T::tFlags, 0x0000 ), "00 00" );
		// struct.pack('<H', 0x1234)
		t << Sample( "tFlags 0x1234", vCount( T::tFlags, 0x1234 ), "34 12" );
		// struct.pack('<H', 0xffff)
		t << Sample( "tFlags 0xffff", vCount( T::tFlags, 0xffff ), "ff ff" );

		// tStringOffset
		// struct.pack('<I', 0x00000000)
		t << Sample( "tStringOffset 0x00000000", vCount( T::tStringOffset, 0x00000000u ), "00 00 00 00" );
		// struct.pack('<I', 0x12345678)
		t << Sample( "tStringOffset 0x12345678", vCount( T::tStringOffset, 0x12345678u ), "78 56 34 12" );
		// struct.pack('<I', 0xffffffff)
		t << Sample( "tStringOffset 0xffffffff", vCount( T::tStringOffset, 0xffffffffu ), "ff ff ff ff" );

		// tStringIndex
		// struct.pack('<I', 0x00000000)
		t << Sample( "tStringIndex 0x00000000", vCount( T::tStringIndex, 0x00000000u ), "00 00 00 00" );
		// struct.pack('<I', 0x00000007)
		t << Sample( "tStringIndex 0x00000007", vCount( T::tStringIndex, 0x00000007u ), "07 00 00 00" );
		// struct.pack('<I', 0xffffffff)
		t << Sample( "tStringIndex 0xffffffff", vCount( T::tStringIndex, 0xffffffffu ), "ff ff ff ff" );

		// tBlockTypeIndex
		// struct.pack('<H', 0x0000)
		t << Sample( "tBlockTypeIndex 0x0000", vCount( T::tBlockTypeIndex, 0x0000 ), "00 00" );
		// struct.pack('<H', 0x8003)
		t << Sample( "tBlockTypeIndex 0x8003", vCount( T::tBlockTypeIndex, 0x8003 ), "03 80" );
		// struct.pack('<H', 0xffff)
		t << Sample( "tBlockTypeIndex 0xffff", vCount( T::tBlockTypeIndex, 0xffff ), "ff ff" );

		// tInt
		// struct.pack('<i', 0)
		t << Sample( "tInt 0", vInt( 0 ), "00 00 00 00" );
		// struct.pack('<i', 1)
		t << Sample( "tInt 1", vInt( 1 ), "01 00 00 00" );
		// struct.pack('<i', -1)
		t << Sample( "tInt -1", vInt( -1 ), "ff ff ff ff" );
		// struct.pack('<i', -123456)
		t << Sample( "tInt -123456", vInt( -123456 ), "c0 1d fe ff" );
		// struct.pack('<i', -2147483648)
		t << Sample( "tInt -2147483648", vInt( INT_MIN ), "00 00 00 80" );
		// struct.pack('<i', 2147483647)
		t << Sample( "tInt 2147483647", vInt( INT_MAX ), "ff ff ff 7f" );

		// tShort: stored in 16 bits, read back zero extended: compared as 16 bits
		// struct.pack('<h', 0)
		t << Sample( "tShort 0", vShort( 0 ), "00 00" );
		// struct.pack('<h', 1)
		t << Sample( "tShort 1", vShort( 1 ), "01 00" );
		// struct.pack('<h', -1)
		t << Sample( "tShort -1", vShort( -1 ), "ff ff" );
		// struct.pack('<h', -2)
		t << Sample( "tShort -2", vShort( -2 ), "fe ff" );
		// struct.pack('<h', 32767)
		t << Sample( "tShort 32767", vShort( 32767 ), "ff 7f" );
		// struct.pack('<h', -32768)
		t << Sample( "tShort -32768", vShort( -32768 ), "00 80" );

		// tULittle32: always little endian, even in big endian files
		// struct.pack('<I', 0x00000000)
		t << Sample( "tULittle32 0x00000000", vCount( T::tULittle32, 0x00000000u ), "00 00 00 00" );
		// struct.pack('<I', 0x01020304)
		t << Sample( "tULittle32 0x01020304", vCount( T::tULittle32, 0x01020304u ), "04 03 02 01" );
		// struct.pack('<I', 0xffffffff)
		t << Sample( "tULittle32 0xffffffff", vCount( T::tULittle32, 0xffffffffu ), "ff ff ff ff" );

		// tUInt
		// struct.pack('<I', 0x00000000)
		t << Sample( "tUInt 0x00000000", vCount( T::tUInt, 0x00000000u ), "00 00 00 00" );
		// struct.pack('<I', 0x00000001)
		t << Sample( "tUInt 0x00000001", vCount( T::tUInt, 0x00000001u ), "01 00 00 00" );
		// struct.pack('<I', 0x80000000)
		t << Sample( "tUInt 0x80000000", vCount( T::tUInt, 0x80000000u ), "00 00 00 80" );
		// struct.pack('<I', 0xdeadbeef)
		t << Sample( "tUInt 0xdeadbeef", vCount( T::tUInt, 0xdeadbeefu ), "ef be ad de" );
		// struct.pack('<I', 0xffffffff)
		t << Sample( "tUInt 0xffffffff", vCount( T::tUInt, 0xffffffffu ), "ff ff ff ff" );

		// tLink: Ref / Ptr: version 3.3.0.13 and up store the index, older files index + 1 (so null is 0 there); null is -1
		// struct.pack('<i', -1)
		t << Sample( "tLink null (-1), 0-based", vLink( T::tLink, -1 ), "ff ff ff ff" ).from( kLinksZeroBased );
		// struct.pack('<i', -1 + 1)
		t << Sample( "tLink null (-1), 1-based", vLink( T::tLink, -1 ), "00 00 00 00" ).to( kLinksZeroBased - 1 );
		// struct.pack('<i', 0)
		t << Sample( "tLink 0, 0-based", vLink( T::tLink, 0 ), "00 00 00 00" ).from( kLinksZeroBased );
		// struct.pack('<i', 0 + 1)
		t << Sample( "tLink 0, 1-based", vLink( T::tLink, 0 ), "01 00 00 00" ).to( kLinksZeroBased - 1 );
		// struct.pack('<i', 5)
		t << Sample( "tLink 5, 0-based", vLink( T::tLink, 5 ), "05 00 00 00" ).from( kLinksZeroBased );
		// struct.pack('<i', 5 + 1)
		t << Sample( "tLink 5, 1-based", vLink( T::tLink, 5 ), "06 00 00 00" ).to( kLinksZeroBased - 1 );
		// struct.pack('<i', 74565)
		t << Sample( "tLink 74565, 0-based", vLink( T::tLink, 74565 ), "45 23 01 00" ).from( kLinksZeroBased );
		// struct.pack('<i', 74565 + 1)
		t << Sample( "tLink 74565, 1-based", vLink( T::tLink, 74565 ), "46 23 01 00" ).to( kLinksZeroBased - 1 );
		// struct.pack('<i', -1)
		t << Sample( "tUpLink null (-1), 0-based", vLink( T::tUpLink, -1 ), "ff ff ff ff" ).from( kLinksZeroBased );
		// struct.pack('<i', -1 + 1)
		t << Sample( "tUpLink null (-1), 1-based", vLink( T::tUpLink, -1 ), "00 00 00 00" ).to( kLinksZeroBased - 1 );
		// struct.pack('<i', 2)
		t << Sample( "tUpLink 2, 0-based", vLink( T::tUpLink, 2 ), "02 00 00 00" ).from( kLinksZeroBased );
		// struct.pack('<i', 2 + 1)
		t << Sample( "tUpLink 2, 1-based", vLink( T::tUpLink, 2 ), "03 00 00 00" ).to( kLinksZeroBased - 1 );

		// tFloat: IEEE 754 binary32, little endian; the special values are compared by their bit patterns
		// struct.pack('<f', 0.0)
		t << Sample( "tFloat 0", vFloat( T::tFloat, 0.0f ), "00 00 00 00" );
		// struct.pack('<f', -0.0)
		t << Sample( "tFloat -0", vFloat( T::tFloat, -0.0f ), "00 00 00 80" );
		// struct.pack('<f', 1.0)
		t << Sample( "tFloat 1", vFloat( T::tFloat, 1.0f ), "00 00 80 3f" );
		// struct.pack('<f', -1.0)
		t << Sample( "tFloat -1", vFloat( T::tFloat, -1.0f ), "00 00 80 bf" );
		// struct.pack('<f', 1.5)
		t << Sample( "tFloat 1.5", vFloat( T::tFloat, 1.5f ), "00 00 c0 3f" );
		// struct.pack('<f', 0.1)
		t << Sample( "tFloat 0.1", vFloat( T::tFloat, 0.1f ), "cd cc cc 3d" );
		// struct.pack('<f', 3.4028234663852886e+38)
		t << Sample( "tFloat FLT_MAX", vFloat( T::tFloat, 3.4028235e+38f ), "ff ff 7f 7f" );
		// struct.pack('<f', 1.1754943508222875e-38)
		t << Sample( "tFloat FLT_MIN (smallest normal)", vFloat( T::tFloat, 1.1754944e-38f ), "00 00 80 00" );
		// struct.pack('<f', 1.401298464324817e-45)
		t << Sample( "tFloat smallest denormal", vFloat( T::tFloat, fromBits( 0x00000001 ) ), "01 00 00 00" );
		// struct.pack('<f', math.inf)
		t << Sample( "tFloat +inf", vFloat( T::tFloat, fromBits( 0x7f800000 ) ), "00 00 80 7f" );
		// struct.pack('<f', -math.inf)
		t << Sample( "tFloat -inf", vFloat( T::tFloat, fromBits( 0xff800000 ) ), "00 00 80 ff" );
		// struct.pack('<f', math.nan)
		t << Sample( "tFloat quiet NaN", vFloat( T::tFloat, fromBits( 0x7fc00000 ) ), "00 00 c0 7f" );

		// tHfloat: IEEE 754 binary16; values that are not representable are rounded to the nearest half
		// struct.pack('<e', 0.0)
		t << Sample( "tHfloat 0", vFloat( T::tHfloat, 0.0f ), "00 00" );
		// struct.pack('<e', -0.0)
		t << Sample( "tHfloat -0", vFloat( T::tHfloat, -0.0f ), "00 80" );
		// struct.pack('<e', 1.0)
		t << Sample( "tHfloat 1", vFloat( T::tHfloat, 1.0f ), "00 3c" );
		// struct.pack('<e', 0.5)
		t << Sample( "tHfloat 0.5", vFloat( T::tHfloat, 0.5f ), "00 38" );
		// struct.pack('<e', -2.25)
		t << Sample( "tHfloat -2.25", vFloat( T::tHfloat, -2.25f ), "80 c0" );
		// struct.pack('<e', 65504.0)
		t << Sample( "tHfloat 65504 (largest)", vFloat( T::tHfloat, 65504.0f ), "ff 7b" );
		// struct.pack('<e', 6.103515625e-05)
		t << Sample( "tHfloat smallest normal", vFloat( T::tHfloat, 6.1035156e-05f ), "00 04" );
		// struct.pack('<e', 5.960464477539063e-08)
		t << Sample( "tHfloat smallest denormal", vFloat( T::tHfloat, 5.9604645e-08f ), "01 00" );
		// struct.pack('<e', math.inf)
		t << Sample( "tHfloat +inf", vFloat( T::tHfloat, fromBits( 0x7f800000 ) ), "00 7c" );
		// struct.pack('<e', -math.inf)
		t << Sample( "tHfloat -inf", vFloat( T::tHfloat, fromBits( 0xff800000 ) ), "00 fc" );
		// struct.pack('<e', 0.1)
		t << Sample( "tHfloat 0.1 (rounds to the nearest half)", vFloat( T::tHfloat, 0.1f ), "66 2e" ).back( vFloat( T::tHfloat, 0.099975586f ) );
		// struct.pack('<e', math.nan)
		t << Sample( "tHfloat quiet NaN", vFloat( T::tHfloat, fromBits( 0x7fc00000 ) ), "00 7e" );

		// tVector2
		// struct.pack('<2f', 0, 0)
		t << Sample( "tVector2 zero", vOf<Vector2>( T::tVector2, Vector2( 0.0f, 0.0f ) ), "00 00 00 00 00 00 00 00" );
		// struct.pack('<2f', 1, -2)
		t << Sample( "tVector2 (1, -2)", vOf<Vector2>( T::tVector2, Vector2( 1.0f, -2.0f ) ), "00 00 80 3f 00 00 00 c0" );
		// struct.pack('<2f', 3.4028234663852886e+38, -1.1754943508222875e-38)
		t << Sample( "tVector2 extremes", vOf<Vector2>( T::tVector2, Vector2( 3.4028235e+38f, -1.1754944e-38f ) ), "ff ff 7f 7f 00 00 80 80" );

		// tVector3
		// struct.pack('<3f', 0, 0, 0)
		t << Sample( "tVector3 zero", vOf<Vector3>( T::tVector3, Vector3( 0.0f, 0.0f, 0.0f ) ), "00 00 00 00 00 00 00 00 00 00 00 00" );
		// struct.pack('<3f', 1, 2, 3)
		t << Sample( "tVector3 (1, 2, 3)", vOf<Vector3>( T::tVector3, Vector3( 1.0f, 2.0f, 3.0f ) ), "00 00 80 3f 00 00 00 40 00 00 40 40" );
		// struct.pack('<3f', -1.5, 0.25, -8)
		t << Sample( "tVector3 (-1.5, 0.25, -8)", vOf<Vector3>( T::tVector3, Vector3( -1.5f, 0.25f, -8.0f ) ), "00 00 c0 bf 00 00 80 3e 00 00 00 c1" );
		// struct.pack('<3f', 3.4028234663852886e+38, -1.1754943508222875e-38, 1.401298464324817e-45)
		t << Sample( "tVector3 extremes", vOf<Vector3>( T::tVector3, Vector3( 3.4028235e+38f, -1.1754944e-38f, fromBits( 0x00000001 ) ) ),
			"ff ff 7f 7f 00 00 80 80 01 00 00 00" );

		// tVector4
		// struct.pack('<4f', 0, 0, 0, 0)
		t << Sample( "tVector4 zero", vOf<Vector4>( T::tVector4, Vector4( 0.0f, 0.0f, 0.0f, 0.0f ) ), "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" );
		// struct.pack('<4f', 1, 2, 3, 4)
		t << Sample( "tVector4 (1, 2, 3, 4)", vOf<Vector4>( T::tVector4, Vector4( 1.0f, 2.0f, 3.0f, 4.0f ) ),
			"00 00 80 3f 00 00 00 40 00 00 40 40 00 00 80 40" );
		// struct.pack('<4f', -1, 0.5, -0.25, 8)
		t << Sample( "tVector4 (-1, 0.5, -0.25, 8)", vOf<Vector4>( T::tVector4, Vector4( -1.0f, 0.5f, -0.25f, 8.0f ) ),
			"00 00 80 bf 00 00 00 3f 00 00 80 be 00 00 00 41" );

		// tColor3
		// struct.pack('<3f', 0, 0, 0)
		t << Sample( "tColor3 black", vOf<Color3>( T::tColor3, Color3( 0.0f, 0.0f, 0.0f ) ), "00 00 00 00 00 00 00 00 00 00 00 00" );
		// struct.pack('<3f', 0.25, 0.5, 1)
		t << Sample( "tColor3 (0.25, 0.5, 1)", vOf<Color3>( T::tColor3, Color3( 0.25f, 0.5f, 1.0f ) ), "00 00 80 3e 00 00 00 3f 00 00 80 3f" );
		// struct.pack('<3f', 2.5, -0.5, 1000)
		t << Sample( "tColor3 unclamped (2.5, -0.5, 1000)", vOf<Color3>( T::tColor3, Color3( 2.5f, -0.5f, 1000.0f ) ),
			"00 00 20 40 00 00 00 bf 00 00 7a 44" );

		// tColor4
		// struct.pack('<4f', 0, 0, 0, 0)
		t << Sample( "tColor4 transparent black", vOf<Color4>( T::tColor4, Color4( 0.0f, 0.0f, 0.0f, 0.0f ) ),
			"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" );
		// struct.pack('<4f', 0.25, 0.5, 1, 0.75)
		t << Sample( "tColor4 (0.25, 0.5, 1, 0.75)", vOf<Color4>( T::tColor4, Color4( 0.25f, 0.5f, 1.0f, 0.75f ) ),
			"00 00 80 3e 00 00 00 3f 00 00 80 3f 00 00 40 3f" );
		// struct.pack('<4f', 2.5, -0.5, 1000, 0.001)
		t << Sample( "tColor4 unclamped (2.5, -0.5, 1000, 0.001)", vOf<Color4>( T::tColor4, Color4( 2.5f, -0.5f, 1000.0f, 0.001f ) ),
			"00 00 20 40 00 00 00 bf 00 00 7a 44 6f 12 83 3a" );

		// tQuat: Quat( w, x, y, z ), written in the order w x y z
		// struct.pack('<4f', 1, 0, 0, 0)
		t << Sample( "tQuat identity", vOf<Quat>( T::tQuat, Quat( 1.0f, 0.0f, 0.0f, 0.0f ) ), "00 00 80 3f 00 00 00 00 00 00 00 00 00 00 00 00" );
		// struct.pack('<4f', 1, 2, 3, 4)
		t << Sample( "tQuat (w 1, x 2, y 3, z 4)", vOf<Quat>( T::tQuat, Quat( 1.0f, 2.0f, 3.0f, 4.0f ) ),
			"00 00 80 3f 00 00 00 40 00 00 40 40 00 00 80 40" );
		// struct.pack('<4f', -0.5, 0.5, -0.25, 0.25)
		t << Sample( "tQuat (w -0.5, x 0.5, y -0.25, z 0.25)", vOf<Quat>( T::tQuat, Quat( -0.5f, 0.5f, -0.25f, 0.25f ) ),
			"00 00 00 bf 00 00 00 3f 00 00 80 be 00 00 80 3e" );

		// tQuatXYZW: the same quaternions, written in the order x y z w (hkQuaternion)
		// struct.pack('<4f', 0, 0, 0, 1)
		t << Sample( "tQuatXYZW identity", vOf<Quat>( T::tQuatXYZW, Quat( 1.0f, 0.0f, 0.0f, 0.0f ) ), "00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3f" );
		// struct.pack('<4f', 2, 3, 4, 1)
		t << Sample( "tQuatXYZW (w 1, x 2, y 3, z 4)", vOf<Quat>( T::tQuatXYZW, Quat( 1.0f, 2.0f, 3.0f, 4.0f ) ),
			"00 00 00 40 00 00 40 40 00 00 80 40 00 00 80 3f" );
		// struct.pack('<4f', 0.5, -0.25, 0.25, -0.5)
		t << Sample( "tQuatXYZW (w -0.5, x 0.5, y -0.25, z 0.25)", vOf<Quat>( T::tQuatXYZW, Quat( -0.5f, 0.5f, -0.25f, 0.25f ) ),
			"00 00 00 3f 00 00 80 be 00 00 80 3e 00 00 00 bf" );

		// tMatrix: Matrix::m[r][c] is written row after row (nif.xml: m11 m21 m31 m12 ...): m(r,c) = 1 + 3r + c is stored as 1, 2 ... 9
		// struct.pack('<9f', 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
		t << Sample( "tMatrix identity", vMatrix( { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f } ),
			"00 00 80 3f 00 00 00 00 00 00 00 00 00 00 00 00 "
			"00 00 80 3f 00 00 00 00 00 00 00 00 00 00 00 00 "
			"00 00 80 3f" );
		// struct.pack('<9f', 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0)
		t << Sample( "tMatrix m(r,c) = 1 + 3r + c", vMatrix( { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f } ),
			"00 00 80 3f 00 00 00 40 00 00 40 40 00 00 80 40 "
			"00 00 a0 40 00 00 c0 40 00 00 e0 40 00 00 00 41 "
			"00 00 10 41" );
		// struct.pack('<9f', 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0)
		t << Sample( "tMatrix permutation (0 1 0, 0 0 1, 1 0 0)", vMatrix( { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f } ),
			"00 00 00 00 00 00 80 3f 00 00 00 00 00 00 00 00 "
			"00 00 00 00 00 00 80 3f 00 00 80 3f 00 00 00 00 "
			"00 00 00 00" );

		// tMatrix4: same rule, 16 floats
		// struct.pack('<16f', 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)
		t << Sample( "tMatrix4 identity", vMatrix4( { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f } ),
			"00 00 80 3f 00 00 00 00 00 00 00 00 00 00 00 00 "
			"00 00 00 00 00 00 80 3f 00 00 00 00 00 00 00 00 "
			"00 00 00 00 00 00 00 00 00 00 80 3f 00 00 00 00 "
			"00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3f" );
		// struct.pack('<16f', 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0)
		t << Sample( "tMatrix4 m(r,c) = 1 + 4r + c", vMatrix4( { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f } ),
			"00 00 80 3f 00 00 00 40 00 00 40 40 00 00 80 40 "
			"00 00 a0 40 00 00 c0 40 00 00 e0 40 00 00 00 41 "
			"00 00 10 41 00 00 20 41 00 00 30 41 00 00 40 41 "
			"00 00 50 41 00 00 60 41 00 00 70 41 00 00 80 41" );

		// tTriangle: three 16-bit vertex indices v1 v2 v3
		// struct.pack('<3H', 0, 0, 0)
		t << Sample( "tTriangle zero", vOf<Triangle>( T::tTriangle, Triangle( 0, 0, 0 ) ), "00 00 00 00 00 00" );
		// struct.pack('<3H', 1, 2, 3)
		t << Sample( "tTriangle (1, 2, 3)", vOf<Triangle>( T::tTriangle, Triangle( 1, 2, 3 ) ), "01 00 02 00 03 00" );
		// struct.pack('<3H', 65535, 32768, 1)
		t << Sample( "tTriangle (0xFFFF, 0x8000, 1)", vOf<Triangle>( T::tTriangle, Triangle( 65535, 32768, 1 ) ), "ff ff 00 80 01 00" );

		// tByteVector3: byte = floor( ( x + 1 ) / 2 * 255 + 0.5 ) (C round(): a tie goes away from zero), read back as byte / 255 * 2 - 1
		// bytes(math.floor(((c + 1.0) / 2.0) * 255.0 + 0.5) for c in (-1, 0, 1))
		t << Sample( "tByteVector3 (-1, 0, 1): 0.0 is the rounding tie 127.5 -> 128", vOf<ByteVector3>( T::tByteVector3, ByteVector3( -1.0f, 0.0f, 1.0f ) ),
			"00 80 ff" )
			.back( vOf<ByteVector3>( T::tByteVector3, ByteVector3( -1.0f, 0.003921569f, 1.0f ) ) );
		// bytes(math.floor(((c + 1.0) / 2.0) * 255.0 + 0.5) for c in (1, -1, 0))
		t << Sample( "tByteVector3 (1, -1, 0): component order", vOf<ByteVector3>( T::tByteVector3, ByteVector3( 1.0f, -1.0f, 0.0f ) ),
			"ff 00 80" )
			.back( vOf<ByteVector3>( T::tByteVector3, ByteVector3( 1.0f, -1.0f, 0.003921569f ) ) );
		// bytes(math.floor(((c + 1.0) / 2.0) * 255.0 + 0.5) for c in (-1, -1, -1))
		t << Sample( "tByteVector3 (-1, -1, -1)", vOf<ByteVector3>( T::tByteVector3, ByteVector3( -1.0f, -1.0f, -1.0f ) ), "00 00 00" );
		// bytes(math.floor(((c + 1.0) / 2.0) * 255.0 + 0.5) for c in (1, 1, 1))
		t << Sample( "tByteVector3 (1, 1, 1)", vOf<ByteVector3>( T::tByteVector3, ByteVector3( 1.0f, 1.0f, 1.0f ) ), "ff ff ff" );
		// bytes(math.floor(((c + 1.0) / 2.0) * 255.0 + 0.5) for c in (-0.9921568632125854, -0.003921568859368563, 0.9921568632125854))
		t << Sample( "tByteVector3 fixed points k/255*2-1 for k = 1, 127, 254", vOf<ByteVector3>( T::tByteVector3, ByteVector3( -0.99215686f, -0.003921569f, 0.99215686f ) ),
			"01 7f fe" );
		// bytes(math.floor(((c + 1.0) / 2.0) * 255.0 + 0.5) for c in (0.5, -0.5, 0.25))
		t << Sample( "tByteVector3 off the grid (0.5, -0.5, 0.25) snaps to the nearest byte", vOf<ByteVector3>( T::tByteVector3, ByteVector3( 0.5f, -0.5f, 0.25f ) ),
			"bf 40 9f" )
			.back( vOf<ByteVector3>( T::tByteVector3, ByteVector3( 0.49803922f, -0.49803922f, 0.24705882f ) ) );

		// tHalfVector3: three binary16 values
		// struct.pack('<3e', 0, 0, 0)
		t << Sample( "tHalfVector3 zero", vOf<HalfVector3>( T::tHalfVector3, HalfVector3( 0.0f, 0.0f, 0.0f ) ), "00 00 00 00 00 00" );
		// struct.pack('<3e', 0.5, -1, 2)
		t << Sample( "tHalfVector3 (0.5, -1, 2)", vOf<HalfVector3>( T::tHalfVector3, HalfVector3( 0.5f, -1.0f, 2.0f ) ), "00 38 00 bc 00 40" );
		// struct.pack('<3e', 65504, -0.25, 1024)
		t << Sample( "tHalfVector3 (65504, -0.25, 1024)", vOf<HalfVector3>( T::tHalfVector3, HalfVector3( 65504.0f, -0.25f, 1024.0f ) ),
			"ff 7b 00 b4 00 64" );
		// struct.pack('<3e', -0.0, 6.103515625e-05, 5.960464477539063e-08)
		t << Sample( "tHalfVector3 (-0, smallest normal, smallest denormal)", vOf<HalfVector3>( T::tHalfVector3, HalfVector3( -0.0f, 6.1035156e-05f, 5.9604645e-08f ) ),
			"00 80 00 04 01 00" );
		// struct.pack('<3e', -0.5, -1, -2)
		t << Sample( "tHalfVector3 (-0.5, -1, -2): every component negative", vOf<HalfVector3>( T::tHalfVector3, HalfVector3( -0.5f, -1.0f, -2.0f ) ),
			"00 b8 00 bc 00 c0" );

		// tHalfVector2
		// struct.pack('<2e', 0, 0)
		t << Sample( "tHalfVector2 zero", vOf<HalfVector2>( T::tHalfVector2, HalfVector2( 0.0f, 0.0f ) ), "00 00 00 00" );
		// struct.pack('<2e', 0.5, -1)
		t << Sample( "tHalfVector2 (0.5, -1)", vOf<HalfVector2>( T::tHalfVector2, HalfVector2( 0.5f, -1.0f ) ), "00 38 00 bc" );
		// struct.pack('<2e', 65504, -0.25)
		t << Sample( "tHalfVector2 (65504, -0.25)", vOf<HalfVector2>( T::tHalfVector2, HalfVector2( 65504.0f, -0.25f ) ), "ff 7b 00 b4" );
		// struct.pack('<2e', -0.5, 0.25)
		t << Sample( "tHalfVector2 (-0.5, 0.25): the first component negative", vOf<HalfVector2>( T::tHalfVector2, HalfVector2( -0.5f, 0.25f ) ), "00 b8 00 34" );

		// tByteColor4: byte = floor( c * 255 + 0.5 ), read back as byte / 255
		// bytes(math.floor(c * 255.0 + 0.5) for c in (0, 0, 0, 0))
		t << Sample( "tByteColor4 (0, 0, 0, 0)", vByteColor( 0.0f, 0.0f, 0.0f, 0.0f ), "00 00 00 00" );
		// bytes(math.floor(c * 255.0 + 0.5) for c in (1, 1, 1, 1))
		t << Sample( "tByteColor4 (1, 1, 1, 1)", vByteColor( 1.0f, 1.0f, 1.0f, 1.0f ), "ff ff ff ff" );
		// bytes(math.floor(c * 255.0 + 0.5) for c in (0, 0.5, 1, 0.2))
		t << Sample( "tByteColor4 (0, 0.5, 1, 0.2): 0.5 is the rounding tie 127.5 -> 128", vByteColor( 0.0f, 0.5f, 1.0f, 0.2f ),
			"00 80 ff 33" )
			.back( vByteColor( 0.0f, 0.5019608f, 1.0f, 0.2f ) );
		// bytes(math.floor(c * 255.0 + 0.5) for c in (0.2, 0.4, 0.6, 0.8))
		t << Sample( "tByteColor4 (0.2, 0.4, 0.6, 0.8)", vByteColor( 0.2f, 0.4f, 0.6f, 0.8f ), "33 66 99 cc" );
		// bytes(math.floor(c * 255.0 + 0.5) for c in (0.003921568859368563, 0.49803921580314636, 0.9960784316062927, 0.007843137718737125))
		t << Sample( "tByteColor4 fixed points k/255 for k = 1, 127, 254, 2", vByteColor( 0.003921569f, 0.49803922f, 0.99607843f, 0.007843138f ),
			"01 7f fe 02" );
		// bytes(math.floor(c * 255.0 + 0.5) for c in (0.5, 0.5, 0.5, 0.5))
		t << Sample( "tByteColor4 (0.5, 0.5, 0.5, 0.5): the tie 127.5 -> 128 in every channel", vByteColor( 0.5f, 0.5f, 0.5f, 0.5f ),
			"80 80 80 80" )
			.back( vByteColor( 0.501960814f, 0.501960814f, 0.501960814f, 0.501960814f ) );
		// the scaled values are 10.4, 20.4, 30.4 and 40.4: below one half, so down (a ceil() gives 0b 15 1f 29)
		// bytes(math.floor(c * 255.0 + 0.5) for c in (10.4 / 255, 20.4 / 255, 30.4 / 255, 40.4 / 255))
		t << Sample( "tByteColor4 scaled to 10.4, 20.4, 30.4, 40.4: a fraction below one half rounds down", vByteColor( 0.0407843143f, 0.0799999982f, 0.11921569f, 0.158431366f ),
			"0a 14 1e 28" )
			.back( vByteColor( 0.0392156877f, 0.0784313753f, 0.117647059f, 0.156862751f ) );
		// the scaled values are 10.75, 20.75, 30.75 and 40.75: above one half, so up (a floor() gives 0a 14 1e 28)
		// bytes(math.floor(c * 255.0 + 0.5) for c in (10.75 / 255, 20.75 / 255, 30.75 / 255, 40.75 / 255))
		t << Sample( "tByteColor4 scaled to 10.75, 20.75, 30.75, 40.75: a fraction above one half rounds up", vByteColor( 0.042156864f, 0.0813725516f, 0.120588236f, 0.159803927f ),
			"0b 15 1f 29" )
			.back( vByteColor( 0.0431372561f, 0.0823529437f, 0.121568628f, 0.160784319f ) );
		// 10.495, 100.495, 200.495 and 40.495: so close to one half that only the exact fraction decides, down (a rounding that favours the
		// half by 0.01 gives 0b 65 c9 29), and 10.505 and the like just above it, up. A fraction in each channel, the channels differ
		// bytes(math.floor(c * 255.0 + 0.5) for c in (10.495 / 255, 100.495 / 255, 200.495 / 255, 40.495 / 255)) with c a float32
		t << Sample( "tByteColor4 scaled to 10.495, 100.495, 200.495, 40.495: just below one half rounds down in every channel", vByteColor( 0.041156862f, 0.394098043f, 0.786254883f, 0.158803925f ),
			"0a 64 c8 28" )
			.back( vByteColor( 0.039215688f, 0.392156869f, 0.784313738f, 0.156862751f ) );
		t << Sample( "tByteColor4 scaled to 10.505, 100.505, 200.505, 40.505: just above one half rounds up in every channel", vByteColor( 0.041196078f, 0.394137263f, 0.786294103f, 0.15884313f ),
			"0b 65 c9 29" )
			.back( vByteColor( 0.043137256f, 0.396078438f, 0.788235307f, 0.160784319f ) );

		// tBSVertexDesc: a 64-bit word: flags from bit 44, vertex size / 4 in bits 0-3, the offset / 4 of attribute n in bits 4n+4..4n+7
		// struct.pack('<Q', 0)
		t << Sample( "tBSVertexDesc empty", vDesc( 0, 0, 0, 0, 0 ), "00 00 00 00 00 00 00 00" );
		// struct.pack('<Q', (0x3 << 44) | (12 // 4))
		t << Sample( "tBSVertexDesc VERTEX|UV, size 12", vDesc( VF_VERTEX | VF_UV, 12, 0, 0, 0 ), "03 00 00 00 00 30 00 00" );
		// struct.pack('<Q', (0x2b << 44) | (28 // 4) | ((12 // 4) << 8) | ((16 // 4) << 16) | ((24 // 4) << 24))
		t << Sample( "tBSVertexDesc VERTEX|UV|NORMAL|COLORS, size 28, offsets uv 12 normal 16 color 24", vDesc( VF_VERTEX | VF_UV | VF_NORMAL | VF_COLORS, 28, 12, 16, 24 ),
			"07 03 04 06 00 b0 02 00" );

		// tByteArray
		// struct.pack('<I', 0) + bytes.fromhex('')
		t << Sample( "tByteArray empty", vBytes( T::tByteArray, "" ), "00 00 00 00" );
		// struct.pack('<I', 3) + bytes.fromhex('01 02 03')
		t << Sample( "tByteArray 01 02 03", vBytes( T::tByteArray, "01 02 03" ), "03 00 00 00 01 02 03" );
		// struct.pack('<I', 4) + bytes.fromhex('00 ff 80 00')
		t << Sample( "tByteArray 00 ff 80 00", vBytes( T::tByteArray, "00 ff 80 00" ), "04 00 00 00 00 ff 80 00" );

		// tStringPalette: length, bytes, and the length again
		// struct.pack('<I', 0) + b'' + struct.pack('<I', 0)
		t << Sample( "tStringPalette empty", vBytes( T::tStringPalette, "" ), "00 00 00 00 00 00 00 00" );
		// struct.pack('<I', 7) + b'abc\0de\0' + struct.pack('<I', 7)
		t << Sample( "tStringPalette abc NUL de NUL", vBytes( T::tStringPalette, "61 62 63 00 64 65 00" ),
			"07 00 00 00 61 62 63 00 64 65 00 07 00 00 00" );

		// tByteMatrix: both dimensions, then the bytes row after row
		// struct.pack('<II', 0, 0)
		t << Sample( "tByteMatrix 0x0", vByteMatrix( 0, 0, "" ), "00 00 00 00 00 00 00 00" );
		// struct.pack('<II', 1, 1) + bytes([0x7f])
		t << Sample( "tByteMatrix 1x1", vByteMatrix( 1, 1, "7f" ), "01 00 00 00 01 00 00 00 7f" );
		// struct.pack('<II', 2, 3) + bytes([0, 1, 2, 10, 11, 12])
		t << Sample( "tByteMatrix 2x3 (row after row)", vByteMatrix( 2, 3, "00 01 02 0a 0b 0c" ), "02 00 00 00 03 00 00 00 00 01 02 0a 0b 0c" );
		// struct.pack('<II', 3, 2) + bytes([0, 1, 10, 11, 20, 21])
		t << Sample( "tByteMatrix 3x2 (dimension order)", vByteMatrix( 3, 2, "00 01 0a 0b 14 15" ), "03 00 00 00 02 00 00 00 00 01 0a 0b 14 15" );

		// tBlob: raw bytes without a length; reading fills a target that already has the right size
		// b''
		t << Sample( "tBlob empty", vBytes( T::tBlob, "" ), "" ).target( vBytes( T::tBlob, "" ) );
		// bytes([0xaa, 0xbb, 0xcc, 0xdd])
		t << Sample( "tBlob aa bb cc dd", vBytes( T::tBlob, "aa bb cc dd" ), "aa bb cc dd" ).target( vBytes( T::tBlob, "00 00 00 00" ) );

		// tNone
		// b''
		t << Sample( "tNone", NifValue(), "" );

		// tSizedString: length, then Latin-1 bytes
		// struct.pack('<I', 0) + b''
		t << Sample( "tSizedString empty", vString( T::tSizedString, "" ), "00 00 00 00" );
		// struct.pack('<I', 2) + b'ab'
		t << Sample( "tSizedString ab", vString( T::tSizedString, "ab" ), "02 00 00 00 61 62" );
		// struct.pack('<I', 16) + b'Hello, nifskope!'
		t << Sample( "tSizedString Hello, nifskope!", vString( T::tSizedString, "Hello, nifskope!" ),
			"10 00 00 00 48 65 6c 6c 6f 2c 20 6e 69 66 73 6b "
			"6f 70 65 21" );
		// struct.pack('<I', 11) + b'line1\nline2'
		t << Sample( "tSizedString with a line feed", vString( T::tSizedString, "line1\nline2" ), "0b 00 00 00 6c 69 6e 65 31 0a 6c 69 6e 65 32" );

		// tText
		// struct.pack('<I', 0) + b''
		t << Sample( "tText empty", vString( T::tText, "" ), "00 00 00 00" );
		// struct.pack('<I', 2) + b'ab'
		t << Sample( "tText ab", vString( T::tText, "ab" ), "02 00 00 00 61 62" );

		// tShortString: length including the NUL, bytes, NUL
		// struct.pack('<B', 1) + b'' + b'\0'
		t << Sample( "tShortString empty", vString( T::tShortString, "" ), "01 00" );
		// struct.pack('<B', 3) + b'ab' + b'\0'
		t << Sample( "tShortString ab", vString( T::tShortString, "ab" ), "03 61 62 00" );
		// struct.pack('<B', 6) + b'Hello' + b'\0'
		t << Sample( "tShortString Hello", vString( T::tShortString, "Hello" ), "06 48 65 6c 6c 6f 00" );

		// tHeaderString: bytes and a line feed; reading it makes the model take over the version in the text
		// b'NetImmerse File Format, Version 4.0.0.2' + b'\n'
		t << Sample( "tHeaderString NetImmerse File Format", vString( T::tHeaderString, "NetImmerse File Format, Version 4.0.0.2" ),
			"4e 65 74 49 6d 6d 65 72 73 65 20 46 69 6c 65 20 "
			"46 6f 72 6d 61 74 2c 20 56 65 72 73 69 6f 6e 20 "
			"34 2e 30 2e 30 2e 32 0a" );
		// b'Gamebryo File Format, Version 20.2.0.7' + b'\n'
		t << Sample( "tHeaderString Gamebryo File Format", vString( T::tHeaderString, "Gamebryo File Format, Version 20.2.0.7" ),
			"47 61 6d 65 62 72 79 6f 20 46 69 6c 65 20 46 6f "
			"72 6d 61 74 2c 20 56 65 72 73 69 6f 6e 20 32 30 "
			"2e 32 2e 30 2e 37 0a" );
		// b'NS File Format, Version 10.1.0.0' + b'\n'
		t << Sample( "tHeaderString NS File Format", vString( T::tHeaderString, "NS File Format, Version 10.1.0.0" ),
			"4e 53 20 46 69 6c 65 20 46 6f 72 6d 61 74 2c 20 "
			"56 65 72 73 69 6f 6e 20 31 30 2e 31 2e 30 2e 30 "
			"0a" );

		// tLineString
		// b'' + b'\n'
		t << Sample( "tLineString empty", vString( T::tLineString, "" ), "0a" );
		// b'ab' + b'\n'
		t << Sample( "tLineString ab", vString( T::tLineString, "ab" ), "61 62 0a" );
		// b'Copyright (c) 2026, a line' + b'\n'
		t << Sample( "tLineString Copyright (c) 2026, a line", vString( T::tLineString, "Copyright (c) 2026, a line" ),
			"43 6f 70 79 72 69 67 68 74 20 28 63 29 20 32 30 "
			"32 36 2c 20 61 20 6c 69 6e 65 0a" );

		// tChar8String: exactly 8 bytes, NUL padded, longer text is cut
		// b'\0' * 8
		t << Sample( "tChar8String empty", vString( T::tChar8String, "" ), "00 00 00 00 00 00 00 00" );
		// b'ab' + b'\0' * 6
		t << Sample( "tChar8String ab", vString( T::tChar8String, "ab" ), "61 62 00 00 00 00 00 00" );
		// b'abcdefgh'
		t << Sample( "tChar8String abcdefgh (8 characters, no NUL)", vString( T::tChar8String, "abcdefgh" ), "61 62 63 64 65 66 67 68" );
		// b'abcdefghij'[:8]
		t << Sample( "tChar8String abcdefghij (cut to 8)", vString( T::tChar8String, "abcdefghij" ),
			"61 62 63 64 65 66 67 68" )
			.back( vString( T::tChar8String, "abcdefgh" ) );

		// tString: inline like a SizedString below 20.1.0.3, an index into the header string table from there on
		// struct.pack('<I', 0) + b''
		t << Sample( "tString empty, inline", vString( T::tString, "" ), "00 00 00 00" ).back( vString( T::tSizedString, "" ) ).to( kStringIndexFrom - 1 );
		// struct.pack('<I', 2) + b'ab'
		t << Sample( "tString ab, inline", vString( T::tString, "ab" ),
			"02 00 00 00 61 62" )
			.back( vString( T::tSizedString, "ab" ) )
			.to( kStringIndexFrom - 1 );
		// struct.pack('<I', 7)
		t << Sample( "tString index 7, string table", vString( T::tString, "" ),
			"07 00 00 00" )
			.back( vCount( T::tStringIndex, 7 ) )
			.from( kStringIndexFrom )
			.readOnly();
		// struct.pack('<I', 0xffffffff)
		t << Sample( "tString index 0xffffffff, string table", vString( T::tString, "" ),
			"ff ff ff ff" )
			.back( vCount( T::tStringIndex, 0xffffffffu ) )
			.from( kStringIndexFrom )
			.readOnly();

		// tFilePath: same layout as string; NifValue( tFilePath ) holds no text, so only reading is sampled
		// struct.pack('<I', 2) + b'ab'
		t << Sample( "tFilePath ab, inline", NifValue( T::tFilePath ),
			"02 00 00 00 61 62" )
			.back( vString( T::tSizedString, "ab" ) )
			.to( kStringIndexFrom - 1 )
			.readOnly();
		// struct.pack('<I', 0) + b''
		t << Sample( "tFilePath empty, inline", NifValue( T::tFilePath ),
			"00 00 00 00" )
			.back( vString( T::tSizedString, "" ) )
			.to( kStringIndexFrom - 1 )
			.readOnly();
		// struct.pack('<I', 7)
		t << Sample( "tFilePath index 7, string table", NifValue( T::tFilePath ),
			"07 00 00 00" )
			.back( vCount( T::tStringIndex, 7 ) )
			.from( kStringIndexFrom )
			.readOnly();

		// tFileVersion: u32; the trailer byte stands for the Endian Type byte that follows it in a file (read at 20.0.0.4 and up)
		// struct.pack('<I', 0x04000002)
		t << Sample( "tFileVersion 4.0.0.2", vFileVersion( 0x04000002u ), "02 00 00 04" ).trailer( "01" );
		// struct.pack('<I', 0x14000005)
		t << Sample( "tFileVersion 20.0.0.5", vFileVersion( 0x14000005u ), "05 00 00 14" ).trailer( "01" );
		// struct.pack('<I', 0x0a010000)
		t << Sample( "tFileVersion 10.1.0.0", vFileVersion( 0x0a010000u ), "00 00 01 0a" ).trailer( "01" );
		// struct.pack('<I', 0x14020007)
		t << Sample( "tFileVersion 20.2.0.7", vFileVersion( 0x14020007u ), "07 00 02 14" ).trailer( "01" );
		// struct.pack('<I', 0xffffffff)
		t << Sample( "tFileVersion 0xffffffff", vFileVersion( 0xffffffffu ), "ff ff ff ff" ).trailer( "01" );
		// struct.pack('<I', 0x00000000)
		t << Sample( "tFileVersion 0", vFileVersion( 0x00000000u ), "00 00 00 00" ).trailer( "01" );

			return t;
		}();

		return table;
	}

	static void addSampleRows( bool writersOnly )
	{
		QTest::addColumn<int>( "sample" );

		const QList<Sample> & all = samples();
		for ( int i = 0; i < all.count(); i++ ) {
			if ( !writersOnly || all.at( i ).canWrite )
				QTest::newRow( all.at( i ).name ) << i;
		}
	}

	// ---- helpers of the individual tests

	static QByteArray le32( quint32 n )
	{
		QByteArray b( 4, 0 );
		for ( int i = 0; i < 4; i++ )
			b[i] = char( n >> ( 8 * i ) );

		return b;
	}

	//! Binary16 to float from the definition (sign, exponent, mantissa), without half.cpp; every NaN comes back as the quiet NaN
	static float halfValue( quint16 h )
	{
		int e = ( h >> 10 ) & 0x1f;
		int m = h & 0x3ff;
		bool negative = ( h & 0x8000 ) != 0;
		double v;

		if ( e == 0 )
			v = std::ldexp( double( m ), -24 );             // denormal: m / 1024 * 2^-14
		else if ( e == 31 )
			return m ? std::numeric_limits<float>::quiet_NaN() : ( negative ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity() );
		else
			v = std::ldexp( double( 1024 + m ), e - 25 );   // ( 1 + m / 1024 ) * 2^( e - 15 )

		return float( negative ? -v : v );
	}

	static QString hex16( quint16 n ) { return QString( "%1" ).arg( n, 4, 16, QLatin1Char( '0' ) ); }

	//! Non-ASCII text for the string tests. Built from code points: a narrow string literal is not portable
	static QString eAcute() { return QString( "caf" ) + QChar( 0x00e9 ); }
	static QString uUmlaut() { return QString( "Gr" ) + QChar( 0x00fc ) + QString( "n" ); }
	static QString hiragana() { return QString( "a" ) + QChar( 0x3042 ) + QString( "b" ); }

	//! Save a model and load the bytes into another one, the call pattern of tst_NifRoundTrip's helpers
	static bool saveAndLoad( const NifModel & from, NifModel & into )
	{
		QBuffer out;
		out.open( QIODevice::WriteOnly );
		if ( !from.save( out ) )
			return false;

		QBuffer in;
		in.setData( out.data() );
		in.open( QIODevice::ReadOnly );
		return into.load( in );
	}

private slots:
	void initTestCase()
	{
		QString err = TestEnv::reloadXml();
		QVERIFY2( err.isEmpty(), qPrintable( err ) );
	}

	void cleanupTestCase()
	{
		models.clear();
	}

	void cleanup()
	{
		// Nothing in these tests may pop up a QMessageBox
		QStringList boxes = TestEnv::takeMessageBoxes();
		QVERIFY2( boxes.isEmpty(), qPrintable( boxes.join( " | " ) ) );
	}

	//! The table itself: unique names, hex that is hex, every row applies to some context
	void table_wellFormed()
	{
		QRegularExpression pattern( QStringLiteral( "^([0-9a-f]{2}( [0-9a-f]{2})*)?$" ) );
		QSet<QString> names;

		for ( const Sample & s : samples() ) {
			QString row = QString( "row '%1'" ).arg( s.name );

			QVERIFY2( !names.contains( s.name ), qPrintable( row + " is not unique" ) );
			names << s.name;
			QVERIFY2( pattern.match( QString::fromLatin1( s.hex ) ).hasMatch(), qPrintable( row + ": malformed hex" ) );
			QVERIFY2( s.fromVersion <= s.toVersion, qPrintable( row + ": empty version range" ) );

			int applies = 0;
			for ( const TestEnv::Profile & p : contexts() )
				applies += s.appliesTo( versionOf( p ) );

			QVERIFY2( applies > 0, qPrintable( row + " applies to no context" ) );
		}
	}

	//! A new NifValue::Type must get a case in all three streams and a row in the table: the stream refuses types it does not know
	void everyTypeHasACase()
	{
		QSet<int> sampled;
		for ( const Sample & s : samples() )
			sampled << int( s.value.type() );

		auto nif = modelFor( contexts().first() );
		QBuffer buf;
		buf.open( QIODevice::WriteOnly );
		NifOStream os( nif.get(), &buf );

		int known = 0;
		for ( int t = 0; t < 256; t++ ) {
			// A bare NifValue( tFilePath ) has no QString and an uninitialised data pointer (changeType() allocates nothing for it),
			// which NifOStream would follow. The sample table reads into one and nothing else is done with it.
			bool writes = t == NifValue::tFilePath || os.write( NifValue( NifValue::Type( t ) ) );
			known += writes;

			QVERIFY2( writes == sampled.contains( t ),
			          qPrintable( QString( "type %1: NifOStream %2, the sample table %3" ).arg( t ).arg( writes ? "writes it" : "does not know it" ).arg( sampled.contains( t ) ? "has rows" : "has no rows" ) ) );

			// ... and NifIStream does not read it either, and takes nothing for the attempt
			if ( !sampled.contains( t ) ) {
				NifValue unknown( typeOf( t ) );
				QBuffer in;
				in.setData( QByteArray( 8, 'x' ) );
				in.open( QIODevice::ReadOnly );
				QVERIFY2( !NifIStream( nif.get(), &in ).read( unknown ), qPrintable( QString( "type %1: NifIStream read it" ).arg( t ) ) );
				QVERIFY2( in.pos() == 0, qPrintable( QString( "type %1: NifIStream took %2 bytes for it" ).arg( t ).arg( in.pos() ) ) );
				QVERIFY2( NifSStream( nif.get() ).size( unknown ) == 0, qPrintable( QString( "type %1: NifSStream::size has a size for it" ).arg( t ) ) );
			}
		}

		// 43 types from tBool to tBSVertexDesc (tag 17 is unassigned) and tNone
		QCOMPARE( known, 44 );
	}

	void writeBytes_data() { addSampleRows( true ); }

	//! NifOStream writes the bytes computed with python, and NifSStream agrees with the length
	void writeBytes()
	{
		QFETCH( int, sample );
		const Sample & s = samples().at( sample );

		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( !s.appliesTo( versionOf( p ) ) )
				continue;

			checked++;
			QByteArray bytes;
			int size = -1;
			QVERIFY2( streamWrite( s.value, p, bytes, &size ), qPrintable( where( s, p ) + ": NifOStream::write failed" ) );
			QVERIFY2( bytes == s.bytes, qPrintable( where( s, p ) + ": wrote " + hex( bytes ) + ", expected " + hex( s.bytes ) ) );
			QVERIFY2( size == s.bytes.size(), qPrintable( where( s, p ) + QString( ": NifSStream::size says %1" ).arg( size ) ) );
		}

		QVERIFY2( checked > 0, "no context applies" );
	}

	void readBytes_data() { addSampleRows( false ); }

	//! NifIStream turns the bytes computed with python into the value, and takes exactly the bytes of the value
	void readBytes()
	{
		QFETCH( int, sample );
		const Sample & s = samples().at( sample );

		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( !s.appliesTo( versionOf( p ) ) )
				continue;

			checked++;
			NifValue got = s.readTarget();
			qint64 used = -1;
			QVERIFY2( streamRead( got, s.input(), p, used ), qPrintable( where( s, p ) + ": NifIStream::read failed" ) );
			QVERIFY2( used == s.bytes.size(), qPrintable( where( s, p ) + QString( ": read %1 of %2 bytes" ).arg( used ).arg( s.bytes.size() ) ) );
			QVERIFY2( describe( got ) == describe( s.expected() ), qPrintable( where( s, p ) + ": read " + describe( got ) + ", expected " + describe( s.expected() ) ) );
		}

		QVERIFY2( checked > 0, "no context applies" );
	}

	void roundTrip_data() { addSampleRows( true ); }

	//! write -> size -> read: the length NifSStream reports is what was written, and the value comes back
	void roundTrip()
	{
		QFETCH( int, sample );
		const Sample & s = samples().at( sample );

		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( !s.appliesTo( versionOf( p ) ) )
				continue;

			checked++;
			QByteArray bytes;
			int size = -1;
			QVERIFY2( streamWrite( s.value, p, bytes, &size ), qPrintable( where( s, p ) + ": NifOStream::write failed" ) );
			QVERIFY2( size == bytes.size(), qPrintable( where( s, p ) + QString( ": NifSStream::size says %1, NifOStream wrote %2" ).arg( size ).arg( bytes.size() ) ) );

			NifValue got = s.readTarget();
			qint64 used = -1;
			QVERIFY2( streamRead( got, bytes + s.followingBytes(), p, used ), qPrintable( where( s, p ) + ": NifIStream::read failed" ) );
			QVERIFY2( used == bytes.size(), qPrintable( where( s, p ) + QString( ": read %1 of %2 bytes" ).arg( used ).arg( bytes.size() ) ) );
			QVERIFY2( describe( got ) == describe( s.expected() ), qPrintable( where( s, p ) + ": read " + describe( got ) + ", expected " + describe( s.expected() ) ) );
		}

		QVERIFY2( checked > 0, "no context applies" );
	}

	void readTruncated_data()
	{
		QTest::addColumn<int>( "sample" );

		const QList<Sample> & all = samples();
		for ( int i = 0; i < all.count(); i++ ) {
			if ( isFixedWidth( all.at( i ).value.type() ) && !all.at( i ).bytes.isEmpty() )
				QTest::newRow( all.at( i ).name ) << i;
		}
	}

	//! A value of fixed width is not read from fewer bytes
	void readTruncated()
	{
		QFETCH( int, sample );
		const Sample & s = samples().at( sample );

		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( !s.appliesTo( versionOf( p ) ) )
				continue;

			checked++;
			qint64 used;
			NifValue shortRead = s.readTarget();
			QVERIFY2( !streamRead( shortRead, s.bytes.left( s.bytes.size() - 1 ), p, used ), qPrintable( where( s, p ) + ": read succeeded with one byte missing" ) );
			NifValue noRead = s.readTarget();
			QVERIFY2( !streamRead( noRead, QByteArray(), p, used ), qPrintable( where( s, p ) + ": read succeeded without input" ) );
		}

		QVERIFY2( checked > 0, "no context applies" );
	}

	void readNarrow_clearsUpperBits_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QByteArray>( "bytes" );
		QTest::addColumn<quint32>( "count" );

		typedef NifValue T;
		// a Short is read zero extended too: -1 comes back as ffff
		QTest::newRow( "tBool, 8 bits" ) << int( T::tBool ) << QByteArray::fromHex( "01" ) << 1u;
		QTest::newRow( "tByte" ) << int( T::tByte ) << QByteArray::fromHex( "7f" ) << 0x7fu;
		QTest::newRow( "tWord" ) << int( T::tWord ) << QByteArray::fromHex( "ef be" ) << 0xbeefu;
		QTest::newRow( "tFlags" ) << int( T::tFlags ) << QByteArray::fromHex( "34 12" ) << 0x1234u;
		QTest::newRow( "tBlockTypeIndex" ) << int( T::tBlockTypeIndex ) << QByteArray::fromHex( "03 80" ) << 0x8003u;
		QTest::newRow( "tShort" ) << int( T::tShort ) << QByteArray::fromHex( "ff ff" ) << 0xffffu;
	}

	//! A value narrower than 32 bits is read into a target that already holds a wider number: the count is what was read, not what
	//! was there (the model reads into items that hold their old value, and toCount() hands out all 32 bits)
	void readNarrow_clearsUpperBits()
	{
		QFETCH( int, type );
		QFETCH( QByteArray, bytes );
		QFETCH( quint32, count );

		// 20.2.0.7: a bool is one byte
		TestEnv::Profile p{ "", "20.2.0.7", 12, 83 };
		NifValue v = vCount( typeOf( type ), 0xffffffffu );
		qint64 used = -1;
		QVERIFY( streamRead( v, bytes, p, used ) );
		QCOMPARE( int( used ), int( bytes.size() ) );
		QCOMPARE( hex32( v.toCount() ), hex32( count ) );
	}

	void readTooShort_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QByteArray>( "target" );   // tBlob: what the value holds, which says how many bytes to read
		QTest::addColumn<QByteArray>( "input" );

		typedef NifValue T;
		QByteArray index = QByteArray::fromHex( "07 00 00 00" );

		// from 20.1.0.3 a string and a FilePath are a 4 byte index: three bytes are not enough, whatever they say
		for ( int n = 0; n < 4; n++ ) {
			QTest::newRow( qPrintable( QString( "string index from %1 of 4 bytes" ).arg( n ) ) ) << int( T::tString ) << QByteArray() << index.left( n );
			QTest::newRow( qPrintable( QString( "FilePath index from %1 of 4 bytes" ).arg( n ) ) ) << int( T::tFilePath ) << QByteArray() << index.left( n );
		}

		// a blob has as many bytes as the value it is read into
		QByteArray four = QByteArray::fromHex( "aa bb cc dd" );
		for ( int n = 0; n < 4; n++ )
			QTest::newRow( qPrintable( QString( "blob of 4 bytes from %1 bytes" ).arg( n ) ) ) << int( T::tBlob ) << QByteArray( 4, 0 ) << four.left( n );
	}

	//! Input that ends before the value does is refused, also where the reader sizes the value by the bytes of its target
	void readTooShort()
	{
		QFETCH( int, type );
		QFETCH( QByteArray, target );
		QFETCH( QByteArray, input );

		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			// an index is what a string is from 20.1.0.3 on; below it the string is inline and these bytes are a length, not a short index
			if ( ( type == NifValue::tString || type == NifValue::tFilePath ) && versionOf( p ) < kStringIndexFrom )
				continue;

			checked++;
			NifValue v = type == NifValue::tBlob ? vOf<QByteArray>( NifValue::tBlob, target ) : NifValue( typeOf( type ) );
			qint64 used = -1;
			QVERIFY2( !streamRead( v, input, p, used ), qPrintable( QString( "%1: the read succeeded with %2" ).arg( p.name, hex( input ) ) ) );
		}

		QVERIFY2( checked > 0, "no context applies" );
	}

	//! One bit of a half at a time, in every component of the two half vectors: the bytes are the halves, little endian, in the order x y z.
	//! Every bit has to get to its place (a lost sign, mantissa bit or exponent bit shows up at the pattern that has it)
	void halfVector_everyBit()
	{
		TestEnv::Profile p = contexts().first();

		// one bit set at a time, then the largest finite half and the largest denormal (all bits of the mantissa)
		QList<quint16> patterns;
		for ( int bit = 0; bit < 16; bit++ )
			patterns << quint16( 1 << bit );
		patterns << 0x7bff << 0xfbff << 0x03ff << 0x83ff;

		// the halves the other components hold: 2.0, -1.5 and 0.25, so that a component taking its neighbour's bits shows
		const quint16 fillers[3] = { 0x4000, 0xbe00, 0x3400 };

		for ( int components = 2; components <= 3; components++ ) {
			for ( int at = 0; at < components; at++ ) {
				for ( quint16 pattern : patterns ) {
					quint16 halves[3];
					float values[3];
					QByteArray bytes;
					for ( int c = 0; c < components; c++ ) {
						halves[c] = c == at ? pattern : fillers[c];
						values[c] = halfValue( halves[c] );
						bytes.append( char( halves[c] & 0xff ) );
						bytes.append( char( halves[c] >> 8 ) );
					}

					QString what = QString( "HalfVector%1, component %2 holds %3" ).arg( components ).arg( at ).arg( hex16( pattern ) );

					NifValue v = components == 3 ? vOf<HalfVector3>( NifValue::tHalfVector3, HalfVector3( values[0], values[1], values[2] ) )
					                             : vOf<HalfVector2>( NifValue::tHalfVector2, HalfVector2( values[0], values[1] ) );
					QByteArray written;
					QVERIFY2( streamWrite( v, p, written ), qPrintable( what ) );
					QVERIFY2( written == bytes, qPrintable( what + ": wrote " + hex( written ) + ", expected " + hex( bytes ) ) );

					NifValue back( components == 3 ? NifValue::tHalfVector3 : NifValue::tHalfVector2 );
					qint64 used = -1;
					QVERIFY2( streamRead( back, bytes, p, used ), qPrintable( what ) );
					QVERIFY2( used == bytes.size(), qPrintable( what ) );

					QVERIFY2( describe( back ) == describe( v ), qPrintable( what + ": read " + describe( back ) + ", expected " + describe( v ) ) );
				}
			}
		}
	}

	//! A KfmModel is not a NIF: its streams leave out the rules that depend on a NIF header version. KFM 2.0.0.0b would otherwise count as
	//! "up to 4.0.0.2" and "below 3.3.0.13": 32 bit bools, links stored plus one
	void kfmModel_streamsIgnoreNifVersionRules()
	{
		KfmModel kfm;
		QCOMPARE( hex32( kfm.getVersionNumber() ), hex32( 0x0200000b ) );

		// a bool is a byte and a link the number itself
		QByteArray input = QByteArray::fromHex( "01 02 03 04 05 00 00 00" );
		{
			QBuffer buf;
			buf.setData( input );
			buf.open( QIODevice::ReadOnly );
			NifIStream is( &kfm, &buf );

			NifValue b( NifValue::tBool );
			QVERIFY( is.read( b ) );
			QCOMPARE( int( buf.pos() ), 1 );
			QCOMPARE( int( b.toCount() ), 1 );

			NifValue skip( NifValue::tFlags );
			QVERIFY( is.read( skip ) );		// 02 03
			NifValue skip2( NifValue::tByte );
			QVERIFY( is.read( skip2 ) );		// 04

			NifValue link( NifValue::tLink );
			QVERIFY( is.read( link ) );
			QCOMPARE( link.toLink(), 5 );
		}

		QByteArray written;
		{
			QBuffer buf;
			buf.open( QIODevice::WriteOnly );
			NifOStream os( &kfm, &buf );
			QVERIFY( os.write( vCount( NifValue::tBool, 1 ) ) );
			QVERIFY( os.write( vLink( NifValue::tLink, 5 ) ) );
			written = buf.data();
		}
		QCOMPARE( hex( written ), hex( QByteArray::fromHex( "01 05 00 00 00" ) ) );

		QCOMPARE( NifSStream( &kfm ).size( vCount( NifValue::tBool, 1 ) ), 1 );
		QCOMPARE( NifSStream( &kfm ).size( vLink( NifValue::tLink, 5 ) ), 4 );
	}

	//! From 20.1.0.3 a string is stored as an index into the header's string table, so the stream writes 4 bytes whatever the text is.
	//! (The model replaces the value by a tStringIndex when the item is inserted, so only a bare NifValue reaches this branch.)
	void string_indexForm()
	{
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( versionOf( p ) < kStringIndexFrom )
				continue;

			QByteArray bytes;
			int size = -1;
			QVERIFY2( streamWrite( vString( NifValue::tString, "ab" ), p, bytes, &size ), p.name );
			QVERIFY2( bytes.size() == 4, qPrintable( QString( "%1: wrote %2" ).arg( p.name, hex( bytes ) ) ) );
			QVERIFY2( size == 4, qPrintable( QString( "%1: NifSStream::size says %2" ).arg( p.name ).arg( size ) ) );

			// a bare FilePath can be written here too: its value is a number (below 20.1.0.3 it would be text, and has none, hence readOnly() rows)
			QByteArray bare;
			size = -1;
			QVERIFY2( streamWrite( NifValue( NifValue::tFilePath ), p, bare, &size ), qPrintable( QString( "%1: the write of a bare FilePath failed" ).arg( p.name ) ) );
			QVERIFY2( bare.size() == 4 && size == 4, qPrintable( QString( "%1: wrote %2, NifSStream::size says %3" ).arg( p.name, hex( bare ) ).arg( size ) ) );
		}
	}


	void hfloat_conversion_data()
	{
		QTest::addColumn<quint32>( "floatBits" );
		QTest::addColumn<quint16>( "half" );
		QTest::addColumn<quint32>( "backBits" );

		// float -> half, then the half -> float of that half. Computed with python3 struct ('<e'); the expression is on each row.
		QTest::newRow( "0" ) << 0x00000000u << quint16( 0x0000 ) << 0x00000000u;	// struct.pack('<e', 0.0)
		QTest::newRow( "-0" ) << 0x80000000u << quint16( 0x8000 ) << 0x80000000u;	// struct.pack('<e', -0.0)
		QTest::newRow( "1" ) << 0x3f800000u << quint16( 0x3c00 ) << 0x3f800000u;	// struct.pack('<e', 1.0)
		QTest::newRow( "-1" ) << 0xbf800000u << quint16( 0xbc00 ) << 0xbf800000u;	// struct.pack('<e', -1.0)
		QTest::newRow( "0.5" ) << 0x3f000000u << quint16( 0x3800 ) << 0x3f000000u;	// struct.pack('<e', 0.5)
		QTest::newRow( "2" ) << 0x40000000u << quint16( 0x4000 ) << 0x40000000u;	// struct.pack('<e', 2.0)
		QTest::newRow( "0.1" ) << 0x3dcccccdu << quint16( 0x2e66 ) << 0x3dccc000u;	// struct.pack('<e', 0.1)
		QTest::newRow( "0.333333" ) << 0x3eaaaa9fu << quint16( 0x3555 ) << 0x3eaaa000u;	// struct.pack('<e', 0.333333)
		QTest::newRow( "3.14159" ) << 0x40490fd0u << quint16( 0x4248 ) << 0x40490000u;	// struct.pack('<e', 3.14159)
		QTest::newRow( "8.5" ) << 0x41080000u << quint16( 0x4840 ) << 0x41080000u;	// struct.pack('<e', 8.5)
		QTest::newRow( "100" ) << 0x42c80000u << quint16( 0x5640 ) << 0x42c80000u;	// struct.pack('<e', 100.0)
		QTest::newRow( "1000" ) << 0x447a0000u << quint16( 0x63d0 ) << 0x447a0000u;	// struct.pack('<e', 1000.0)
		QTest::newRow( "1e-4" ) << 0x38d1b717u << quint16( 0x068e ) << 0x38d1c000u;	// struct.pack('<e', 0.0001)
		QTest::newRow( "2^-14 (smallest normal half)" ) << 0x38800000u << quint16( 0x0400 ) << 0x38800000u;	// struct.pack('<e', 6.103515625e-05)
		QTest::newRow( "1023 * 2^-24 (largest denormal half)" ) << 0x387fc000u << quint16( 0x03ff ) << 0x387fc000u;	// struct.pack('<e', 6.097555160522461e-05)
		QTest::newRow( "2^-24 (smallest denormal half)" ) << 0x33800000u << quint16( 0x0001 ) << 0x33800000u;	// struct.pack('<e', 5.960464477539063e-08)
		QTest::newRow( "1e-8 (underflows to 0)" ) << 0x322bcc77u << quint16( 0x0000 ) << 0x00000000u;	// struct.pack('<e', 1e-08)
		QTest::newRow( "-1e-8 (underflows to -0)" ) << 0xb22bcc77u << quint16( 0x8000 ) << 0x80000000u;	// struct.pack('<e', -1e-08)
		QTest::newRow( "65504 (largest half)" ) << 0x477fe000u << quint16( 0x7bff ) << 0x477fe000u;	// struct.pack('<e', 65504.0)
		QTest::newRow( "-65504" ) << 0xc77fe000u << quint16( 0xfbff ) << 0xc77fe000u;	// struct.pack('<e', -65504.0)
		QTest::newRow( "65519 (below the halfway point to 65536)" ) << 0x477fef00u << quint16( 0x7bff ) << 0x477fe000u;	// struct.pack('<e', 65519.0)
		QTest::newRow( "65520 (the halfway point rounds up to infinity)" ) << 0x477ff000u << quint16( 0x7c00 ) << 0x7f800000u;	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "65536" ) << 0x47800000u << quint16( 0x7c00 ) << 0x7f800000u;	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "1e10" ) << 0x501502f9u << quint16( 0x7c00 ) << 0x7f800000u;	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "FLT_MAX" ) << 0x7f7fffffu << quint16( 0x7c00 ) << 0x7f800000u;	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "+inf" ) << 0x7f800000u << quint16( 0x7c00 ) << 0x7f800000u;	// struct.pack('<e', inf)
		QTest::newRow( "-inf" ) << 0xff800000u << quint16( 0xfc00 ) << 0xff800000u;	// struct.pack('<e', -inf)
		QTest::newRow( "quiet NaN" ) << 0x7fc00000u << quint16( 0x7e00 ) << 0x7fc00000u;	// struct.pack('<e', nan)
	}

	//! half.cpp: the float -> half conversion rounds to the nearest half, the half -> float conversion is exact
	void hfloat_conversion()
	{
		QFETCH( quint32, floatBits );
		QFETCH( quint16, half );
		QFETCH( quint32, backBits );

		QCOMPARE( hex16( half_from_float( floatBits ) ), hex16( half ) );
		QCOMPARE( hex32( half_to_float( half ) ), hex32( backBits ) );
	}

	void hfloat_overflow_data()
	{
		QTest::addColumn<quint32>( "floatBits" );
		QTest::addColumn<quint16>( "half" );

		QTest::newRow( "66000" ) << 0x4780e800u << quint16( 0x7c00 );	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "70000" ) << 0x4788b800u << quint16( 0x7c00 );	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "100000" ) << 0x47c35000u << quint16( 0x7c00 );	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "131071" ) << 0x47ffff80u << quint16( 0x7c00 );	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "-70000" ) << 0xc788b800u << quint16( 0xfc00 );	// IEEE 754: rounds to infinity (python raises OverflowError)
		QTest::newRow( "-131071" ) << 0xc7ffff80u << quint16( 0xfc00 );	// IEEE 754: rounds to infinity (python raises OverflowError)
	}

	//! A float that is too large for a half is infinity. half_from_float() turns the floats from 65568 (2^16 + 32) up to just below 2^17
	//! into NaNs (131071 into -0): its overflow test uses 0x1f where the largest biased exponent of a finite half is 0x1e.
	void hfloat_overflow()
	{
		QFETCH( quint32, floatBits );
		QFETCH( quint16, half );

		QEXPECT_FAIL( "", "half_from_float() overflows to NaN from 65568 up to just below 131072 (lib/half.cpp, h_e_mask_value is 0x1f); fix then remove", Continue );
		QCOMPARE( hex16( half_from_float( floatBits ) ), hex16( half ) );
	}

	//! Every one of the 65536 halves: decode against the definition, encode what was decoded, and round the values around each midpoint
	void hfloat_allHalves()
	{
		for ( quint32 i = 0; i < 0x10000; i++ ) {
			quint16 h = quint16( i );
			float want = halfValue( h );
			float got = fromBits( half_to_float( h ) );

			if ( std::isnan( want ) ) {
				if ( !std::isnan( got ) )
					QFAIL( qPrintable( QString( "half %1 is a NaN but decodes to %2" ).arg( hex16( h ), f( got ) ) ) );

				// a quiet NaN stays a NaN (hfloat_signallingNaN has the others)
				quint16 again = half_from_float( toBits( got ) );
				if ( ( h & 0x0200 ) && ( ( again & 0x7c00 ) != 0x7c00 || !( again & 0x03ff ) ) )
					QFAIL( qPrintable( QString( "the NaN half %1 encodes to %2" ).arg( hex16( h ), hex16( again ) ) ) );

				continue;
			}

			if ( toBits( got ) != toBits( want ) )
				QFAIL( qPrintable( QString( "half %1 decodes to %2, expected %3" ).arg( hex16( h ), f( got ), f( want ) ) ) );

			quint16 back = half_from_float( toBits( want ) );
			if ( back != h )
				QFAIL( qPrintable( QString( "half %1 (%2) encodes to %3" ).arg( hex16( h ), f( want ), hex16( back ) ) ) );
		}

		// Between two neighbouring halves a float rounds to the nearer one: just below the midpoint to the lower half, just above it to the upper
		// (the midpoint itself is left out: half.cpp rounds it away from zero, IEEE 754 to even). Both signs.
		// Above the midpoint only from the normal numbers on, hfloat_denormalRounding has the denormals.
		for ( quint32 i = 0; i < 0x7bff; i++ ) {
			float lo = halfValue( quint16( i ) );
			float hi = halfValue( quint16( i + 1 ) );
			float mid = float( ( double( lo ) + double( hi ) ) / 2.0 );     // 12 significant bits, exact in a float
			float above = std::nextafter( mid, hi );
			float below = std::nextafter( mid, lo );

			quint16 up = half_from_float( toBits( above ) );
			quint16 down = half_from_float( toBits( below ) );
			quint16 upNeg = half_from_float( toBits( -above ) );
			quint16 downNeg = half_from_float( toBits( -below ) );

			bool upOk = up == quint16( i + 1 ) && upNeg == quint16( ( i + 1 ) | 0x8000 );
			bool downOk = down == quint16( i ) && downNeg == quint16( i | 0x8000 );
			if ( !downOk || ( !upOk && i >= 0x0400 ) )
				QFAIL( qPrintable( QString( "around %1 (between halves %2 and %3): above gives %4, below %5, negated %6 / %7" )
				                   .arg( f( mid ), hex16( quint16( i ) ), hex16( quint16( i + 1 ) ), hex16( up ), hex16( down ), hex16( upNeg ), hex16( downNeg ) ) ) );
		}

		// the last one: halfway between the largest half and 2^16 (infinity)
		QCOMPARE( hex16( half_from_float( toBits( std::nextafter( 65520.0f, 0.0f ) ) ) ), hex16( 0x7bff ) );
		QCOMPARE( hex16( half_from_float( toBits( std::nextafter( 65520.0f, 70000.0f ) ) ) ), hex16( 0x7c00 ) );
	}

	void hfloat_denormalRounding_data()
	{
		QTest::addColumn<quint32>( "floatBits" );
		QTest::addColumn<quint16>( "half" );

		// Values in the range of the denormal halves (below 2^-14), each nearer to the next half up than to the one below it
		QTest::newRow( "1e-5" ) << 0x3727c5acu << quint16( 0x00a8 );	// struct.pack('<e', 1e-05)
		QTest::newRow( "-1e-5" ) << 0xb727c5acu << quint16( 0x80a8 );	// struct.pack('<e', -1e-05)
		QTest::newRow( "1.5e-7" ) << 0x34210fb0u << quint16( 0x0003 );	// struct.pack('<e', 1.5e-07)
		QTest::newRow( "6e-5" ) << 0x387ba882u << quint16( 0x03ef );	// struct.pack('<e', 6e-05)
	}

	//! A float in the denormal range of the half rounds to the nearest half. half_from_float() cuts the extra bits off instead:
	//! 1e-5 is 167.77 times 2^-24 and becomes 167, not 168.
	void hfloat_denormalRounding()
	{
		QFETCH( quint32, floatBits );
		QFETCH( quint16, half );

		QEXPECT_FAIL( "", "half_from_float() truncates floats in the denormal range of the half instead of rounding them (lib/half.cpp); fix then remove", Continue );
		QCOMPARE( hex16( half_from_float( floatBits ) ), hex16( half ) );
	}

	//! The same for all 1024 denormal halves: the float just above the midpoint to the next half must give that half
	void hfloat_denormalRounding_all()
	{
		int wrong = 0;
		for ( quint32 i = 0; i < 0x0400; i++ ) {
			float lo = halfValue( quint16( i ) );
			float hi = halfValue( quint16( i + 1 ) );
			float above = std::nextafter( float( ( double( lo ) + double( hi ) ) / 2.0 ), hi );
			wrong += half_from_float( toBits( above ) ) != quint16( i + 1 );
		}

		QEXPECT_FAIL( "", "half_from_float() truncates floats in the denormal range of the half instead of rounding them (lib/half.cpp); fix then remove", Continue );
		QCOMPARE( wrong, 0 );
	}

	void hfloat_signallingNaN_data()
	{
		QTest::addColumn<quint32>( "floatBits" );

		QTest::newRow( "smallest signalling NaN" ) << 0x7f800001u;
		QTest::newRow( "payload 0x2000" ) << 0x7f802000u;
		QTest::newRow( "payload 0x200000" ) << 0x7fa00000u;
		QTest::newRow( "negative" ) << 0xff800001u;
	}

	//! A NaN is a NaN whatever its payload. half_from_float() turns the NaNs without the quiet bit (a float mantissa below 0x400000)
	//! into infinity, and so does every signalling NaN half that went through half_to_float().
	void hfloat_signallingNaN()
	{
		QFETCH( quint32, floatBits );

		quint16 h = half_from_float( floatBits );

		QEXPECT_FAIL( "", "half_from_float() turns a NaN without the quiet bit into infinity (lib/half.cpp); fix then remove", Continue );
		QVERIFY2( ( h & 0x7c00 ) == 0x7c00 && ( h & 0x03ff ), qPrintable( QString( "%1 became %2" ).arg( hex32( floatBits ), hex16( h ) ) ) );
	}

	//! ByteVector3 holds 256 values per component: the byte k stands for k / 255 * 2 - 1, and that value is written back as k
	void quantised_byteVector3()
	{
		auto nif = modelFor( contexts().first() );

		for ( int k = 0; k < 256; k++ ) {
			// a different byte in every component
			int bytesIn[3] = { k, 255 - k, ( k * 7 ) % 256 };
			float value[3];
			for ( int c = 0; c < 3; c++ )
				value[c] = float( bytesIn[c] / 255.0 * 2.0 - 1.0 );

			QBuffer out;
			out.open( QIODevice::WriteOnly );
			NifOStream os( nif.get(), &out );
			QVERIFY( os.write( vOf<ByteVector3>( NifValue::tByteVector3, ByteVector3( value[0], value[1], value[2] ) ) ) );

			QByteArray want;
			for ( int c = 0; c < 3; c++ )
				want.append( char( bytesIn[c] ) );
			QVERIFY2( out.data() == want, qPrintable( QString( "k = %1: wrote %2, expected %3" ).arg( k ).arg( hex( out.data() ), hex( want ) ) ) );

			QBuffer in;
			in.setData( want );
			in.open( QIODevice::ReadOnly );
			NifIStream is( nif.get(), &in );
			NifValue v( NifValue::tByteVector3 );
			QVERIFY( is.read( v ) );
			ByteVector3 got = v.get<ByteVector3>();
			QVERIFY2( toBits( got[0] ) == toBits( value[0] ) && toBits( got[1] ) == toBits( value[1] ) && toBits( got[2] ) == toBits( value[2] ),
			          qPrintable( QString( "k = %1: read %2, expected %3 %4 %5" ).arg( k ).arg( describe( v ), f( value[0] ), f( value[1] ), f( value[2] ) ) ) );
		}
	}

	//! ByteColor4: the byte k stands for k / 255, and that value is written back as k
	void quantised_byteColor4()
	{
		auto nif = modelFor( contexts().first() );

		for ( int k = 0; k < 256; k++ ) {
			int bytesIn[4] = { k, 255 - k, ( k * 7 ) % 256, ( k * 31 + 5 ) % 256 };
			float value[4];
			for ( int c = 0; c < 4; c++ )
				value[c] = float( bytesIn[c] / 255.0 );

			QBuffer out;
			out.open( QIODevice::WriteOnly );
			NifOStream os( nif.get(), &out );
			QVERIFY( os.write( vByteColor( value[0], value[1], value[2], value[3] ) ) );

			QByteArray want;
			for ( int c = 0; c < 4; c++ )
				want.append( char( bytesIn[c] ) );
			QVERIFY2( out.data() == want, qPrintable( QString( "k = %1: wrote %2, expected %3" ).arg( k ).arg( hex( out.data() ), hex( want ) ) ) );

			QBuffer in;
			in.setData( want );
			in.open( QIODevice::ReadOnly );
			NifIStream is( nif.get(), &in );
			NifValue v( NifValue::tByteColor4 );
			QVERIFY( is.read( v ) );
			ByteColor4 got = v.get<ByteColor4>();
			QVERIFY2( toBits( got[0] ) == toBits( value[0] ) && toBits( got[1] ) == toBits( value[1] ) && toBits( got[2] ) == toBits( value[2] ) && toBits( got[3] ) == toBits( value[3] ),
			          qPrintable( QString( "k = %1: read %2, expected %3 %4 %5 %6" ).arg( k ).arg( describe( v ), f( value[0] ), f( value[1] ), f( value[2] ), f( value[3] ) ) ) );
		}
	}

	void stringLimits_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QByteArray>( "input" );
		QTest::addColumn<bool>( "ok" );
		QTest::addColumn<QString>( "text" );   // what the value is when ok
		QTest::addColumn<int>( "used" );       // how many bytes were taken when ok
		QTest::addColumn<QString>( "issue" );  // not empty: the reader gets this row wrong

		typedef NifValue T;
		const int sized = 0x8000;              // NifIStream::maxLength

		auto x = []( int n ) { return QByteArray( n, 'x' ); };
		auto xs = []( int n ) { return QString( n, QLatin1Char( 'x' ) ); };

		// length prefix, then the bytes; the reader refuses more than 0x8000 or less than 0, and fewer bytes than announced
		for ( int type : { int( T::tSizedString ), int( T::tText ), int( T::tString ), int( T::tFilePath ) } ) {
			QString name = type == T::tSizedString ? "SizedString" : ( type == T::tText ? "Text" : ( type == T::tString ? "string below 20.1.0.3" : "FilePath below 20.1.0.3" ) );

			QTest::newRow( qPrintable( name + " empty" ) ) << type << le32( 0 ) << true << QString() << 4 << QString();
			QTest::newRow( qPrintable( name + " 255 bytes" ) ) << type << le32( 255 ) + x( 255 ) << true << xs( 255 ) << 4 + 255 << QString();
			QTest::newRow( qPrintable( name + " 256 bytes (the length needs its second byte)" ) ) << type << le32( 256 ) + x( 256 ) << true << xs( 256 ) << 4 + 256 << QString();
			QTest::newRow( qPrintable( name + " 0x8000 bytes (the longest)" ) ) << type << le32( sized ) + x( sized ) << true << xs( sized ) << 4 + sized << QString();
			QTest::newRow( qPrintable( name + " 0x8001 bytes" ) ) << type << le32( sized + 1 ) + x( sized + 1 ) << false << QString() << 0 << QString();
			QTest::newRow( qPrintable( name + " length -1" ) ) << type << le32( 0xffffffffu ) + x( 8 ) << false << QString() << 0 << QString();
			QTest::newRow( qPrintable( name + " 5 announced, 2 present" ) ) << type << le32( 5 ) + QByteArray( "ab" ) << false << QString() << 0 << QString();
		}

		// the text of a string ends at a NUL in it, and the bytes after the NUL are taken all the same; blanks around the text are part of it
		for ( int type : { int( T::tSizedString ), int( T::tText ), int( T::tString ), int( T::tFilePath ) } ) {
			QString name = type == T::tSizedString ? "SizedString" : ( type == T::tText ? "Text" : ( type == T::tString ? "string below 20.1.0.3" : "FilePath below 20.1.0.3" ) );

			QTest::newRow( qPrintable( name + " with a NUL inside" ) ) << type << le32( 5 ) + QByteArray( "ab\0cd", 5 ) << true << QString( "ab" ) << 4 + 5 << QString();
			QTest::newRow( qPrintable( name + " with blanks around the text" ) ) << type << le32( 6 ) + QByteArray( "  ab  " ) << true << QString( "  ab  " ) << 4 + 6 << QString();
		}

		QTest::newRow( "LineString with a NUL inside" ) << int( T::tLineString ) << QByteArray( "ab\0cd\n", 6 ) << true << QString( "ab" ) << 6 << QString();
		QTest::newRow( "HeaderString with a NUL inside" ) << int( T::tHeaderString ) << QByteArray( "NetImmerse File Format, Version 4.0.0.2" ) + QByteArray( "\0junk\n", 6 ) << true << QString( "NetImmerse File Format, Version 4.0.0.2" ) << 39 + 6 << QString();

		// byte array: no upper limit but memory
		QTest::newRow( "ByteArray 3 bytes" ) << int( T::tByteArray ) << le32( 3 ) + QByteArray( "abc" ) << true << QString( "abc" ) << 7 << QString();
		QTest::newRow( "ByteArray length -1" ) << int( T::tByteArray ) << le32( 0xffffffffu ) + x( 8 ) << false << QString() << 0 << QString();
		QTest::newRow( "ByteArray 10 announced, 3 present" ) << int( T::tByteArray ) << le32( 10 ) + QByteArray( "abc" ) << false << QString() << 0 << QString();

		// palette: at most 0xffff bytes, and the length again behind them
		QTest::newRow( "StringPalette 0xffff bytes (the longest)" ) << int( T::tStringPalette ) << le32( 0xffff ) + x( 0xffff ) + le32( 0xffff ) << true << xs( 0xffff ) << 4 + 0xffff + 4 << QString();
		QTest::newRow( "StringPalette 0x10000 bytes" ) << int( T::tStringPalette ) << le32( 0x10000 ) + x( 0x10000 ) + le32( 0x10000 ) << false << QString() << 0 << QString();
		QTest::newRow( "StringPalette length -1" ) << int( T::tStringPalette ) << le32( 0xffffffffu ) + x( 8 ) << false << QString() << 0 << QString();
		QTest::newRow( "StringPalette 5 announced, 2 present" ) << int( T::tStringPalette ) << le32( 5 ) + QByteArray( "ab" ) << false << QString() << 0
			<< "NifIStream does not check that a StringPalette has all the bytes it announces (nifstream.cpp); fix then remove";

		QTest::newRow( "ByteMatrix negative width" ) << int( T::tByteMatrix ) << le32( 0xffffffffu ) + le32( 2 ) + x( 8 ) << false << QString() << 0 << QString();
		QTest::newRow( "ByteMatrix negative height" ) << int( T::tByteMatrix ) << le32( 2 ) + le32( 0xffffffffu ) + x( 8 ) << false << QString() << 0 << QString();
		// both negative: the product is a valid size, only the check on each of them refuses it
		QTest::newRow( "ByteMatrix negative width and height" ) << int( T::tByteMatrix ) << le32( 0xfffffffeu ) + le32( 0xfffffffdu ) + x( 6 ) << false << QString() << 0 << QString();
		// one of them negative, the product is 0 and no negative size: only the check on each of them refuses it
		QTest::newRow( "ByteMatrix negative width, no height" ) << int( T::tByteMatrix ) << le32( 0xffffffffu ) + le32( 0 ) + x( 8 ) << false << QString() << 0 << QString();
		QTest::newRow( "ByteMatrix no width, negative height" ) << int( T::tByteMatrix ) << le32( 0 ) + le32( 0xffffffffu ) + x( 8 ) << false << QString() << 0 << QString();
		QTest::newRow( "ByteMatrix 2x2 announced, 3 bytes present" ) << int( T::tByteMatrix ) << le32( 2 ) + le32( 2 ) + QByteArray( "abc" ) << false << QString() << 0 << QString();

		// one byte length, counting the NUL
		QTest::newRow( "ShortString empty without NUL" ) << int( T::tShortString ) << QByteArray( 1, 0 ) << true << QString() << 1 << QString();
		QTest::newRow( "ShortString 254 characters (the longest)" ) << int( T::tShortString ) << QByteArray( 1, char( 255 ) ) + x( 254 ) + QByteArray( 1, 0 ) << true << xs( 254 ) << 256 << QString();
		QTest::newRow( "ShortString 5 announced, 2 present" ) << int( T::tShortString ) << QByteArray( 1, 5 ) + QByteArray( "ab" ) << false << QString() << 0 << QString();

		// lines end at a line feed; the reader counts the characters it has read
		QString header = "NetImmerse File Format, Version 4.0.0.2";
		QTest::newRow( "HeaderString 78 characters (the longest)" ) << int( T::tHeaderString ) << ( header + xs( 78 - header.length() ) ).toLatin1() + "\n" << true << ( header + xs( 78 - header.length() ) ) << 79 << QString();
		QTest::newRow( "HeaderString 79 characters" ) << int( T::tHeaderString ) << ( header + xs( 79 - header.length() ) ).toLatin1() + "\n" << false << QString() << 0 << QString();
		QTest::newRow( "LineString 253 characters (the longest)" ) << int( T::tLineString ) << x( 253 ) + "\n" << true << xs( 253 ) << 254 << QString();
		QTest::newRow( "LineString 254 characters" ) << int( T::tLineString ) << x( 254 ) + "\n" << false << QString() << 0 << QString();
		// only a line feed ends a line: a carriage return is a character like any other
		QTest::newRow( "LineString with a carriage return inside" ) << int( T::tLineString ) << QByteArray( "ab\rcd\n" ) << true << QString( "ab\rcd" ) << 6 << QString();
		QTest::newRow( "HeaderString with a carriage return inside" ) << int( T::tHeaderString ) << ( header + " a\rb" ).toLatin1() + "\n" << true << ( header + " a\rb" ) << header.length() + 5 << QString();

		// always eight bytes, whatever follows
		QTest::newRow( "Char8String 8 characters" ) << int( T::tChar8String ) << x( 8 ) << true << xs( 8 ) << 8 << QString();
		QTest::newRow( "Char8String reads 8 of 10" ) << int( T::tChar8String ) << x( 10 ) << true << xs( 8 ) << 8 << QString();
	}

	//! The limits of the readers and what they do with input that is too short
	void stringLimits()
	{
		QFETCH( int, type );
		QFETCH( QByteArray, input );
		QFETCH( bool, ok );
		QFETCH( QString, text );
		QFETCH( int, used );
		QFETCH( QString, issue );

		// 20.0.0.5: a string is inline there
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		NifValue v( typeOf( type ) );
		qint64 taken = -1;
		bool read = streamRead( v, input, p, taken );

		if ( !issue.isEmpty() )
			QEXPECT_FAIL( "", qPrintable( issue ), Continue );
		QVERIFY2( read == ok, read ? "the reader accepted it" : "the reader refused it" );
		if ( !ok )
			return;

		QVERIFY2( taken == used, qPrintable( QString( "took %1 bytes, expected %2" ).arg( taken ).arg( used ) ) );

		// the values are up to 64 KiB long: do not let a failure print them
		QString got = v.isByteArray() ? QString::fromLatin1( v.get<QByteArray>() ) : v.get<QString>();
		QVERIFY2( got == text, qPrintable( QString( "value has %1 characters, expected %2" ).arg( got.length() ).arg( text.length() ) ) );
	}

	void shortString_data()
	{
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<QByteArray>( "bytes" );   // what is written
		QTest::addColumn<QString>( "back" );       // what reading those bytes gives
		QTest::addColumn<bool>( "sizeAgrees" );    // NifSStream::size() is the length written

		auto x = []( int n ) { return QByteArray( n, 'x' ); };

		QTest::newRow( "empty" ) << QString() << QByteArray::fromHex( "01 00" ) << QString() << true;
		QTest::newRow( "ab" ) << QString( "ab" ) << QByteArray::fromHex( "03 61 62 00" ) << QString( "ab" ) << true;
		QTest::newRow( "253 characters" ) << QString( 253, QLatin1Char( 'x' ) ) << QByteArray( 1, char( 254 ) ) + x( 253 ) + QByteArray( 1, 0 ) << QString( 253, QLatin1Char( 'x' ) ) << true;
		QTest::newRow( "254 characters (the longest)" ) << QString( 254, QLatin1Char( 'x' ) ) << QByteArray( 1, char( 255 ) ) + x( 254 ) + QByteArray( 1, 0 ) << QString( 254, QLatin1Char( 'x' ) ) << true;
		// the length is one byte: a longer text is cut without a message
		QTest::newRow( "255 characters are cut to 254" ) << QString( 255, QLatin1Char( 'x' ) ) << QByteArray( 1, char( 255 ) ) + x( 254 ) + QByteArray( 1, 0 ) << QString( 254, QLatin1Char( 'x' ) ) << true;
		QTest::newRow( "300 characters are cut to 254" ) << QString( 300, QLatin1Char( 'x' ) ) << QByteArray( 1, char( 255 ) ) + x( 254 ) + QByteArray( 1, 0 ) << QString( 254, QLatin1Char( 'x' ) ) << true;
		// the writer turns the two characters backslash n / backslash r into a line feed / carriage return; the reader keeps what it finds
		QTest::newRow( "backslash n becomes a line feed" ) << QString( "a\\nb" ) << QByteArray::fromHex( "04 61 0a 62 00" ) << QString( "a\nb" ) << false;
		QTest::newRow( "backslash r becomes a carriage return" ) << QString( "a\\rb" ) << QByteArray::fromHex( "04 61 0d 62 00" ) << QString( "a\rb" ) << false;
		QTest::newRow( "a real line feed stays" ) << QString( "a\nb" ) << QByteArray::fromHex( "04 61 0a 62 00" ) << QString( "a\nb" ) << true;
	}

	//! ShortString: length including the NUL, at most 254 characters, backslash n and r written as the control characters
	void shortString()
	{
		QFETCH( QString, text );
		QFETCH( QByteArray, bytes );
		QFETCH( QString, back );
		QFETCH( bool, sizeAgrees );

		for ( const TestEnv::Profile & p : contexts() ) {
			QByteArray written;
			int size = -1;
			QVERIFY2( streamWrite( vOf<QString>( NifValue::tShortString, text ), p, written, &size ), p.name );
			QVERIFY2( written == bytes, qPrintable( QString( "%1: wrote %2, expected %3" ).arg( p.name, hex( written ), hex( bytes ) ) ) );
			if ( sizeAgrees )
				QVERIFY2( size == written.size(), qPrintable( QString( "%1: NifSStream::size says %2" ).arg( p.name ).arg( size ) ) );

			NifValue v( NifValue::tShortString );
			qint64 used = -1;
			QVERIFY2( streamRead( v, bytes, p, used ), p.name );
			QVERIFY2( used == bytes.size(), p.name );
			QVERIFY2( v.get<QString>() == back, qPrintable( QString( "%1: read %2, expected %3" ).arg( p.name, show( v.get<QString>() ), show( back ) ) ) );
		}
	}

	void shortString_sizeMatchesWrite_data()
	{
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<bool>( "disagrees" );

		// NifOStream writes toLocal8Bit() and replaces \n and \r, NifSStream::size() uses toLatin1() and replaces nothing.
		// The two give the same length for a text the local codec stores in one byte per character (a Latin-1 host), so only
		// a text where they differ is expected to fail.
		auto lengthsDiffer = []( const QString & s ) { return s.toLocal8Bit().size() != s.toLatin1().size(); };

		QTest::newRow( "plain text" ) << QString( "plain text" ) << false;
		QTest::newRow( "backslash n" ) << QString( "a\\nb" ) << true;
		QTest::newRow( "backslash r" ) << QString( "a\\rb" ) << true;
		QTest::newRow( "e-acute" ) << eAcute() << lengthsDiffer( eAcute() );
		QTest::newRow( "hiragana" ) << hiragana() << lengthsDiffer( hiragana() );
	}

	//! NifSStream::size() has to be the number of bytes NifOStream writes: block sizes and the string table are computed from it
	void shortString_sizeMatchesWrite()
	{
		QFETCH( QString, text );
		QFETCH( bool, disagrees );

		TestEnv::Profile p{ "", "20.2.0.7", 12, 83 };
		QByteArray bytes;
		int size = -1;
		QVERIFY( streamWrite( vOf<QString>( NifValue::tShortString, text ), p, bytes, &size ) );

		if ( disagrees )
			QEXPECT_FAIL( "", "NifSStream::size() sizes a ShortString with toLatin1() and no \\n \\r replacement, NifOStream writes toLocal8Bit() with it (nifstream.cpp); fix then remove", Continue );
		QCOMPARE( size, int( bytes.size() ) );
	}

	void nonAscii_shortString_data()
	{
		QTest::addColumn<QString>( "text" );

		QTest::newRow( "e-acute" ) << eAcute();
		QTest::newRow( "u-umlaut" ) << uUmlaut();
		QTest::newRow( "hiragana" ) << hiragana();
	}

	//! ShortString uses the local 8-bit codec in both directions (toLocal8Bit() / fromLocal8Bit()), so a text that codec can hold comes back
	void nonAscii_shortString()
	{
		QFETCH( QString, text );

		QByteArray local = text.toLocal8Bit();
		if ( QString::fromLocal8Bit( local ) != text )
			QSKIP( "the local 8-bit codec of this machine cannot hold the text" );

		TestEnv::Profile p{ "", "20.2.0.7", 12, 83 };
		QByteArray bytes;
		QVERIFY( streamWrite( vOf<QString>( NifValue::tShortString, text ), p, bytes ) );
		// the length counts the NUL
		QByteArray framed = QByteArray( 1, char( local.size() + 1 ) ) + local + QByteArray( 1, 0 );
		QVERIFY2( bytes == framed, qPrintable( QString( "wrote %1, expected %2" ).arg( hex( bytes ), hex( framed ) ) ) );

		NifValue back( NifValue::tShortString );
		qint64 used = -1;
		QVERIFY( streamRead( back, bytes, p, used ) );
		QCOMPARE( int( used ), int( bytes.size() ) );
		QCOMPARE( show( back.get<QString>() ), show( text ) );
	}

	void nonAscii_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<bool>( "lossless" );   // false: the text is not Latin-1, and the writer turns it into '?'

		typedef NifValue T;
		struct Type { const char * name; T::Type type; };
		const Type types[] = { { "SizedString", T::tSizedString }, { "Text", T::tText }, { "string below 20.1.0.3", T::tString },
		                       { "LineString", T::tLineString }, { "Char8String", T::tChar8String } };

		for ( const Type & t : types ) {
			// ASCII is stored as it is
			QTest::newRow( qPrintable( QString( "%1: ASCII" ).arg( t.name ) ) ) << int( t.type ) << QString( "Root_01" ) << true;
			// U+00E9 and U+00FC are one byte each in Latin-1, which is what the writer produces
			QTest::newRow( qPrintable( QString( "%1: e-acute" ).arg( t.name ) ) ) << int( t.type ) << eAcute() << true;
			QTest::newRow( qPrintable( QString( "%1: u-umlaut" ).arg( t.name ) ) ) << int( t.type ) << uUmlaut() << true;
			// U+3042 has no Latin-1 byte
			QTest::newRow( qPrintable( QString( "%1: hiragana" ).arg( t.name ) ) ) << int( t.type ) << hiragana() << false;
		}

		// the header line, behind a version the model takes
		QString header = "Gamebryo File Format, Version 20.0.0.5 ";
		QTest::newRow( "HeaderString: ASCII" ) << int( T::tHeaderString ) << header + "Root_01" << true;
		QTest::newRow( "HeaderString: e-acute" ) << int( T::tHeaderString ) << header + eAcute() << true;
		QTest::newRow( "HeaderString: hiragana" ) << int( T::tHeaderString ) << header + hiragana() << false;
	}

	//! What goes in comes out. The writers use toLatin1() and the readers QString( QByteArray ), which is UTF-8: only ASCII survives.
	void nonAscii()
	{
		QFETCH( int, type );
		QFETCH( QString, text );
		QFETCH( bool, lossless );

		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		QByteArray bytes;
		int size = -1;
		QVERIFY( streamWrite( vOf<QString>( NifValue::Type( type ), text ), p, bytes, &size ) );
		QCOMPARE( size, int( bytes.size() ) );

		NifValue back( typeOf( type ) );
		qint64 used = -1;
		QVERIFY( streamRead( back, bytes, p, used ) );
		QCOMPARE( int( used ), int( bytes.size() ) );

		bool ascii = true;
		for ( QChar c : text )
			ascii = ascii && c.unicode() < 0x80;

		if ( !ascii && lossless )
			QEXPECT_FAIL( "", "NifOStream writes the text as Latin-1 (toLatin1()), NifIStream reads it as UTF-8 (QString( QByteArray )): nifstream.cpp; fix then remove", Continue );
		else if ( !ascii )
			QEXPECT_FAIL( "", "not Latin-1: written as '?'; the text only survives when both sides use UTF-8 (nifstream.cpp); fix then remove", Continue );
		QCOMPARE( show( back.get<QString>() ), show( text ) );
	}

	void nonAscii_filePath_data()
	{
		QTest::addColumn<QString>( "text" );

		QTest::newRow( "ASCII" ) << QString( "Root_01" );
		QTest::newRow( "e-acute" ) << eAcute();
	}

	//! Below 20.1.0.3 a FilePath is stored like a string, but a NifValue( tFilePath ) has no text to write: take the bytes the
	//! SizedString writer produces for the text (a FilePath has the same layout) and read them as a FilePath, so the row follows
	//! whatever encoding the writers use
	void nonAscii_filePath()
	{
		QFETCH( QString, text );

		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		QByteArray bytes;
		QVERIFY( streamWrite( vOf<QString>( NifValue::tSizedString, text ), p, bytes ) );

		NifValue v( NifValue::tFilePath );
		qint64 used = -1;
		QVERIFY( streamRead( v, bytes, p, used ) );
		QCOMPARE( int( used ), int( bytes.size() ) );

		bool ascii = true;
		for ( QChar c : text )
			ascii = ascii && c.unicode() < 0x80;

		if ( !ascii )
			QEXPECT_FAIL( "", "NifOStream writes the text as Latin-1 (toLatin1()), NifIStream reads it as UTF-8 (QString( QByteArray )): nifstream.cpp; fix then remove", Continue );
		QCOMPARE( show( v.get<QString>() ), show( text ) );
	}

	void nonAscii_writerBytes_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QString>( "text" );
		QTest::addColumn<QByteArray>( "bytes" );

		typedef NifValue T;

		// Latin-1: U+00E9 is 0xE9, U+00FC is 0xFC, U+3042 has no Latin-1 byte and becomes '?' (0x3F)
		QTest::newRow( "SizedString e-acute" ) << int( T::tSizedString ) << eAcute() << QByteArray::fromHex( "04 00 00 00 63 61 66 e9" );
		QTest::newRow( "SizedString u-umlaut" ) << int( T::tSizedString ) << uUmlaut() << QByteArray::fromHex( "04 00 00 00 47 72 fc 6e" );
		QTest::newRow( "SizedString hiragana" ) << int( T::tSizedString ) << hiragana() << QByteArray::fromHex( "03 00 00 00 61 3f 62" );
		QTest::newRow( "Text e-acute" ) << int( T::tText ) << eAcute() << QByteArray::fromHex( "04 00 00 00 63 61 66 e9" );
		QTest::newRow( "string below 20.1.0.3 e-acute" ) << int( T::tString ) << eAcute() << QByteArray::fromHex( "04 00 00 00 63 61 66 e9" );
		QTest::newRow( "LineString e-acute" ) << int( T::tLineString ) << eAcute() << QByteArray::fromHex( "63 61 66 e9 0a" );
		QTest::newRow( "Char8String e-acute" ) << int( T::tChar8String ) << eAcute() << QByteArray::fromHex( "63 61 66 e9 00 00 00 00" );
		QTest::newRow( "Char8String hiragana" ) << int( T::tChar8String ) << hiragana() << QByteArray::fromHex( "61 3f 62 00 00 00 00 00" );
	}

	//! The encoding of the writers as it is today (Latin-1). Both sides have to change together, so this is the one test to update
	//! when the readers or the writers are fixed: it only records what the bytes of a Latin-1 text are.
	void nonAscii_writerBytes()
	{
		QFETCH( int, type );
		QFETCH( QString, text );
		QFETCH( QByteArray, bytes );

		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		QByteArray written;
		int size = -1;
		QVERIFY( streamWrite( vOf<QString>( NifValue::Type( type ), text ), p, written, &size ) );
		QVERIFY2( written == bytes, qPrintable( QString( "wrote %1, expected %2" ).arg( hex( written ), hex( bytes ) ) ) );
		QCOMPARE( size, int( bytes.size() ) );
	}

	void nonAscii_bytesSurviveResave_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QByteArray>( "bytes" );
		QTest::addColumn<bool>( "survives" );

		typedef NifValue T;

		// A file with a non-ASCII name: reading it and writing the value again must give the same bytes
		QTest::newRow( "SizedString ASCII" ) << int( T::tSizedString ) << QByteArray::fromHex( "04 00 00 00 52 6f 6f 74" ) << true;
		QTest::newRow( "SizedString UTF-8 e-acute" ) << int( T::tSizedString ) << QByteArray::fromHex( "05 00 00 00 63 61 66 c3 a9" ) << false;
		QTest::newRow( "SizedString Latin-1 e-acute" ) << int( T::tSizedString ) << QByteArray::fromHex( "04 00 00 00 63 61 66 e9" ) << false;
		QTest::newRow( "Text UTF-8 e-acute" ) << int( T::tText ) << QByteArray::fromHex( "05 00 00 00 63 61 66 c3 a9" ) << false;
		QTest::newRow( "string below 20.1.0.3 UTF-8 e-acute" ) << int( T::tString ) << QByteArray::fromHex( "05 00 00 00 63 61 66 c3 a9" ) << false;
		QTest::newRow( "LineString UTF-8 e-acute" ) << int( T::tLineString ) << QByteArray::fromHex( "63 61 66 c3 a9 0a" ) << false;
	}

	//! Load -> save changes the bytes of a name with a non-ASCII character (a UTF-8 file becomes Latin-1, a Latin-1 file loses the character)
	void nonAscii_bytesSurviveResave()
	{
		QFETCH( int, type );
		QFETCH( QByteArray, bytes );
		QFETCH( bool, survives );

		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		NifValue v( typeOf( type ) );
		qint64 used = -1;
		QVERIFY( streamRead( v, bytes, p, used ) );
		QCOMPARE( int( used ), int( bytes.size() ) );

		QByteArray again;
		QVERIFY( streamWrite( v, p, again ) );

		if ( !survives )
			QEXPECT_FAIL( "", "a non-ASCII text is read as UTF-8 and written as Latin-1 (nifstream.cpp), so a load and save rewrites it; fix then remove", Continue );
		QVERIFY2( again == bytes, qPrintable( QString( "read %1 and wrote %2" ).arg( hex( bytes ), hex( again ) ) ) );
	}

	void nonAscii_stringTable_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "userVersion" );
		QTest::addColumn<int>( "userVersion2" );
		QTest::addColumn<QString>( "text" );

		struct Version { const char * name; const char * version; int uv; int uv2; };
		// inline names (SizedString) below 20.1.0.3, names in the header's string table from there on
		const Version versions[] = { { "Morrowind 4.0.0.2", "4.0.0.2", 0, 0 }, { "Oblivion 20.0.0.5", "20.0.0.5", 11, 11 },
		                             { "20.1.0.3 (string table)", "20.1.0.3", 0, 0 }, { "Skyrim LE 20.2.0.7 (string table)", "20.2.0.7", 12, 83 } };

		for ( const Version & v : versions ) {
			QTest::newRow( qPrintable( QString( "%1: ASCII" ).arg( v.name ) ) ) << v.version << v.uv << v.uv2 << QString( "Scene Root" );
			QTest::newRow( qPrintable( QString( "%1: e-acute" ).arg( v.name ) ) ) << v.version << v.uv << v.uv2 << eAcute();
			QTest::newRow( qPrintable( QString( "%1: hiragana" ).arg( v.name ) ) ) << v.version << v.uv << v.uv2 << hiragana();
		}
	}

	//! A block's name goes through the streams as a SizedString, directly or as an entry of the header's string table
	void nonAscii_stringTable()
	{
		QFETCH( QString, version );
		QFETCH( int, userVersion );
		QFETCH( int, userVersion2 );
		QFETCH( QString, text );

		QByteArray v = version.toLatin1();
		TestEnv::Profile p{ "", v.constData(), userVersion, userVersion2 };
		auto a = TestEnv::makeModel( p );
		QModelIndex iNode = a->insertNiBlock( "NiNode" );
		QVERIFY( iNode.isValid() );
		QVERIFY( a->set<QString>( iNode, "Name", text ) );

		NifModel b;
		QVERIFY( saveAndLoad( *a, b ) );
		QCOMPARE( b.getBlockCount(), 1 );

		bool ascii = true;
		for ( QChar c : text )
			ascii = ascii && c.unicode() < 0x80;

		if ( !ascii )
			QEXPECT_FAIL( "", "names are saved as Latin-1 and loaded as UTF-8 (NifOStream / NifIStream, nifstream.cpp); fix then remove", Continue );
		QCOMPARE( show( b.get<QString>( b.getBlock( 0 ), "Name" ) ), show( text ) );
	}

	void fileVersion_neoSteam()
	{
		TestEnv::Profile p{ "", "10.1.0.0", 0, 0 };
		NifValue fv = vFileVersion( 0x0a010000 );

		// NeoSteam files say "NS" instead of NetImmerse and store 0x08F35232 where the version 10.1.0.0 belongs
		auto ordinary = TestEnv::makeModel( p );
		{
			QBuffer buf;
			buf.open( QIODevice::WriteOnly );
			QVERIFY( NifOStream( ordinary.get(), &buf ).write( fv ) );
			QCOMPARE( buf.data(), QByteArray::fromHex( "00 00 01 0a" ) );
		}

		auto neosteam = TestEnv::makeModel( p );
		QVERIFY( neosteam->set<QString>( neosteam->getHeader(), "Header String", "NS File Format, Version 10.1.0.0" ) );
		{
			QBuffer buf;
			buf.open( QIODevice::WriteOnly );
			QVERIFY( NifOStream( neosteam.get(), &buf ).write( fv ) );
			QCOMPARE( buf.data(), QByteArray::fromHex( "32 52 f3 08" ) );
		}

		// reading goes back: 0x08F35232 is 10.1.0.0, its neighbours are what they say
		for ( const TestEnv::Profile & c : contexts() ) {
			qint64 used = -1;
			NifValue neo( NifValue::tFileVersion );
			QVERIFY2( streamRead( neo, QByteArray::fromHex( "32 52 f3 08 01" ), c, used ), c.name );
			QCOMPARE( hex32( neo.toFileVersion() ), hex32( 0x0a010000 ) );

			NifValue next( NifValue::tFileVersion );
			QVERIFY2( streamRead( next, QByteArray::fromHex( "33 52 f3 08 01" ), c, used ), c.name );
			QCOMPARE( hex32( next.toFileVersion() ), hex32( 0x08f35233 ) );
		}
	}

	void fileVersion_endianPeek_data()
	{
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<int>( "endianType" );
		QTest::addColumn<bool>( "bigEndian" );

		// NifIStream looks at the byte behind the version (the Endian Type of the header) from 20.0.0.4 on, and reads big endian after a 0
		QTest::newRow( "20.0.0.5, Endian Type 1" ) << "20.0.0.5" << 1 << false;
		QTest::newRow( "20.0.0.5, Endian Type 0" ) << "20.0.0.5" << 0 << true;
		QTest::newRow( "20.0.0.4, Endian Type 0" ) << "20.0.0.4" << 0 << true;
		QTest::newRow( "20.2.0.7, Endian Type 0" ) << "20.2.0.7" << 0 << true;
		QTest::newRow( "10.4.0.1, the byte is not looked at" ) << "10.4.0.1" << 0 << false;
		QTest::newRow( "10.2.0.0, the byte is not looked at" ) << "10.2.0.0" << 0 << false;
		QTest::newRow( "4.0.0.2, the byte is not looked at" ) << "4.0.0.2" << 0 << false;
	}

	void fileVersion_endianPeek()
	{
		QFETCH( QString, version );
		QFETCH( int, endianType );
		QFETCH( bool, bigEndian );

		QByteArray v = version.toLatin1();
		TestEnv::Profile p{ "", v.constData(), 0, 0 };
		auto nif = TestEnv::makeModel( p );

		// FileVersion, Endian Type, then values written as 01 02 03 04 / 3f 80 00 00 / 12 34 / 3c 00 in that order
		QByteArray input = QByteArray::fromHex( "05 00 00 14" ) + QByteArray( 1, char( endianType ) )
		                   + QByteArray::fromHex( "01 02 03 04 01 02 03 04 3f 80 00 00 12 34 3c 00" );
		QBuffer buf;
		buf.setData( input );
		buf.open( QIODevice::ReadOnly );
		NifIStream is( nif.get(), &buf );

		NifValue version32( NifValue::tFileVersion );
		QVERIFY( is.read( version32 ) );
		QCOMPARE( hex32( version32.toFileVersion() ), hex32( 0x14000005 ) );     // never swapped

		NifValue endian( NifValue::tByte );
		QVERIFY( is.read( endian ) );
		QCOMPARE( int( endian.toCount() ), endianType );

		NifValue u( NifValue::tUInt );
		QVERIFY( is.read( u ) );
		QCOMPARE( hex32( u.toCount() ), hex32( bigEndian ? 0x01020304 : 0x04030201 ) );

		NifValue little( NifValue::tULittle32 );
		QVERIFY( is.read( little ) );
		QCOMPARE( hex32( little.toCount() ), hex32( 0x04030201 ) );              // little endian in any file

		NifValue fl( NifValue::tFloat );
		QVERIFY( is.read( fl ) );
		QCOMPARE( hex32( toBits( fl.toFloat() ) ), hex32( bigEndian ? 0x3f800000 : 0x0000803f ) );

		NifValue w( NifValue::tWord );
		QVERIFY( is.read( w ) );
		QCOMPARE( hex16( quint16( w.toCount() ) ), hex16( bigEndian ? 0x1234 : 0x3412 ) );

		NifValue hf( NifValue::tHfloat );
		QVERIFY( is.read( hf ) );
		QCOMPARE( f( hf.toFloat() ), f( bigEndian ? 1.0f : halfValue( 0x003c ) ) );

		QVERIFY( buf.atEnd() );
	}

	void headerString_setsModelVersion_data()
	{
		QTest::addColumn<QString>( "modelVersion" );
		QTest::addColumn<QString>( "header" );
		QTest::addColumn<QString>( "newVersion" );
		QTest::addColumn<QString>( "next" );       // type of the value that follows: bool or Ref
		QTest::addColumn<QByteArray>( "bytes" );   // its bytes
		QTest::addColumn<int>( "value" );          // what it reads as, in the new version
		QTest::addColumn<int>( "length" );         // how many bytes it takes

		// Reading the header string makes the model take over the version in it, and the stream re-reads its flags: the next value is read for that version
		QTest::newRow( "4.0.0.2: bool is 32 bits" ) << "20.0.0.5" << "NetImmerse File Format, Version 4.0.0.2" << "4.0.0.2" << "bool" << QByteArray::fromHex( "01 00 00 00" ) << 1 << 4;
		QTest::newRow( "20.2.0.7: bool is a byte" ) << "4.0.0.2" << "Gamebryo File Format, Version 20.2.0.7" << "20.2.0.7" << "bool" << QByteArray::fromHex( "01 00 00 00" ) << 1 << 1;
		QTest::newRow( "3.1: Ref is index + 1" ) << "20.0.0.5" << "NetImmerse File Format, Version 3.1" << "3.1" << "Ref" << QByteArray::fromHex( "01 00 00 00" ) << 0 << 4;
		QTest::newRow( "20.0.0.5: Ref is the index" ) << "3.1" << "Gamebryo File Format, Version 20.0.0.5" << "20.0.0.5" << "Ref" << QByteArray::fromHex( "01 00 00 00" ) << 1 << 4;
	}

	void headerString_setsModelVersion()
	{
		QFETCH( QString, modelVersion );
		QFETCH( QString, header );
		QFETCH( QString, newVersion );
		QFETCH( QString, next );
		QFETCH( QByteArray, bytes );
		QFETCH( int, value );
		QFETCH( int, length );

		QByteArray mv = modelVersion.toLatin1();
		TestEnv::Profile p{ "", mv.constData(), 0, 0 };
		auto nif = TestEnv::makeModel( p );

		QByteArray input = header.toLatin1() + "\n" + bytes;
		QBuffer buf;
		buf.setData( input );
		buf.open( QIODevice::ReadOnly );
		NifIStream is( nif.get(), &buf );

		NifValue h( NifValue::tHeaderString );
		QVERIFY( is.read( h ) );
		QCOMPARE( h.get<QString>(), header );
		QCOMPARE( nif->getVersion(), newVersion );

		NifValue n( next == "bool" ? NifValue::tBool : NifValue::tLink );
		QVERIFY( is.read( n ) );
		QCOMPARE( int( n.type() == NifValue::tBool ? n.toCount() : quint32( n.toLink() ) ), value );
		QCOMPARE( int( buf.pos() ), header.length() + 1 + length );
	}

	void headerString_refused_data()
	{
		QTest::addColumn<QString>( "header" );

		QTest::newRow( "not a NIF header" ) << "Hello, Version 4.0.0.2";
		QTest::newRow( "no version" ) << "NetImmerse File Format";
		QTest::newRow( "unsupported version" ) << "Gamebryo File Format, Version 99.0.0.0";
	}

	//! A header string the model does not take makes the read fail, with a message
	void headerString_refused()
	{
		QFETCH( QString, header );

		auto nif = TestEnv::makeModel( contexts().first() );
		nif->setMessageMode( BaseModel::TstMessage );

		QBuffer buf;
		buf.setData( header.toLatin1() + "\n" );
		buf.open( QIODevice::ReadOnly );
		NifIStream is( nif.get(), &buf );

		NifValue h( NifValue::tHeaderString );
		QVERIFY( !is.read( h ) );
		QCOMPARE( nif->getMessages().count(), 1 );
	}

	// ---- a device that fails, a device that gives less than was asked

	void writeFails_data() { addSampleRows( true ); }

	//! A write the device refuses, or does in part, is a failed write whichever of the writes of a value it is, though all the others
	//! work: a value is written in steps (a length, the text, the padding of a Char8String, the two halves of a QuatXYZW), and the
	//! result of every one of them counts
	void writeFails()
	{
		QFETCH( int, sample );
		const Sample & s = samples().at( sample );

		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( !s.appliesTo( versionOf( p ) ) )
				continue;

			checked++;
			auto nif = modelFor( p );

			// the device that takes everything says how many writes the value is
			FaultyWriteDevice counting;
			QVERIFY2( NifOStream( nif.get(), &counting ).write( s.value ), qPrintable( where( s, p ) + ": NifOStream::write failed" ) );
			QVERIFY2( counting.written == s.bytes, qPrintable( where( s, p ) + ": wrote " + hex( counting.written ) + ", expected " + hex( s.bytes ) ) );
			QVERIFY2( counting.calls > 0 || s.bytes.isEmpty(), qPrintable( where( s, p ) + ": bytes without a write" ) );

			for ( int step = 0; step < counting.calls; step++ ) {
				for ( FaultyWriteDevice::Fault fault : { FaultyWriteDevice::Refuse, FaultyWriteDevice::Short } ) {
					FaultyWriteDevice device;
					device.failAt = step;
					device.fault = fault;
					bool ok = NifOStream( nif.get(), &device ).write( s.value );
					QVERIFY2( !ok, qPrintable( where( s, p ) + QString( ": write %1 of %2 %3, and the value was reported as written" )
					                                              .arg( step + 1 ).arg( counting.calls ).arg( fault == FaultyWriteDevice::Refuse ? "failed" : "was cut short" ) ) );
				}
			}
		}

		QVERIFY2( checked > 0, "no context applies" );
	}

	void readShortRead_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QString>( "version" );
		QTest::addColumn<QByteArray>( "bytes" );   // the value as it is stored
		QTest::addColumn<int>( "split" );          // how many of the bytes come in the first piece, the others come after
		QTest::addColumn<bool>( "ok" );

		typedef NifValue T;

		// struct.pack('<4f', 2, 3, 4, 1): x y z w
		QByteArray quat = QByteArray::fromHex( "00 00 00 40 00 00 40 40 00 00 80 40 00 00 80 3f" );
		// a QuatXYZW is read in two parts, the 12 bytes of x y z and the 4 of w: a piece that ends between them is no short read
		for ( int split : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16 } )
			QTest::newRow( qPrintable( QString( "QuatXYZW, first piece of %1 bytes" ).arg( split ) ) ) << int( T::tQuatXYZW ) << "20.0.0.5" << quat << split << ( split == 12 || split == 16 );

		// values that are read with one read of all of their bytes
		QByteArray bytes36( 36, 'm' ), bytes64( 64, 'n' ), bytes12( 12, 'c' ), bytes4( 4, 'i' );
		QTest::newRow( "Matrix, first piece of 17 bytes" ) << int( T::tMatrix ) << "20.0.0.5" << bytes36 << 17 << false;
		QTest::newRow( "Matrix, first piece of 35 bytes" ) << int( T::tMatrix ) << "20.0.0.5" << bytes36 << 35 << false;
		QTest::newRow( "Matrix, one piece" ) << int( T::tMatrix ) << "20.0.0.5" << bytes36 << 36 << true;
		QTest::newRow( "Matrix4, first piece of 33 bytes" ) << int( T::tMatrix4 ) << "20.0.0.5" << bytes64 << 33 << false;
		QTest::newRow( "Matrix4, one piece" ) << int( T::tMatrix4 ) << "20.0.0.5" << bytes64 << 64 << true;
		QTest::newRow( "Color3, first piece of 7 bytes" ) << int( T::tColor3 ) << "20.0.0.5" << bytes12 << 7 << false;
		QTest::newRow( "Color3, one piece" ) << int( T::tColor3 ) << "20.0.0.5" << bytes12 << 12 << true;
		// 4.0.0.2: the byte behind a version is not looked at
		QTest::newRow( "FileVersion, first piece of 2 bytes" ) << int( T::tFileVersion ) << "4.0.0.2" << bytes4 << 2 << false;
		QTest::newRow( "FileVersion, one piece" ) << int( T::tFileVersion ) << "4.0.0.2" << bytes4 << 4 << true;
		QTest::newRow( "string index, first piece of 3 bytes" ) << int( T::tString ) << "20.2.0.7" << bytes4 << 3 << false;
		QTest::newRow( "FilePath index, first piece of 1 byte" ) << int( T::tFilePath ) << "20.2.0.7" << bytes4 << 1 << false;
		QTest::newRow( "string index, one piece" ) << int( T::tString ) << "20.2.0.7" << bytes4 << 4 << true;
		QTest::newRow( "Blob of 4 bytes, first piece of 2 bytes" ) << int( T::tBlob ) << "20.0.0.5" << bytes4 << 2 << false;
		QTest::newRow( "Blob of 4 bytes, one piece" ) << int( T::tBlob ) << "20.0.0.5" << bytes4 << 4 << true;
	}

	//! A read that comes back with fewer bytes than the value has is a failed read, though more bytes are on their way (the device is no
	//! file: it does not say that it is at its end)
	void readShortRead()
	{
		QFETCH( int, type );
		QFETCH( QString, version );
		QFETCH( QByteArray, bytes );
		QFETCH( int, split );
		QFETCH( bool, ok );

		QByteArray v = version.toLatin1();
		TestEnv::Profile p{ "", v.constData(), 0, 0 };
		auto nif = modelFor( p );

		ChunkedReadDevice device;
		device.chunks << bytes.left( split );
		if ( split < bytes.size() )
			device.chunks << bytes.mid( split );

		NifIStream is( nif.get(), &device );
		NifValue value = type == NifValue::tBlob ? vOf<QByteArray>( NifValue::tBlob, QByteArray( bytes.size(), 0 ) ) : NifValue( typeOf( type ) );
		bool read = is.read( value );
		QVERIFY2( read == ok, qPrintable( QString( "the read %1 with the first %2 of %3 bytes in a piece of their own" ).arg( read ? "worked" : "failed" ).arg( split ).arg( bytes.size() ) ) );
	}

	//! A SizedString whose length is cut short has no length: the stream (QDataStream) says it failed, and says so from then on. The read
	//! of the text itself takes it for a length of 0 and works, which is the other way round than it should be
	void sizedString_cutShortLength()
	{
		auto nif = modelFor( contexts().first() );

		ChunkedReadDevice device;
		device.chunks << QByteArray::fromHex( "02 00" ) << QByteArray::fromHex( "00 00 61 62 07 00 00 00" );
		NifIStream is( nif.get(), &device );

		NifValue text( NifValue::tSizedString );
		bool first = is.read( text );
		QEXPECT_FAIL( "", "NifIStream::read( tSizedString ) takes a length that is cut short for a length of 0 and returns true with an empty text: it does not look at the status of its QDataStream (nifstream.cpp); fix then remove", Continue );
		QVERIFY( !first );

		// the bytes of the next value are there, and the stream has failed: it is refused all the same
		NifValue number( NifValue::tUInt );
		QVERIFY2( !is.read( number ), "a read that follows the failed one worked" );
	}

	// ---- values the NifValue functions cannot build

	//! A blob with no data (NifValue( tBlob ) always has some): nothing to read into, nothing to write, and no size
	void blob_withoutData()
	{
		TestEnv::Profile p = contexts().first();

		RawValue target( NifValue::tBlob, 0u );
		qint64 used = -1;
		QVERIFY2( !streamRead( target, QByteArray::fromHex( "aa bb cc dd" ), p, used ), "a blob without data was read into" );

		RawValue none( NifValue::tBlob, 0u );
		QByteArray bytes;
		int size = -1;
		QVERIFY( streamWrite( none, p, bytes, &size ) );
		QCOMPARE( hex( bytes ), hex( QByteArray() ) );
		QCOMPARE( size, 0 );
	}

	//! A FilePath of the kind NifValue( tFilePath ) is, with no text: written like a string without characters, in every version
	void filePath_withoutText()
	{
		for ( const TestEnv::Profile & p : contexts() ) {
			for ( NifValue::Type type : { NifValue::tFilePath, NifValue::tString } ) {
				RawValue v( type, 0u );
				QByteArray bytes;
				QVERIFY2( streamWrite( v, p, bytes ), p.name );
				QVERIFY2( bytes == QByteArray( 4, 0 ), qPrintable( QString( "%1: type %2 wrote %3" ).arg( p.name ).arg( int( type ) ).arg( hex( bytes ) ) ) );
			}
		}
	}

	//! A FilePath with text (below 20.1.0.3 it is stored like a SizedString, which is what the model makes of it): the text, and
	//! what NifSStream says it takes
	void filePath_withText()
	{
		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( versionOf( p ) >= kStringIndexFrom )
				continue;

			checked++;
			RawValue v( NifValue::tFilePath, QString( "abc" ) );
			QByteArray bytes;
			int size = -1;
			QVERIFY2( streamWrite( v, p, bytes, &size ), p.name );
			QVERIFY2( bytes == QByteArray::fromHex( "03 00 00 00 61 62 63" ), qPrintable( QString( "%1: wrote %2" ).arg( p.name, hex( bytes ) ) ) );
			QVERIFY2( size == 7, qPrintable( QString( "%1: NifSStream::size says %2" ).arg( p.name ).arg( size ) ) );
		}

		QVERIFY( checked > 0 );
	}

	void stringIndex_inTheValue_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<quint32>( "number" );
		QTest::addColumn<QByteArray>( "bytes" );

		// from 20.1.0.3 a string is a number of the string table, 4 bytes. One that does not fit in 16 bits is written as 0
		for ( int type : { int( NifValue::tString ), int( NifValue::tFilePath ) } ) {
			QString name = type == NifValue::tString ? "string" : "FilePath";
			QTest::newRow( qPrintable( name + " index 0" ) ) << type << 0u << QByteArray::fromHex( "00 00 00 00" );
			QTest::newRow( qPrintable( name + " index 7" ) ) << type << 7u << QByteArray::fromHex( "07 00 00 00" );
			QTest::newRow( qPrintable( name + " index 0x0102 (byte order)" ) ) << type << 0x0102u << QByteArray::fromHex( "02 01 00 00" );
			QTest::newRow( qPrintable( name + " index 0xfffe" ) ) << type << 0xfffeu << QByteArray::fromHex( "fe ff 00 00" );
			QTest::newRow( qPrintable( name + " index 0xffff (the last that fits)" ) ) << type << 0xffffu << QByteArray::fromHex( "ff ff 00 00" );
			QTest::newRow( qPrintable( name + " index 0x10000" ) ) << type << 0x10000u << QByteArray::fromHex( "00 00 00 00" );
			QTest::newRow( qPrintable( name + " index 0x1ffff" ) ) << type << 0x1ffffu << QByteArray::fromHex( "00 00 00 00" );
			QTest::newRow( qPrintable( name + " index 0x12345678" ) ) << type << 0x12345678u << QByteArray::fromHex( "00 00 00 00" );
			QTest::newRow( qPrintable( name + " index 0xffffffff" ) ) << type << 0xffffffffu << QByteArray::fromHex( "00 00 00 00" );
		}
	}

	//! A string or a FilePath that holds a number, and no text: the model never builds one (it makes the item a tStringIndex), the
	//! writer is written for it all the same
	void stringIndex_inTheValue()
	{
		QFETCH( int, type );
		QFETCH( quint32, number );
		QFETCH( QByteArray, bytes );

		int checked = 0;
		for ( const TestEnv::Profile & p : contexts() ) {
			if ( versionOf( p ) < kStringIndexFrom )
				continue;

			checked++;
			RawValue v( typeOf( type ), number );
			QByteArray written;
			int size = -1;
			QVERIFY2( streamWrite( v, p, written, &size ), p.name );
			QVERIFY2( written == bytes, qPrintable( QString( "%1: wrote %2, expected %3" ).arg( p.name, hex( written ), hex( bytes ) ) ) );
			QVERIFY2( size == 4, qPrintable( QString( "%1: NifSStream::size says %2" ).arg( p.name ).arg( size ) ) );

			// the one write of the index: the number of the string or the 0 in its place can fail
			auto nif = modelFor( p );
			for ( FaultyWriteDevice::Fault fault : { FaultyWriteDevice::Refuse, FaultyWriteDevice::Short } ) {
				FaultyWriteDevice device;
				device.failAt = 0;
				device.fault = fault;
				QVERIFY2( !NifOStream( nif.get(), &device ).write( v ), qPrintable( QString( "%1: the failed write was reported as written" ).arg( p.name ) ) );
			}
		}

		QVERIFY( checked > 0 );
	}

	// ---- what is read is consumed, and no more

	//! A line that has no end within the limit is refused after the limit: a header string after 80 bytes, a line after 255
	void overlongLine_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<int>( "length" );
		QTest::addColumn<int>( "limit" );

		QTest::newRow( "HeaderString of 100 bytes" ) << int( NifValue::tHeaderString ) << 100 << 80;
		QTest::newRow( "HeaderString of 81 bytes" ) << int( NifValue::tHeaderString ) << 81 << 80;
		QTest::newRow( "LineString of 300 bytes" ) << int( NifValue::tLineString ) << 300 << 255;
		QTest::newRow( "LineString of 256 bytes" ) << int( NifValue::tLineString ) << 256 << 255;
	}

	void overlongLine()
	{
		QFETCH( int, type );
		QFETCH( int, length );
		QFETCH( int, limit );

		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		NifValue v( typeOf( type ) );
		qint64 used = -1;
		QVERIFY2( !streamRead( v, QByteArray( length, 'x' ), p, used ), "a line that does not end was accepted" );
		QVERIFY2( used == limit, qPrintable( QString( "took %1 bytes of the line, expected %2" ).arg( used ).arg( limit ) ) );
	}

	void refusedLength_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<quint32>( "length" );

		typedef NifValue T;
		for ( int type : { int( T::tSizedString ), int( T::tText ), int( T::tString ), int( T::tFilePath ) } ) {
			QString name = type == T::tSizedString ? "SizedString" : ( type == T::tText ? "Text" : ( type == T::tString ? "string below 20.1.0.3" : "FilePath below 20.1.0.3" ) );
			QTest::newRow( qPrintable( name + " length 0x8001" ) ) << type << 0x8001u;
			QTest::newRow( qPrintable( name + " length -1" ) ) << type << 0xffffffffu;
			QTest::newRow( qPrintable( name + " length -2" ) ) << type << 0xfffffffeu;
			QTest::newRow( qPrintable( name + " length INT_MIN" ) ) << type << 0x80000000u;
		}
	}

	//! A length the reader refuses (more than 0x8000, or below 0) is answered with a text in the value that says so, and not read from
	void refusedLength()
	{
		QFETCH( int, type );
		QFETCH( quint32, length );

		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		NifValue v( typeOf( type ) );
		if ( v.isString() )
			QVERIFY( v.set<QString>( "kept" ) );

		qint64 used = -1;
		QVERIFY2( !streamRead( v, le32( length ) + QByteArray( 16, 'x' ), p, used ), "the length was accepted" );
		QString text = v.get<QString>();
		QVERIFY2( text.startsWith( "<string too long" ), qPrintable( "the value says " + show( text ) ) );
	}

	void refusedLength_byteArray()
	{
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		for ( quint32 length : { 0xffffffffu, 0xfffffffeu, 0x80000000u } ) {
			NifValue v = vBytes( NifValue::tByteArray, "6b 65 70 74" );
			qint64 used = -1;
			QVERIFY2( !streamRead( v, le32( length ) + QByteArray( 16, 'x' ), p, used ), "the length was accepted" );
			// not read into, so that what the value held stays
			QCOMPARE( hex( v.get<QByteArray>() ), hex( QByteArray( "kept" ) ) );
		}
	}

	// ---- the local 8-bit codec

	//! A ShortString is text in the local 8-bit codec of the machine, whichever that is. Here it is Latin-1, so that one text
	//! has the same bytes everywhere
	void shortString_localCodec()
	{
		LocalCodecScope latin1( "ISO-8859-1" );
		TestEnv::Profile p{ "", "20.2.0.7", 12, 83 };

		// the length is 5: four characters and the NUL, the last character is one byte: e9
		QByteArray bytes;
		int size = -1;
		QVERIFY( streamWrite( vOf<QString>( NifValue::tShortString, eAcute() ), p, bytes, &size ) );
		QVERIFY2( bytes == QByteArray::fromHex( "05 63 61 66 e9 00" ), qPrintable( "wrote " + hex( bytes ) ) );

		NifValue v( NifValue::tShortString );
		qint64 used = -1;
		QVERIFY( streamRead( v, bytes, p, used ) );
		QCOMPARE( int( used ), int( bytes.size() ) );
		QCOMPARE( show( v.get<QString>() ), show( eAcute() ) );

		// and the other texts are not: a SizedString of UTF-8 bytes is read as UTF-8 under any local codec (the bytes are those of the
		// nonAscii_bytesSurviveResave rows)
	}

	void utf8Strings_data()
	{
		QTest::addColumn<int>( "type" );
		QTest::addColumn<QByteArray>( "bytes" );

		typedef NifValue T;
		QByteArray utf8 = QByteArray::fromHex( "63 61 66 c3 a9" );    // "caf" and U+00E9 as UTF-8
		QTest::newRow( "SizedString" ) << int( T::tSizedString ) << le32( 5 ) + utf8;
		QTest::newRow( "Text" ) << int( T::tText ) << le32( 5 ) + utf8;
		QTest::newRow( "string below 20.1.0.3" ) << int( T::tString ) << le32( 5 ) + utf8;
		QTest::newRow( "FilePath below 20.1.0.3" ) << int( T::tFilePath ) << le32( 5 ) + utf8;
		QTest::newRow( "LineString" ) << int( T::tLineString ) << utf8 + "\n";
		QTest::newRow( "Char8String" ) << int( T::tChar8String ) << utf8 + QByteArray( 3, 0 );
	}

	//! Every string but the ShortString is read as UTF-8, and not with the local codec (which is another thing on another machine)
	void utf8Strings()
	{
		QFETCH( int, type );
		QFETCH( QByteArray, bytes );

		LocalCodecScope latin1( "ISO-8859-1" );
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		NifValue v( typeOf( type ) );
		qint64 used = -1;
		QVERIFY( streamRead( v, bytes, p, used ) );
		QCOMPARE( int( used ), int( bytes.size() ) );
		QCOMPARE( show( v.get<QString>() ), show( eAcute() ) );
	}

	// ---- the header: version, endianness, the stream started over

	//! "NS" at the start of the header string, and only there, makes a file a NeoSteam file
	void fileVersion_neoSteam_onlyAtTheStart()
	{
		TestEnv::Profile p{ "", "10.1.0.0", 0, 0 };

		for ( const char * header : { "Not NS File Format, Version 10.1.0.0", "NetImmerse File Format, Version 10.1.0.0 NS", "ns File Format, Version 10.1.0.0" } ) {
			auto nif = TestEnv::makeModel( p );
			QVERIFY( nif->set<QString>( nif->getHeader(), "Header String", QString::fromLatin1( header ) ) );

			QBuffer buf;
			buf.open( QIODevice::WriteOnly );
			QVERIFY( NifOStream( nif.get(), &buf ).write( vFileVersion( 0x0a010000 ) ) );
			QVERIFY2( buf.data() == QByteArray::fromHex( "00 00 01 0a" ), header );
		}
	}

	//! The NeoSteam version is 10.1.0.0 for a file in little endian: nothing else is read differently behind it
	void fileVersion_neoSteam_keepsLittleEndian()
	{
		TestEnv::Profile p{ "", "10.1.0.0", 0, 0 };
		auto nif = TestEnv::makeModel( p );

		// FileVersion of a NeoSteam file, then a number that is always little endian, then a number as the file has it
		QBuffer buf;
		buf.setData( QByteArray::fromHex( "32 52 f3 08" ) + QByteArray::fromHex( "04 03 02 01" ) + QByteArray::fromHex( "04 03 02 01" ) );
		buf.open( QIODevice::ReadOnly );
		NifIStream is( nif.get(), &buf );

		NifValue version( NifValue::tFileVersion );
		QVERIFY( is.read( version ) );
		QCOMPARE( hex32( version.toFileVersion() ), hex32( 0x0a010000 ) );

		NifValue little( NifValue::tULittle32 );
		QVERIFY( is.read( little ) );
		QCOMPARE( hex32( little.toCount() ), hex32( 0x01020304 ) );

		NifValue number( NifValue::tUInt );
		QVERIFY( is.read( number ) );
		QCOMPARE( hex32( number.toCount() ), hex32( 0x01020304 ) );
	}

	//! A header string starts the stream over: the next value is read as the stream starts, little endian, even when a file version
	//! before it said big endian
	void headerString_startsStreamOver()
	{
		TestEnv::Profile p{ "", "20.2.0.7", 12, 83 };
		auto nif = TestEnv::makeModel( p );

		// the FileVersion and its Endian Type 0 (big endian), the header line, and then the numbers 04 03 02 01 and 01 02 03 04
		QByteArray header = "Gamebryo File Format, Version 20.2.0.7";
		QBuffer buf;
		buf.setData( QByteArray::fromHex( "07 00 02 14 00" ) + header + "\n" + QByteArray::fromHex( "04 03 02 01 04 03 02 01" ) );
		buf.open( QIODevice::ReadOnly );
		NifIStream is( nif.get(), &buf );

		NifValue version( NifValue::tFileVersion );
		QVERIFY( is.read( version ) );
		NifValue endian( NifValue::tByte );
		QVERIFY( is.read( endian ) );
		QCOMPARE( int( endian.toCount() ), 0 );

		NifValue line( NifValue::tHeaderString );
		QVERIFY( is.read( line ) );

		// little endian again: 01 02 03 04 for a number that is always little endian, and for the one that follows it
		NifValue little( NifValue::tULittle32 );
		QVERIFY( is.read( little ) );
		QCOMPARE( hex32( little.toCount() ), hex32( 0x01020304 ) );
		NifValue number( NifValue::tUInt );
		QVERIFY( is.read( number ) );
		QCOMPARE( hex32( number.toCount() ), hex32( 0x01020304 ) );
	}

	// ---- a model that is not a NIF

	//! The version rules of the NIF format are a NifModel's: a model of another kind whose version number says 3.1 or 20.2.0.7 has
	//! one byte for a bool, links as they are, a string of its own text and no endian byte behind the file version
	void nonNifModel_ignoresVersionRules()
	{
		// what a NifModel makes 32 bit bools and links plus one of
		{
			StubModel model( 0x03010000 );
			QBuffer buf;
			buf.setData( QByteArray::fromHex( "01 02 05 00 00 00" ) );
			buf.open( QIODevice::ReadOnly );
			NifIStream is( &model, &buf );

			NifValue b( NifValue::tBool );
			QVERIFY( is.read( b ) );
			QCOMPARE( int( buf.pos() ), 1 );
			NifValue skip( NifValue::tByte );
			QVERIFY( is.read( skip ) );
			NifValue link( NifValue::tLink );
			QVERIFY( is.read( link ) );
			QCOMPARE( link.toLink(), 5 );

			QBuffer out;
			out.open( QIODevice::WriteOnly );
			NifOStream os( &model, &out );
			QVERIFY( os.write( vCount( NifValue::tBool, 1 ) ) );
			QVERIFY( os.write( vLink( NifValue::tLink, 5 ) ) );
			QCOMPARE( hex( out.data() ), hex( QByteArray::fromHex( "01 05 00 00 00" ) ) );
		}

		// what a NifModel makes a string table of, and looks at the endian byte of
		{
			StubModel model( 0x14020007 );

			// a string is a SizedString, in all three streams
			QBuffer buf;
			buf.setData( QByteArray::fromHex( "02 00 00 00 61 62" ) );
			buf.open( QIODevice::ReadOnly );
			NifValue s( NifValue::tString );
			QVERIFY( NifIStream( &model, &buf ).read( s ) );
			QCOMPARE( int( buf.pos() ), 6 );
			QCOMPARE( int( s.type() ), int( NifValue::tSizedString ) );
			QCOMPARE( show( s.get<QString>() ), show( "ab" ) );

			QBuffer out;
			out.open( QIODevice::WriteOnly );
			QVERIFY( NifOStream( &model, &out ).write( vString( NifValue::tString, "ab" ) ) );
			QCOMPARE( hex( out.data() ), hex( QByteArray::fromHex( "02 00 00 00 61 62" ) ) );
			QCOMPARE( NifSStream( &model ).size( vString( NifValue::tString, "ab" ) ), 6 );

			// the byte behind the file version is a byte like any other: the numbers behind it stay little endian
			QBuffer in;
			in.setData( QByteArray::fromHex( "07 00 02 14 00 04 03 02 01" ) );
			in.open( QIODevice::ReadOnly );
			NifIStream is( &model, &in );
			NifValue version( NifValue::tFileVersion );
			QVERIFY( is.read( version ) );
			NifValue endian( NifValue::tByte );
			QVERIFY( is.read( endian ) );
			NifValue number( NifValue::tUInt );
			QVERIFY( is.read( number ) );
			QCOMPARE( hex32( number.toCount() ), hex32( 0x01020304 ) );
		}
	}

	//! Matrix22 is not a NifValue type but a compound of four floats in nif.xml: m11 m21 m12 m22, written in that order
	void matrix22_fieldOrder()
	{
		TestEnv::Profile p{ "", "20.0.0.5", 11, 11 };
		auto nif = TestEnv::makeModel( p );

		// NiTexturingProperty.Bump Map Matrix, present when the property has a bump map
		QModelIndex iBlock = nif->insertNiBlock( "NiTexturingProperty" );
		QVERIFY( iBlock.isValid() );
		QVERIFY( nif->set<int>( iBlock, "Texture Count", 6 ) );
		QVERIFY( nif->set<int>( iBlock, "Has Bump Map Texture", 1 ) );

		QModelIndex iMatrix = nif->getIndex( iBlock, "Bump Map Matrix" );
		QVERIFY( iMatrix.isValid() );
		QCOMPARE( nif->itemType( iMatrix ), QString( "Matrix22" ) );
		QCOMPARE( nif->rowCount( iMatrix ), 4 );

		QStringList names;
		for ( int r = 0; r < 4; r++ )
			names << nif->itemName( nif->index( r, 0, iMatrix ) );
		QCOMPARE( names, QStringList() << "m11" << "m21" << "m12" << "m22" );

		// the value of each field is its number: m11 = 1, m21 = 2, m12 = 3, m22 = 4
		QVERIFY( nif->set<float>( iMatrix, "m11", 1.0f ) );
		QVERIFY( nif->set<float>( iMatrix, "m21", 2.0f ) );
		QVERIFY( nif->set<float>( iMatrix, "m12", 3.0f ) );
		QVERIFY( nif->set<float>( iMatrix, "m22", 4.0f ) );

		QBuffer buf;
		buf.open( QIODevice::WriteOnly );
		NifOStream os( nif.get(), &buf );
		int size = 0;
		for ( int r = 0; r < 4; r++ ) {
			NifValue v = nif->getValue( nif->index( r, 0, iMatrix ) );
			QVERIFY( os.write( v ) );
			size += NifSStream( nif.get() ).size( v );
		}

		// struct.pack('<4f', 1.0, 2.0, 3.0, 4.0): the file order is m11 m21 m12 m22 ("column-major" in the nif.xml description)
		QCOMPARE( buf.data(), QByteArray::fromHex( "00 00 80 3f 00 00 00 40 00 00 40 40 00 00 80 40" ) );
		QCOMPARE( size, 16 );
	}
};

REGISTER_TEST( tst_NifStream )

#include "tst_roundtrip.moc"
