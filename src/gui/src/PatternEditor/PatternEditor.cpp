/*
 * Hydrogen
 * Copyright(c) 2002-2008 by the Hydrogen Team
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

#include "PatternEditor.h"

#include "PatternEditorRuler.h"
#include "PatternEditorSidebar.h"
#include "PatternEditorPanel.h"
#include "PianoRollEditor.h"
#include "../CommonStrings.h"
#include "../HydrogenApp.h"
#include "../UndoActions.h"
#include "../Skin.h"

#include <core/Globals.h>
#include <core/Basics/Song.h>
#include <core/Hydrogen.h>
#include <core/Preferences/Preferences.h>
#include <core/Basics/Drumkit.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/InstrumentComponent.h>
#include <core/Basics/Pattern.h>
#include <core/Basics/PatternList.h>
#include <core/Basics/Adsr.h>
#include <core/Basics/Note.h>
#include <core/AudioEngine/AudioEngine.h>
#include <core/Helpers/Xml.h>

#include <QtMath>

using namespace std;
using namespace H2Core;


PatternEditor::PatternEditor( QWidget *pParent )
	: Object()
	, QWidget( pParent )
	, m_selection( this )
	, m_bEntered( false )
	, m_bCopyNotMove( false )
	, m_nTick( -1 )
	, m_editor( Editor::None )
	, m_property( Property::None )
	, m_nCursorPitch( 0 )
	, m_dragType( DragType::None )
	, m_nDragStartColumn( 0 )
	, m_nDragY( 0 )
	, m_dragStart( QPoint() )
	, m_update( Update::Background )
{
	m_pPatternEditorPanel = HydrogenApp::get_instance()->getPatternEditorPanel();

	const auto pPref = H2Core::Preferences::get_instance();

	m_fGridWidth = pPref->getPatternEditorGridWidth();
	m_nEditorWidth = PatternEditor::nMargin + m_fGridWidth * 4 * 4 *
		H2Core::nTicksPerQuarter;
	m_nActiveWidth = m_nEditorWidth;

	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking( true );

	// Popup context menu
	m_pPopupMenu = new QMenu( this );
	m_selectionActions.push_back( m_pPopupMenu->addAction( tr( "&Cut" ), this, SLOT( cut() ) ) );
	m_selectionActions.push_back( m_pPopupMenu->addAction( tr( "&Copy" ), this, SLOT( copy() ) ) );
	m_pPopupMenu->addAction( tr( "&Paste" ), this, SLOT( paste() ) );
	m_selectionActions.push_back( m_pPopupMenu->addAction( tr( "&Delete" ), this, SLOT( deleteSelection() ) ) );
	m_selectionActions.push_back( m_pPopupMenu->addAction( tr( "A&lign to grid" ), this, SLOT( alignToGrid() ) ) );
	m_selectionActions.push_back( m_pPopupMenu->addAction( tr( "Randomize velocity" ), this, SLOT( randomizeVelocity() ) ) );
	m_pPopupMenu->addAction( tr( "Select &all" ), this, SLOT( selectAll() ) );
	m_selectionActions.push_back( 	m_pPopupMenu->addAction( tr( "Clear selection" ), this, SLOT( selectNone() ) ) );
	connect( m_pPopupMenu, SIGNAL( aboutToShow() ),
			 this, SLOT( popupMenuAboutToShow() ) );
	connect( m_pPopupMenu, SIGNAL( aboutToHide() ),
			 this, SLOT( popupMenuAboutToHide() ) );


	updateWidth();

	qreal pixelRatio = devicePixelRatio();
	m_pBackgroundPixmap = new QPixmap( m_nEditorWidth * pixelRatio,
									   height() * pixelRatio );
	m_pPatternPixmap = new QPixmap( m_nEditorWidth * pixelRatio,
									height() * pixelRatio );
	m_pBackgroundPixmap->setDevicePixelRatio( pixelRatio );
	m_pPatternPixmap->setDevicePixelRatio( pixelRatio );
}

PatternEditor::~PatternEditor()
{
	m_draggedNotes.clear();

	if ( m_pPatternPixmap != nullptr ) {
		delete m_pPatternPixmap;
	}
	if ( m_pBackgroundPixmap != nullptr ) {
		delete m_pBackgroundPixmap;
	}
}

void PatternEditor::zoomIn()
{
	if (m_fGridWidth >= 3) {
		m_fGridWidth *= 2;
	} else {
		m_fGridWidth *= 1.5;
	}
}

void PatternEditor::zoomOut()
{
	if ( m_fGridWidth > 1.5 ) {
		if (m_fGridWidth > 3) {
			m_fGridWidth /= 2;
		} else {
			m_fGridWidth /= 1.5;
		}
	}
}

void PatternEditor::zoomLasso( float fOldGridWidth ) {
	if ( m_selection.isLasso() ) {
		const float fScale = m_fGridWidth / fOldGridWidth;
		m_selection.scaleLasso( fScale, PatternEditor::nMargin );
	}
}

QColor PatternEditor::computeNoteColor( float fVelocity ) {
	float fRed, fGreen, fBlue;

	const auto pPref = H2Core::Preferences::get_instance();

	QColor fullColor = pPref->getTheme().m_color.m_patternEditor_noteVelocityFullColor;
	QColor defaultColor = pPref->getTheme().m_color.m_patternEditor_noteVelocityDefaultColor;
	QColor halfColor = pPref->getTheme().m_color.m_patternEditor_noteVelocityHalfColor;
	QColor zeroColor = pPref->getTheme().m_color.m_patternEditor_noteVelocityZeroColor;

	// The colors defined in the Preferences correspond to fixed
	// velocity values. In case the velocity lies between two of those
	// the corresponding colors will be interpolated.
	float fWeightFull = 0;
	float fWeightDefault = 0;
	float fWeightHalf = 0;
	float fWeightZero = 0;

	if ( fVelocity >= VELOCITY_MAX ) {
		fWeightFull = 1.0;
	} else if ( fVelocity >= VELOCITY_DEFAULT ) {
		fWeightDefault = ( 1.0 - fVelocity )/ ( 1.0 - 0.8 );
		fWeightFull = 1.0 - fWeightDefault;
	} else if ( fVelocity >= 0.5 ) {
		fWeightHalf = ( 0.8 - fVelocity )/ ( 0.8 - 0.5 );
		fWeightDefault = 1.0 - fWeightHalf;
	} else {
		fWeightZero = ( 0.5 - fVelocity )/ ( 0.5 );
		fWeightHalf = 1.0 - fWeightZero;
	}

	fRed = fWeightFull * fullColor.redF() +
		fWeightDefault * defaultColor.redF() +
		fWeightHalf * halfColor.redF() + fWeightZero * zeroColor.redF();
	fGreen = fWeightFull * fullColor.greenF() +
		fWeightDefault * defaultColor.greenF() +
		fWeightHalf * halfColor.greenF() + fWeightZero * zeroColor.greenF();
	fBlue = fWeightFull * fullColor.blueF() +
		fWeightDefault * defaultColor.blueF() +
		fWeightHalf * halfColor.blueF() + fWeightZero * zeroColor.blueF();

	QColor color;
	color.setRedF( fRed );
	color.setGreenF( fGreen );
	color.setBlueF( fBlue );
	
	return color;
}


void PatternEditor::drawNote( QPainter &p, std::shared_ptr<H2Core::Note> pNote,
							  NoteStyle noteStyle ) const
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr || pNote == nullptr ) {
		return;
	}

	// Determine the center of the note symbol.
	int nY;
	if ( m_editor == Editor::DrumPattern ) {
		const int nRow = m_pPatternEditorPanel->findRowDB( pNote );
		nY = ( nRow * m_nGridHeight) + (m_nGridHeight / 2) - 3;

	}
	else {
		const auto selectedRow = m_pPatternEditorPanel->getRowDB(
			m_pPatternEditorPanel->getSelectedRowDB() );
		if ( ! selectedRow.contains( pNote ) ) {
			ERRORLOG( QString( "Provided note [%1] is not part of selected row [%2]" )
					  .arg( pNote->toQString() ).arg( selectedRow.toQString() ) );
			return;
		}

		nY = m_nGridHeight *
			Note::pitchToLine( pNote->getPitchFromKeyOctave() ) +
			(m_nGridHeight / 2) - 3;
	}
	const int nX = PatternEditor::nMargin + pNote->getPosition() * m_fGridWidth;

	p.setRenderHint( QPainter::Antialiasing );

	uint w = 8, h =  8;

	// NoPlayback is handled in here in order to not bloat calling routines
	// (since it has to be calculated for every note drawn).
	if ( ! checkNotePlayback( pNote ) ) {
		noteStyle =
			static_cast<NoteStyle>(noteStyle | NoteStyle::NoPlayback);
	}

	const int nNoteLength = calculateEffectiveNoteLength( pNote );
	if ( nNoteLength != pNote->getLength() ) {
		noteStyle =
			static_cast<NoteStyle>(noteStyle | NoteStyle::EffectiveLength);
	}

	QPen notePen, noteTailPen, highlightPen, movingPen;
	QBrush noteBrush, noteTailBrush, highlightBrush, movingBrush;
	applyColor( pNote, &notePen, &noteBrush, &noteTailPen, &noteTailBrush,
				&highlightPen, &highlightBrush, &movingPen, &movingBrush,
				noteStyle );

	QPoint movingOffset;
	if ( noteStyle & NoteStyle::Moved ) {
		QPoint delta = movingGridOffset();
		movingOffset = QPoint( delta.x() * m_fGridWidth,
							   delta.y() * m_nGridHeight );
	}

	if ( pNote->getNoteOff() == false ) {
		int width = w;

		if ( ! ( noteStyle & NoteStyle::Moved) &&
			 noteStyle & ( NoteStyle::Selected |
						   NoteStyle::Hovered |
						   NoteStyle::NoPlayback ) ) {
			p.setPen( highlightPen );
			p.setBrush( highlightBrush );
			p.drawEllipse( nX - 4 - 3, nY - 3, w + 6, h + 6 );
			p.setBrush( Qt::NoBrush );
		}

		// Draw tail
		if ( nNoteLength != LENGTH_ENTIRE_SAMPLE ) {
			if ( nNoteLength == pNote->getLength() ) {
				// When we deal with a genuine length of a note instead of an
				// indication when playback for this note will be stopped, we
				// have to take its pitch into account.
				float fNotePitch = pNote->getPitchFromKeyOctave();
				float fStep = Note::pitchToFrequency( ( double )fNotePitch );

				width = m_fGridWidth * nNoteLength / fStep;
			}
			else {
				width = m_fGridWidth * nNoteLength;
			}
			width = width - 1;	// lascio un piccolo spazio tra una nota ed un altra

			// Since the note body is transparent for an inactive note, we
			// try to start the tail at its boundary. For regular notes we
			// do not care about an overlap, as it ensures that there are no
			// white artifacts between tail and note body regardless of the
			// scale factor.
			if ( ! ( noteStyle & NoteStyle::Moved ) ) {
				if ( noteStyle & ( NoteStyle::Selected |
								   NoteStyle::Hovered |
								   NoteStyle::NoPlayback ) ) {
					p.setPen( highlightPen );
					p.setBrush( highlightBrush );
					// Tail highlight
					p.drawRect( nX - 3, nY - 1, width + 6, 3 + 6 );
					p.drawEllipse( nX - 4 - 3, nY - 3, w + 6, h + 6 );
					p.fillRect( nX - 4, nY, width, 3 + 4, highlightBrush );
				}

				p.setPen( noteTailPen );
				p.setBrush( noteTailBrush );

				int nRectOnsetX = nX;
				int nRectWidth = width;
				if ( noteStyle & NoteStyle::Background ) {
					nRectOnsetX = nRectOnsetX + w / 2;
					nRectWidth = nRectWidth - w / 2;
				}

				p.drawRect( nRectOnsetX, nY + 2, nRectWidth, 3 );
				p.drawLine( nX + width, nY, nX + width, nY + h );
			}
		}

		// Draw note
		if ( ! ( noteStyle & NoteStyle::Moved ) ) {
			p.setPen( notePen );
			p.setBrush( noteBrush );
			p.drawEllipse( nX -4 , nY, w, h );
		}
		else {
			p.setPen( movingPen );
			p.setBrush( movingBrush );

			if ( nNoteLength == LENGTH_ENTIRE_SAMPLE ) {
				p.drawEllipse( movingOffset.x() + nX -4 -2,
							   movingOffset.y() + nY -2 , w + 4, h + 4 );
			}
			else {
				// Moving note with tail

				const int nDiameterNote = w + 4;
				const int nHeightTail = 7;
				// Angle of triangle at note center with note radius as hypotenuse
				// and half the tail height as opposite.
				const int nAngleIntersection = static_cast<int>(
					std::round( qRadiansToDegrees(
									qAsin( static_cast<qreal>(nHeightTail) /
										   static_cast<qreal>(nDiameterNote) ) ) ) );

				const int nMoveX = movingOffset.x() + nX;
				const int nMoveY = movingOffset.y() + nY;

				p.drawArc( nMoveX - 4 - 2, nMoveY - 2, nDiameterNote, nDiameterNote,
						   nAngleIntersection * 16,
						   ( 360 - 2 * nAngleIntersection ) * 16 );

				p.drawLine( nMoveX + w - 2, nMoveY,
							nMoveX + width + 2, nMoveY );
				p.drawLine( nMoveX + width + 2, nMoveY,
							nMoveX + width + 2, nMoveY + nHeightTail );
				p.drawLine( nMoveX + w - 2, nMoveY + nHeightTail,
							nMoveX + width + 2, nMoveY + nHeightTail );
			}
		}
	}
	else if ( pNote->getNoteOff() ) {

		if ( ! ( noteStyle & NoteStyle::Moved ) ) {

			if ( noteStyle & ( NoteStyle::Selected |
							   NoteStyle::Hovered |
							   NoteStyle::NoPlayback ) ) {
				p.setPen( highlightPen );
				p.setBrush( highlightBrush );
				p.drawEllipse( nX - 4 - 3, nY - 3, w + 6, h + 6 );
				p.setBrush( Qt::NoBrush );
			}

			p.setPen( notePen );
			p.setBrush( noteBrush );
			p.drawEllipse( nX -4 , nY, w, h );
		}
		else {
			p.setPen( movingPen );
			p.setBrush( movingBrush );
			p.drawEllipse( movingOffset.x() + nX -4 -2, movingOffset.y() + nY -2,
						   w + 4, h + 4 );
		}
	}
}

void PatternEditor::eventPointToColumnRow( const QPoint& point, int* pColumn,
										   int* pRow, int* pRealColumn,
										   bool bUseFineGrained ) const {
	if ( pRow != nullptr ) {
		*pRow = static_cast<int>(
			std::floor( static_cast<float>(point.y()) /
						static_cast<float>(m_nGridHeight) ) );
	}

	if ( pColumn != nullptr ) {
		int nGranularity = 1;
		if ( ! ( bUseFineGrained &&
				 ! m_pPatternEditorPanel->isQuantized() ) ) {
			nGranularity = granularity();
		}
		const int nWidth = m_fGridWidth * nGranularity;
		int nColumn = ( point.x() - PatternEditor::nMargin + (nWidth / 2) ) /
			nWidth;
		*pColumn = std::max( 0, nColumn * nGranularity );
	}

	if ( pRealColumn != nullptr ) {
		if ( point.x() > PatternEditor::nMargin ) {
			*pRealColumn = static_cast<int>(
				std::floor( ( point.x() -
							  static_cast<float>(PatternEditor::nMargin) ) /
							static_cast<float>(m_fGridWidth) ) );
		}
		else {
			*pRealColumn = 0;
		}
	}
}

void PatternEditor::popupMenuAboutToShow() {
	if ( m_notesToSelectForPopup.size() > 0 ) {
		m_selection.clearSelection();

		for ( const auto& ppNote : m_notesToSelectForPopup ) {
			m_selection.addToSelection( ppNote );
		}

		m_pPatternEditorPanel->getVisibleEditor()->updateEditor( true );
		m_pPatternEditorPanel->getVisiblePropertiesRuler()->updateEditor( true );
	}
}

void PatternEditor::popupMenuAboutToHide() {
	if ( m_notesToSelectForPopup.size() > 0 ) {
		m_selection.clearSelection();

		m_pPatternEditorPanel->getVisibleEditor()->updateEditor( true );
		m_pPatternEditorPanel->getVisiblePropertiesRuler()->updateEditor( true );
	}
}

void PatternEditor::updateEditor( bool bPatternOnly )
{
	if ( updateWidth() ) {
		m_update = Update::Background;
	}
	else if ( bPatternOnly && m_update != Update::Background ) {
		// Background takes priority over Pattern.
		m_update = Update::Pattern;
	}
	else {
		m_update = Update::Background;
	}

	// update hovered notes
	if ( hasFocus() ) {
		updateHoveredNotesKeyboard( false );
		const QPoint globalPos = QCursor::pos();
		const QPoint widgetPos = mapFromGlobal( globalPos );
		if ( widgetPos.x() >= 0 && widgetPos.x() < width() &&
			 widgetPos.y() >= 0 && widgetPos.y() < height() ) {
			auto pEvent = new QMouseEvent(
				QEvent::MouseButtonRelease, widgetPos, globalPos, Qt::LeftButton,
				Qt::LeftButton, Qt::NoModifier );
			updateHoveredNotesMouse( pEvent, false );
		}
	}

	// redraw
	update();
}

void PatternEditor::selectNone()
{
	m_selection.clearSelection();
	m_selection.updateWidgetGroup();
}

void PatternEditor::showPopupMenu( QMouseEvent* pEvent )
{
	if ( m_editor == Editor::DrumPattern || m_editor == Editor::PianoRoll ) {
		// Enable or disable menu actions that only operate on selected notes.
		for ( auto & action : m_selectionActions ) {
			action->setEnabled( m_notesHoveredForPopup.size() > 0 );
		}
	}

	m_pPopupMenu->popup( pEvent->globalPos() );
}

///
/// Copy selection to clipboard in XML
///
void PatternEditor::copy( bool bHandleSetupTeardown )
{
	if ( bHandleSetupTeardown ) {
		popupSetup();
	}

	XMLDoc doc;
	XMLNode selection = doc.set_root( "noteSelection" );
	XMLNode noteList = selection.createNode( "noteList");
	XMLNode positionNode = selection.createNode( "sourcePosition" );
	bool bWroteNote = false;
	// "Top left" of selection, in the three dimensional time*instrument*pitch space.
	int nMinColumn, nMinRow, nMaxPitch;

	for ( const auto& ppNote : m_selection ) {
		const int nPitch = ppNote->getPitchFromKeyOctave();
		const int nColumn = ppNote->getPosition();
		const int nRow = m_pPatternEditorPanel->findRowDB( ppNote );
		if ( bWroteNote ) {
			nMinColumn = std::min( nColumn, nMinColumn );
			nMinRow = std::min( nRow, nMinRow );
			nMaxPitch = std::max( nPitch, nMaxPitch );
		} else {
			nMinColumn = nColumn;
			nMinRow = nRow;
			nMaxPitch = nPitch;
			bWroteNote = true;
		}
		XMLNode note_node = noteList.createNode( "note" );
		ppNote->saveTo( note_node );
	}

	if ( bWroteNote ) {
		positionNode.write_int( "minColumn", nMinColumn );
		positionNode.write_int( "minRow", nMinRow );
		positionNode.write_int( "maxPitch", nMaxPitch );
	} else {
		positionNode.write_int( "minColumn",
								m_pPatternEditorPanel->getCursorColumn() );
		positionNode.write_int( "minRow",
								m_pPatternEditorPanel->getSelectedRowDB() );
	}

	QClipboard *clipboard = QApplication::clipboard();
	clipboard->setText( doc.toString() );

	// This selection will probably be pasted at some point. So show the
	// keyboard cursor as this is the place where the selection will be pasted.
	handleKeyboardCursor( true );

	if ( bHandleSetupTeardown ) {
		popupTeardown();
	}
}


void PatternEditor::cut()
{
	popupSetup();

	copy( false );
	deleteSelection( false );

	popupTeardown();
}

///
/// Paste selection
///
/// Selection is XML containing notes, contained in a root 'note_selection' element.
///
void PatternEditor::paste()
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		// No pattern selected.
		return;
	}

	auto pHydrogenApp = HydrogenApp::get_instance();
	QClipboard *clipboard = QApplication::clipboard();
	const int nSelectedRow = m_pPatternEditorPanel->getSelectedRowDB();
	const auto selectedRow = m_pPatternEditorPanel->getRowDB( nSelectedRow );
	if ( selectedRow.nInstrumentID == EMPTY_INSTR_ID &&
		 selectedRow.sType.isEmpty() ) {
		DEBUGLOG( "Empty row" );
		return;
	}

	XMLNode noteList;
	int nDeltaPos = 0, nDeltaRow = 0, nDeltaPitch = 0;

	XMLDoc doc;
	if ( ! doc.setContent( clipboard->text() ) ) {
		// Pasted something that's not valid XML.
		return;
	}

	XMLNode selection = doc.firstChildElement( "noteSelection" );
	if ( ! selection.isNull() ) {

		// Got a noteSelection.
		// <noteSelection>
		//   <noteList>
		//     <note> ...
		noteList = selection.firstChildElement( "noteList" );
		if ( noteList.isNull() ) {
			return;
		}

		XMLNode positionNode = selection.firstChildElement( "sourcePosition" );

		// If position information is supplied in the selection, use
		// it to adjust the location relative to the current keyboard
		// input cursor.
		if ( ! positionNode.isNull() ) {
			int nCurrentPos = m_pPatternEditorPanel->getCursorColumn();
			nDeltaPos = nCurrentPos -
				positionNode.read_int( "minColumn", nCurrentPos );

			// In NotePropertiesRuler there is no vertical offset.
			if ( m_editor == Editor::PianoRoll ) {
				nDeltaPitch = m_nCursorPitch -
					positionNode.read_int( "maxPitch", m_nCursorPitch );
			}
			else if ( m_editor == Editor::DrumPattern ) {
				nDeltaRow = nSelectedRow -
					positionNode.read_int( "minRow", nSelectedRow );
			}
		}
	}
	else {

		XMLNode instrumentLine = doc.firstChildElement( "instrument_line" );
		if ( ! instrumentLine.isNull() ) {
			// Found 'instrument_line', structure is:
			// <instrument_line>
			//   <patternList>
			//     <pattern>
			//       <noteList>
			//         <note> ...
			XMLNode patternList = instrumentLine.firstChildElement( "patternList" );
			if ( patternList.isNull() ) {
				return;
			}
			XMLNode pattern = patternList.firstChildElement( "pattern" );
			if ( pattern.isNull() ) {
				return;
			}
			// Don't attempt to paste multiple patterns
			if ( ! pattern.nextSiblingElement( "pattern" ).isNull() ) {
				QMessageBox::information( this, "Hydrogen",
										  tr( "Cannot paste multi-pattern selection" ) );
				return;
			}
			noteList = pattern.firstChildElement( "noteList" );
			if ( noteList.isNull() ) {
				return;
			}
		}
	}

	m_selection.clearSelection();
	bool bAppendedToDB = false;

	if ( noteList.hasChildNodes() ) {

		pHydrogenApp->beginUndoMacro( tr( "paste notes" ) );
		for ( XMLNode n = noteList.firstChildElement( "note" ); ! n.isNull();
			  n = n.nextSiblingElement() ) {
			auto pNote = Note::loadFrom( n );
			if ( pNote == nullptr ) {
				ERRORLOG( QString( "Unable to load note from XML node [%1]" )
						  .arg( n.toQString() ) );
				continue;
			}

			const int nPos = pNote->getPosition() + nDeltaPos;
			if ( nPos < 0 || nPos >= pPattern->getLength() ) {
				continue;
			}

			int nInstrumentId;
			QString sType;
			DrumPatternRow targetRow;
			if ( m_editor == Editor::DrumPattern ) {
				const auto nNoteRow =
					m_pPatternEditorPanel->findRowDB( pNote, true );
				if ( nNoteRow != -1 ) {
					// Note belongs to a row already present in the DB.
					const int nRow = nNoteRow + nDeltaRow;
					if ( nRow < 0 ||
						 nRow >= m_pPatternEditorPanel->getRowNumberDB() ) {
						continue;
					}
					targetRow = m_pPatternEditorPanel->getRowDB( nRow );
					nInstrumentId = targetRow.nInstrumentID;
					sType = targetRow.sType;
				}
				else {
					// Note can not be represented in the current DB. This means
					// it might be a type-only one copied from a a different
					// pattern. We will append it to the DB.
					nInstrumentId = pNote->getInstrumentId();
					sType = pNote->getType();
					bAppendedToDB = true;
				}
			}
			else {
				targetRow = m_pPatternEditorPanel->getRowDB( nSelectedRow );
				nInstrumentId = targetRow.nInstrumentID;
				sType = targetRow.sType;
			}

			int nKey, nOctave;
			if ( m_editor == Editor::PianoRoll ) {
				const int nPitch = pNote->getPitchFromKeyOctave() + nDeltaPitch;
				if ( nPitch < KEYS_PER_OCTAVE * OCTAVE_MIN ||
					 nPitch >= KEYS_PER_OCTAVE * ( OCTAVE_MAX + 1 ) ) {
					continue;
				}

				nKey = Note::pitchToKey( nPitch );
				nOctave = Note::pitchToOctave( nPitch );
			}
			else {
				nKey = pNote->getKey();
				nOctave = pNote->getOctave();
			}

			pHydrogenApp->pushUndoCommand(
				new SE_addOrRemoveNoteAction(
					nPos,
					nInstrumentId,
					sType,
					m_pPatternEditorPanel->getPatternNumber(),
					pNote->getLength(),
					pNote->getVelocity(),
					pNote->getPan(),
					pNote->getLeadLag(),
					nKey,
					nOctave,
					pNote->getProbability(),
					/* bIsDelete */ false,
					/* bIsNoteOff */ pNote->getNoteOff(),
					targetRow.bMappedToDrumkit,
					AddNoteAction::AddToSelection ) );
		}
		pHydrogenApp->endUndoMacro();
	}

	if ( bAppendedToDB ) {
		// We added a note to the pattern currently not represented by the DB.
		// We have to force its update in order to avoid inconsistencies.
		const int nOldSize = m_pPatternEditorPanel->getRowNumberDB();
		m_pPatternEditorPanel->updateDB();
		m_pPatternEditorPanel->updateEditors( true );
		m_pPatternEditorPanel->resizeEvent( nullptr );

		// Select the append line
		m_pPatternEditorPanel->setSelectedRowDB( nOldSize );
	}
}

void PatternEditor::selectAllNotesInRow( int nRow, int nPitch )
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}

	const auto row = m_pPatternEditorPanel->getRowDB( nRow );

	m_selection.clearSelection();

	if ( nPitch != PITCH_INVALID ) {
		const auto key = Note::pitchToKey( nPitch );
		const auto octave = Note::pitchToOctave( nPitch );
		for ( const auto& [ _, ppNote ] : *pPattern->getNotes() ) {
			if ( ppNote != nullptr && row.contains( ppNote ) &&
				 ppNote->getKey() == key && ppNote->getOctave() == octave ) {
				m_selection.addToSelection( ppNote );
			}
		}
	}
	else {
		for ( const auto& [ _, ppNote ] : *pPattern->getNotes() ) {
			if ( ppNote != nullptr && row.contains( ppNote ) ) {
				m_selection.addToSelection( ppNote );
			}
		}
	}
	m_selection.updateWidgetGroup();
}


///
/// Align selected (or all) notes to the current grid
///
void PatternEditor::alignToGrid() {
	auto pHydrogen = Hydrogen::get_instance();
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		// No pattern selected.
		return;
	}

	popupSetup();

	validateSelection();
	if ( m_selection.isEmpty() ) {
		return;
	}

	// Every deleted note will be removed from the selection. Therefore, we can
	// not iterate the selection directly.
	std::vector< std::shared_ptr<Note> > notes;
	for ( const auto& ppNote : m_selection ) {
		notes.push_back( ppNote );
	}

	auto pHydrogenApp = HydrogenApp::get_instance();

	// Move the notes
	pHydrogenApp->beginUndoMacro( tr( "Align notes to grid" ) );

	for ( const auto& ppNote : notes ) {
		if ( ppNote == nullptr ) {
			continue;
		}

		const int nRow = m_pPatternEditorPanel->findRowDB( ppNote );
		const auto row = m_pPatternEditorPanel->getRowDB( nRow );
		const int nPosition = ppNote->getPosition();
		const int nNewInstrument = nRow;
		const int nGranularity = granularity();

		// Round to the nearest position in the current grid. We add 1 to round
		// up when the note is precisely in the middle. This allows us to change
		// a 4/4 pattern to a 6/8 swing feel by changing the grid to 1/8th
		// triplest, and hitting 'align'.
		const int nNewPosition = nGranularity *
			( (nPosition+(nGranularity/2)+1) / nGranularity );

		// Cache note properties since a potential first note deletion will also
		// call the note's destructor.
		const int nInstrumentId = ppNote->getInstrumentId();
		const QString sType = ppNote->getType();
		const int nLength = ppNote->getLength();
		const float fVelocity = ppNote->getVelocity();
		const float fPan = ppNote->getPan();
		const float fLeadLag = ppNote->getLeadLag();
		const int nKey = ppNote->getKey();
		const int nOctave = ppNote->getOctave();
		const float fProbability = ppNote->getProbability();
		const bool bNoteOff = ppNote->getNoteOff();
		const bool bIsMappedToDrumkit = ppNote->getInstrument() != nullptr;

		// Move note -> delete at source position
		pHydrogenApp->pushUndoCommand( new SE_addOrRemoveNoteAction(
						 nPosition,
						 nInstrumentId,
						 sType,
						 m_pPatternEditorPanel->getPatternNumber(),
						 nLength,
						 fVelocity,
						 fPan,
						 fLeadLag,
						 nKey,
						 nOctave,
						 fProbability,
						 /* bIsDelete */ true,
						 bNoteOff,
						 bIsMappedToDrumkit,
						 PatternEditor::AddNoteAction::None ) );

		auto addNoteAction = AddNoteAction::None;
		if ( m_notesHoveredForPopup.size() > 0 ) {
			for ( const auto& ppHoveredNote : m_notesHoveredForPopup ) {
				if ( ppNote == ppHoveredNote ) {
					addNoteAction = AddNoteAction::MoveCursorTo;
					break;
				}
			}
		}

		// Add at target position
		pHydrogenApp->pushUndoCommand( new SE_addOrRemoveNoteAction(
						 nNewPosition,
						 nInstrumentId,
						 sType,
						 m_pPatternEditorPanel->getPatternNumber(),
						 nLength,
						 fVelocity,
						 fPan,
						 fLeadLag,
						 nKey,
						 nOctave,
						 fProbability,
						 /* bIsDelete */ false,
						 bNoteOff,
						 bIsMappedToDrumkit,
						 addNoteAction ) );
	}

	pHydrogenApp->endUndoMacro();

	popupTeardown();
}


void PatternEditor::randomizeVelocity() {
	auto pHydrogen = Hydrogen::get_instance();
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		// No pattern selected. Nothing to be randomized.
		return;
	}

	popupSetup();

	validateSelection();
	if ( m_selection.isEmpty() ) {
		return;
	}

	auto pHydrogenApp = HydrogenApp::get_instance();

	pHydrogenApp->beginUndoMacro( tr( "Random velocity" ) );

	for ( const auto& ppNote : m_selection ) {
		if ( ppNote == nullptr ) {
			continue;
		}

		float fVal = ( rand() % 100 ) / 100.0;
		fVal = std::clamp( ppNote->getVelocity() + ( ( fVal - 0.50 ) / 2 ),
						   0.0, 1.0 );
		pHydrogenApp->pushUndoCommand(
			new SE_editNotePropertiesAction(
				PatternEditor::Property::Velocity,
				m_pPatternEditorPanel->getPatternNumber(),
				ppNote->getPosition(),
				ppNote->getInstrumentId(),
				ppNote->getInstrumentId(),
				ppNote->getType(),
				ppNote->getType(),
				fVal,
				ppNote->getVelocity(),
				ppNote->getPan(),
				ppNote->getPan(),
				ppNote->getLeadLag(),
				ppNote->getLeadLag(),
				ppNote->getProbability(),
				ppNote->getProbability(),
				ppNote->getLength(),
				ppNote->getLength(),
				ppNote->getKey(),
				ppNote->getKey(),
				ppNote->getOctave(),
				ppNote->getOctave() ) );
	}

	pHydrogenApp->endUndoMacro();

	std::vector< std::shared_ptr<Note> > notes;
	for ( const auto& ppNote : m_selection ) {
		if ( ppNote != nullptr ) {
			notes.push_back( ppNote );
		}
	}

	triggerStatusMessage( notes, Property::Velocity );

	popupTeardown();
}

void PatternEditor::mousePressEvent( QMouseEvent *ev ) {
	const auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}

	// Property drawing in the ruler is allowed to start within the margin.
	// There is currently no plan to introduce a widget within this margin and
	// in contrast to lasso selection this action is unique to the ruler.
	if ( ev->x() > m_nActiveWidth ||
		 ( ev->x() <= PatternEditor::nMarginSidebar &&
		   ! ( m_editor == Editor::NotePropertiesRuler &&
			   ev->button() == Qt::RightButton ) ) ) {
		if ( ! m_selection.isEmpty() ) {
			m_selection.clearSelection();
			m_pPatternEditorPanel->getVisibleEditor()->updateEditor( true );
			m_pPatternEditorPanel->getVisiblePropertiesRuler()->updateEditor( true );
		}
		return;
	}

	updateModifiers( ev );

	m_notesToSelectForPopup.clear();
	m_notesHoveredForPopup.clear();
	m_notesHoveredOnDragStart.clear();
	m_notesToSelect.clear();

	if ( ( ev->buttons() == Qt::LeftButton || ev->buttons() == Qt::RightButton ) &&
		 ! ( ev->modifiers() & Qt::ControlModifier ) ) {

		// When interacting with note(s) not already in a selection, we will
		// discard the current selection and add these notes under point to a
		// transient one.
		const auto notesUnderPoint = getElementsAtPoint(
			ev->pos(), getCursorMargin( ev ), pPattern );

		bool bSelectionHovered = false;
		for ( const auto& ppNote : notesUnderPoint ) {
			if ( ppNote != nullptr && m_selection.isSelected( ppNote ) ) {
				bSelectionHovered = true;
				break;
			}
		}

		// We honor the current selection.
		if ( bSelectionHovered ) {
			for ( const auto& ppNote : notesUnderPoint ) {
				if ( ppNote != nullptr && m_selection.isSelected( ppNote ) ) {
					m_notesHoveredOnDragStart.push_back( ppNote );
				}
			}
		}
		else {
			m_notesToSelect = notesUnderPoint;
			m_notesHoveredOnDragStart = notesUnderPoint;
		}

		if ( ev->button() == Qt::RightButton ) {
			m_notesToSelectForPopup = m_notesToSelect;
			m_notesHoveredForPopup = m_notesHoveredOnDragStart;
		}

		// Property drawing in the ruler must not select notes.
		if ( m_editor == Editor::NotePropertiesRuler &&
			 ev->button() == Qt::RightButton ) {
			m_notesToSelect.clear();
		}
	}

	// propagate event to selection. This could very well cancel a lasso created
	// via keyboard events.
	m_selection.mousePressEvent( ev );

	// Hide cursor in case this behavior was selected in the
	// Preferences.
	handleKeyboardCursor( false );
}

void PatternEditor::mouseClickEvent( QMouseEvent *ev )
{
	auto pHydrogenApp = HydrogenApp::get_instance();
	const auto pCommonStrings = pHydrogenApp->getCommonStrings();
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}

	updateModifiers( ev );

	int nRow, nColumn, nRealColumn;
	eventPointToColumnRow( ev->pos(), &nColumn, &nRow, &nRealColumn,
						   /* fineGrained */true );


	// Select the corresponding row
	if ( m_editor == Editor::DrumPattern ) {
		const auto row = m_pPatternEditorPanel->getRowDB( nRow );
		if ( row.nInstrumentID != EMPTY_INSTR_ID || ! row.sType.isEmpty() ) {
			m_pPatternEditorPanel->setSelectedRowDB( nRow );
		}
	}
	else if ( m_editor == Editor::PianoRoll ) {
		// Update the row of the piano roll itself.
		setCursorPitch( Note::lineToPitch( nRow ) );

		// Use the row of the DrumPatternEditor/DB for further note
		// interactions.
		nRow = m_pPatternEditorPanel->getSelectedRowDB();
	}

	// main button action
	if ( ev->button() == Qt::LeftButton &&
		 m_editor != Editor::NotePropertiesRuler ) {

		// Check whether an existing note or an empty grid cell was clicked.
		const auto notesAtPoint = getElementsAtPoint(
			ev->pos(), getCursorMargin( ev ), pPattern );
		if ( notesAtPoint.size() == 0 ) {
			// Empty grid cell

			// By pressing the Alt button the user can bypass quantization of
			// new note to the grid.
			const int nTargetColumn =
				m_pPatternEditorPanel->isQuantized() ? nColumn : nRealColumn;

			int nKey = KEY_MIN;
			int nOctave = OCTAVE_DEFAULT;
			if ( m_editor == Editor::PianoRoll ) {
				nOctave = Note::pitchToOctave( m_nCursorPitch );
				nKey = Note::pitchToKey( m_nCursorPitch );
			}

			m_pPatternEditorPanel->addOrRemoveNotes(
				nTargetColumn, nRow, nKey, nOctave,
				/* bDoAdd */true, /* bDoDelete */false,
				/* bIsNoteOff */ ev->modifiers() & Qt::ShiftModifier,
				PatternEditor::AddNoteAction::Playback );

			m_pPatternEditorPanel->setCursorColumn( nTargetColumn );
		}
		else {
			// Move cursor to center notes
			m_pPatternEditorPanel->setCursorColumn(
				notesAtPoint[ 0 ]->getPosition() );

			// Note(s) clicked. Delete them.
			pHydrogenApp->beginUndoMacro(
				pCommonStrings->getActionDeleteNotes() );
			for ( const auto& ppNote : notesAtPoint ) {
				pHydrogenApp->pushUndoCommand(
					new SE_addOrRemoveNoteAction(
						ppNote->getPosition(),
						ppNote->getInstrumentId(),
						ppNote->getType(),
						m_pPatternEditorPanel->getPatternNumber(),
						ppNote->getLength(),
						ppNote->getVelocity(),
						ppNote->getPan(),
						ppNote->getLeadLag(),
						ppNote->getKey(),
						ppNote->getOctave(),
						ppNote->getProbability(),
						/* bIsDelete */ true,
						/* bIsNoteOff */ ppNote->getNoteOff(),
						 ppNote->getInstrument() != nullptr,
						PatternEditor::AddNoteAction::None ) );
			}
			pHydrogenApp->endUndoMacro();
		}
		m_selection.clearSelection();
		updateHoveredNotesMouse( ev );

	}
	else if ( ev->button() == Qt::RightButton ) {
		if ( m_notesHoveredForPopup.size() > 0 ) {
			m_pPatternEditorPanel->setCursorColumn(
				m_notesHoveredForPopup[ 0 ]->getPosition() );
		}
		else {
			// For pasting we can not rely on the position of preexising notes.
			if ( m_pPatternEditorPanel->isQuantized() ) {
				m_pPatternEditorPanel->setCursorColumn( nColumn );
			}
			else {
				m_pPatternEditorPanel->setCursorColumn( nRealColumn );
			}
		}
		showPopupMenu( ev );
	}

	update();
}

void PatternEditor::mouseMoveEvent( QMouseEvent *ev )
{
	if ( m_pPatternEditorPanel->getPattern() == nullptr ) {
		return;
	}

	if ( m_notesToSelect.size() > 0 ) {
		if ( ev->buttons() == Qt::LeftButton ||
			 ev->buttons() == Qt::RightButton ) {
			m_selection.clearSelection();
			for ( const auto& ppNote : m_notesToSelect ) {
				m_selection.addToSelection( ppNote );
			}
		}
		else {
			m_notesToSelect.clear();
		}
	}

	updateModifiers( ev );

	// Check which note is hovered.
	updateHoveredNotesMouse( ev );

	if ( ev->buttons() != Qt::NoButton ) {
		m_selection.mouseMoveEvent( ev );
		if ( m_selection.isMoving() ) {
			m_pPatternEditorPanel->getVisibleEditor()->update();
			m_pPatternEditorPanel->getVisiblePropertiesRuler()->update();
			}
		else if ( syncLasso() ) {
			m_pPatternEditorPanel->getVisibleEditor()->updateEditor( true );
			m_pPatternEditorPanel->getVisiblePropertiesRuler()->updateEditor( true );
		}
	}
}

void PatternEditor::mouseReleaseEvent( QMouseEvent *ev )
{
	// Don't call updateModifiers( ev ) in here because we want to apply the
	// state of the modifiers used during the last update/rendering. Else the
	// user might position a note carefully and it jumps to different place
	// because she released the Alt modifier slightly earlier than the mouse
	// button.

	bool bUpdate = false;

	// In case we just cancelled a lasso, we have to tell the other editors.
	const bool oldState = m_selection.getSelectionState();
	m_selection.mouseReleaseEvent( ev );
	if ( oldState != m_selection.getSelectionState() ) {
		syncLasso();
		bUpdate = true;
	}

	m_notesHoveredOnDragStart.clear();

	if ( ev->button() == Qt::LeftButton && m_notesToSelect.size() > 0 ) {
		// We used a transient selection of note(s) at a single position.
		m_selection.clearSelection();
		m_notesToSelect.clear();
		bUpdate = true;
	}

	if ( bUpdate ) {
		m_pPatternEditorPanel->getVisibleEditor()->updateEditor( true );
		m_pPatternEditorPanel->getVisiblePropertiesRuler()->updateEditor( true );
	}
}

void PatternEditor::updateModifiers( QInputEvent *ev ) {
	m_pPatternEditorPanel->updateQuantization( ev );

	// Key: Ctrl + drag: copy notes rather than moving
	m_bCopyNotMove = ev->modifiers() & Qt::ControlModifier;

	if ( QKeyEvent* pKeyEvent = dynamic_cast<QKeyEvent*>( ev ) ) {
		// Keyboard events for press and release of modifier keys don't have
		// those keys in the modifiers set, so explicitly update these.
		bool bPressed = ev->type() == QEvent::KeyPress;
		if ( pKeyEvent->key() == Qt::Key_Control ) {
			m_bCopyNotMove = bPressed;
		}
	}

	if ( m_selection.isMouseGesture() && m_selection.isMoving() ) {
		// If a selection is currently being moved, change the cursor
		// appropriately. Selection will change it back after the move
		// is complete (or abandoned)
		if ( m_bCopyNotMove &&  cursor().shape() != Qt::DragCopyCursor ) {
			setCursor( QCursor( Qt::DragCopyCursor ) );
		}
		else if ( ! m_bCopyNotMove && cursor().shape() != Qt::DragMoveCursor ) {
			setCursor( QCursor( Qt::DragMoveCursor ) );
		}
	}
}

// Ensure updateModifiers() was called on the input event before calling this
// action!
int PatternEditor::getCursorMargin( QInputEvent* pEvent ) const {

	// Disabled quantization is used for more fine grained control throughout
	// Hydrogen and will diminish the cursor margin.
	if ( pEvent != nullptr && ! m_pPatternEditorPanel->isQuantized() ) {
		return 0;
	}

	const int nResolution = m_pPatternEditorPanel->getResolution();
	if ( nResolution < 32 ) {
		return PatternEditor::nDefaultCursorMargin;
	}
	else if ( nResolution < 4 * H2Core::nTicksPerQuarter ) {
		return PatternEditor::nDefaultCursorMargin / 2;
	}
	else {
		return 0;
	}
}

bool PatternEditor::checkDeselectElements( const std::vector<SelectionIndex>& elements )
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return false;
	}

	auto pCommonStrings = HydrogenApp::get_instance()->getCommonStrings();
	
	//	Hydrogen *pH = Hydrogen::get_instance();
	std::set< std::shared_ptr<Note> > duplicates;
	for ( const auto& ppNote : elements ) {
		if ( duplicates.find( ppNote ) != duplicates.end() ) {
			// Already marked ppNote as a duplicate of some other ppNote. Skip it.
			continue;
		}
		FOREACH_NOTE_CST_IT_BOUND_END( pPattern->getNotes(), it, ppNote->getPosition() ) {
			// Duplicate note of a selected note is anything occupying the same
			// position. Multiple notes sharing the same location might be
			// selected; we count these as duplicates too. They will appear in
			// both the duplicates and selection lists.
			if ( it->second != ppNote && ppNote->matchPosition( it->second ) ) {
				duplicates.insert( it->second );
			}
		}
	}
	if ( !duplicates.empty() ) {
		auto pPref = Preferences::get_instance();
		bool bOk = true;

		if ( pPref->getShowNoteOverwriteWarning() ) {
			m_selection.cancelGesture();
			QString sMsg ( tr( "Placing these notes here will overwrite %1 duplicate notes." ) );
			QMessageBox messageBox ( QMessageBox::Warning, "Hydrogen", sMsg.arg( duplicates.size() ),
									 QMessageBox::Cancel | QMessageBox::Ok, this );
			messageBox.setCheckBox( new QCheckBox( pCommonStrings->getMutableDialog() ) );
			messageBox.checkBox()->setChecked( false );
			bOk = messageBox.exec() == QMessageBox::Ok;
			if ( messageBox.checkBox()->isChecked() ) {
				pPref->setShowNoteOverwriteWarning( false );
			}
		}

		if ( bOk ) {
			std::vector< std::shared_ptr<Note> >overwritten;
			for ( const auto& pNote : duplicates ) {
				overwritten.push_back( pNote );
			}
			HydrogenApp::get_instance()->pushUndoCommand(
				new SE_deselectAndOverwriteNotesAction( elements, overwritten ) );

		} else {
			return false;
		}
	}
	return true;
}


void PatternEditor::deselectAndOverwriteNotes( const std::vector< std::shared_ptr<H2Core::Note> >& selected,
											   const std::vector< std::shared_ptr<H2Core::Note> >& overwritten )
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}
	
	// Iterate over all the notes in 'selected' and 'overwrite' by erasing any *other* notes occupying the
	// same position.
	auto pHydrogen = Hydrogen::get_instance();
	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );
	Pattern::notes_t *pNotes = const_cast< Pattern::notes_t *>( pPattern->getNotes() );
	for ( auto pSelectedNote : selected ) {
		m_selection.removeFromSelection( pSelectedNote, /* bCheck=*/false );
		bool bFoundExact = false;
		int nPosition = pSelectedNote->getPosition();
		for ( auto it = pNotes->lower_bound( nPosition ); it != pNotes->end() && it->first == nPosition; ) {
			auto pNote = it->second;
			if ( !bFoundExact && pSelectedNote->match( pNote ) ) {
				// Found an exact match. We keep this.
				bFoundExact = true;
				++it;
			}
			else if ( pNote->getInstrumentId() == pSelectedNote->getInstrumentId() &&
					  pNote->getType() == pSelectedNote->getType() &&
					  pNote->getKey() == pSelectedNote->getKey() &&
					  pNote->getOctave() == pSelectedNote->getOctave() &&
					  pNote->getPosition() == pSelectedNote->getPosition() ) {
				// Something else occupying the same position (which may or may not be an exact duplicate)
				it = pNotes->erase( it );
			}
			else {
				// Any other note
				++it;
			}
		}
	}
	pHydrogen->getAudioEngine()->unlock();
	pHydrogen->setIsModified( true );
}


void PatternEditor::undoDeselectAndOverwriteNotes( const std::vector< std::shared_ptr<H2Core::Note> >& selected,
												   const std::vector< std::shared_ptr<H2Core::Note> >& overwritten )
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}
	
	auto pHydrogen = Hydrogen::get_instance();
	// Restore previously-overwritten notes, and select notes that were selected before.
	m_selection.clearSelection( /* bCheck=*/false );
	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );
	for ( const auto& ppNote : overwritten ) {
		auto pNewNote = std::make_shared<Note>( ppNote );
		pPattern->insertNote( pNewNote );
	}
	// Select the previously-selected notes
	for ( auto pNote : selected ) {
		FOREACH_NOTE_CST_IT_BOUND_END( pPattern->getNotes(), it, pNote->getPosition() ) {
			if ( pNote->match( it->second ) ) {
				m_selection.addToSelection( it->second );
				break;
			}
		}
	}
	pHydrogen->getAudioEngine()->unlock();
	pHydrogen->setIsModified( true );
	m_pPatternEditorPanel->updateEditors( true );
}

QPoint PatternEditor::movingGridOffset( ) const {
	QPoint rawOffset = m_selection.movingOffset();

	// Quantization in y direction is mandatory. A note can not be placed
	// between lines.
	int nQuantY = m_nGridHeight;
	int nBiasY = nQuantY / 2;
	if ( rawOffset.y() < 0 ) {
		nBiasY = -nBiasY;
	}
	const int nOffsetY = (rawOffset.y() + nBiasY) / nQuantY;

	int nOffsetX;
	if ( ! m_pPatternEditorPanel->isQuantized() ) {
		// No quantization
		nOffsetX = static_cast<int>(
			std::floor( static_cast<float>(rawOffset.x()) / m_fGridWidth ) );
	}
	else {
		// Quantize offset to multiples of m_nGrid{Width,Height}
		const float fFactor = granularity();
		const int nQuantX = m_fGridWidth * fFactor;
		int nBiasX = nQuantX / 2;
		if ( rawOffset.x() < 0 ) {
			nBiasX = -nBiasX;
		}
		nOffsetX = fFactor * static_cast<int>((rawOffset.x() + nBiasX) / nQuantX);
	}


	return QPoint( nOffsetX, nOffsetY);
}

//! Draw lines for note grid.
void PatternEditor::drawGridLines( QPainter &p, const Qt::PenStyle& style ) const
{
	const auto pPref = H2Core::Preferences::get_instance();
	const std::vector<QColor> colorsActive = {
		QColor( pPref->getTheme().m_color.m_patternEditor_line1Color ),
		QColor( pPref->getTheme().m_color.m_patternEditor_line2Color ),
		QColor( pPref->getTheme().m_color.m_patternEditor_line3Color ),
		QColor( pPref->getTheme().m_color.m_patternEditor_line4Color ),
		QColor( pPref->getTheme().m_color.m_patternEditor_line5Color ),
	};
	const std::vector<QColor> colorsInactive = {
		QColor( pPref->getTheme().m_color.m_windowTextColor.darker( 170 ) ),
		QColor( pPref->getTheme().m_color.m_windowTextColor.darker( 190 ) ),
		QColor( pPref->getTheme().m_color.m_windowTextColor.darker( 210 ) ),
		QColor( pPref->getTheme().m_color.m_windowTextColor.darker( 230 ) ),
		QColor( pPref->getTheme().m_color.m_windowTextColor.darker( 250 ) ),
	};

	// In case quantization as turned off, notes can be moved at all possible
	// ticks. To indicate this state, we show less pronounced grid lines at all
	// additional positions.
	const auto lineStyleGridOff = Qt::DotLine;

	const bool bTriplets = m_pPatternEditorPanel->isUsingTriplets();

	auto lineStyle = style;

	// The following part is intended for the non-triplet grid lines. But
	// whenever quantization was turned off, we also use it to draw the less
	// pronounced grid lines.
	if ( ! bTriplets || ! m_pPatternEditorPanel->isQuantized() ) {
		// For each successive set of finer-spaced lines, the even
		// lines will have already been drawn at the previous coarser
		// pitch, so only the odd numbered lines need to be drawn.
		int nColour = 0;

		if ( bTriplets ) {
			nColour = colorsActive.size() - 1;
			lineStyle = lineStyleGridOff;
		}

		// Draw vertical lines. To minimise pen colour changes (and
		// avoid unnecessary division operations), we draw them in
		// multiple passes, of successively finer spacing (and
		// advancing the colour selection at each refinement) until
		// we've drawn enough to satisfy the resolution setting.
		//
		// The drawing sequence looks something like:
		// |       |       |       |         - first pass, all 1/4 notes
		// |   :   |   :   |   :   |   :     - second pass, odd 1/8th notes
		// | . : . | . : . | . : . | . : .   - third pass, odd 1/16th notes

		// First, quarter note markers. All the quarter note markers must be
		// drawn. These will be drawn on all resolutions.
		float fStep = H2Core::nTicksPerQuarter * m_fGridWidth;
		float x = PatternEditor::nMargin;
		p.setPen( QPen( colorsActive[ nColour ], 1, lineStyle ) );
		while ( x < m_nActiveWidth ) {
			p.drawLine( x, 1, x, m_nEditorHeight - 1 );
			x += fStep;
		}
			
		p.setPen( QPen( colorsInactive[ nColour ], 1, lineStyle ) );
		while ( x < m_nEditorWidth ) {
			p.drawLine( x, 1, x, m_nEditorHeight - 1 );
			x += fStep;
		}

		++nColour;

		// Resolution 4 was already taken into account above;
		std::vector<int> availableResolutions = { 8, 16, 32, 64,
		    4 * H2Core::nTicksPerQuarter };
		const int nResolution = m_pPatternEditorPanel->getResolution();

		for ( int nnRes : availableResolutions ) {
			if ( nnRes > nResolution ) {
				if ( m_pPatternEditorPanel->isQuantized() ) {
					break;
				}
				else {
					lineStyle = lineStyleGridOff;
					nColour = colorsActive.size();
				}
			}

			fStep = 4 * H2Core::nTicksPerQuarter / nnRes * m_fGridWidth;
			float x = PatternEditor::nMargin + fStep;
			p.setPen( QPen( colorsActive[ std::min( nColour, static_cast<int>(colorsActive.size()) - 1 ) ],
							1, lineStyle ) );

			if ( nnRes != 4 * H2Core::nTicksPerQuarter ) {
				// With each increase of resolution 1/4 -> 1/8 -> 1/16 -> 1/32
				// -> 1/64 the number of available notes doubles and all we need
				// to do is to draw another grid line right between two existing
				// ones.
				while ( x < m_nActiveWidth + fStep ) {
					p.drawLine( x, 1, x, m_nEditorHeight - 1 );
					x += fStep * 2;
				}
			}
			else {
				// When turning resolution off, things get a bit more tricky.
				// Between 1/64 -> 1/192 (1/(4 * H2Core::nTicksPerQuarter)) the
				// space between existing grid line will be filled by two
				// instead of one new line.
				while ( x < m_nActiveWidth + fStep ) {
					p.drawLine( x, 1, x, m_nEditorHeight - 1 );
					x += fStep;
					p.drawLine( x, 1, x, m_nEditorHeight - 1 );
					x += fStep * 2;
				}
			}

			p.setPen( QPen( colorsInactive[ std::min( nColour, static_cast<int>(colorsInactive.size()) - 1 ) ],
							1, lineStyle ) );
			if ( nnRes != 4 * H2Core::nTicksPerQuarter ||
				 pPref->getQuantizeEvents() ) {
				while ( x < m_nEditorWidth ) {
					p.drawLine( x, 1, x, m_nEditorHeight - 1 );
					x += fStep * 2;
				}
			}
			else {
				while ( x < m_nEditorWidth ) {
					p.drawLine( x, 1, x, m_nEditorHeight - 1 );
					x += fStep;
					p.drawLine( x, 1, x, m_nEditorHeight - 1 );
					x += fStep * 2;
				}
			}

			nColour++;
		}

	}

	if ( bTriplets ) {
		lineStyle = style;

		// Triplet line markers, we only differentiate colours on the
		// first of every triplet.
		float fStep = granularity() * m_fGridWidth;
		float x = PatternEditor::nMargin;
		p.setPen(  QPen( colorsActive[ 0 ], 1, lineStyle ) );
		while ( x < m_nActiveWidth ) {
			p.drawLine(x, 1, x, m_nEditorHeight - 1);
			x += fStep * 3;
		}
		
		p.setPen(  QPen( colorsInactive[ 0 ], 1, lineStyle ) );
		while ( x < m_nEditorWidth ) {
			p.drawLine(x, 1, x, m_nEditorHeight - 1);
			x += fStep * 3;
		}
		
		// Second and third marks
		x = PatternEditor::nMargin + fStep;
		p.setPen(  QPen( colorsActive[ 2 ], 1, lineStyle ) );
		while ( x < m_nActiveWidth + fStep ) {
			p.drawLine(x, 1, x, m_nEditorHeight - 1);
			p.drawLine(x + fStep, 1, x + fStep, m_nEditorHeight - 1);
			x += fStep * 3;
		}
		
		p.setPen( QPen( colorsInactive[ 2 ], 1, lineStyle ) );
		while ( x < m_nEditorWidth ) {
			p.drawLine(x, 1, x, m_nEditorHeight - 1);
			p.drawLine(x + fStep, 1, x + fStep, m_nEditorHeight - 1);
			x += fStep * 3;
		}
	}
}


void PatternEditor::applyColor( std::shared_ptr<H2Core::Note> pNote,
								QPen* pNotePen, QBrush* pNoteBrush,
								QPen* pNoteTailPen, QBrush* pNoteTailBrush,
								QPen* pHighlightPen, QBrush* pHighlightBrush,
								QPen* pMovingPen, QBrush* pMovingBrush,
								NoteStyle noteStyle ) const {
	
	const auto colorTheme =
		H2Core::Preferences::get_instance()->getTheme().m_color;

	const auto backgroundPenStyle = Qt::DotLine;
	const auto backgroundBrushStyle = Qt::Dense4Pattern;
	const auto foregroundPenStyle = Qt::SolidLine;
	const auto foregroundBrushStyle = Qt::SolidPattern;
	const auto movingPenStyle = Qt::DotLine;
	const auto movingBrushStyle = Qt::NoBrush;

	int nHue, nSaturation, nValue;

	// Note color
	QColor noteFillColor;
	if ( ! pNote->getNoteOff() ) {
		noteFillColor = PatternEditor::computeNoteColor( pNote->getVelocity() );
	} else {
		noteFillColor = colorTheme.m_patternEditor_noteOffColor;
	}

	// color base note will be filled with
	pNoteBrush->setColor( noteFillColor );

	if ( noteStyle & NoteStyle::Background ) {
		pNoteBrush->setStyle( backgroundBrushStyle );
	}
	else {
		pNoteBrush->setStyle( foregroundBrushStyle );
	}

	// outline color
	pNotePen->setColor( Qt::black );

	if ( pNote != nullptr && pNote->getNoteOff() ) {
		pNotePen->setStyle( Qt::NoPen );
	}
	else if ( noteStyle & NoteStyle::Background ) {
		pNotePen->setStyle( backgroundPenStyle );
	}
	else {
		pNotePen->setStyle( foregroundPenStyle );
	}

	// Tail color
	pNoteTailPen->setColor( pNotePen->color() );
	pNoteTailPen->setStyle( pNotePen->style() );

	if ( noteStyle & NoteStyle::EffectiveLength ) {
		// Use a more subtle version of the note off color. As this color is
		// surrounded by the note outline - which is always black - we do not
		// have to check the value but can always go for a more lighter color.
		QColor effectiveLengthColor( colorTheme.m_patternEditor_noteOffColor );
		effectiveLengthColor = effectiveLengthColor.lighter( 125 );
		pNoteTailBrush->setColor( effectiveLengthColor );
	}
	else {
		pNoteTailBrush->setColor( pNoteBrush->color() );
	}
	pNoteTailBrush->setStyle( pNoteBrush->style() );

	// Highlight color
	QColor selectionColor;
	if ( m_pPatternEditorPanel->hasPatternEditorFocus() ) {
		selectionColor = colorTheme.m_selectionHighlightColor;
	}
	else {
		selectionColor = colorTheme.m_selectionInactiveColor;
	}

	QColor highlightColor;
	if ( noteStyle & NoteStyle::Selected ) {
		// Selected notes have the highest priority
		highlightColor = selectionColor;
	}
	else if ( noteStyle & NoteStyle::NoPlayback ) {
		// Notes that won't be played back maintain their special color.
		highlightColor = colorTheme.m_muteColor;

		// The color of the mute button itself would be too flash and draw too
		// much attention to the note which are probably the ones the user does
		// not care about. We make the color more subtil.
		highlightColor.getHsv( &nHue, &nSaturation, &nValue );

		const int nSubtleValueFactor = 112;
		const int nSubtleSaturation = std::max(
			static_cast<int>(std::round( nSaturation * 0.85 )), 0 );
		highlightColor.setHsv( nHue, nSubtleSaturation, nValue );

		if ( Skin::moreBlackThanWhite( highlightColor ) ) {
			highlightColor = highlightColor.darker( nSubtleValueFactor );
		} else {
			highlightColor = highlightColor.lighter( nSubtleValueFactor );
		}
}
	else {
		highlightColor = selectionColor;
	}

	int nFactor = 100;
	if ( noteStyle & NoteStyle::Selected && noteStyle & NoteStyle::Hovered ) {
		nFactor = 107;
	}
	else if ( noteStyle & NoteStyle::Hovered ) {
		nFactor = 125;
	}

	if ( noteStyle & NoteStyle::Hovered ) {
		// Depending on the highlight color, we make it either darker or
		// lighter.
		if ( Skin::moreBlackThanWhite( highlightColor ) ) {
			highlightColor = highlightColor.lighter( nFactor );
		} else {
			highlightColor = highlightColor.darker( nFactor );
		}
	}

	pHighlightBrush->setColor( highlightColor );

	if ( noteStyle & NoteStyle::Background ) {
		pHighlightBrush->setStyle( backgroundBrushStyle );
	}
	else {
		pHighlightBrush->setStyle( foregroundBrushStyle );
	}

	if ( Skin::moreBlackThanWhite( highlightColor ) ) {
		pHighlightPen->setColor( Qt::white );
	} else {
		pHighlightPen->setColor( Qt::black );
	}

	if ( noteStyle & NoteStyle::Background ) {
		pHighlightPen->setStyle( backgroundPenStyle );
	}
	else {
		pHighlightPen->setStyle( foregroundPenStyle );
	}

	// Moving note color
	pMovingBrush->setStyle( movingBrushStyle );

	pMovingPen->setColor( Qt::black );
	pMovingPen->setStyle( movingPenStyle );
	pMovingPen->setWidth( 2 );
}

void PatternEditor::sortAndDrawNotes( QPainter& p,
									  std::vector< std::shared_ptr<Note> > notes,
									  NoteStyle baseStyle ) {
	std::sort( notes.begin(), notes.end(), Note::compare );

	// Prioritze selected notes over not selected ones.
	std::vector< std::shared_ptr<Note> > selectedNotes, notSelectedNotes;
	for ( const auto& ppNote : notes ) {
		if ( m_selection.isSelected( ppNote ) ) {
			selectedNotes.push_back( ppNote );
		}
		else {
			notSelectedNotes.push_back( ppNote );
		}
	}

	for ( const auto& ppNote : notSelectedNotes ) {
		drawNote( p, ppNote, baseStyle );
	}
	auto selectedStyle =
		static_cast<NoteStyle>(NoteStyle::Selected | baseStyle);
	for ( const auto& ppNote : selectedNotes ) {
		drawNote( p, ppNote, selectedStyle );
	}
}

///
/// Ensure selection only refers to valid notes, and does not contain any stale references to deleted notes.
///
void PatternEditor::validateSelection()
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}
	
	// Rebuild selection from valid notes.
	std::set< std::shared_ptr<Note> > valid;
	std::vector< std::shared_ptr<Note> > invalidated;
	FOREACH_NOTE_CST_IT_BEGIN_END(pPattern->getNotes(), it) {
		if ( m_selection.isSelected( it->second ) ) {
			valid.insert( it->second );
		}
	}
	for (auto i : m_selection ) {
		if ( valid.find(i) == valid.end()) {
			// Keep the note to invalidate, but don't remove from the selection while walking the selection
			// set.
			invalidated.push_back( i );
		}
	}
	for ( auto i : invalidated ) {
		m_selection.removeFromSelection( i, /* bCheck=*/false );
	}
}

void PatternEditor::deleteSelection( bool bHandleSetupTeardown )
{
	if ( bHandleSetupTeardown ) {
		popupSetup();
	}

	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		// No pattern selected.
		return;
	}

	if ( m_selection.begin() != m_selection.end() ) {
		// Selection exists, delete it.

		auto pHydrogenApp = HydrogenApp::get_instance();

		validateSelection();

		// Construct list of UndoActions to perform before performing any of them, as the
		// addOrDeleteNoteAction may delete duplicate notes in undefined order.
		std::list<QUndoCommand*> actions;
		for ( const auto pNote : m_selection ) {
			if ( pNote != nullptr && m_selection.isSelected( pNote ) ) {
				actions.push_back( new SE_addOrRemoveNoteAction(
									   pNote->getPosition(),
									   pNote->getInstrumentId(),
									   pNote->getType(),
									   m_pPatternEditorPanel->getPatternNumber(),
									   pNote->getLength(),
									   pNote->getVelocity(),
									   pNote->getPan(),
									   pNote->getLeadLag(),
									   pNote->getKey(),
									   pNote->getOctave(),
									   pNote->getProbability(),
									   true, // bIsDelete
									   pNote->getNoteOff(),
									   pNote->getInstrument() != nullptr,
									   PatternEditor::AddNoteAction::None ) );
			}
		}
		m_selection.clearSelection();

		if ( actions.size() > 0 ) {
			pHydrogenApp->beginUndoMacro(
				HydrogenApp::get_instance()->getCommonStrings()
				->getActionDeleteNotes() );
			for ( QUndoCommand *pAction : actions ) {
				pHydrogenApp->pushUndoCommand( pAction );
			}
			pHydrogenApp->endUndoMacro();
		}
	}

	if ( bHandleSetupTeardown ) {
		popupTeardown();
	}
}

// Selection manager interface
void PatternEditor::selectionMoveEndEvent( QInputEvent *ev )
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		// No pattern selected.
		return;
	}

	// Don't call updateModifiers( ev ) in here because we want to apply the
	// state of the modifiers used during the last update/rendering. Else the
	// user might position a note carefully and it jumps to different place
	// because she released the Alt modifier slightly earlier than the mouse
	// button.

	QPoint offset = movingGridOffset();
	if ( offset.x() == 0 && offset.y() == 0 ) {
		// Move with no effect.
		return;
	}

	validateSelection();

	const int nSelectedRow = m_pPatternEditorPanel->getSelectedRowDB();

	 auto pHydrogenApp = HydrogenApp::get_instance();

	if ( m_bCopyNotMove ) {
		pHydrogenApp->beginUndoMacro( tr( "copy notes" ) );
	} else {
		pHydrogenApp->beginUndoMacro( tr( "move notes" ) );
	}
	std::list< std::shared_ptr<Note> > selectedNotes;
	for ( auto pNote : m_selection ) {
		selectedNotes.push_back( pNote );
	}

	for ( auto pNote : selectedNotes ) {
		if ( pNote == nullptr ) {
			continue;
		}
		const int nPosition = pNote->getPosition();
		const int nNewPosition = nPosition + offset.x();

		const int nRow = m_pPatternEditorPanel->findRowDB( pNote );
		int nNewRow = nRow;
		// For all other editors the moved/copied note is still associated to
		// the same instrument.
		if ( m_editor == Editor::DrumPattern && offset.y() != 0 ) {
			nNewRow += offset.y();
		}
		const auto row = m_pPatternEditorPanel->getRowDB( nRow );
		const auto newRow = m_pPatternEditorPanel->getRowDB( nNewRow );

		int nNewKey = pNote->getKey();
		int nNewOctave = pNote->getOctave();
		int nNewPitch = pNote->getPitchFromKeyOctave();
		if ( m_editor == Editor::PianoRoll && offset.y() != 0 ) {
			nNewPitch -= offset.y();
			nNewKey = Note::pitchToKey( nNewPitch );
			nNewOctave = Note::pitchToOctave( nNewPitch );
		}

		// For NotePropertiesRuler there is no vertical displacement.

		bool bNoteInRange = nNewPosition >= 0 &&
			nNewPosition <= pPattern->getLength();
		if ( m_editor == Editor::DrumPattern ) {
			bNoteInRange = bNoteInRange && nNewRow >= 0 &&
				nNewRow <= m_pPatternEditorPanel->getRowNumberDB();
		}
		else if ( m_editor == Editor::PianoRoll ){
			bNoteInRange = bNoteInRange && nNewOctave >= OCTAVE_MIN &&
				nNewOctave <= OCTAVE_MAX;
		}

		// Cache note properties since a potential first note deletion will also
		// call the note's destructor.
		const int nLength = pNote->getLength();
		const float fVelocity = pNote->getVelocity();
		const float fPan = pNote->getPan();
		const float fLeadLag = pNote->getLeadLag();
		const int nKey = pNote->getKey();
		const int nOctave = pNote->getOctave();
		const float fProbability = pNote->getProbability();
		const bool bNoteOff = pNote->getNoteOff();
		const bool bIsMappedToDrumkit = pNote->getInstrument() != nullptr;

		// We'll either select the new, duplicated note or the new, moved
		// replacement of the note.
		m_selection.removeFromSelection( pNote, false );

		if ( ! m_bCopyNotMove ) {
			// Note is moved either out of range or to a new position. Delete
			// the note at the source position.
			pHydrogenApp->pushUndoCommand(
				new SE_addOrRemoveNoteAction(
					nPosition,
					pNote->getInstrumentId(),
					pNote->getType(),
					m_pPatternEditorPanel->getPatternNumber(),
					nLength,
					fVelocity,
					fPan,
					fLeadLag,
					nKey,
					nOctave,
					fProbability,
					/* bIsDelete */ true,
					bNoteOff,
					bIsMappedToDrumkit,
					PatternEditor::AddNoteAction::None ) );
		}

		auto addNoteAction = AddNoteAction::AddToSelection;
		// Check whether the note was hovered when the drag move action was
		// started. If so, we will move the keyboard cursor to the resulting
		// position.
		for ( const auto ppHoveredNote : m_notesHoveredOnDragStart ) {
			if ( ppHoveredNote == pNote ) {
				addNoteAction = static_cast<AddNoteAction>(
					AddNoteAction::AddToSelection | AddNoteAction::MoveCursorTo );
				break;
			}
		}

		if ( bNoteInRange ) {
			// Create a new note at the target position
			pHydrogenApp->pushUndoCommand(
				new SE_addOrRemoveNoteAction(
					nNewPosition,
					newRow.nInstrumentID,
					newRow.sType,
					m_pPatternEditorPanel->getPatternNumber(),
					nLength,
					fVelocity,
					fPan,
					fLeadLag,
					nNewKey,
					nNewOctave,
					fProbability,
					/* bIsDelete */ false,
					bNoteOff,
					bIsMappedToDrumkit,
					addNoteAction ) );
		}
	}

	// Selecting the clicked row
	auto pMouseEvent = dynamic_cast<QMouseEvent*>(ev);
	if ( pMouseEvent != nullptr ) {
		int nRow;
		eventPointToColumnRow( pMouseEvent->pos(), nullptr, &nRow, nullptr );

		if ( m_editor == Editor::DrumPattern ) {
			m_pPatternEditorPanel->setSelectedRowDB( nRow );
		}
		else if ( m_editor == Editor::PianoRoll ) {
			setCursorPitch( Note::lineToPitch( nRow ) );
		}

		auto hoveredNotes = getElementsAtPoint(
			pMouseEvent->pos(), getCursorMargin( ev ) );
		if ( hoveredNotes.size() > 0 ) {
			m_pPatternEditorPanel->setCursorColumn(
				hoveredNotes[ 0 ]->getPosition(), true );
		}
	}

	pHydrogenApp->endUndoMacro();
}

void PatternEditor::scrolled( int nValue ) {
	UNUSED( nValue );
	update();
}

int PatternEditor::granularity() const {
	int nBase;
	if ( m_pPatternEditorPanel->isUsingTriplets() ) {
		nBase = 3;
	}
	else {
		nBase = 4;
	}
	return 4 * 4 * H2Core::nTicksPerQuarter /
		( nBase * m_pPatternEditorPanel->getResolution() );
}

void PatternEditor::keyPressEvent( QKeyEvent *ev, bool bFullUpdate )
{
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}

	auto pHydrogenApp = HydrogenApp::get_instance();
	const bool bOldCursorHidden = pHydrogenApp->hideKeyboardCursor();

	const int nWordSize = 5;

	// Checks whether the notes at point are part of the current selection. If
	// not, the latter is cleared and notes at point/cursor will be selected
	// instead.
	auto selectNotesAtPoint = [&]() {
		const auto notesUnderPoint = getElementsAtPoint(
			getCursorPosition(), 0, pPattern );
		if ( notesUnderPoint.size() == 0 ) {
			return false;
		}

		bool bNotesSelected = false;
		if ( ! m_selection.isEmpty() ) {
			for ( const auto& ppNote : notesUnderPoint ) {
				if ( m_selection.isSelected( ppNote ) ) {
					bNotesSelected = true;
					break;
				}
			}
		}

		if ( ! bNotesSelected ) {
			m_selection.clearSelection();
			for ( const auto& ppNote : notesUnderPoint ) {
				m_selection.addToSelection( ppNote );
			}

			return true;
		}

		return false;
	};

	bool bUnhideCursor = ev->key() != Qt::Key_Delete;

	auto pCleanedEvent = new QKeyEvent(
		QEvent::KeyPress, ev->key(), Qt::NoModifier, ev->text() );

	// Check whether the event was already handled by a method of a child class.
	if ( ! ev->isAccepted() ) {
		updateModifiers( ev );

		if ( ev->matches( QKeySequence::MoveToNextChar ) ||
			 ev->matches( QKeySequence::SelectNextChar ) ||
			 ( ev->modifiers() & Qt::AltModifier && (
				 pCleanedEvent->matches( QKeySequence::MoveToNextChar ) ||
				 pCleanedEvent->matches( QKeySequence::SelectNextChar ) ) ) ) {
			// ->
			m_pPatternEditorPanel->moveCursorRight( ev );
		}
		else if ( ev->matches( QKeySequence::MoveToNextWord ) ||
				  ev->matches( QKeySequence::SelectNextWord ) ) {
			// -->
			m_pPatternEditorPanel->moveCursorRight( ev, nWordSize );
		}
		else if ( ev->matches( QKeySequence::MoveToEndOfLine ) ||
				  ev->matches( QKeySequence::SelectEndOfLine ) ) {
			// -->|
			m_pPatternEditorPanel->setCursorColumn( pPattern->getLength() );
		}
		else if ( ev->matches( QKeySequence::MoveToPreviousChar ) ||
				  ev->matches( QKeySequence::SelectPreviousChar ) ||
				  ( ev->modifiers() & Qt::AltModifier && (
					  pCleanedEvent->matches( QKeySequence::MoveToPreviousChar ) ||
					  pCleanedEvent->matches( QKeySequence::SelectPreviousChar ) ) ) ) {
			// <-
			m_pPatternEditorPanel->moveCursorLeft( ev );
		}
		else if ( ev->matches( QKeySequence::MoveToPreviousWord ) ||
				  ev->matches( QKeySequence::SelectPreviousWord ) ) {
			// <--
			m_pPatternEditorPanel->moveCursorLeft( ev, nWordSize );
		}
		else if ( ev->matches( QKeySequence::MoveToStartOfLine ) ||
				  ev->matches( QKeySequence::SelectStartOfLine ) ) {
			// |<--
			m_pPatternEditorPanel->setCursorColumn( 0 );
		}
		else if ( ev->matches( QKeySequence::SelectAll ) ) {
			// Key: Ctrl + A: Select all
			bUnhideCursor = false;
			selectAll();
		}
		else if ( ev->matches( QKeySequence::Deselect ) ) {
			// Key: Shift + Ctrl + A: clear selection
			bUnhideCursor = false;
			selectNone();
		}
		else if ( ev->matches( QKeySequence::Copy ) ) {
			bUnhideCursor = false;
			const bool bTransientSelection = selectNotesAtPoint();

			copy();

			if ( bTransientSelection ) {
				m_selection.clearSelection();
			}
		}
		else if ( ev->matches( QKeySequence::Paste ) ) {
			bUnhideCursor = false;
			paste();
		}
		else if ( ev->matches( QKeySequence::Cut ) ) {
			bUnhideCursor = false;
			const bool bTransientSelection = selectNotesAtPoint();

			cut();

			if ( bTransientSelection ) {
				m_selection.clearSelection();
			}
		}
		else {
			ev->ignore();
			return;
		}
	}

	// synchronize lassos
	auto pVisibleEditor = m_pPatternEditorPanel->getVisibleEditor();
	// In case we use keyboard events to _continue_ an existing lasso in
	// NotePropertiesRuler started in DrumPatternEditor (followed by moving
	// focus to NPR using tab key), DrumPatternEditor has to be used to update
	// the shared set of selected notes. Else, only notes of the current row
	// will be added after an update.
	if ( m_editor == Editor::NotePropertiesRuler &&
		 pVisibleEditor->m_selection.isLasso() && m_selection.isLasso() &&
		 dynamic_cast<DrumPatternEditor*>(pVisibleEditor) != nullptr ) {
		pVisibleEditor->m_selection.updateKeyboardCursorPosition();
		bFullUpdate = pVisibleEditor->syncLasso() || bFullUpdate;
	}
	else {
		m_selection.updateKeyboardCursorPosition();
		bFullUpdate = syncLasso() || bFullUpdate;
	}
	updateHoveredNotesKeyboard();

	if ( bUnhideCursor ) {
		handleKeyboardCursor( bUnhideCursor );
	}

	if ( bFullUpdate ) {
		// Notes have might become selected. We have to update the background as
		// well.
		m_pPatternEditorPanel->getVisibleEditor()->updateEditor( true );
		m_pPatternEditorPanel->getVisiblePropertiesRuler()->updateEditor( true );
	}
	else {
		m_pPatternEditorPanel->getVisibleEditor()->update();
		m_pPatternEditorPanel->getVisiblePropertiesRuler()->update();
	}

	if ( ! ev->isAccepted() ) {
		ev->accept();
	}
}

void PatternEditor::handleKeyboardCursor( bool bVisible ) {
	auto pHydrogenApp = HydrogenApp::get_instance();
	const bool bOldCursorHidden = pHydrogenApp->hideKeyboardCursor();

	pHydrogenApp->setHideKeyboardCursor( ! bVisible );

	// Only update on state changes
	if ( bOldCursorHidden != pHydrogenApp->hideKeyboardCursor() ) {
		updateHoveredNotesKeyboard();
		if ( bVisible ) {
			m_selection.updateKeyboardCursorPosition();
			m_pPatternEditorPanel->ensureVisible();

			if ( m_selection.isLasso() && m_update != Update::Background ) {
				// Since the event was used to alter the note selection, we need
				// to repainting all note symbols (including whether or not they
				// are selected).
				m_update = Update::Pattern;
			}
		}

		m_pPatternEditorPanel->getSidebar()->updateEditor();
		m_pPatternEditorPanel->getPatternEditorRuler()->update();
		m_pPatternEditorPanel->getVisibleEditor()->update();
		m_pPatternEditorPanel->getVisiblePropertiesRuler()->update();
	}
}

void PatternEditor::keyReleaseEvent( QKeyEvent *ev ) {
	// Don't call updateModifiers( ev ) in here because we want to apply the
	// state of the modifiers used during the last update/rendering. Else the
	// user might position a note carefully and it jumps to different place
	// because she released the Alt modifier slightly earlier than the mouse
	// button.
}

void PatternEditor::enterEvent( QEvent *ev ) {
	UNUSED( ev );
	m_bEntered = true;

	// Update focus, hovered notes and selection color.
	m_pPatternEditorPanel->updateEditors( true );
}

void PatternEditor::leaveEvent( QEvent *ev ) {
	UNUSED( ev );
	m_bEntered = false;

	if ( m_pPatternEditorPanel->getHoveredNotes().size() > 0 ) {
		std::vector< std::pair< std::shared_ptr<Pattern>,
								std::vector< std::shared_ptr<Note> > > > empty;
		// Takes care of the update.
		m_pPatternEditorPanel->setHoveredNotesMouse( empty );
	}

	// Ending the enclosing undo context. This is key to enable the Undo/Redo
	// buttons in the main menu again and it feels like a good rule of thumb to
	// consider an action done whenever the user moves mouse or cursor away from
	// the widget.
	HydrogenApp::get_instance()->endUndoContext();

	// Update focus, hovered notes and selection color.
	m_pPatternEditorPanel->updateEditors( true );
}

void PatternEditor::focusInEvent( QFocusEvent *ev ) {
	UNUSED( ev );
	if ( ev->reason() == Qt::TabFocusReason ||
		 ev->reason() == Qt::BacktabFocusReason ) {
		handleKeyboardCursor( true );
	}

	// Update hovered notes, cursor, background color, selection color...
	m_pPatternEditorPanel->updateEditors();
}

void PatternEditor::focusOutEvent( QFocusEvent *ev ) {
	UNUSED( ev );
	// Update hovered notes, cursor, background color, selection color...
	m_pPatternEditorPanel->updateEditors();
}

void PatternEditor::paintEvent( QPaintEvent* ev )
{
	if (!isVisible()) {
		return;
	}

	auto pPattern = m_pPatternEditorPanel->getPattern();

	const auto pPref = Preferences::get_instance();

	const qreal pixelRatio = devicePixelRatio();
	if ( pixelRatio != m_pBackgroundPixmap->devicePixelRatio() ||
		 m_update == Update::Background ) {
		createBackground();
	}

	if ( m_update == Update::Background || m_update == Update::Pattern ) {
		drawPattern();
		m_update = Update::None;
	}

	QPainter painter( this );
	painter.drawPixmap( ev->rect(), *m_pPatternPixmap,
						QRectF( pixelRatio * ev->rect().x(),
								pixelRatio * ev->rect().y(),
								pixelRatio * ev->rect().width(),
								pixelRatio * ev->rect().height() ) );

	// Draw playhead
	if ( m_nTick != -1 ) {

		const int nOffset = Skin::getPlayheadShaftOffset();
		const int nX = static_cast<int>(
			static_cast<float>(PatternEditor::nMargin) +
			static_cast<float>(m_nTick) * m_fGridWidth );
		Skin::setPlayheadPen( &painter, false );
		painter.drawLine( nX, 0, nX, height() );
	}

	drawFocus( painter );

	m_selection.paintSelection( &painter );

	// Draw cursor
	if ( ! HydrogenApp::get_instance()->hideKeyboardCursor() &&
		 m_pPatternEditorPanel->hasPatternEditorFocus() &&
		 pPattern != nullptr ) {
		QColor cursorColor( pPref->getTheme().m_color.m_cursorColor );
		if ( ! hasFocus() ) {
			cursorColor.setAlpha( Skin::nInactiveCursorAlpha );
		}

		QPen p( cursorColor );
		p.setWidth( 2 );
		painter.setPen( p );
		painter.setBrush( Qt::NoBrush );
		painter.setRenderHint( QPainter::Antialiasing );
		painter.drawRoundedRect( getKeyboardCursorRect(), 4, 4 );
	}
}

void PatternEditor::drawPattern()
{
	const qreal pixelRatio = devicePixelRatio();

	QPainter p( m_pPatternPixmap );
	// copy the background image
	p.drawPixmap( rect(), *m_pBackgroundPixmap,
						QRectF( pixelRatio * rect().x(),
								pixelRatio * rect().y(),
								pixelRatio * rect().width(),
								pixelRatio * rect().height() ) );

	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}
	const auto pPref = H2Core::Preferences::get_instance();
	const QFont font( pPref->getTheme().m_font.m_sApplicationFontFamily,
					  getPointSize( pPref->getTheme().m_font.m_fontSize ) );
	const QColor textColor(
		pPref->getTheme().m_color.m_patternEditor_noteVelocityDefaultColor );
	QColor textBackgroundColor( textColor );
	textBackgroundColor.setAlpha( 150 );

	validateSelection();

	const auto selectedRow = m_pPatternEditorPanel->getRowDB(
		m_pPatternEditorPanel->getSelectedRowDB() );

	// We count notes in each position so we can display markers for rows which
	// have more than one note in the same position (a chord or genuine
	// duplicates).
	int nLastColumn = -1;
	// Aggregates the notes for various rows (key) and one specific column.
	std::map< int, std::vector< std::shared_ptr<Note> > > notesAtRow;
	struct PosCount {
		int nRow;
		int nColumn;
		int nNotes;
	};
	std::vector<PosCount> posCounts;
	for ( const auto& ppPattern : m_pPatternEditorPanel->getPatternsToShow() ) {
		posCounts.clear();
		const auto baseStyle = ppPattern == pPattern ?
			NoteStyle::Foreground : NoteStyle::Background;

		const auto fontColor = ppPattern == pPattern ?
			textColor : textBackgroundColor;

		for ( const auto& [ nnColumn, ppNote ] : *ppPattern->getNotes() ) {
			if ( nnColumn >= ppPattern->getLength() ) {
				// Notes are located beyond the active length of the editor and
				// aren't visible even when drawn.
				break;
			}
			if ( ppNote == nullptr || ( m_editor == Editor::PianoRoll &&
										! selectedRow.contains( ppNote ) ) ) {
				continue;
			}

			int nRow = -1;
			nRow = m_pPatternEditorPanel->findRowDB( ppNote );
			auto row = m_pPatternEditorPanel->getRowDB( nRow );
			if ( nRow == -1 ||
				 ( row.nInstrumentID == EMPTY_INSTR_ID && row.sType.isEmpty() ) ) {
				ERRORLOG( QString( "Note [%1] not associated with DB" )
						  .arg( ppNote->toQString() ) );
				m_pPatternEditorPanel->printDB();
				continue;
			}

			if ( m_editor == Editor::PianoRoll ) {
				nRow = Note::pitchToLine( ppNote->getPitchFromKeyOctave() );
			}

			// Check for duplicates
			if ( nnColumn != nLastColumn ) {
				// New column

				for ( const auto& [ nnRow, nnotes ] : notesAtRow ) {
					sortAndDrawNotes( p, nnotes, baseStyle );
					if ( nnotes.size() > 1 ) {
						posCounts.push_back(
							{ nnRow, nLastColumn,
							  static_cast<int>(nnotes.size()) } );
					}
				}

				nLastColumn = nnColumn;
				notesAtRow.clear();
			}


			if ( notesAtRow.find( nRow ) == notesAtRow.end() ) {
				notesAtRow[ nRow ] =
					std::vector< std::shared_ptr<Note> >{ ppNote };
			}
			else {
				notesAtRow[ nRow ].push_back( ppNote);
			}
		}

		// Handle last column too
		for ( const auto& [ nnRow, nnotes ] : notesAtRow ) {
			sortAndDrawNotes( p, nnotes, baseStyle );
			if ( nnotes.size() > 1 ) {
				posCounts.push_back( { nnRow, nLastColumn,
						static_cast<int>(nnotes.size()) } );
			}
		}
		notesAtRow.clear();

		// Go through used rows list and draw markers for superimposed notes
		for ( const auto [ nnRow, nnColumn, nnNotes ] : posCounts ) {
			// Draw "2x" text to the left of the note
			const int x = PatternEditor::nMargin +
				( nnColumn * m_fGridWidth );
			const int y = nnRow * m_nGridHeight;
			const int boxWidth = 128;

			p.setFont( font );
			p.setPen( fontColor );

			p.drawText(
				QRect( x - boxWidth - 6, y, boxWidth, m_nGridHeight ),
				Qt::AlignRight | Qt::AlignVCenter,
				( QString( "%1" ) + QChar( 0x00d7 )).arg( nnNotes ) );
		}
	}
}

void PatternEditor::drawFocus( QPainter& p ) {

	if ( ! m_bEntered && ! hasFocus() ) {
		return;
	}

	const auto pPref = H2Core::Preferences::get_instance();

	QColor color = pPref->getTheme().m_color.m_highlightColor;

	// If the mouse is placed on the widget but the user hasn't clicked it yet,
	// the highlight will be done more transparent to indicate that keyboard
	// inputs are not accepted yet.
	if ( ! hasFocus() ) {
		color.setAlpha( 125 );
	}

	const QScrollArea* pScrollArea;

	if ( m_editor == Editor::DrumPattern ) {
		pScrollArea = m_pPatternEditorPanel->getDrumPatternEditorScrollArea();
	}
	else if ( m_editor == Editor::PianoRoll ) {
		pScrollArea = m_pPatternEditorPanel->getPianoRollEditorScrollArea();
	}
	else if ( m_editor == Editor::NotePropertiesRuler ) {
		switch ( m_property ) {
		case PatternEditor::Property::Velocity:
			pScrollArea = m_pPatternEditorPanel->getNoteVelocityScrollArea();
			break;
		case PatternEditor::Property::Pan:
			pScrollArea = m_pPatternEditorPanel->getNotePanScrollArea();
			break;
		case PatternEditor::Property::LeadLag:
			pScrollArea = m_pPatternEditorPanel->getNoteLeadLagScrollArea();
			break;
		case PatternEditor::Property::KeyOctave:
			pScrollArea = m_pPatternEditorPanel->getNoteKeyOctaveScrollArea();
			break;
		case PatternEditor::Property::Probability:
			pScrollArea = m_pPatternEditorPanel->getNoteProbabilityScrollArea();
			break;
		case PatternEditor::Property::None:
		default:
			return;
		}
	}

	const int nStartY = pScrollArea->verticalScrollBar()->value();
	const int nStartX = pScrollArea->horizontalScrollBar()->value();
	int nEndY = nStartY + pScrollArea->viewport()->size().height();
	if ( m_editor == Editor::DrumPattern ) {
		nEndY = std::min( static_cast<int>(m_nGridHeight) *
						  m_pPatternEditorPanel->getRowNumberDB(), nEndY );
	}
	// In order to match the width used in the DrumPatternEditor.
	int nEndX = std::min( nStartX + pScrollArea->viewport()->size().width(),
								static_cast<int>( m_nEditorWidth ) );

	int nMargin;
	if ( nEndX == static_cast<int>( m_nEditorWidth ) ) {
		nEndX = nEndX - 2;
		nMargin = 1;
	} else {
		nMargin = 0;
	}

	QPen pen( color );
	pen.setWidth( 4 );
	p.setPen( pen );
	p.drawLine( QPoint( nStartX, nStartY ), QPoint( nEndX, nStartY ) );
	p.drawLine( QPoint( nStartX, nStartY ), QPoint( nStartX, nEndY ) );
	p.drawLine( QPoint( nEndX, nEndY ), QPoint( nStartX, nEndY ) );

	if ( nMargin != 0 ) {
		// Since for all other lines we are drawing at a border with just half
		// of the line being painted in the visual viewport, there has to be
		// some tweaking since the NotePropertiesRuler is paintable to the
		// right.
		pen.setWidth( 2 );
		p.setPen( pen );
	}
	p.drawLine( QPoint( nEndX + nMargin, nStartY ),
					  QPoint( nEndX + nMargin, nEndY ) );
}

void PatternEditor::drawBorders( QPainter& p ) {
	const auto pPref = H2Core::Preferences::get_instance();

	const QColor borderColor(
		pPref->getTheme().m_color.m_patternEditor_lineColor );
	const QColor borderInactiveColor(
		pPref->getTheme().m_color.m_windowTextColor.darker( 170 ) );

	p.setPen( borderColor );
	p.drawLine( 0, 0, m_nActiveWidth, 0 );
	p.drawLine( 0, m_nEditorHeight - 1,
					  m_nActiveWidth, m_nEditorHeight - 1 );

	if ( m_nActiveWidth + 1 < m_nEditorWidth ) {
		p.setPen( QPen( borderInactiveColor, 1, Qt::SolidLine ) );
		p.drawLine( m_nActiveWidth, 0, m_nEditorWidth, 0 );
		p.drawLine( m_nActiveWidth, m_nEditorHeight - 1,
						  m_nEditorWidth, m_nEditorHeight - 1 );
		p.drawLine( m_nEditorWidth - 1, 0,
						  m_nEditorWidth - 1, m_nEditorHeight );
	}
	else {
		p.drawLine( m_nActiveWidth, 0,
						  m_nActiveWidth, m_nEditorHeight );
	}
}

void PatternEditor::createBackground() {
}

bool PatternEditor::updateWidth() {
	auto pHydrogen = H2Core::Hydrogen::get_instance();
	auto pPattern = m_pPatternEditorPanel->getPattern();

	int nEditorWidth, nActiveWidth;
	if ( pPattern != nullptr ) {
		nActiveWidth = PatternEditor::nMargin + m_fGridWidth *
			pPattern->getLength();
		
		// In case there are other patterns playing which are longer
		// than the selected one, their notes will be placed using a
		// different color set between m_nActiveWidth and
		// m_nEditorWidth.
		if ( pHydrogen->getMode() == Song::Mode::Song &&
			 pPattern != nullptr && pPattern->isVirtual() &&
			 ! pHydrogen->isPatternEditorLocked() ) {
			nEditorWidth =
				std::max( PatternEditor::nMargin + m_fGridWidth *
						  pPattern->longestVirtualPatternLength() + 1,
						  static_cast<float>(nActiveWidth) );
		}
		else if ( PatternEditorPanel::isUsingAdditionalPatterns( pPattern ) ) {
			nEditorWidth =
				std::max( PatternEditor::nMargin + m_fGridWidth *
						  pHydrogen->getAudioEngine()->getPlayingPatterns()->longestPatternLength( false ) + 1,
						  static_cast<float>(nActiveWidth) );
		}
		else {
			nEditorWidth = nActiveWidth;
		}
	}
	else {
		nEditorWidth = PatternEditor::nMargin + 4 * H2Core::nTicksPerQuarter *
			m_fGridWidth;
		nActiveWidth = nEditorWidth;
	}

	if ( m_nEditorWidth != nEditorWidth || m_nActiveWidth != nActiveWidth ) {
		m_nEditorWidth = nEditorWidth;
		m_nActiveWidth = nActiveWidth;

		resize( m_nEditorWidth, m_nEditorHeight );

		return true;
	}

	return false;
}

void PatternEditor::updatePosition( float fTick ) {
	if ( m_nTick == (int)fTick ) {
		return;
	}

	float fDiff = m_fGridWidth * (fTick - m_nTick);

	m_nTick = fTick;

	int nOffset = Skin::getPlayheadShaftOffset();
	int nX = static_cast<int>(static_cast<float>(PatternEditor::nMargin) +
							  static_cast<float>(m_nTick) *
							  m_fGridWidth );

	QRect updateRect( nX -2, 0, 4 + Skin::nPlayheadWidth, height() );
	update( updateRect );
	if ( fDiff > 1.0 || fDiff < -1.0 ) {
		// New cursor is far enough away from the old one that the single update rect won't cover both. So
		// update at the old location as well.
		updateRect.translate( -fDiff, 0 );
		update( updateRect );
	}
}

void PatternEditor::mouseDragStartEvent( QMouseEvent *ev ) {
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		return;
	}

	auto pHydrogenApp = HydrogenApp::get_instance();

	m_property = m_pPatternEditorPanel->getSelectedNoteProperty();

	if ( ev->button() == Qt::RightButton ) {
		updateModifiers( ev );

		// Adjusting note properties.
		const auto notesAtPoint = getElementsAtPoint(
			ev->pos(), getCursorMargin( ev ), pPattern );
		if ( notesAtPoint.size() == 0 ) {
			return;
		}

		// Focus cursor on dragged note(s).
		m_pPatternEditorPanel->setCursorColumn(
			notesAtPoint[ 0 ]->getPosition() );
		m_pPatternEditorPanel->setSelectedRowDB(
			m_pPatternEditorPanel->findRowDB( notesAtPoint[ 0 ] ) );

		m_draggedNotes.clear();
		// Either all or none of the notes at point should be selected. It is
		// safe to just check the first one.
		if ( m_selection.isSelected( notesAtPoint[ 0 ] ) ) {
			// The clicked note is part of the current selection. All selected
			// notes will be edited.
			for ( const auto& ppNote : m_selection ) {
				if ( ppNote != nullptr &&
					 ! ( ppNote->getNoteOff() &&
						 ( m_property != Property::LeadLag &&
						   m_property != Property::Probability ) ) ) {
					m_draggedNotes[ ppNote ] = std::make_shared<Note>( ppNote );
				}
			}
		}
		else {
			for ( const auto& ppNote : notesAtPoint ) {
				// NoteOff notes can have both a custom lead/lag and
				// probability. But all other properties won't take effect.
				if ( ! ( ppNote->getNoteOff() &&
						( m_property != Property::LeadLag &&
						  m_property != Property::Probability ) ) ) {
					m_draggedNotes[ ppNote ] = std::make_shared<Note>( ppNote );
				}
			}
		}
		// All notes at located at the same point.
		m_nDragStartColumn = notesAtPoint[ 0 ]->getPosition();
		m_nDragY = ev->y();
		m_dragStart = ev->pos();
	}
}

void PatternEditor::mouseDragUpdateEvent( QMouseEvent *ev) {
	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr || m_draggedNotes.size() == 0 ) {
		return;
	}

	updateModifiers( ev );

	auto pHydrogen = Hydrogen::get_instance();
	int nColumn, nRealColumn;
	eventPointToColumnRow( ev->pos(), &nColumn, nullptr, &nRealColumn );

	// In case this is the first drag update, decided whether we deal with a
	// length or property drag.
	if ( m_dragType == DragType::None ) {
		const int nDiffY = std::abs( ev->y() - m_dragStart.y() );
		const int nDiffX = std::abs( ev->x() - m_dragStart.x() );

		if ( nDiffX == nDiffY ) {
			// User is dragging diagonally and hasn't decided yet.
			return;
		}
		else if ( nDiffX > nDiffY ) {
			m_dragType = DragType::Length;
		}
		else {
			m_dragType = DragType::Property;
		}
	}

	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );

	const int nTargetColumn =
		m_pPatternEditorPanel->isQuantized() ? nColumn : nRealColumn;

	int nLen = nTargetColumn - m_nDragStartColumn;

	if ( nLen <= 0 ) {
		nLen = -1;
	}

	for ( auto& [ ppNote, _ ] : m_draggedNotes ) {
		if ( m_dragType == DragType::Length ) {
			float fStep = 1.0;
			if ( nLen > -1 ){
				fStep = Note::pitchToFrequency( ppNote->getPitchFromKeyOctave() );
			}
			ppNote->setLength( nLen * fStep );

			triggerStatusMessage( m_notesHoveredOnDragStart, Property::Length );
		}
		else if ( m_dragType == DragType::Property &&
				  m_property != Property::KeyOctave ) {
			// edit note property. We do not support the note key property.
			float fValue = 0.0;
			if ( m_property == Property::Velocity ) {
				fValue = ppNote->getVelocity();
			}
			else if ( m_property == Property::Pan ) {
				fValue = ppNote->getPanWithRangeFrom0To1();
			}
			else if ( m_property == Property::LeadLag ) {
				fValue = ( ppNote->getLeadLag() - 1.0 ) / -2.0 ;
			}
			else if ( m_property == Property::Probability ) {
				fValue = ppNote->getProbability();
			}
		
			fValue = fValue + static_cast<float>(m_nDragY - ev->y()) / 100;
			if ( fValue > 1 ) {
				fValue = 1;
			}
			else if ( fValue < 0.0 ) {
				fValue = 0.0;
			}

			if ( m_property == Property::Velocity ) {
				ppNote->setVelocity( fValue );
			}
			else if ( m_property == Property::Pan ) {
				ppNote->setPanWithRangeFrom0To1( fValue );
			}
			else if ( m_property == Property::LeadLag ) {
				ppNote->setLeadLag( ( fValue * -2.0 ) + 1.0 );
			}
			else if ( m_property == Property::Probability ) {
				ppNote->setProbability( fValue );
			}

			triggerStatusMessage( m_notesHoveredOnDragStart, m_property );
		}
	}

	m_nDragY = ev->y();

	pHydrogen->getAudioEngine()->unlock(); // unlock the audio engine
	pHydrogen->setIsModified( true );

	m_pPatternEditorPanel->updateEditors( true );
}

void PatternEditor::mouseDragEndEvent( QMouseEvent* ev ) {

	UNUSED( ev );
	unsetCursor();

	auto pPattern = m_pPatternEditorPanel->getPattern();
	if ( pPattern == nullptr ) {
		m_dragType = DragType::None;
		return;
	}

	if ( m_draggedNotes.size() == 0 ||
		 ( m_dragType == DragType::Property &&
		   m_property == Property::KeyOctave ) ) {
		m_dragType = DragType::None;
		return;
	}

	auto pHydrogenApp = HydrogenApp::get_instance();
	const auto pCommonStrings = pHydrogenApp->getCommonStrings();

	bool bMacroStarted = false;
	if ( m_draggedNotes.size() > 1 ) {

		auto sMacro = tr( "Drag edit note property:" );
		if ( m_dragType == DragType::Length ) {
			sMacro.append(
				QString( " %1" ).arg( pCommonStrings->getNotePropertyLength() ) );
		}
		else if ( m_dragType == DragType::Property ) {
			switch ( m_property ) {
			case Property::Velocity:
				sMacro.append( QString( " %1" ).arg(
								   pCommonStrings->getNotePropertyVelocity() ) );
				break;
			case Property::Pan:
				sMacro.append( QString( " %1" ).arg(
								   pCommonStrings->getNotePropertyPan() ) );
				break;
			case Property::LeadLag:
				sMacro.append( QString( " %1" ).arg(
								   pCommonStrings->getNotePropertyLeadLag() ) );
				break;
			case Property::Probability:
				sMacro.append( QString( " %1" ).arg(
								   pCommonStrings->getNotePropertyProbability() ) );
				break;
			default:
				ERRORLOG( "property not supported" );
			}
		}

		pHydrogenApp->beginUndoMacro( sMacro );
		bMacroStarted = true;
	}

	auto editNoteProperty = [=]( PatternEditor::Property property,
								 std::shared_ptr<Note> pNewNote,
								 std::shared_ptr<Note> pOldNote ) {
		if ( m_dragType == DragType::Length ) {
			pHydrogenApp->pushUndoCommand(
				new SE_editNotePropertiesAction(
					property,
					m_pPatternEditorPanel->getPatternNumber(),
					pOldNote->getPosition(),
					pOldNote->getInstrumentId(),
					pOldNote->getInstrumentId(),
					pOldNote->getType(),
					pOldNote->getType(),
					pOldNote->getVelocity(),
					pOldNote->getVelocity(),
					pOldNote->getPan(),
					pOldNote->getPan(),
					pOldNote->getLeadLag(),
					pOldNote->getLeadLag(),
					pOldNote->getProbability(),
					pOldNote->getProbability(),
					pNewNote->getLength(),
					pOldNote->getLength(),
					pOldNote->getKey(),
					pOldNote->getKey(),
					pOldNote->getOctave(),
					pOldNote->getOctave() ) );
		}
		else if ( m_dragType == DragType::Property ) {
			pHydrogenApp->pushUndoCommand(
				new SE_editNotePropertiesAction(
					property,
					m_pPatternEditorPanel->getPatternNumber(),
					pOldNote->getPosition(),
					pOldNote->getInstrumentId(),
					pOldNote->getInstrumentId(),
					pOldNote->getType(),
					pOldNote->getType(),
					pNewNote->getVelocity(),
					pOldNote->getVelocity(),
					pNewNote->getPan(),
					pOldNote->getPan(),
					pNewNote->getLeadLag(),
					pOldNote->getLeadLag(),
					pNewNote->getProbability(),
					pOldNote->getProbability(),
					pOldNote->getLength(),
					pOldNote->getLength(),
					pOldNote->getKey(),
					pOldNote->getKey(),
					pOldNote->getOctave(),
					pOldNote->getOctave() ) );
		}
	};

	std::vector< std::shared_ptr<Note> > notesStatus;

	for ( const auto& [ ppUpdatedNote, ppOriginalNote ] : m_draggedNotes ) {
		if ( ppUpdatedNote == nullptr || ppOriginalNote == nullptr ) {
			continue;
		}

		if ( m_dragType == DragType::Length &&
			 ppUpdatedNote->getLength() != ppOriginalNote->getLength() ) {
			editNoteProperty( Property::Length, ppUpdatedNote, ppOriginalNote );

			// We only trigger status messages for notes hovered by the user.
			for ( const auto ppNote : m_notesHoveredOnDragStart ) {
				if ( ppNote == ppOriginalNote ) {
					notesStatus.push_back( ppUpdatedNote );
				}
			}
		}
		else if ( m_dragType == DragType::Property &&
				  ( ppUpdatedNote->getVelocity() !=
					ppOriginalNote->getVelocity() ||
					ppUpdatedNote->getPan() != ppOriginalNote->getPan() ||
					ppUpdatedNote->getLeadLag() != ppOriginalNote->getLeadLag() ||
					ppUpdatedNote->getProbability() !=
					ppOriginalNote->getProbability() ) ) {
			editNoteProperty( m_property, ppUpdatedNote, ppOriginalNote );

			// We only trigger status messages for notes hovered by the user.
			for ( const auto ppNote : m_notesHoveredOnDragStart ) {
				if ( ppNote == ppOriginalNote ) {
					notesStatus.push_back( ppUpdatedNote );
				}
			}
		}
	}

	if ( m_draggedNotes.size() > 0 ) {
		if ( m_dragType == DragType::Length ) {
			triggerStatusMessage( notesStatus, Property::Length );
		}
		else if ( m_dragType == DragType::Property ) {
			triggerStatusMessage( notesStatus, m_property );
		}
		else {
			ERRORLOG( "Invalid drag type" );
		}
	}

	if ( bMacroStarted ) {
		pHydrogenApp->endUndoMacro();
	}

	m_draggedNotes.clear();
	m_dragType = DragType::None;
}

void PatternEditor::editNotePropertiesAction( const Property& property,
											  int nPatternNumber,
											  int nPosition,
											  int nOldInstrumentId,
											  int nNewInstrumentId,
											  const QString& sOldType,
											  const QString& sNewType,
											  float fVelocity,
											  float fPan,
											  float fLeadLag,
											  float fProbability,
											  int nLength,
											  int nNewKey,
											  int nOldKey,
											  int nNewOctave,
											  int nOldOctave )
{
	auto pPatternEditorPanel =
		HydrogenApp::get_instance()->getPatternEditorPanel();
	auto pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		return;
	}
	auto pPatternList = pSong->getPatternList();
	std::shared_ptr<H2Core::Pattern> pPattern;

	if ( nPatternNumber != -1 &&
		 nPatternNumber < pPatternList->size() ) {
		pPattern = pPatternList->get( nPatternNumber );
	}
	if ( pPattern == nullptr ) {
		return;
	}

	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );

	// Find the note to edit
	auto pNote = pPattern->findNote(
		nPosition, nOldInstrumentId, sOldType, static_cast<Note::Key>(nOldKey),
		static_cast<Note::Octave>(nOldOctave) );
	if ( pNote == nullptr && property == Property::Type ) {
		// Maybe the type of an unmapped note was set to one already present in
		// the drumkit. In this case the instrument id of the note is remapped
		// and might not correspond to the value used to create the undo/redo
		// action.
		//
		bool bOk;
		const int nKitId =
			pSong->getDrumkit()->toDrumkitMap()->getId( sOldType, &bOk );
		if ( bOk ) {
			pNote = pPattern->findNote(
				nPosition, nKitId, sOldType, static_cast<Note::Key>(nOldKey),
				static_cast<Note::Octave>(nOldOctave) );
		}
	}
	else if ( pNote == nullptr && property == Property::InstrumentId ) {
		// When adding an instrument to a row on typed but unmapped notes, the
		// redo part of the instrument ID is done automatically as part of the
		// mapping to the updated kit. Only the undo part needs to be covered in
		// here.
		pHydrogen->getAudioEngine()->unlock();
		return;
	}

	bool bValueChanged = false;

	if ( pNote != nullptr ){
		switch ( property ) {
		case Property::Velocity:
			if ( pNote->getVelocity() != fVelocity ) {
				pNote->setVelocity( fVelocity );
				bValueChanged = true;
			}
			break;
		case Property::Pan:
			if ( pNote->getPan() != fPan ) {
				pNote->setPan( fPan );
				bValueChanged = true;
			}
			break;
		case Property::LeadLag:
			if ( pNote->getLeadLag() != fLeadLag ) {
				pNote->setLeadLag( fLeadLag );
				bValueChanged = true;
			}
			break;
		case Property::KeyOctave:
			if ( pNote->getKey() != nNewKey ||
				 pNote->getOctave() != nNewOctave ) {
				pNote->setKeyOctave( static_cast<Note::Key>(nNewKey),
									 static_cast<Note::Octave>(nNewOctave) );
				bValueChanged = true;
			}
			break;
		case Property::Probability:
			if ( pNote->getProbability() != fProbability ) {
				pNote->setProbability( fProbability );
				bValueChanged = true;
			}
			break;
		case Property::Length:
			if ( pNote->getLength() != nLength ) {
				pNote->setLength( nLength );
				bValueChanged = true;
			}
			break;
		case Property::Type:
			if ( pNote->getType() != sNewType ||
				 pNote->getInstrumentId() != nNewInstrumentId ) {
				pNote->setInstrumentId( nNewInstrumentId );
				pNote->setType( sNewType );

				pNote->mapTo( pSong->getDrumkit(), pSong->getDrumkit() );

				// Changing a type is effectively moving the note to another row
				// of the DrumPatternEditor. This could result in overlapping
				// notes at the same position. To guard against this, select all
				// adjusted notes to harness the checkDeselectElements
				// capabilities.
				pPatternEditorPanel->getVisibleEditor()->
					m_selection.addToSelection( pNote );

				bValueChanged = true;
			}
			break;
		case Property::InstrumentId:
			if ( pNote->getInstrumentId() != nNewInstrumentId ) {
				pNote->setInstrumentId( nNewInstrumentId );
				bValueChanged = true;
			}
			break;
		case Property::None:
		default:
			ERRORLOG("No property set. No note property adjusted.");
		}			
	} else {
		ERRORLOG("note could not be found");
	}

	pHydrogen->getAudioEngine()->unlock();

	if ( bValueChanged ) {
		pHydrogen->setIsModified( true );
		std::vector< std::shared_ptr<Note > > notes{ pNote };

		if ( property == Property::Type || property == Property::InstrumentId ) {
			pPatternEditorPanel->updateDB();
			pPatternEditorPanel->updateEditors();
			pPatternEditorPanel->resizeEvent( nullptr );
		}
		else {
			pPatternEditorPanel->updateEditors( true );
		}
	}
}

void PatternEditor::addOrRemoveNoteAction( int nPosition,
										   int nInstrumentId,
										   const QString& sType,
										   int nPatternNumber,
										   int nOldLength,
										   float fOldVelocity,
										   float fOldPan,
										   float fOldLeadLag,
										   int nOldKey,
										   int nOldOctave,
										   float fOldProbability,
										   bool bIsDelete,
										   bool bIsNoteOff,
										   bool bIsMappedToDrumkit,
										   AddNoteAction addNoteAction )
{
	Hydrogen *pHydrogen = Hydrogen::get_instance();
	auto pSong = pHydrogen->getSong();
	if ( pSong == nullptr || pSong->getDrumkit() == nullptr ) {
		ERRORLOG( "No song set yet" );
		return;
	}

	auto pPatternList = pSong->getPatternList();
	if ( nPatternNumber < 0 ||
		 nPatternNumber >= pPatternList->size() ) {
		ERRORLOG( QString( "Pattern number [%1] out of bound [0,%2]" )
				  .arg( nPatternNumber ).arg( pPatternList->size() ) );
		return;
	}

	auto pPattern = pPatternList->get( nPatternNumber );
	if ( pPattern == nullptr ) {
		ERRORLOG( QString( "Pattern found for pattern number [%1] is not valid" )
				  .arg( nPatternNumber ) );
		return;
	}

	if ( nInstrumentId == EMPTY_INSTR_ID && sType.isEmpty() ) {
		DEBUGLOG( QString( "Empty row" ) );
		return;
	}

	auto pPatternEditorPanel = HydrogenApp::get_instance()->getPatternEditorPanel();
	auto pVisibleEditor = pPatternEditorPanel->getVisibleEditor();

	pHydrogen->getAudioEngine()->lock( RIGHT_HERE );	// lock the audio engine

	if ( bIsDelete ) {
		// Find and delete an existing (matching) note.

		// In case there are multiple notes at this position, use all provided
		// properties to find right one.
		std::vector< std::shared_ptr<Note> > notesFound;
		const auto pNotes = pPattern->getNotes();
		for ( auto it = pNotes->lower_bound( nPosition );
			  it != pNotes->end() && it->first <= nPosition; ++it ) {
			auto ppNote = it->second;
			if ( ppNote != nullptr &&
				 ppNote->getInstrumentId() == nInstrumentId &&
				 ppNote->getType() == sType &&
				 ppNote->getKey() == static_cast<Note::Key>(nOldKey) &&
				 ppNote->getOctave() == static_cast<Note::Octave>(nOldOctave) ) {
				notesFound.push_back( ppNote );
			}
		}

		auto removeNote = [&]( std::shared_ptr<Note> pNote ) {
			if ( pVisibleEditor->m_selection.isSelected( pNote ) ) {
				pVisibleEditor->m_selection.removeFromSelection( pNote, false );
			}
			pPattern->removeNote( pNote );
		};

		if ( notesFound.size() == 1 ) {
			// There is just a single note at this position. Remove it
			// regardless of its properties.
			removeNote( notesFound[ 0 ] );
		}
		else if ( notesFound.size() > 1 ) {
			bool bFound = false;

			for ( const auto& ppNote : notesFound ) {
				if ( ppNote->getLength() == nOldLength &&
					 ppNote->getVelocity() == fOldVelocity &&
					 ppNote->getPan() == fOldPan &&
					 ppNote->getLeadLag() == fOldLeadLag &&
					 ppNote->getProbability() == fOldProbability &&
					 ppNote->getNoteOff() == bIsNoteOff ) {
					bFound = true;
					removeNote( ppNote );
				}
			}

			if ( ! bFound ) {
				QStringList noteStrings;
				for ( const auto& ppNote : notesFound ) {
					noteStrings << "\n - " << ppNote->toQString();
				}
				ERRORLOG( QString( "length: %1, velocity: %2, pan: %3, lead&lag: %4, probability: %5, noteOff: %6 not found amongst notes:%7" )
						  .arg( nOldLength ).arg( fOldVelocity ).arg( fOldPan )
						  .arg( fOldLeadLag ).arg( fOldProbability )
						  .arg( bIsNoteOff ).arg( noteStrings.join( "" ) ) );
			}
		}
		else {
			ERRORLOG( "Did not find note to delete" );
		}

	}
	else {
		// create the new note
		float fVelocity = fOldVelocity;
		float fPan = fOldPan ;
		int nLength = nOldLength;

		if ( bIsNoteOff ) {
			fVelocity = VELOCITY_MIN;
			fPan = PAN_DEFAULT;
			nLength = 1;
		}

		std::shared_ptr<Instrument> pInstrument = nullptr;
		if ( nInstrumentId != EMPTY_INSTR_ID && bIsMappedToDrumkit ) {
			// Can still be nullptr for notes in unmapped rows.
			pInstrument =
				pSong->getDrumkit()->getInstruments()->find( nInstrumentId );
		}

		auto pNote = std::make_shared<Note>( pInstrument, nPosition, fVelocity,
											 fPan, nLength );
		pNote->setInstrumentId( nInstrumentId );
		pNote->setType( sType );
		pNote->setNoteOff( bIsNoteOff );
		pNote->setLeadLag( fOldLeadLag );
		pNote->setProbability( fOldProbability );
		pNote->setKeyOctave( static_cast<Note::Key>(nOldKey),
							   static_cast<Note::Octave>(nOldOctave) );
		pPattern->insertNote( pNote );

		if ( addNoteAction & AddNoteAction::AddToSelection ) {
			pVisibleEditor->m_selection.addToSelection( pNote );
		}

		if ( addNoteAction & AddNoteAction::MoveCursorTo ) {
			pPatternEditorPanel->setCursorColumn( pNote->getPosition() );
			pPatternEditorPanel->setSelectedRowDB(
				pPatternEditorPanel->findRowDB( pNote ) );
		}
	}
	pHydrogen->getAudioEngine()->unlock(); // unlock the audio engine
	pHydrogen->setIsModified( true );

	pPatternEditorPanel->updateEditors( true );
}

QString PatternEditor::editorToQString( const Editor& editor ) {
	switch ( editor ) {
	case PatternEditor::Editor::DrumPattern:
		return "DrumPattern";
	case PatternEditor::Editor::PianoRoll:
		return "PianoRoll";
	case PatternEditor::Editor::NotePropertiesRuler:
		return "NotePropertiesRuler";
	case PatternEditor::Editor::None:
	default:
		return QString( "Unknown editor [%1]" ).arg( static_cast<int>(editor) ) ;
	}
}

QString PatternEditor::propertyToQString( const Property& property ) {
	const auto pCommonStrings = HydrogenApp::get_instance()->getCommonStrings();
	QString s;
	
	switch ( property ) {
	case PatternEditor::Property::Velocity:
		s = pCommonStrings->getNotePropertyVelocity();
		break;
	case PatternEditor::Property::Pan:
		s = pCommonStrings->getNotePropertyPan();
		break;
	case PatternEditor::Property::LeadLag:
		s = pCommonStrings->getNotePropertyLeadLag();
		break;
	case PatternEditor::Property::KeyOctave:
		s = pCommonStrings->getNotePropertyKeyOctave();
		break;
	case PatternEditor::Property::Probability:
		s = pCommonStrings->getNotePropertyProbability();
		break;
	case PatternEditor::Property::Length:
		s = pCommonStrings->getNotePropertyLength();
		break;
	case PatternEditor::Property::Type:
		s = pCommonStrings->getInstrumentType();
		break;
	case PatternEditor::Property::InstrumentId:
		s = pCommonStrings->getInstrumentId();
		break;
	default:
		s = QString( "Unknown property [%1]" ).arg( static_cast<int>(property) ) ;
		break;
	}

	return s;
}

QString PatternEditor::updateToQString( const Update& update ) {
	switch ( update ) {
	case PatternEditor::Update::Background:
		return "Background";
	case PatternEditor::Update::Pattern:
		return "Pattern";
	case PatternEditor::Update::None:
		return "None";
	default:
		return QString( "Unknown update [%1]" ).arg( static_cast<int>(update) ) ;
	}
}

void PatternEditor::triggerStatusMessage(
	const std::vector< std::shared_ptr<Note> > notes, const Property& property,
	bool bSquash ) {
	QString sCaller( _class_name() );
	QString sUnit( tr( "ticks" ) );

	// Aggregate all values of the provided notes
	QStringList values;
	float fValue;
	for ( const auto& ppNote : notes ) {
		if ( ppNote == nullptr ) {
			continue;
		}

		if ( ! bSquash ) {
			// Allow the status message widget to squash all changes
			// corresponding to the same property of the same set to notes.
			sCaller.append( QString( "::%1:%2" )
							.arg( ppNote->getPosition() )
							.arg( m_pPatternEditorPanel->findRowDB( ppNote ) ) );
		}

		switch ( property ) {
		case PatternEditor::Property::Velocity:
			if ( ! ppNote->getNoteOff() ) {
				values << QString( "%1").arg( ppNote->getVelocity(), 2, 'f', 2 );
			}
			break;

		case PatternEditor::Property::Pan:
			if ( ! ppNote->getNoteOff() ) {

				// Round the pan to not miss the center due to fluctuations
				fValue = ppNote->getPan() * 100;
				fValue = std::round( fValue );
				fValue = fValue / 100;

				if ( fValue > 0.0 ) {
					values << QString( "%1 (%2)" ).arg( fValue / 2, 2, 'f', 2 )
						/*: Direction used when panning a note. */
						.arg( tr( "right" ) );
				}
				else if ( fValue < 0.0 ) {
					values << QString( "%1 (%2)" )
						/*: Direction used when panning a note. */
						.arg( -1 * fValue / 2, 2, 'f', 2 ).arg( tr( "left" ) );
				}
				else {
					/*: Direction used when panning a note. */
					values <<  tr( "centered" );
				}
			}
			break;

		case PatternEditor::Property::LeadLag:
			// Round the pan to not miss the center due to fluctuations
			fValue = ppNote->getLeadLag() * 100;
			fValue = std::round( fValue );
			fValue = fValue / 100;
			if ( fValue < 0.0 ) {
				values << QString( "%1 (%2)" )
					.arg( fValue * -1 * AudioEngine::getLeadLagInTicks(),
						  2, 'f', 2 )
					/*: Relative temporal position when setting note lead & lag. */
					.arg( tr( "lead" ) );
			}
			else if ( fValue > 0.0 ) {
				values << QString( "%1 (%2)" )
					.arg( fValue * AudioEngine::getLeadLagInTicks(), 2, 'f', 2 )
					/*: Relative temporal position when setting note lead & lag. */
					.arg( tr( "lag" ) );
			}
			else {
				/*: Relative temporal position when setting note lead & lag. */
				values << tr( "on beat" );
			}
			break;

		case PatternEditor::Property::KeyOctave:
			if ( ! ppNote->getNoteOff() ) {
				values << QString( "%1 : %2" )
					.arg( Note::KeyToQString( ppNote->getKey() ) )
					.arg( ppNote->getOctave() );
			}
			break;

		case PatternEditor::Property::Probability:
			values << QString( "%1" ).arg( ppNote->getProbability(), 2, 'f', 2 );
			break;

		case PatternEditor::Property::Length:
			if ( ! ppNote->getNoteOff() ) {
				values << QString( "%1" )
					.arg( ppNote->getProbability(), 2, 'f', 2 );
			}
			break;
		case PatternEditor::Property::InstrumentId:
			return;
		case PatternEditor::Property::Type:
		case PatternEditor::Property::None:
		default:
			break;
		}
	}

	if ( values.size() == 0 && property != PatternEditor::Property::Type ) {
		return;
	}

	// Compose the actual status message
	QString s;
	switch ( property ) {
	case PatternEditor::Property::Velocity:
		s = QString( tr( "Set note velocity" ) )
				.append( QString( ": [%1]").arg( values.join( ", " ) ) );
		sCaller.append( ":Velocity" );
		break;
		
	case PatternEditor::Property::Pan:
		s = QString( tr( "Set note pan" ) )
				.append( QString( ": [%1]").arg( values.join( ", " ) ) );
		sCaller.append( ":Pan" );
		break;
		
	case PatternEditor::Property::LeadLag:
		s = QString( tr( "Set note lead/lag" ) )
			.append( QString( ": [%1]").arg( values.join( ", " ) ) );
		sCaller.append( ":LeadLag" );
		break;

	case PatternEditor::Property::KeyOctave:
		s = QString( tr( "Set note pitch" ) )
			.append( QString( ": [%1]").arg( values.join( ", " ) ) );
		break;

	case PatternEditor::Property::Probability:
		s = tr( "Set note probability" )
			.append( QString( ": [%1]" ).arg( values.join( ", " ) ) );
		sCaller.append( ":Probability" );
		break;

	case PatternEditor::Property::Length:
		s = tr( "Set note length" )
			.append( QString( ": [%1]" ).arg( values.join( ", " ) ) );
		sCaller.append( ":Length" );
		break;

	case PatternEditor::Property::Type:
		// All notes should have the same type. No need to aggregate in here.
		s = tr( "Set note type" )
			.append( QString( ": [%1]" ).arg( notes[ 0 ]->getType() ) );
		sCaller.append( ":Type" );
		break;

	default:
		ERRORLOG( PatternEditor::propertyToQString( property ) );
	}

	if ( ! s.isEmpty() ) {
		HydrogenApp::get_instance()->showStatusBarMessage( s, sCaller );
	}
}

QPoint PatternEditor::getCursorPosition()
{
	const int nX = PatternEditor::nMargin +
		m_pPatternEditorPanel->getCursorColumn() * m_fGridWidth;
	int nY;
	if ( m_editor == Editor::PianoRoll ) {
		nY = m_nGridHeight * Note::pitchToLine( m_nCursorPitch ) + 1;
	}
	else {
		nY = m_nGridHeight * m_pPatternEditorPanel->getSelectedRowDB();
	}

	return QPoint( nX, nY );
}

void PatternEditor::setCursorPitch( int nCursorPitch ) {
	const int nMinPitch = Note::octaveKeyToPitch(
		(Note::Octave)OCTAVE_MIN, (Note::Key)KEY_MIN );
	const int nMaxPitch = Note::octaveKeyToPitch(
		(Note::Octave)OCTAVE_MAX, (Note::Key)KEY_MAX );

	if ( nCursorPitch < nMinPitch ) {
		nCursorPitch = nMinPitch;
	}
	else if ( nCursorPitch >= nMaxPitch ) {
		nCursorPitch = nMaxPitch;
	}

	if ( nCursorPitch == m_nCursorPitch ) {
		return;
	}

	m_nCursorPitch = nCursorPitch;

	// Highlight selected row.
	if ( m_editor == Editor::PianoRoll ) {
		m_update = Update::Background;
		update();
	}

	if ( ! HydrogenApp::get_instance()->hideKeyboardCursor() ) {
		m_pPatternEditorPanel->ensureVisible();
		m_pPatternEditorPanel->getSidebar()->updateEditor();
		m_pPatternEditorPanel->getPatternEditorRuler()->update();
		m_pPatternEditorPanel->getVisiblePropertiesRuler()->update();
	}
}

QRect PatternEditor::getKeyboardCursorRect()
{
	QPoint pos = getCursorPosition();

	float fHalfWidth;
	if ( m_pPatternEditorPanel->getResolution() != 4 * H2Core::nTicksPerQuarter ) {
		// Corresponds to the distance between grid lines on 1/64 resolution.
		fHalfWidth = m_fGridWidth * 3;
	} else {
		// Corresponds to the distance between grid lines set to resolution
		// "off".
		fHalfWidth = m_fGridWidth;
	}
	if ( m_editor == Editor::DrumPattern ) {
		return QRect( pos.x() - fHalfWidth, pos.y() + 2,
					  fHalfWidth * 2, m_nGridHeight - 3 );
	}
	else if ( m_editor == Editor::PianoRoll ){
		return QRect( pos.x() - fHalfWidth, pos.y()-2,
					  fHalfWidth * 2, m_nGridHeight+3 );
	}
	else {
		if ( hasFocus() ) {
			return QRect( pos.x() - fHalfWidth, 3, fHalfWidth * 2, height() - 6 );
		}
		else {
			// We do not have to compensate for the focus highlight.
			return QRect( pos.x() - fHalfWidth, 1, fHalfWidth * 2, height() - 2 );
		}
	}
}

std::vector< std::shared_ptr<Note> > PatternEditor::getElementsAtPoint(
	const QPoint& point, int nCursorMargin,
	std::shared_ptr<H2Core::Pattern> pPattern )
{
	std::vector< std::shared_ptr<Note> > notesUnderPoint;
	if ( pPattern == nullptr ) {
		pPattern = m_pPatternEditorPanel->getPattern();
		if ( pPattern == nullptr ) {
			return std::move( notesUnderPoint );
		}
	}

	int nRow, nRealColumn;
	eventPointToColumnRow( point, nullptr, &nRow, &nRealColumn );

	int nRealColumnLower, nRealColumnUpper;
	eventPointToColumnRow( point - QPoint( nCursorMargin, 0 ),
						   nullptr, nullptr, &nRealColumnLower );
	eventPointToColumnRow( point + QPoint( nCursorMargin, 0 ),
						   nullptr, nullptr, &nRealColumnUpper );


	// Assemble all notes to be edited.
	DrumPatternRow row;
	if ( m_editor == Editor::DrumPattern ) {
		row = m_pPatternEditorPanel->getRowDB( nRow );
	}
	else {
		row = m_pPatternEditorPanel->getRowDB(
			m_pPatternEditorPanel->getSelectedRowDB() );
	}

	// Prior to version 2.0 notes where selected by clicking its grid cell,
	// while this caused only notes on the current grid to be accessible it also
	// made them quite easy select. Just using the position of the mouse cursor
	// would feel like a regression, as it would be way harded to hit the notes.
	// Instead, we introduce a certain rectangle (manhattan distance) around the
	// cursor which can select notes but only return those nearest to the
	// center.
	int nLastDistance = nRealColumnUpper - nRealColumn + 1;

	// We have to ensure to only provide notes from a single position. In case
	// the cursor is placed exactly in the middle of two notes, the left one
	// wins.
	int nLastPosition = -1;

	const auto notes = pPattern->getNotes();
	for ( auto it = notes->lower_bound( nRealColumnLower );
		  it != notes->end() && it->first <= nRealColumnUpper; ++it ) {
		const auto ppNote = it->second;
		if ( ppNote != nullptr && row.contains( ppNote ) &&
			 ppNote->getPosition() < pPattern->getLength() ) {

			const int nDistance =
				std::abs( ppNote->getPosition() - nRealColumn );

			if ( nDistance < nLastDistance ) {
				// This note is nearer than (potential) previous ones.
				notesUnderPoint.clear();
				nLastDistance = nDistance;
				nLastPosition = ppNote->getPosition();
			}

			if ( nDistance <= nLastDistance &&
				 ppNote->getPosition() == nLastPosition ) {
				// In case of the PianoRoll editor we do have to additionally
				// differentiate between different pitches.
				if ( m_editor != Editor::PianoRoll ||
					 ( m_editor == Editor::PianoRoll &&
					   ppNote->getKey() ==
					   Note::pitchToKey( Note::lineToPitch( nRow ) ) &&
					   ppNote->getOctave() ==
					   Note::pitchToOctave( Note::lineToPitch( nRow ) ) ) ) {
					notesUnderPoint.push_back( ppNote );
				}
			}
		}
	}

	// Within the ruler all selected and hovered notes along with notes of the
	// selected row are rendered. These notes can be interacted with (property
	// change, deselect etc.).
	if ( m_editor == Editor::NotePropertiesRuler ) {
		// Ensure we do not add the same note twice.
		std::set< std::shared_ptr<Note> > furtherNotes;

		// Check and add selected notes.
		bool bFound = false;
		for ( const auto& ppSelectedNote : m_selection ) {
			bFound = false;
			for ( const auto& ppPatternNote : notesUnderPoint ) {
				if ( ppPatternNote == ppSelectedNote ) {
					bFound = true;
					break;
				}
			}
			if ( ! bFound && ppSelectedNote != nullptr ) {
				furtherNotes.insert( ppSelectedNote );
			}
		}

		// Check and add hovered notes.
		for ( const auto& [ ppPattern, nnotes ] :
				  m_pPatternEditorPanel->getHoveredNotes() ) {
			if ( ppPattern != pPattern ) {
				continue;
			}

			for ( const auto& ppHoveredNote : nnotes ) {
				bFound = false;
				for ( const auto& ppPatternNote : notesUnderPoint ) {
					if ( ppPatternNote == ppHoveredNote ) {
						bFound = true;
						break;
					}
				}
				if ( ! bFound && ppHoveredNote != nullptr ) {
					furtherNotes.insert( ppHoveredNote );
				}
			}
		}

		for ( const auto& ppNote : furtherNotes ) {
			const int nDistance = std::abs( ppNote->getPosition() - nRealColumn );

			if ( nDistance < nLastDistance ) {
				// This note is nearer than (potential) previous ones.
				notesUnderPoint.clear();
				nLastDistance = nDistance;
				nLastPosition = ppNote->getPosition();
			}

			if ( nDistance <= nLastDistance &&
				 ppNote->getPosition() == nLastPosition ) {
				notesUnderPoint.push_back( ppNote );
			}
		}
	}

	return std::move( notesUnderPoint );
}

void PatternEditor::updateHoveredNotesMouse( QMouseEvent* pEvent,
											 bool bUpdateEditors ) {
	const int nCursorMargin = getCursorMargin( pEvent );

	int nRealColumn;
	eventPointToColumnRow( pEvent->pos(), nullptr, nullptr, &nRealColumn );
	int nRealColumnUpper;
	eventPointToColumnRow( pEvent->pos() + QPoint( nCursorMargin, 0 ),
						   nullptr, nullptr, &nRealColumnUpper );

	// getElementsAtPoint is generous in finding notes by taking a margin around
	// the cursor into account as well. We have to ensure we only use to closest
	// notes reported.
	int nLastDistance = nRealColumnUpper - nRealColumn + 1;

	// In addition, we have to ensure to only provide notes from a single
	// position. In case the cursor is placed exactly in the middle of two
	// notes, the left one wins.
	int nLastPosition = -1;

	std::vector< std::pair< std::shared_ptr<Pattern>,
							std::vector< std::shared_ptr<Note> > > > hovered;
	// We do not highlight hovered notes during a property drag. Else, the
	// hovered ones would appear in front of the dragged one in the ruler,
	// hiding the newly adjusted value.
	if ( m_dragType == DragType::None &&
		 pEvent->x() > PatternEditor::nMarginSidebar ) {
		for ( const auto& ppPattern : m_pPatternEditorPanel->getPatternsToShow() ) {
			const auto hoveredNotes = getElementsAtPoint(
				pEvent->pos(), nCursorMargin, ppPattern );
			if ( hoveredNotes.size() > 0 ) {
				const int nDistance =
					std::abs( hoveredNotes[ 0 ]->getPosition() - nRealColumn );
				if ( nDistance < nLastDistance ) {
					// This batch of notes is nearer than (potential) previous ones.
					hovered.clear();
					nLastDistance = nDistance;
					nLastPosition = hoveredNotes[ 0 ]->getPosition();
				}

				if ( hoveredNotes[ 0 ]->getPosition() == nLastPosition ) {
					hovered.push_back( std::make_pair( ppPattern, hoveredNotes ) );
				}
			}
		}
	}
	m_pPatternEditorPanel->setHoveredNotesMouse( hovered, bUpdateEditors );
}

void PatternEditor::updateHoveredNotesKeyboard( bool bUpdateEditors ) {
	std::vector< std::pair< std::shared_ptr<Pattern>,
							std::vector< std::shared_ptr<Note> > > > hovered;
	if ( ! HydrogenApp::get_instance()->hideKeyboardCursor() ) {
		// cursor visible

		// In case we are within the property ruler and a note from a different
		// row is hovered by mouse in the drum pattern editor, we must ensure we
		// are not adding this one to the keyboard hovered notes too.
		PatternEditor* pEditor = this;
		if ( m_editor == Editor::NotePropertiesRuler ) {
			auto pVisibleEditor = m_pPatternEditorPanel->getVisibleEditor();
			if ( dynamic_cast<DrumPatternEditor*>( pVisibleEditor ) != nullptr ) {
				pEditor = pVisibleEditor;
			}
		}

		const auto point = pEditor->getCursorPosition();

		for ( const auto& ppPattern : m_pPatternEditorPanel->getPatternsToShow() ) {
			const auto hoveredNotes =
				pEditor->getElementsAtPoint( point, 0, ppPattern );
			if ( hoveredNotes.size() > 0 ) {
				hovered.push_back( std::make_pair( ppPattern, hoveredNotes ) );
			}
		}
	}
	m_pPatternEditorPanel->setHoveredNotesKeyboard( hovered, bUpdateEditors );
}

bool PatternEditor::syncLasso() {

	const int nMargin = 5;

	bool bUpdate = false;
	if ( m_editor == Editor::NotePropertiesRuler ) {
		auto pVisibleEditor = m_pPatternEditorPanel->getVisibleEditor();

		QRect prevLasso;
		QRect cursorStart = m_selection.getKeyboardCursorStart();
		QRect lasso = m_selection.getLasso();
		QRect cursor = getKeyboardCursorRect();

		// Ensure lasso is full height as we do not support lasso selecting
		// notes by property value.
		lasso.setY( cursor.y() );
		lasso.setHeight( cursor.height() );
		cursorStart.setY( cursor.y() );
		m_selection.syncLasso(
			m_selection.getSelectionState(), cursorStart, lasso );

		if ( dynamic_cast<DrumPatternEditor*>(pVisibleEditor) != nullptr ) {
			// The ruler does not feature a proper y and height coordinate. We
			// have to ensure to either keep the one already present in the
			// others or use the current line as fallback.
			if ( pVisibleEditor->m_selection.isLasso() ) {
				cursor = pVisibleEditor->m_selection.getKeyboardCursorStart();
				prevLasso = pVisibleEditor->m_selection.getLasso();
			}
			else {
				cursor = pVisibleEditor->getKeyboardCursorRect();
				prevLasso = cursor;
			}
		}
		else {
			// PianoRollEditor
			//
			// All notes shown in the NotePropertiesRuler are shown in
			// PianoRollEditor as well. But scattered all over the place. In
			// DrumPatternEditor we just have to mark a row. In PRE we have to
			// ensure that all notes are properly covered by the lasso. In here
			// we expect all selected notes already being added and adjust lasso
			// dimensions to cover them.
			auto pPianoRoll = dynamic_cast<PianoRollEditor*>(pVisibleEditor);
			if ( pPianoRoll == nullptr ) {
				ERRORLOG( "this ain't piano roll" );
				return false;
			}
			if ( pVisibleEditor->m_selection.isLasso() ) {
				cursor = pVisibleEditor->m_selection.getKeyboardCursorStart();
				prevLasso = pVisibleEditor->m_selection.getLasso();
			}
			else {
				cursor = pVisibleEditor->getKeyboardCursorRect();
				prevLasso = cursor;
			}

			// The selection can be started in DrumPatternEditor and contain
			// notes not shown in PianoRollEditor.
			const auto row = m_pPatternEditorPanel->getRowDB(
				m_pPatternEditorPanel->getSelectedRowDB() );

			for ( const auto& ppNote : m_selection ) {
				if ( ppNote != nullptr && row.contains( ppNote ) ) {
					const QPoint np = pPianoRoll->noteToPoint( ppNote );
					const QRect noteRect = QRect(
						np.x() - cursor.width() / 2, np.y() - cursor.height() / 2,
						cursor.width(), cursor.height() );
					if ( ! prevLasso.intersects( noteRect ) ) {
						prevLasso = prevLasso.united( noteRect );
					}
				}
			}
		}
		cursorStart.setY( cursor.y() );
		cursorStart.setHeight( cursor.height() );
		lasso.setY( prevLasso.y() );
		lasso.setHeight( prevLasso.height() );

		bUpdate = pVisibleEditor->m_selection.syncLasso(
			m_selection.getSelectionState(), cursorStart, lasso );
	}
	else {
		// DrumPattern or Piano roll
		const auto pVisibleRuler =
			m_pPatternEditorPanel->getVisiblePropertiesRuler();

		// The ruler does not feature a proper y coordinate and height. We have
		// to use the entire height instead.
		QRect cursorStart = m_selection.getKeyboardCursorStart();
		QRect lasso = m_selection.getLasso();
		QRect lassoStart = m_selection.getKeyboardCursorStart();
		const QRect cursor = pVisibleRuler->getKeyboardCursorRect();
		cursorStart.setY( cursor.y() );
		cursorStart.setHeight( cursor.height() );
		lasso.setY( cursor.y() );
		lasso.setHeight( cursor.height() );

		pVisibleRuler->m_selection.syncLasso(
			m_selection.getSelectionState(), cursorStart, lasso );

		// We force a full update lasso could have been changed in vertical
		// direction (note selection).
		bUpdate = true;
	}

	return bUpdate;
}

bool PatternEditor::isSelectionMoving() const {
	return m_selection.isMoving();
}

void PatternEditor::popupSetup() {
	if ( m_notesToSelectForPopup.size() > 0 ) {
		m_selection.clearSelection();

		for ( const auto& ppNote : m_notesToSelectForPopup ) {
			m_selection.addToSelection( ppNote );
		}
	}
}

void PatternEditor::popupTeardown() {
	if ( m_notesToSelectForPopup.size() > 0 ) {
		m_notesToSelectForPopup.clear();
		m_selection.clearSelection();
	}

	// The popup might have caused the cursor to move out of this widget and the
	// latter will loose focus once the popup is torn down. We have to ensure
	// not to display some glitchy notes previously hovered by mouse which are
	// not present anymore (e.g. since they were aligned to a different
	// position).
	const QPoint globalPos = QCursor::pos();
	const QPoint widgetPos = mapFromGlobal( globalPos );
	if ( widgetPos.x() < 0 || widgetPos.x() >= width() ||
		 widgetPos.y() < 0 || widgetPos.y() >= height() ) {
		std::vector< std::pair< std::shared_ptr<Pattern>,
								std::vector< std::shared_ptr<Note> > > > empty;
		m_pPatternEditorPanel->setHoveredNotesMouse( empty );
	}
}

bool PatternEditor::checkNotePlayback( std::shared_ptr<H2Core::Note> pNote ) const {
	if ( ! Preferences::get_instance()->
		 getTheme().m_interface.m_bIndicateNotePlayback ) {
		return true;
	}

	if ( pNote == nullptr || pNote->getInstrument() == nullptr ) {
		return false;
	}

	auto pSong = Hydrogen::get_instance()->getSong();
	// If the note is part of a mute group, only the bottom most note at the
	// same position within the group will be rendered.
	if ( pNote->getInstrument()->getMuteGroup() != -1 &&
		 pSong != nullptr && pSong->getDrumkit() != nullptr ) {
		const auto pInstrumentList = pSong->getDrumkit()->getInstruments();
		const int nMuteGroup = pNote->getInstrument()->getMuteGroup();
		for ( const auto& ppPattern : m_pPatternEditorPanel->getPatternsToShow() ) {
			for ( const auto& [ nnPosition, ppNote ] : *ppPattern->getNotes() ) {
				if ( ppNote != nullptr && ppNote->getInstrument() != nullptr &&
					 ppNote->getInstrument()->getMuteGroup() == nMuteGroup &&
					 ppNote->getPosition() == pNote->getPosition() &&
					 pInstrumentList->index( pNote->getInstrument() ) <
					 pInstrumentList->index( ppNote->getInstrument() ) ) {
					return false;
				}
			}
		}
	}

	// Check for a note off at the same position.
	if ( ! pNote->getNoteOff() ) {
		const auto pInstrument = pNote->getInstrument();

		for ( const auto& ppPattern : m_pPatternEditorPanel->getPatternsToShow() ) {
			for ( const auto& [ nnPosition, ppNote ] : *ppPattern->getNotes() ) {
				if ( ppNote != nullptr && ppNote->getNoteOff() &&
					 ppNote->getPosition() == pNote->getPosition() &&
					 ppNote->getInstrument() == pInstrument ) {
					return false;
				}
			}
		}
	}

	const auto row = m_pPatternEditorPanel->getRowDB(
		m_pPatternEditorPanel->findRowDB( pNote ) );
	return row.bPlaysBackAudio;
}

int PatternEditor::calculateEffectiveNoteLength( std::shared_ptr<H2Core::Note> pNote ) const {
	if ( pNote == nullptr ) {
		return -1;
	}

	// Check for the closest note off or note of the same mute group.
	if ( Preferences::get_instance()->
		 getTheme().m_interface.m_bIndicateEffectiveNoteLength ) {

		const auto pInstrument = pNote->getInstrument();

		// mute group
		const int nLargeNumber = 100000;
		int nEffectiveLength = nLargeNumber;
		if ( pNote->getInstrument() != nullptr &&
			 pNote->getInstrument()->getMuteGroup() != -1 ) {
			const int nMuteGroup = pNote->getInstrument()->getMuteGroup();
			for ( const auto& ppPattern : m_pPatternEditorPanel->getPatternsToShow() ) {
				for ( const auto& [ nnPosition, ppNote ] : *ppPattern->getNotes() ) {
					if ( ppNote != nullptr && ppNote->getInstrument() != nullptr &&
						 ppNote->getInstrument()->getMuteGroup() == nMuteGroup &&
						 ppNote->getInstrument() != pInstrument &&
						 ppNote->getPosition() > pNote->getPosition() &&
						 ( ppNote->getPosition() - pNote->getPosition() ) <
						 nEffectiveLength ) {
						nEffectiveLength = ppNote->getPosition() -
							pNote->getPosition();
					}
				}
			}
		}

		// Note Off
		if ( ! pNote->getNoteOff() && pNote->getInstrument() != nullptr ) {
			for ( const auto& ppPattern : m_pPatternEditorPanel->getPatternsToShow() ) {
				for ( const auto& [ nnPosition, ppNote ] : *ppPattern->getNotes() ) {
					if ( ppNote != nullptr && ppNote->getNoteOff() &&
						 ppNote->getInstrument() == pInstrument &&
						 ppNote->getPosition() > pNote->getPosition() &&
						 ( ppNote->getPosition() - pNote->getPosition() ) <
						 nEffectiveLength ) {
						nEffectiveLength = ppNote->getPosition() -
							pNote->getPosition();
					}
				}
			}
		}

		if ( nEffectiveLength == nLargeNumber ) {
			return pNote->getLength();
		}

		// We only apply this effective length (in ticks) in case it is indeed
		// smaller than the length (in frames) of the longest sample which can
		// be triggered by the note. We consider the current tempo to be
		// constant over the whole note length. This is done as we do not know
		// at which point of the song - thus using which tempo - the note will
		// be played back.
		const int nMaxFrames = pNote->getInstrument()->getLongestSampleFrames();

		// We also need to take the note's pitch into account as this
		// effectively scales the length of the note too.
		const float fCurrentTickSize = Hydrogen::get_instance()->getAudioEngine()
			->getTransportPosition()->getTickSize();
		const int nEffectiveFrames = static_cast<int>(
			TransportPosition::computeFrame(
				nEffectiveLength * Note::pitchToFrequency(
					static_cast<double>(pNote->getPitchFromKeyOctave() ) ),
				fCurrentTickSize ) );

		if ( nEffectiveFrames < nMaxFrames ) {
			return nEffectiveLength;
		}
	}

	return pNote->getLength();
}

QString PatternEditor::DragTypeToQString( DragType dragType ) {
	switch( dragType ) {
	case DragType::Length:
		return "Length";
	case DragType::Property:
		return "Property";
	default:
		return QString( "Unknown type [%1]" ).arg( static_cast<int>(dragType) );
	}
}
