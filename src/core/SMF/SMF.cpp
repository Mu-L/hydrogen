/*
 * Hydrogen
 * Copyright(c) 2002-2008 by Alex >Comix< Cominu [comix@users.sourceforge.net]
 * Copyright(c) 2008-2025 The hydrogen development team [hydrogen-devel@lists.sourceforge.net]
 *
 * http://www.hydrogen-music.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see https://www.gnu.org/licenses
 *
 */

#include <core/SMF/SMF.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/Pattern.h>
#include <core/Basics/PatternList.h>
#include <core/Basics/Note.h>
#include <core/Basics/Song.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/AutomationPath.h>

#include <QFile>
#include <QTextCodec>
#include <QTextStream>

namespace H2Core
{

QString SMFHeader::FormatToQString( Format format ) {
	switch( format ) {
	case Format::SingleMultiChannelTrack:
		return "SingleMultiChannelTrack";
	case Format::SimultaneousTracks:
		return "SimultaneousTracks";
	case Format::SequentialIndependentTracks:
		return "SequentialIndependentTracks";
	default:
		return QString( "Unknown format value [%1]" )
			.arg( static_cast<int>(format) );
	}
}

SMFHeader::SMFHeader( Format format )
		: m_format( format )
		, m_nTracks( 0 ) {
}


SMFHeader::~SMFHeader() {
}

void SMFHeader::addTrack() {
	m_nTracks++;	
}

QByteArray SMFHeader::getBuffer() const
{
	SMFBuffer buffer;

	buffer.writeDWord( 1297377380 );		// MThd
	buffer.writeDWord( 6 );				// Header length = 6
	buffer.writeWord( static_cast<int>(m_format) );
	buffer.writeWord( m_nTracks );
	buffer.writeWord( SMF::nTicksPerQuarter );

	return buffer.m_buffer;
}

QString SMFHeader::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[SMFHeader]\n" ).arg( sPrefix )
			.append( QString( "%1%2m_format: %3\n" ).arg( sPrefix )
					 .arg( s ).arg( FormatToQString( m_format ) ) )
			.append( QString( "%1%2m_nTracks: %3\n" ).arg( sPrefix )
					 .arg( s ).arg( m_nTracks ) );
	}
	else {
		sOutput = QString( "[SMFHeader] " )
			.append( QString( "m_format: %1" )
					 .arg( FormatToQString( m_format ) ) )
			.append( QString( ", m_nTracks: %1" ).arg( m_nTracks ) );
	}

	return sOutput;
}

// :::::::::::::::

SMFTrack::SMFTrack()
		: Object() {
}

SMFTrack::~SMFTrack() {
}

QByteArray SMFTrack::getBuffer() const {
	QByteArray trackData;

	for ( const auto& ppEvent : m_eventList ) {
		if ( ppEvent == nullptr ) {
			continue;
		}

		auto buf = ppEvent->getBuffer();

		for ( unsigned j = 0; j < buf.size(); j++ ) {
			trackData.push_back( buf[ j ] );
		}
	}

	SMFBuffer buf;

	buf.writeDWord( 1297379947 );		// MTrk
	buf.writeDWord( trackData.size() + 4 );	// Track length

	auto trackBuf = buf.getBuffer();

	for ( unsigned i = 0; i < trackData.size(); i++ ) {
		trackBuf.push_back( trackData[i] );
	}

	//  track end
	trackBuf.push_back( static_cast<char>(0x00) );		// delta
	trackBuf.push_back( static_cast<char>(0xFF) );
	trackBuf.push_back( static_cast<char>(0x2F) );
	trackBuf.push_back( static_cast<char>(0x00) );

	return trackBuf;
}

void SMFTrack::addEvent( std::shared_ptr<SMFEvent> pEvent ) {
	m_eventList.push_back( pEvent );
}

QString SMFTrack::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[SMFTrack] m_eventList: \n" ).arg( sPrefix );
		for ( const auto& ppEvent : m_eventList ) {
			sOutput.append( QString( "%1%2\n" ).arg( sPrefix + s )
							.arg( ppEvent->toQString( "", true ) ) );
		}
	}
	else {
		sOutput = QString( "[SMFTrack] m_eventList: [" );
		for ( const auto& ppEvent : m_eventList ) {
			sOutput.append( QString( "[%1] " )
							.arg( ppEvent->toQString( "", true ) ) );
		}
		sOutput.append( "]" );
	}

	return sOutput;
}

// ::::::::::::::::::::::

SMF::SMF( SMFHeader::Format format ) {
	m_pHeader = std::make_shared<SMFHeader>( format );
}

SMF::~SMF() {
}

void SMF::addTrack( std::shared_ptr<SMFTrack> pTrack ) {
	if ( pTrack == nullptr ) {
		return;
	}

	if ( m_pHeader == nullptr ) {
		ERRORLOG( "Header not properly set up yet." );
		return;
	}

	m_pHeader->addTrack();
	m_trackList.push_back( pTrack );
}

QByteArray SMF::getBuffer() const {
	// header
	auto smfBuffer = m_pHeader->getBuffer();

	// tracks
	for ( const auto& ppTrack : m_trackList ) {
		if ( ppTrack != nullptr ) {
			smfBuffer.append( ppTrack->getBuffer() );
		}
	}

	return smfBuffer;
}

QString SMF::bufferToQString() const {
	return QString( getBuffer().toHex( ' ' ) );
}

QString SMF::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput.append( QString( "%1[SMF]\n%1%2m_pHeader: %3\n" ).arg( sPrefix )
						.arg( s ).arg( m_pHeader->toQString( s, true ) ) )
			.append( QString( "%1%2m_trackList:\n" ).arg( sPrefix ).arg( s ) );
		for ( const auto& ppTrack : m_trackList ) {
			sOutput.append( QString( "%1" )
							.arg( ppTrack->toQString( s, false ) ) );
		}
	}
	else {
		sOutput.append( QString( "[SMF] m_pHeader: %1" )
						.arg( m_pHeader->toQString( "", true ) ) )
			.append( ", m_trackList: [" );
		for ( const auto& ppTrack : m_trackList ) {
			sOutput.append( QString( "[%1] " )
							.arg( ppTrack->toQString( "", true ) ) );
		}
		sOutput.append( "]" );
	}

	return sOutput;
}

// :::::::::::::::::::...

constexpr unsigned int DRUM_CHANNEL = 9;
constexpr unsigned int NOTE_LENGTH = 12;

SMFWriter::SMFWriter() {
}


SMFWriter::~SMFWriter() {
}


std::shared_ptr<SMFTrack> SMFWriter::createTrack0( std::shared_ptr<Song> pSong ) {
	if ( pSong == nullptr ) {
		ERRORLOG( "Invalid song" );
		return nullptr;
	}

	auto pTrack0 = std::make_shared<SMFTrack>();
	pTrack0->addEvent(
		std::make_shared<SMFCopyRightNoticeMetaEvent>( pSong->getAuthor() , 0 ) );
	pTrack0->addEvent(
		std::make_shared<SMFTrackNameMetaEvent>( pSong->getName() , 0 ) );
	pTrack0->addEvent(
		std::make_shared<SMFSetTempoMetaEvent>(
			static_cast<int>(std::round( pSong->getBpm() )), 0 ) );
	pTrack0->addEvent(
		std::make_shared<SMFTimeSignatureMetaEvent>( 4 , 4 , 24 , 8 , 0 ) );

	return pTrack0;
}

void SMFWriter::save( const QString& sFilename, std::shared_ptr<Song> pSong )
{
	if ( pSong == nullptr || pSong->getTimeline() == nullptr ||
		 pSong->getDrumkit() == nullptr ) {
		return;
	}

	INFOLOG( QString( "Export MIDI to [%1]" ).arg( sFilename ) );

	auto pSmf = createSMF( pSong );

	AutomationPath* pAutomationPath = pSong->getVelocityAutomationPath();

	// here writers must prepare to receive pattern events
	prepareEvents( pSong, pSmf );

	auto pInstrumentList = pSong->getDrumkit()->getInstruments();
	int nTick = 0;
	for ( int nnColumn = 0;
		  nnColumn < pSong->getPatternGroupVector()->size() ;
		  nnColumn++ ) {
		auto pColumn = ( *(pSong->getPatternGroupVector()) )[ nnColumn ];

		// Instead of working on the raw patternList of the column, we need to
		// expand all virtual patterns.
		auto pPatternList = std::make_shared<PatternList>();
		for ( const auto& ppPattern : *pColumn ) {
			pPatternList->add( ppPattern, true );
		}

		int nColumnLength;
		if ( pPatternList->size() > 0 ) {
			nColumnLength = pPatternList->longestPatternLength( false );
		}
		else {
			nColumnLength = 4 * H2Core::nTicksPerQuarter;
		}

		for ( const auto& ppPattern : *pPatternList ) {
			if ( ppPattern == nullptr ) {
				continue;
			}

			for ( const auto& [ nnNote, ppNote ] : *ppPattern->getNotes() ) {
				if ( ppNote != nullptr && ppNote->getInstrument() != nullptr ) {
					if ( ppNote->getProbability() <
						 static_cast<float>(rand()) / static_cast<float>(RAND_MAX) ) {
						// Skip note
						continue;
					}

					const float fColumnPos = static_cast<float>(nnColumn) +
						static_cast<float>(nnNote) /
						static_cast<float>(nColumnLength);
					const float fVelocityAdjustment =
						pAutomationPath->get_value( fColumnPos );
					const int nVelocity = static_cast<int>(
						127.0 * ppNote->getVelocity() * fVelocityAdjustment );

					const auto pInstr = ppNote->getInstrument();
					const int nPitch = ppNote->getMidiKey();
						
					int nChannel =  pInstr->getMidiOutChannel();
					if ( nChannel == -1 ) {
						// A channel of -1 is Hydrogen's old way of disabling
						// MIDI output during playback.
						nChannel = DRUM_CHANNEL;
					}
						
					int nLength = ppNote->getLength();
					if ( nLength == LENGTH_ENTIRE_SAMPLE ) {
						nLength = NOTE_LENGTH;
					}
						
					// get events for specific instrument
					auto pEventList = getEvents( pSong, pInstr );
					if ( pEventList != nullptr ) {
						pEventList->push_back(
							std::make_shared<SMFNoteOnEvent>(
								nTick + nnNote,
								nChannel,
								nPitch,
								nVelocity
								)
							);

						pEventList->push_back(
							std::make_shared<SMFNoteOffEvent>(
								nTick + nnNote + nLength,
								nChannel,
								nPitch,
								nVelocity
								)
							);
					}
					else {
						ERRORLOG( "Invalid event list" );
					}
				}
			}
		}

		nTick += nColumnLength;
	}

	//tracks creation
	packEvents(pSong, pSmf);

	saveSMF(sFilename, pSmf);
}

void SMFWriter::sortEvents( std::shared_ptr<EventList> pEvents ) {
	if ( pEvents == nullptr ) {
		return;
	}
	// awful bubble sort..
	for ( unsigned i = 0; i < pEvents->size(); i++ ) {
		for ( auto it = pEvents->begin() ;
			  it != ( pEvents->end() - 1 ) ;
			  it++ ) {
			auto pEvent = *it;
			auto pNextEvent = *( it + 1 );
			if ( pEvent == nullptr || pNextEvent == nullptr ) {
				ERRORLOG( "Abort. Invalid event" );
				return;
			}

			if ( pNextEvent->m_nTicks < pEvent->m_nTicks ) {
				// swap
				*it = pNextEvent;
				*( it +1 ) = pEvent;
			}
		}
	}
}

void SMFWriter::saveSMF( const QString& sFilename, std::shared_ptr<SMF> pSmf ) {
	if ( pSmf == nullptr ) {
		ERRORLOG( "Invalid SMF" );
		return;
	}

	// save the midi file
	QFile file( sFilename );
	if ( ! file.open( QIODevice::WriteOnly ) ) {
		ERRORLOG( QString( "Unable to open file [%1] for writing" )
				  .arg( sFilename ) );
		return;
	}

	QDataStream stream( &file );
	const auto buffer = pSmf->getBuffer();
	stream.writeRawData( buffer.constData(), buffer.size() );

	file.close();
}


// SMF1Writer - base class for two smf1 writers

SMF1Writer::SMF1Writer() : SMFWriter() {
}

SMF1Writer::~SMF1Writer() {
}

std::shared_ptr<SMF> SMF1Writer::createSMF( std::shared_ptr<Song> pSong ){
	auto pSmf = std::make_shared<SMF>( SMFHeader::Format::SimultaneousTracks );
	// Standard MIDI format 1 files should have the first track being the tempo map
	// which is a track that contains global meta events only.

	auto pTrack0 = createTrack0( pSong );
	pSmf->addTrack( pTrack0 );
	
	// Standard MIDI Format 1 files should have note events in tracks =>2
	return pSmf;
}

SMF1WriterSingle::SMF1WriterSingle()
		: SMF1Writer(),
		 m_pEventList( std::make_shared<EventList>() ) {
}

SMF1WriterSingle::~SMF1WriterSingle() {
}

std::shared_ptr<EventList> SMF1WriterSingle::getEvents( std::shared_ptr<Song> pSong,
														std::shared_ptr<Instrument> pInstr )
{
	return m_pEventList;
}

void SMF1WriterSingle::prepareEvents( std::shared_ptr<Song> pSong,
									  std::shared_ptr<SMF> pSmf ) {
	if ( m_pEventList != nullptr ) {
		m_pEventList->clear();
	}
}

void SMF1WriterSingle::packEvents( std::shared_ptr<Song> pSong,
								   std::shared_ptr<SMF> pSmf ) {
	if ( pSmf == nullptr ) {
		ERRORLOG( "Invalid SMF" );
		return;
	}

	if ( m_pEventList == nullptr ) {
		ERRORLOG( "Event List not properly set up" );
		return;
	}

	sortEvents( m_pEventList );

	auto pTrack1 = std::make_shared<SMFTrack>();
	pSmf->addTrack( pTrack1 );

	int nLastTick = 0;
	for( auto& pEvent : *m_pEventList ) {
		pEvent->m_nDeltaTime =
			( pEvent->m_nTicks - nLastTick ) * SMF::nTickFactor;
		nLastTick = pEvent->m_nTicks;

		pTrack1->addEvent( pEvent );
	}

	m_pEventList->clear();
}

QString SMF1WriterSingle::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[SMF1WriterSingle] m_pEventList: \n" ).arg( sPrefix );
		for ( const auto& ppEvent : *m_pEventList ) {
			sOutput.append( QString( "%1%2\n" ).arg( sPrefix + s )
							.arg( ppEvent->toQString( s, true ) ) );
		}
	}
	else {
		sOutput = QString( "[SMF1WriterSingle] m_pEventList: [" );
		for ( const auto& ppEvent : *m_pEventList ) {
			sOutput.append( QString( "[%1] " )
							.arg( ppEvent->toQString( "", true ) ) );
		}
		sOutput.append( "]" );
	}

	return sOutput;
}

// SMF1 MIDI MULTI EXPORT

SMF1WriterMulti::SMF1WriterMulti()
		: SMF1Writer(),
		 m_eventLists() {
}

SMF1WriterMulti::~SMF1WriterMulti() {
}

void SMF1WriterMulti::prepareEvents( std::shared_ptr<Song> pSong,
									 std::shared_ptr<SMF> pSmf )
{
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		m_eventLists.clear();
		m_eventLists.push_back( std::make_shared<EventList>() );
		return;
	}

	auto pInstrumentList = pSong->getDrumkit()->getInstruments();
	m_eventLists.clear();
	for ( unsigned nInstr=0; nInstr < pInstrumentList->size(); nInstr++ ){
		m_eventLists.push_back( std::make_shared<EventList>() );
	}
}

std::shared_ptr<EventList> SMF1WriterMulti::getEvents( std::shared_ptr<Song> pSong,
													   std::shared_ptr<Instrument> pInstr ) {
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		if ( m_eventLists.size() > 0 ) {
			return m_eventLists[ 0 ];
		}
		else {
			return nullptr;
		}
	}

	return m_eventLists.at( 
		pSong->getDrumkit()->getInstruments()->index( pInstr ) );
}

void SMF1WriterMulti::packEvents( std::shared_ptr<Song> pSong,
								  std::shared_ptr<SMF> pSmf )
{
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return;
	}

	auto pInstrumentList = pSong->getDrumkit()->getInstruments();
	for ( unsigned nTrack = 0; nTrack < m_eventLists.size(); nTrack++ ) {
		auto pEventList = m_eventLists.at( nTrack );
		auto pInstrument = pInstrumentList->get( nTrack );
		if ( pEventList == nullptr || pInstrument == nullptr ) {
			continue;
		}

		sortEvents( pEventList );

		auto pTrack = std::make_shared<SMFTrack>();
		pSmf->addTrack( pTrack );
		
		// Set instrument name as track name
		pTrack->addEvent(
			std::make_shared<SMFTrackNameMetaEvent>( pInstrument->getName() , 0 ) );
		
		int nLastTick = 0;
		for ( auto& ppEvent : *pEventList ) {
			ppEvent->m_nDeltaTime =
				( ppEvent->m_nTicks - nLastTick ) * SMF::nTickFactor;
			nLastTick = ppEvent->m_nTicks;

			pTrack->addEvent( ppEvent );
		}
	}
	m_eventLists.clear();
}

QString SMF1WriterMulti::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[SMF1WriterMulti] m_eventLists: \n" ).arg( sPrefix );
		for ( int ii = 0; ii < m_eventLists.size(); ii++ ) {
			sOutput.append( QString( "%1%2[%3]:\n" ).arg( sPrefix )
							.arg( s ).arg( ii ) );
			for ( const auto& ppEvent : *m_eventLists[ ii ] ) {
				sOutput.append( QString( "%1%2\n" ).arg( sPrefix + s + s )
								.arg( ppEvent->toQString( "", true ) ) );
			}
		}
	}
	else {
		sOutput = QString( "[SMF1WriterMulti] m_eventLists: [" );
		for ( int ii = 0; ii < m_eventLists.size(); ii++ ) {
			sOutput.append( QString( "[[%1]: " ).arg( ii ) );
			for ( const auto& ppEvent : *m_eventLists[ ii ] ) {
				sOutput.append( QString( "[%1] " )
								.arg( ppEvent->toQString( s + s, true ) ) );
			}
			sOutput.append( "] " );
		}
		sOutput.append( "]" );
	}

	return sOutput;
}

// SMF0 MIDI  EXPORT

SMF0Writer::SMF0Writer() : SMFWriter()
						 , m_pTrack( nullptr )
						 , m_pEventList( std::make_shared<EventList>() ) {
}

SMF0Writer::~SMF0Writer() {
}

std::shared_ptr<SMF> SMF0Writer::createSMF( std::shared_ptr<Song> pSong ){
	// MIDI files format 0 have all their events in one track
	auto pSmf = std::make_shared<SMF>( SMFHeader::Format::SingleMultiChannelTrack );
	m_pTrack = createTrack0( pSong );
	pSmf->addTrack( m_pTrack );
	return pSmf;
}

std::shared_ptr<EventList> SMF0Writer::getEvents( std::shared_ptr<Song> pSong,
												  std::shared_ptr<Instrument> pInstr ) {
	return m_pEventList;
}

void SMF0Writer::prepareEvents( std::shared_ptr<Song> pSong,
								std::shared_ptr<SMF> pSmf ) {
	if ( m_pEventList != nullptr ) {
		m_pEventList->clear();
	}
}

void SMF0Writer::packEvents( std::shared_ptr<Song> pSong,
							 std::shared_ptr<SMF> pSmf ) {
	if ( m_pEventList == nullptr ) {
		ERRORLOG( "Event List not properly set up" );
		return;
	}

	if ( m_pTrack == nullptr ) {
		ERRORLOG( "Track not properly set up" );
		return;
	}

	sortEvents( m_pEventList );

	int nLastTick = 0;
	for ( auto& ppEvent : *m_pEventList ) {
		ppEvent->m_nDeltaTime = ( ppEvent->m_nTicks - nLastTick ) *
			SMF::nTickFactor;
		nLastTick = ppEvent->m_nTicks;
		
		m_pTrack->addEvent( ppEvent );
	}

	m_pEventList->clear();
}

QString SMF0Writer::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[SMF0Writer] m_pEventList: \n" ).arg( sPrefix );
		for ( const auto& ppEvent : *m_pEventList ) {
			sOutput.append( QString( "%1%2\n" ).arg( sPrefix + s )
							.arg( ppEvent->toQString( "", true ) ) );
		}
		sOutput.append( QString( "%1%2m_pTrack: %3\n" ).arg( sPrefix )
						.arg( s ).arg( m_pTrack->toQString( s, false ) ) );
	}
	else {
		sOutput = QString( "[SMF0Writer] m_pEventList: [" );
		for ( const auto& ppEvent : *m_pEventList ) {
			sOutput.append( QString( "[%1] " )
							.arg( ppEvent->toQString( "", true ) ) );
		}
		sOutput.append( QString( "], m_pTrack: %1" )
						.arg( m_pTrack->toQString( "", true ) ) );
	}

	return sOutput;
}

};
