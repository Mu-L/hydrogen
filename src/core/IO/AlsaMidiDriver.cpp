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

#include <core/IO/AlsaMidiDriver.h>

#if defined(H2CORE_HAVE_ALSA) || _DOXYGEN_

#include <core/Preferences/Preferences.h>
#include <core/Hydrogen.h>
#include <core/AudioEngine/AudioEngine.h>

#include <core/Globals.h>

#include <pthread.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/Note.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>

namespace H2Core
{

pthread_t midiDriverThread;

bool isMidiDriverRunning = false;

snd_seq_t *seq_handle = nullptr;
int npfd;
struct pollfd *pfd;
int portId;
int clientId;
int outPortId;


void* alsaMidiDriver_thread( void* param )
{
	Base * __object = ( Base * )param;
	AlsaMidiDriver *pDriver = ( AlsaMidiDriver* )param;
	__INFOLOG( "starting" );

	if ( seq_handle != nullptr ) {
		__ERRORLOG( "seq_handle != NULL" );
		pthread_exit( nullptr );
	}

	int err;
	if ( ( err = snd_seq_open( &seq_handle, "hw", SND_SEQ_OPEN_DUPLEX, 0 ) ) < 0 ) {
		__ERRORLOG( QString( "Error opening ALSA sequencer: %1" ).arg( QString::fromLocal8Bit(snd_strerror(err)) ) );
		pthread_exit( nullptr );
	}

	snd_seq_set_client_name( seq_handle, "Hydrogen" );

	if ( ( portId = snd_seq_create_simple_port( 	seq_handle,
					"Hydrogen Midi-In",
					SND_SEQ_PORT_CAP_WRITE |
					SND_SEQ_PORT_CAP_SUBS_WRITE,
					SND_SEQ_PORT_TYPE_APPLICATION
											  )
		 ) < 0 ) {
		__ERRORLOG( "Error creating sequencer port." );
		pthread_exit( nullptr );
	}

	if ( ( outPortId = snd_seq_create_simple_port( 	seq_handle,
					"Hydrogen Midi-Out",
					SND_SEQ_PORT_CAP_READ |
					SND_SEQ_PORT_CAP_SUBS_READ,
					SND_SEQ_PORT_TYPE_APPLICATION
											  )
		 ) < 0 ) {
		__ERRORLOG( "Error creating sequencer port." );
		pthread_exit( nullptr );
	}

	clientId = snd_seq_client_id( seq_handle );

	int m_local_addr_inport = portId;
	int m_local_addr_outport = outPortId;	
	int m_local_addr_client = clientId;

	QString sPortName = Preferences::get_instance()->m_sMidiPortName;
	int m_dest_addr_port = -1;
	int m_dest_addr_client = -1;
	pDriver->getPortInfo( sPortName, m_dest_addr_client, m_dest_addr_port );
	__INFOLOG( "MIDI input port name: "  + sPortName );
	__INFOLOG( QString( "MIDI input addr client: %1").arg( m_dest_addr_client ) );
	__INFOLOG( QString( "MIDI input addr port: %1").arg( m_dest_addr_port ) );

	if ( ( m_dest_addr_port != -1 ) && ( m_dest_addr_client != -1 ) ) {
		snd_seq_port_subscribe_t *subs;
		snd_seq_port_subscribe_alloca( &subs );
		snd_seq_addr_t sender, dest;

		sender.client = m_dest_addr_client;
		sender.port = m_dest_addr_port;
		dest.client = m_local_addr_client;
		dest.port = m_local_addr_inport;

		/* set in and out ports */
		snd_seq_port_subscribe_set_sender( subs, &sender );
		snd_seq_port_subscribe_set_dest( subs, &dest );

		/* subscribe */
		int ret = snd_seq_subscribe_port( seq_handle, subs );
		if ( ret < 0 ) {
			__ERRORLOG( QString( "snd_seq_subscribe_port(%1:%2) error" ).arg( m_dest_addr_client ).arg( m_dest_addr_port ) );
		}
	}

	__INFOLOG( QString( "Midi input port at %1:%2" ).arg( clientId ).arg( portId ) );
	
	//Connect output port to predefined output
	sPortName = Preferences::get_instance()->m_sMidiOutputPortName;
	m_dest_addr_port = -1;
	m_dest_addr_client = -1;
	pDriver->getPortInfo( sPortName, m_dest_addr_client, m_dest_addr_port );
	__INFOLOG( "MIDI output port name: "  + sPortName );
	__INFOLOG( QString( "MIDI output addr client: %1").arg( m_dest_addr_client ) );
	__INFOLOG( QString( "MIDI output addr port: %1").arg( m_dest_addr_port ) );

	if ( ( m_dest_addr_port != -1 ) && ( m_dest_addr_client != -1 ) ) {
		snd_seq_port_subscribe_t *subs;
		snd_seq_port_subscribe_alloca( &subs );
		snd_seq_addr_t sender, dest;

		sender.client = m_local_addr_client;
		sender.port = m_local_addr_outport;
		dest.client = m_dest_addr_client;
		dest.port = m_dest_addr_port;

		/* set in and out ports */
		snd_seq_port_subscribe_set_sender( subs, &sender );
		snd_seq_port_subscribe_set_dest( subs, &dest );

		/* subscribe */
		int ret = snd_seq_subscribe_port( seq_handle, subs );
		if ( ret < 0 ) {
			__ERRORLOG( QString( "snd_seq_subscribe_port(%1:%2) error" ).arg( m_dest_addr_client ).arg( m_dest_addr_port ) );
		}
	}
	
	__INFOLOG( QString( "Midi output port at %1:%2" ).arg( clientId ).arg( outPortId ) );
	

	npfd = snd_seq_poll_descriptors_count( seq_handle, POLLIN );
	pfd = ( struct pollfd* )alloca( npfd * sizeof( struct pollfd ) );
	snd_seq_poll_descriptors( seq_handle, pfd, npfd, POLLIN );

	__INFOLOG( "MIDI Thread INIT" );
	while ( isMidiDriverRunning ) {
		if ( poll( pfd, npfd, 100 ) > 0 ) {
			pDriver->midi_action( seq_handle );
		}
	}
	snd_seq_close ( seq_handle );
	seq_handle = nullptr;
	__INFOLOG( "MIDI Thread DESTROY" );

	pthread_exit( nullptr );
	return nullptr;
}




AlsaMidiDriver::AlsaMidiDriver()
		: MidiInput(), MidiOutput(), Object<AlsaMidiDriver>()
{
//	infoLog("INIT");
}




AlsaMidiDriver::~AlsaMidiDriver()
{
	if ( isMidiDriverRunning ) {
		close();
	}
//	infoLog("DESTROY");
}

void AlsaMidiDriver::open()
{
	// start main thread
	isMidiDriverRunning = true;
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_create( &midiDriverThread, &attr, alsaMidiDriver_thread, ( void* )this );
}




void AlsaMidiDriver::close()
{
	isMidiDriverRunning = false;
	pthread_join( midiDriverThread, nullptr );
}





void AlsaMidiDriver::midi_action( snd_seq_t *seq_handle )
{
	auto pAudioEngine = Hydrogen::get_instance()->getAudioEngine();
	if ( ( pAudioEngine->getState() != AudioEngine::State::Ready ) &&
		 ( pAudioEngine->getState() != AudioEngine::State::Playing ) ) {
// 		ERRORLOG( "Skipping midi event! Audio Engine not ready." );
		return;
	}

//	bool useMidiTransport = true;

	snd_seq_event_t *ev;
	do {
		if ( !seq_handle ) {
			break;
		}
		snd_seq_event_input( seq_handle, &ev );

		if ( m_bActive && ev != nullptr ) {

			MidiMessage msg;

			switch ( ev->type ) {
			case SND_SEQ_EVENT_NOTEON:
				msg.m_type = MidiMessage::NOTE_ON;
				msg.m_nData1 = ev->data.note.note;
				msg.m_nData2 = ev->data.note.velocity;
				msg.m_nChannel = ev->data.control.channel;
				break;

			case SND_SEQ_EVENT_NOTEOFF:
				msg.m_type = MidiMessage::NOTE_OFF;
				msg.m_nData1 = ev->data.note.note;
				msg.m_nData2 = ev->data.note.velocity;
				msg.m_nChannel = ev->data.control.channel;
				break;

			case SND_SEQ_EVENT_CONTROLLER:
				msg.m_type = MidiMessage::CONTROL_CHANGE;
				msg.m_nData1 = ev->data.control.param;
				msg.m_nData2 = ev->data.control.value;
				msg.m_nChannel = ev->data.control.channel;
				break;

			case SND_SEQ_EVENT_PGMCHANGE:
				msg.m_type = MidiMessage::PROGRAM_CHANGE;
				msg.m_nData1 = ev->data.control.value;
				msg.m_nChannel = ev->data.control.channel;
				break;

			case SND_SEQ_EVENT_KEYPRESS:
				msg.m_type = MidiMessage::POLYPHONIC_KEY_PRESSURE;
				msg.m_nData1 = ev->data.note.note;
				msg.m_nData2 = ev->data.note.velocity;
				msg.m_nChannel = ev->data.control.channel;
				break;

			case SND_SEQ_EVENT_CHANPRESS:
				msg.m_type = MidiMessage::CHANNEL_PRESSURE;
				msg.m_nData1 = ev->data.control.param;
				msg.m_nData2 = ev->data.control.value;
				msg.m_nChannel = ev->data.control.channel;
				break;

			case SND_SEQ_EVENT_PITCHBEND:
				msg.m_type = MidiMessage::PITCH_WHEEL;
				msg.m_nData1 = ev->data.control.param;
				msg.m_nData2 = ev->data.control.value;
				msg.m_nChannel = ev->data.control.channel;
				break;

			case SND_SEQ_EVENT_SYSEX: {
				msg.m_type = MidiMessage::SYSEX;
				snd_midi_event_t *seq_midi_parser;
				if ( snd_midi_event_new( 32, &seq_midi_parser ) ) {
					ERRORLOG( "Error creating midi event parser" );
				}
				unsigned char midi_event_buffer[ 256 ];
				int _bytes_read = snd_midi_event_decode( seq_midi_parser, midi_event_buffer, 32, ev );

				for ( int i = 0; i < _bytes_read; ++i ) {
					msg.m_sysexData.push_back( midi_event_buffer[ i ] );
				}
			}
			break;

			case SND_SEQ_EVENT_QFRAME:
				msg.m_type = MidiMessage::QUARTER_FRAME;
				msg.m_nData1 = ev->data.control.value;
				msg.m_nData2 = ev->data.control.param;
				break;

			case SND_SEQ_EVENT_SONGPOS:
				msg.m_type = MidiMessage::SONG_POS;
				msg.m_nData1 = ev->data.control.value;
				msg.m_nData2 = ev->data.control.param;
				break;

			case SND_SEQ_EVENT_SONGSEL:
				msg.m_type = MidiMessage::SONG_SELECT;
				msg.m_nData1 = ev->data.control.value;
				msg.m_nData2 = ev->data.control.param;
				break;

			case SND_SEQ_EVENT_TUNE_REQUEST:
				msg.m_type = MidiMessage::TUNE_REQUEST;
				msg.m_nData1 = ev->data.control.value;
				msg.m_nData2 = ev->data.control.param;
				break;

			case SND_SEQ_EVENT_CLOCK:
				msg.m_type = MidiMessage::TIMING_CLOCK;
				break;

			case SND_SEQ_EVENT_START:
				msg.m_type = MidiMessage::START;
				break;

			case SND_SEQ_EVENT_CONTINUE:
				msg.m_type = MidiMessage::CONTINUE;
				break;

			case SND_SEQ_EVENT_STOP:
				msg.m_type = MidiMessage::STOP;
				break;

			case SND_SEQ_EVENT_SENSING:
				msg.m_type = MidiMessage::ACTIVE_SENSING;
				break;

			case SND_SEQ_EVENT_RESET:
				msg.m_type = MidiMessage::RESET;
				break;

			case SND_SEQ_EVENT_CLIENT_EXIT:
				INFOLOG( "SND_SEQ_EVENT_CLIENT_EXIT" );
				break;

			case SND_SEQ_EVENT_PORT_SUBSCRIBED:
				INFOLOG( "SND_SEQ_EVENT_PORT_SUBSCRIBED" );
				break;

			case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
				INFOLOG( "SND_SEQ_EVENT_PORT_UNSUBSCRIBED" );
				break;

			default:
				WARNINGLOG( QString( "Unknown MIDI Event. type = %1" ).arg( ( int )ev->type ) );
			}
			if ( msg.m_type != MidiMessage::UNKNOWN ) {
				handleMidiMessage( msg );
			}
		}
		snd_seq_free_event( ev );
	} while ( snd_seq_event_input_pending( seq_handle, 0 ) > 0 );
}

std::vector<QString> AlsaMidiDriver::getInputPortList()
{
	std::vector<QString> inputList;

	if ( seq_handle == nullptr ) {
		return inputList;
	}

	snd_seq_client_info_t *cinfo;	// client info
	snd_seq_port_info_t *pinfo;	// port info

	snd_seq_client_info_alloca( &cinfo );
	snd_seq_client_info_set_client( cinfo, -1 );

	/* while the next client one the sequencer is available */
	while ( snd_seq_query_next_client( seq_handle, cinfo ) >= 0 ) {
		// get client from cinfo
		int client = snd_seq_client_info_get_client( cinfo );

		// fill pinfo
		snd_seq_port_info_alloca( &pinfo );
		snd_seq_port_info_set_client( pinfo, client );
		snd_seq_port_info_set_port( pinfo, -1 );

		// while the next port is available
		while ( snd_seq_query_next_port( seq_handle, pinfo ) >= 0 ) {

			/* get its capability */
			int cap =  snd_seq_port_info_get_capability( pinfo );

			if ( snd_seq_client_id( seq_handle ) != snd_seq_port_info_get_client( pinfo ) && snd_seq_port_info_get_client( pinfo ) != 0 ) {
				// output ports
				if  (
					( cap & SND_SEQ_PORT_CAP_SUBS_WRITE ) != 0 &&
					snd_seq_client_id( seq_handle ) != snd_seq_port_info_get_client( pinfo )
				) {
					INFOLOG( snd_seq_port_info_get_name( pinfo ) );
					inputList.push_back( snd_seq_port_info_get_name( pinfo ) );
					//info.m_nClient = snd_seq_port_info_get_client(pinfo);
					//info.m_nPort = snd_seq_port_info_get_port(pinfo);
				}
			}
		}
	}

	return inputList;
}


std::vector<QString> AlsaMidiDriver::getOutputPortList()
{
	std::vector<QString> outputList;

	if ( seq_handle == nullptr ) {
		return outputList;
	}

	snd_seq_client_info_t *cinfo;	// client info
	snd_seq_port_info_t *pinfo;	// port info

	snd_seq_client_info_alloca( &cinfo );
	snd_seq_client_info_set_client( cinfo, -1 );

	/* while the next client one the sequencer is available */
	while ( snd_seq_query_next_client( seq_handle, cinfo ) >= 0 ) {
		// get client from cinfo
		int client = snd_seq_client_info_get_client( cinfo );

		// fill pinfo
		snd_seq_port_info_alloca( &pinfo );
		snd_seq_port_info_set_client( pinfo, client );
		snd_seq_port_info_set_port( pinfo, -1 );

		// while the next port is available
		while ( snd_seq_query_next_port( seq_handle, pinfo ) >= 0 ) {

			/* get its capability */
			int cap =  snd_seq_port_info_get_capability( pinfo );

			if ( snd_seq_client_id( seq_handle ) != snd_seq_port_info_get_client( pinfo ) && snd_seq_port_info_get_client( pinfo ) != 0 ) {
				// output ports
				if  (
					( cap & SND_SEQ_PORT_CAP_SUBS_READ ) != 0 &&
					snd_seq_client_id( seq_handle ) != snd_seq_port_info_get_client( pinfo )
				) {
					INFOLOG( snd_seq_port_info_get_name( pinfo ) );
					outputList.push_back( snd_seq_port_info_get_name( pinfo ) );
					//info.m_nClient = snd_seq_port_info_get_client(pinfo);
					//info.m_nPort = snd_seq_port_info_get_port(pinfo);
				}
			}
		}
	}

	return outputList;
}

void AlsaMidiDriver::getPortInfo( const QString& sPortName, int& nClient, int& nPort )
{
	if ( seq_handle == nullptr ) {
		ERRORLOG( "seq_handle = NULL " );
		return;
	}

	if ( sPortName == Preferences::getNullMidiPort() ) {
		nClient = -1;
		nPort = -1;
		return;
	}

	snd_seq_client_info_t *cinfo;	// client info
	snd_seq_port_info_t *pinfo;	// port info

	snd_seq_client_info_alloca( &cinfo );
	snd_seq_client_info_set_client( cinfo, -1 );

	/* while the next client one the sequencer is available */
	while ( snd_seq_query_next_client( seq_handle, cinfo ) >= 0 ) {
		// get client from cinfo
		int client = snd_seq_client_info_get_client( cinfo );

		// fill pinfo
		snd_seq_port_info_alloca( &pinfo );
		snd_seq_port_info_set_client( pinfo, client );
		snd_seq_port_info_set_port( pinfo, -1 );

		// while the next port is avail
		while ( snd_seq_query_next_port( seq_handle, pinfo ) >= 0 ) {
			int cap =  snd_seq_port_info_get_capability( pinfo );
			if ( snd_seq_client_id( seq_handle ) != snd_seq_port_info_get_client( pinfo ) && snd_seq_port_info_get_client( pinfo ) != 0 ) {
				// output ports
				if 	(
					( cap & SND_SEQ_PORT_CAP_SUBS_READ ) != 0 &&
					snd_seq_client_id( seq_handle ) != snd_seq_port_info_get_client( pinfo )
				) {
					QString sName = snd_seq_port_info_get_name( pinfo );
					if ( sName == sPortName ) {
						nClient = snd_seq_port_info_get_client( pinfo );
						nPort = snd_seq_port_info_get_port( pinfo );

						INFOLOG( QString( "nClient %1" ).arg( nClient ) );
						INFOLOG( QString( "nPort %1" ).arg( nPort ) );
						return;
					}
				}
			}
		}
	}
	ERRORLOG( "Midi port " + sPortName + " not found" );
}

void AlsaMidiDriver::handleQueueNote( std::shared_ptr<Note> pNote)
{
	if ( seq_handle == nullptr ) {
		ERRORLOG( "seq_handle = NULL " );
		return;
	}
	if ( pNote == nullptr || pNote->getInstrument() == nullptr ) {
		ERRORLOG( "Invalid note" );
		return;
	}

	int channel = pNote->getInstrument()->getMidiOutChannel();
	if (channel < 0) {
		return;
	}
	int key = pNote->getMidiKey();
	int velocity = pNote->getMidiVelocity();

	snd_seq_event_t ev;

	//Note off
	snd_seq_ev_clear(&ev);
		snd_seq_ev_set_source(&ev, outPortId);
		snd_seq_ev_set_subs(&ev);
		snd_seq_ev_set_direct(&ev);
	snd_seq_ev_set_noteoff(&ev, channel, key, velocity);
	snd_seq_event_output(seq_handle, &ev);
	snd_seq_drain_output(seq_handle);

	//Note on
	//snd_seq_event_input(seq_handle, &ev);
	snd_seq_ev_clear(&ev);
		snd_seq_ev_set_source(&ev, outPortId);
		snd_seq_ev_set_subs(&ev);
		snd_seq_ev_set_direct(&ev);
		//snd_seq_event_output_direct( seq_handle, ev );

	snd_seq_ev_set_noteon(&ev, channel, key, velocity);
	snd_seq_event_output(seq_handle, &ev);

		//snd_seq_free_event(ev);
	snd_seq_drain_output(seq_handle);
}


void AlsaMidiDriver::handleOutgoingControlChange( int param, int value, int channel )
{
	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);
	
	ev.type = SND_SEQ_EVENT_CONTROLLER;

	snd_seq_ev_set_source(&ev, outPortId);
	snd_seq_ev_set_subs(&ev);
	snd_seq_ev_set_direct(&ev);
	
	ev.data.control.param = param;
	ev.data.control.value = value;
	ev.data.control.channel = channel;
	
	snd_seq_event_output_direct(seq_handle, &ev);
}

void AlsaMidiDriver::handleQueueNoteOff( int channel, int key, int velocity )
{
	if ( seq_handle == nullptr ) {
		ERRORLOG( "seq_handle = NULL " );
		return;
	}

	if (channel < 0) {
		return;
	}

	snd_seq_event_t ev;

	//Note off
	snd_seq_ev_clear(&ev);
	snd_seq_ev_set_source(&ev, outPortId);
	snd_seq_ev_set_subs(&ev);
	snd_seq_ev_set_direct(&ev);
	snd_seq_ev_set_noteoff(&ev, channel, key, velocity);
	snd_seq_event_output(seq_handle, &ev);
	snd_seq_drain_output(seq_handle);
}

void AlsaMidiDriver::handleQueueAllNoteOff()
{
	if ( seq_handle == nullptr ) {
		ERRORLOG( "seq_handle = NULL " );
		return;
	}

	auto instList = Hydrogen::get_instance()->getSong()->getDrumkit()->getInstruments();

	unsigned int numInstruments = instList->size();
	for (int index = 0; index < numInstruments; ++index) {
		auto curInst = instList->get(index);

		int channel = curInst->getMidiOutChannel();
		if (channel < 0) {
			continue;
		}
		int key = curInst->getMidiOutNote();

		snd_seq_event_t ev;

		//Note off
		snd_seq_ev_clear(&ev);
			snd_seq_ev_set_source(&ev, outPortId);
			snd_seq_ev_set_subs(&ev);
			snd_seq_ev_set_direct(&ev);
		snd_seq_ev_set_noteoff(&ev, channel, key, 0);
		snd_seq_event_output(seq_handle, &ev);
		snd_seq_drain_output(seq_handle);
	}
}

QString AlsaMidiDriver::toQString( const QString& sPrefix, bool bShort ) const {
	QString s = Base::sPrintIndention;
	QString sOutput;
	if ( ! bShort ) {
		sOutput = QString( "%1[AlsaMidiDriver]\n" ).arg( sPrefix )
			.append( QString( "%1%2m_bActive: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( m_bActive ) )
			.append( QString( "%1%2isMidiDriverRunning: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( isMidiDriverRunning ) )
			.append( QString( "%1%2portId: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( portId ) )
			.append( QString( "%1%2clientId: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( clientId ) )
			.append( QString( "%1%2outPortId: %3\n" ).arg( sPrefix ).arg( s )
					 .arg( outPortId ) );
	} else {
		sOutput = QString( "[AlsaMidiDriver]" )
			.append( QString( " m_bActive: %1" ).arg( m_bActive ) )
			.append( QString( ", isMidiDriverRunning: %1" ).arg( isMidiDriverRunning ) )
			.append( QString( ", portId: %1" ).arg( portId ) )
			.append( QString( ", clientId: %1" ).arg( clientId ) )
			.append( QString( ", outPortId: %1" ).arg( outPortId ) );
	}

	return sOutput;
}
};

#endif // H2CORE_HAVE_ALSA

