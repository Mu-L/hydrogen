/*
 * Hydrogen
 * Copyright(c) 2002-2008 by Alex >Comix< Cominu [comix@users.sourceforge.net]
 * Copyright(c) 2008-2024 The hydrogen development team [hydrogen-devel@lists.sourceforge.net]
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

#include <core/Preferences/Preferences.h>
#include <core/Hydrogen.h>
#include <core/Basics/Playlist.h>
#include <core/Helpers/Filesystem.h>
#include <core/Helpers/Legacy.h>
#include <core/Helpers/Xml.h>
#include <core/EventQueue.h>

namespace H2Core
{

const QString PlaylistEntry::sLegacyEmptyScriptPath = "no Script";

Playlist::Playlist()
{
	m_sFilename = "";
	m_nActiveSongNumber = -1;
	m_bIsModified = false;
}

void Playlist::clear()
{
	m_entries.clear();
}

std::shared_ptr<Playlist> Playlist::load( const QString& sPath )
{
	std::shared_ptr<Playlist> pPlaylist;
	XMLDoc doc;
	if ( !doc.read( sPath, Filesystem::playlist_xsd_path() ) ) {
		pPlaylist = Legacy::load_playlist( sPath );
		if ( pPlaylist == nullptr ) {
			ERRORLOG( QString( "Unable to load playlist [%1]" )
					  .arg( sPath ) );
			return nullptr;
		}
		WARNINGLOG( QString( "update playlist %1" ).arg( sPath ) );
		pPlaylist->saveAs( sPath, true );
	}
	else {
		XMLNode root = doc.firstChildElement( "playlist" );
		if ( ! root.isNull() ) {
			if ( root.read_string( "name", "", false, false ).isEmpty() ) {
				WARNINGLOG( "Playlist does not contain name" );
			}
			pPlaylist = Playlist::load_from( root, sPath );
		}
		else {
			ERRORLOG( "playlist node not found" );
			pPlaylist = nullptr;
		}

	}

	return pPlaylist;
}

std::shared_ptr<Playlist> Playlist::load_from( const XMLNode& node,
											   const QString& sPath )
{
	QFileInfo fileInfo( sPath );

	auto pPlaylist = std::make_shared<Playlist>();
	pPlaylist->setFilename( fileInfo.absoluteFilePath() );

	XMLNode songsNode = node.firstChildElement( "songs" );
	if ( !songsNode.isNull() ) {
		XMLNode nextNode = songsNode.firstChildElement( "song" );
		while ( !nextNode.isNull() ) {

			QString songPath = nextNode.read_string( "path", "", false, false );
			if ( !songPath.isEmpty() ) {
				auto pEntry = std::make_shared<PlaylistEntry>();
				QFileInfo songPathInfo( fileInfo.absoluteDir(), songPath );
				pEntry->sFilePath = songPathInfo.absoluteFilePath();
				pEntry->bFileExists = songPathInfo.isReadable();
				pEntry->sScriptPath = nextNode.read_string( "scriptPath", "" );
				pEntry->bScriptEnabled = nextNode.read_bool( "scriptEnabled", false );
				pPlaylist->add( pEntry );
			}

			nextNode = nextNode.nextSiblingElement( "song" );
		}
	} else {
		WARNINGLOG( "songs node not found" );
	}
	return pPlaylist;
}

bool Playlist::saveAs( const QString& sTargetPath, bool bSilent ) {
	if ( ! bSilent  ) {
		INFOLOG( QString( "Saving playlist [%1] as [%2]" )
				 .arg( m_sFilename ).arg( sTargetPath ) );
	}

	setFilename( sTargetPath );

	return save( true );
}

bool Playlist::save( bool bSilent ) const {
	if ( m_sFilename.isEmpty() ) {
		ERRORLOG( "No filepath provided!" );
		return false;
	}

	if ( ! bSilent ) {
		INFOLOG( QString( "Saving playlist to [%1]" ).arg( m_sFilename ) );
	}

	XMLDoc doc;
	XMLNode root = doc.set_root( "playlist", "playlist" );

	QFileInfo info( m_sFilename );
	root.write_string( "name", info.fileName() );

	saveTo( root );
	return doc.write( m_sFilename );
}

void Playlist::saveTo( XMLNode& node ) const
{
	XMLNode songs = node.createNode( "songs" );

	for ( const auto& pEntry : m_entries ) {
		QString sPath = pEntry->sFilePath;
		if ( Preferences::get_instance()->isPlaylistUsingRelativeFilenames() ) {
			sPath = QDir( Filesystem::playlists_dir() ).relativeFilePath( sPath );
		}
		XMLNode song_node = songs.createNode( "song" );
		song_node.write_string( "path", sPath );
		song_node.write_string( "scriptPath", pEntry->sScriptPath );
		song_node.write_bool( "scriptEnabled", pEntry->bScriptEnabled);
	}
}

bool Playlist::add( std::shared_ptr<PlaylistEntry> pEntry, int nIndex ) {
	DEBUGLOG( QString( "%1 - %2" ).arg( pEntry->toQString() ).arg( nIndex ) );
	if ( nIndex == -1 ) {
		// Append at the end
		m_entries.push_back( pEntry );
	}
	else {
		// Index is allowed to be one more than the size of m_entries. This
		// represents appending an item.
		if ( nIndex < 0 || nIndex > size() ) {
			ERRORLOG( QString( "Index [%1] out of bound [0,%2]" )
					  .arg( nIndex ).arg( size() ) );
			return false;
		}

		std::vector<std::shared_ptr<PlaylistEntry>> newEntries;
		newEntries.resize( size() + 1 );
		int count = 0;
		for ( int ii = 0; ii <= size(); ii++ ) {
			if ( ii == nIndex ) {
				newEntries[ ii ] = pEntry;
			}
			else {
				newEntries[ ii ] = m_entries[ count ];
				count++;
			}
		}
		m_entries = newEntries;

		if ( nIndex <= m_nActiveSongNumber ) {
			m_nActiveSongNumber++;
		}
	}

	return true;
}

bool Playlist::remove( std::shared_ptr<PlaylistEntry> pEntry, int nIndex ) {
	DEBUGLOG( QString( "%1 - %2" ).arg( pEntry->toQString() ).arg( nIndex ) );

	int nFound = -1;

	if ( nIndex == -1 ) {
		// Remove the first occurrance
		for ( int ii = 0; ii < size(); ii++ ) {
			if ( m_entries[ ii ] == pEntry ) {
				m_entries.erase( m_entries.begin() + ii );
				nFound = ii;
				break;
			}
		}
	}
	else {
		if ( nIndex < 0 || nIndex >= size() ) {
			ERRORLOG( QString( "Index [%1] out of bound [0,%2]" )
					  .arg( nIndex ).arg( size() ) );
			return false;
		}

		if ( m_entries[ nIndex ] == pEntry ) {
			m_entries.erase( m_entries.begin() + nIndex );
			nFound = nIndex;
		}
	}

	if ( nFound == -1 ) {
		if ( nIndex == -1 ) {
			ERRORLOG( QString( "Unable to find entry [%1] in playlist [%2]" )
					  .arg( pEntry->toQString() ).arg( toQString() ) );
		} else {
			ERRORLOG( QString( "Unable to find entry [%1] at index [%2] in playlist [%3]" )
					  .arg( pEntry->toQString() ).arg( nIndex ).arg( toQString() ) );
		}
		return false;
	}

	if ( m_nActiveSongNumber == nFound ) {
		m_nActiveSongNumber = -1;
	}
	if ( m_nActiveSongNumber > nFound ) {
		m_nActiveSongNumber--;
	}

	return true;
}

/* This method is called by Event dispatcher thread ( GUI ) */
void Playlist::activateSong( int songNumber )
{
	setActiveSongNumber( songNumber );

	execScript( songNumber );
}

QString Playlist::getSongFilenameByNumber( int nSongNumber ) const
{
	bool Success = true;
	
	if ( size() == 0 || nSongNumber >= size() || nSongNumber < 0 ) {
		ERRORLOG( QString( "Unable to select song [%1/%2] " )
				  .arg( nSongNumber ).arg( size() ) );
		return "";
	}
	
	return get( nSongNumber )->sFilePath;
}

void Playlist::execScript( int nIndex ) const
{
#ifndef WIN32
	QString sFile = get( nIndex )->sScriptPath;

	if ( !get( nIndex )->bScriptEnabled ) {
		return;
	}
	if ( !QFile( sFile ).exists() ) {
		ERRORLOG( QString( "Script [%1] for playlist [%2] does not exist!" )
				  .arg( sFile ).arg( nIndex ) );
		return;
	}

	int nRes = std::system( sFile.toLocal8Bit() );
	if ( nRes != 0 ) {
		WARNINGLOG( QString( "Script [%1] for playlist [%2] exited with status code [%3]" )
					.arg( sFile ).arg( nIndex ).arg( nRes ) );
	}
#endif

	return;
}

QString Playlist::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[Playlist]\n" ).arg( sPrefix )
			.append( QString( "%1%2m_sFilename: %3\n" ).arg( sPrefix ).arg( s ).arg( m_sFilename ) )
			.append( QString( "%1%2m_nActiveSongNumber: %3\n" ).arg( sPrefix ).arg( s ).arg( m_nActiveSongNumber ) )
			.append( QString( "%1%2entries:\n" ).arg( sPrefix ).arg( s ) );
		if ( size() > 0 ) {
			for ( const auto& pEntry : m_entries ) {
				sOutput.append( QString( "%1\n" )
								.arg( pEntry->toQString( s + s, bShort ) ) );
			}
		}
		sOutput.append( QString( "%1%2m_bIsModified: %3\n" ).arg( sPrefix ).arg( s ).arg( m_bIsModified ) );
	} else {
		sOutput = QString( "[Playlist]" )
			.append( QString( " m_sFilename: %1" ).arg( m_sFilename ) )
			.append( QString( ", m_nActiveSongNumber: %1" ).arg( m_nActiveSongNumber ) )
			.append( ", entries: {" );
		if ( size() > 0 ) {
			for ( const auto& pEntry : m_entries ) {
				sOutput.append( QString( "%1, " )
								.arg( pEntry->toQString( "", bShort ) ) );
			}
		}
		sOutput.append( QString( "}, m_bIsModified: %1\n" ).arg( m_bIsModified ) );
	}

	return sOutput;
}

PlaylistEntry::PlaylistEntry() : sFilePath( "" ),
								 sScriptPath( "" ),
								 bScriptEnabled( false ),
								 bFileExists( false ) {
}

std::shared_ptr<PlaylistEntry> PlaylistEntry::fromMimeText( const QString& sText ) {
	auto pEntry = std::make_shared<PlaylistEntry>();

	auto mimeContents = sText.split( "::" );
	if ( mimeContents.size() >= 2 ) {
		pEntry->sFilePath = mimeContents[ 1 ];
	}
	if ( mimeContents.size() >= 3 ) {
		pEntry->sScriptPath = mimeContents[ 2 ];
	}
	if ( mimeContents.size() >= 4 ) {
		pEntry->bScriptEnabled = mimeContents[ 3 ] == "1" ? true : false;
	}

	return pEntry;
}

QString PlaylistEntry::toMimeText() const {
	return QString( "PlaylistEntry::%1::%2::%3" ).arg( sFilePath )
		.arg( sScriptPath ).arg( QString::number( bScriptEnabled ) );
}

QString PlaylistEntry::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[PlaylistEntry]\n" ).arg( sPrefix )
			.append( QString( "%1%2sFilePath: %3\n" ).arg( sPrefix )
					 .arg( s ).arg( sFilePath ) )
			.append( QString( "%1%2bFileExists: %3\n" ).arg( sPrefix )
					 .arg( s ).arg( bFileExists ) )
			.append( QString( "%1%2sScriptPath: %3\n" ).arg( sPrefix )
					 .arg( s ).arg( sScriptPath ) )
			.append( QString( "%1%2bScriptEnabled: %3\n" ).arg( sPrefix )
					 .arg( s ).arg( bScriptEnabled ) );
	}
	else {
		sOutput = QString( "[PlaylistEntry] " )
				.append( QString( "sFilePath: %1" ).arg( sFilePath ) )
			.append( QString( ", bFileExists: %1" ).arg( bFileExists ) )
			.append( QString( ", sScriptPath: %1" ).arg( sScriptPath ) )
			.append( QString( ", bScriptEnabled: %1" ).arg( bScriptEnabled ) );
	}
	
	return sOutput;
}
};
