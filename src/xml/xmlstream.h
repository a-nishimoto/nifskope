/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#ifndef XMLSTREAM_H
#define XMLSTREAM_H
#pragma once

#include <QIODevice>
#include <QString>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>


//! @file xmlstream.h Drives the NIF and KFM schema handlers with a QXmlStreamReader

//! The attributes of a start tag, looked up by qualified name
/*!
 * An attribute that is not there reads as an empty string, which is what the handlers always got from QXmlAttributes
 * and what they rely on (a missing name and an empty name are the same error).
 */
class XmlAttributes final
{
public:
	explicit XmlAttributes( const QXmlStreamAttributes & attributes ) : attrs( attributes ) {}

	//! The value of the attribute, or an empty string
	QString value( const QString & qualifiedName ) const
	{
		return attrs.value( qualifiedName ).toString();
	}

private:
	//! Shared with the reader's own list: copying it copies a pointer
	QXmlStreamAttributes attrs;
};

/*! Reads a schema document from the device and tells the handler what is in it: one call for every start tag, end
 * tag and run of text, and one for the end of the document.
 *
 * Comments, processing instructions and the DOCTYPE are not reported, and neither is text outside the root element.
 * A run of text ends at every tag, comment, processing instruction and CDATA section, so "a<!-- c -->b" is two
 * calls; the five predefined entities and character references are part of the run. Names are the qualified names
 * as written ("nt:compound"): namespaces are not resolved, a prefix nobody declared is just a name the handler does
 * not know.
 *
 * The handler provides
 * \code
 *	bool startElement( const QString & qualifiedName, const XmlAttributes & attributes );
 *	bool endElement( const QString & qualifiedName );
 *	bool characters( const QString & text );
 *	bool endDocument();  // called only for a well-formed document, after its last element
 *	void fatalError( int line );
 * \endcode
 * The first four return false to stop the parse, the handler has then put its message where it keeps it. Either way,
 * fatalError() is called once, with the line the reader had reached, when the parse stops early: for a handler
 * that said no, and for a document that is not well-formed (then the handler has no message of its own).
 * The line of a start tag is the line on which the tag ends, the line of the end of the document is the number of
 * line breaks in it plus one.
 *
 * Do not open the device with QIODevice::Text: the reader turns CR LF and CR into LF itself, and a device in text mode
 * would remove the CR first, which makes a document that uses CR alone one line long.
 */
template <class Handler>
void parseXmlDocument( QIODevice & device, Handler & handler )
{
	QXmlStreamReader xml( &device );
	xml.setNamespaceProcessing( false );

	int depth = 0;

	while ( !xml.atEnd() ) {
		bool ok = true;

		switch ( xml.readNext() ) {
		case QXmlStreamReader::StartElement:
			depth++;
			ok = handler.startElement( xml.qualifiedName().toString(), XmlAttributes( xml.attributes() ) );
			break;
		case QXmlStreamReader::EndElement:
			depth--;
			ok = handler.endElement( xml.qualifiedName().toString() );
			break;
		case QXmlStreamReader::Characters:
			// white space between the XML declaration, the DOCTYPE and the root is not the handler's business
			if ( depth > 0 )
				ok = handler.characters( xml.text().toString() );

			break;
		case QXmlStreamReader::EndDocument:
			ok = handler.endDocument();
			break;
		default:
			break;
		}

		if ( !ok ) {
			handler.fatalError( int( xml.lineNumber() ) );
			return;
		}
	}

	// the reader stopped at something that is not XML
	if ( xml.hasError() )
		handler.fatalError( int( xml.lineNumber() ) );
}

#endif
