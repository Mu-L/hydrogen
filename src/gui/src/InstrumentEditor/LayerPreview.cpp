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

#include "LayerPreview.h"

#include <QtGui>
#include <QtWidgets>

#include <core/AudioEngine/AudioEngine.h>
#include <core/Basics/Instrument.h>
#include <core/Basics/InstrumentComponent.h>
#include <core/Basics/InstrumentLayer.h>
#include <core/Basics/InstrumentList.h>
#include <core/Basics/Note.h>
#include <core/Basics/Song.h>
#include <core/Hydrogen.h>
#include <core/Preferences/Theme.h>
#include <core/Sampler/Sampler.h>

#include "InstrumentEditorPanel.h"
#include "../HydrogenApp.h"
#include "../Skin.h"

using namespace H2Core;

LayerPreview::LayerPreview( QWidget* pParent, InstrumentEditorPanel* pPanel )
 : QWidget( pParent )
 , m_pInstrumentEditorPanel( pPanel )
 , m_bMouseGrab( false )
 , m_bGrabLeft( false )
{
	setAttribute(Qt::WA_OpaquePaintEvent);

	setMouseTracking( true );

	int nWidth = 276;
	if( InstrumentComponent::getMaxLayers() > 16) {
		nWidth = 261;
	}
	
	const int nHeight = 20 + LayerPreview::m_nLayerHeight
		* InstrumentComponent::getMaxLayers();
	resize( nWidth, nHeight );

	m_speakerPixmap.load( Skin::getSvgImagePath() + "/icons/white/speaker.svg" );

	// We get a style similar to the one used for the 2 buttons on top of the
	// instrument editor panel
	setStyleSheet("font-size: 9px; font-weight: bold;");
}

LayerPreview::~LayerPreview() {
}

void LayerPreview::paintEvent(QPaintEvent *ev)
{
	QPainter p( this );

	auto pPref = H2Core::Preferences::get_instance();

	QFont fontText( pPref->getTheme().m_font.m_sLevel2FontFamily,
					getPointSize( pPref->getTheme().m_font.m_fontSize ) );
	QFont fontButton( pPref->getTheme().m_font.m_sLevel2FontFamily,
					  getPointSizeButton() );
	
	p.fillRect( ev->rect(), pPref->getTheme().m_color.m_windowColor );

	const auto pInstrument = m_pInstrumentEditorPanel->getInstrument();
	const int nSelectedComponent =
		m_pInstrumentEditorPanel->getSelectedComponent();
	std::shared_ptr<InstrumentComponent> pComponent;
	if ( pInstrument != nullptr ) {
		pComponent = pInstrument->getComponent( nSelectedComponent );
	}
	const int nSelectedLayer = m_pInstrumentEditorPanel->getSelectedLayer();

	int nLayers = 0;
	if ( pComponent != nullptr ) {
		nLayers = pComponent->getLayers().size();
	}
	
	// How much the color of the labels for the individual layers
	// are allowed to diverge from the general window color.
	int nColorScalingWidth = 90;
	int nColorScaling = 100;

	QColor layerLabelColor, layerSegmentColor, highlightColor;
	if ( m_pInstrumentEditorPanel->getInstrument() != nullptr ) {
		highlightColor = pPref->getTheme().m_color.m_highlightColor;
	} else {
		highlightColor = pPref->getTheme().m_color.m_lightColor;
	}

	int nLayer = 0;
	for ( int i = InstrumentComponent::getMaxLayers() - 1; i >= 0; i-- ) {
		const int y = 20 + LayerPreview::m_nLayerHeight * i;
		QString label = "< - >";
		
		if ( pInstrument != nullptr && pComponent != nullptr ) {
			auto pLayer = pComponent->getLayer( i );
				
			if ( pLayer != nullptr && nLayers > 0 ) {
				auto pSample = pLayer->getSample();
				if ( pSample != nullptr ) {
					label = pSample->getFilename();
					layerSegmentColor =
						pPref->getTheme().m_color.m_accentColor.lighter( 130 );
				}
				else {
					layerSegmentColor =
						pPref->getTheme().m_color.m_buttonRedColor;
				}
						
					
				const int x1 = (int)( pLayer->getStartVelocity() * width() );
				const int x2 = (int)( pLayer->getEndVelocity() * width() );

				// Labels for layers to the left will have a
				// lighter color as those to the right.
				nColorScaling = static_cast<int>(
					std::round( static_cast<float>(nLayer) /
								static_cast<float>(nLayers) * 2 *
								static_cast<float>(nColorScalingWidth) ) ) -
					nColorScalingWidth + 100;
				layerLabelColor =
					pPref->getTheme().m_color.m_windowColor.lighter( nColorScaling );
					
				p.fillRect( x1, 0, x2 - x1, 19, layerLabelColor );
				p.setPen( pPref->getTheme().m_color.m_windowTextColor );
				p.setFont( fontButton );
				p.drawText( x1, 0, x2 - x1, 20, Qt::AlignCenter, QString("%1").arg( i + 1 ) );

				if ( nSelectedLayer == i ) {
					p.setPen( highlightColor );
				}
				else {
					p.setPen( pPref->getTheme().m_color.m_windowTextColor.darker( 145 ) );
				}
				p.drawRect( x1, 1, x2 - x1 - 1, 18 );	// bordino in alto
					
				// layer view
				p.fillRect( 0, y, width(), LayerPreview::m_nLayerHeight,
							pPref->getTheme().m_color.m_windowColor );
				p.fillRect( x1, y, x2 - x1, LayerPreview::m_nLayerHeight,
							layerSegmentColor );

				nLayer++;
			}
			else {
				// layer view
				p.fillRect( 0, y, width(), LayerPreview::m_nLayerHeight,
							pPref->getTheme().m_color.m_windowColor );
			}
		}
		else {
			// layer view
			p.fillRect( 0, y, width(), LayerPreview::m_nLayerHeight,
						pPref->getTheme().m_color.m_windowColor );
		}

		QColor layerTextColor = pPref->getTheme().m_color.m_windowTextColor;
		layerTextColor.setAlpha( 155 );
		p.setPen( layerTextColor );
		p.setFont( fontText );
		p.drawText( 10, y, width() - 10, 20, Qt::AlignLeft, QString( "%1: %2" ).arg( i + 1 ).arg( label ) );
		p.setPen( layerTextColor.darker( 145 ) );
		p.drawRect( 0, y, width() - 1, LayerPreview::m_nLayerHeight );
	}

	// selected layer
	p.setPen( highlightColor );
	const int y = 20 + LayerPreview::m_nLayerHeight * nSelectedLayer;
	p.drawRect( 0, y, width() - 1, LayerPreview::m_nLayerHeight );
}

void LayerPreview::mouseReleaseEvent(QMouseEvent *ev)
{
	m_bMouseGrab = false;
	setCursor( QCursor( Qt::ArrowCursor ) );

	if ( m_pInstrumentEditorPanel->getInstrument() == nullptr ) {
		return;
	}

	/*
	 * We want the tooltip to still show if mouse pointer
	 * is over an active layer's boundary
	 */
	auto pCompo = m_pInstrumentEditorPanel->getInstrument()->getComponent(
		m_pInstrumentEditorPanel->getSelectedComponent() );
	if ( pCompo ) {
		auto pLayer = pCompo->getLayer(
			m_pInstrumentEditorPanel->getSelectedLayer() );

		if ( pLayer ) {
			int x1 = (int)( pLayer->getStartVelocity() * width() );
			int x2 = (int)( pLayer->getEndVelocity() * width() );
			
			if ( ( ev->x() < x1  + 5 ) && ( ev->x() > x1 - 5 ) ){
				setCursor( QCursor( Qt::SizeHorCursor ) );
				showLayerStartVelocity(pLayer, ev);
			}
			else if ( ( ev->x() < x2 + 5 ) && ( ev->x() > x2 - 5 ) ) {
				setCursor( QCursor( Qt::SizeHorCursor ) );
				showLayerEndVelocity(pLayer, ev);
			}
		}
	}
}

void LayerPreview::mousePressEvent(QMouseEvent *ev)
{
	const int nPosition = 0;
	const auto pInstrument = m_pInstrumentEditorPanel->getInstrument();
	const int nSelectedComponent =
		m_pInstrumentEditorPanel->getSelectedComponent();
	const int nSelectedLayer = m_pInstrumentEditorPanel->getSelectedLayer();
	if ( pInstrument == nullptr ) {
		return;
	}
	if ( ev->y() < 20 ) {
		const float fVelocity = (float)ev->x() / (float)width();

		if ( pInstrument->hasSamples() ) {
			auto pNote = std::make_shared<Note>(
				pInstrument, nPosition, fVelocity );
			pNote->setSpecificCompoIdx( nSelectedComponent );
			Hydrogen::get_instance()->getAudioEngine()->getSampler()->noteOn(pNote);
		}

		int nNewLayer = -1;
		for ( int ii = 0; ii < InstrumentComponent::getMaxLayers(); ii++ ) {
			auto pCompo = pInstrument->getComponent( nSelectedComponent );
			if ( pCompo != nullptr ){
				auto pLayer = pCompo->getLayer( ii );
				if ( pLayer != nullptr ) {
					if ( fVelocity > pLayer->getStartVelocity() &&
						 fVelocity < pLayer->getEndVelocity() ) {
						if ( ii != nSelectedLayer ) {
							nNewLayer = ii;
						}
						break;
					}
				}
			}
		}

		if ( nNewLayer != -1 ) {
			m_pInstrumentEditorPanel->setSelectedLayer( nNewLayer );
			m_pInstrumentEditorPanel->updateEditors();
		}
	}
	else {
		const int nClickedLayer = ( ev->y() - 20 ) / LayerPreview::m_nLayerHeight;
		if ( nClickedLayer < InstrumentComponent::getMaxLayers() &&
			 nClickedLayer >= 0 ) {
			m_pInstrumentEditorPanel->setSelectedLayer( nClickedLayer );
			m_pInstrumentEditorPanel->updateEditors();

			auto pCompo = pInstrument->getComponent( nSelectedComponent );
			if( pCompo != nullptr ) {
				auto pLayer = pCompo->getLayer( nClickedLayer );
				if ( pLayer != nullptr ) {
					const float fVelocity = pLayer->getEndVelocity() - 0.01;
					auto pNote = std::make_shared<Note>(
						pInstrument, nPosition, fVelocity );
					pNote->setSpecificCompoIdx( nSelectedComponent );
					Hydrogen::get_instance()->getAudioEngine()->getSampler()->
						noteOn( pNote );
				
					int x1 = (int)( pLayer->getStartVelocity() * width() );
					int x2 = (int)( pLayer->getEndVelocity() * width() );
				
					if ( ( ev->x() < x1  + 5 ) && ( ev->x() > x1 - 5 ) ){
						setCursor( QCursor( Qt::SizeHorCursor ) );
						m_bGrabLeft = true;
						m_bMouseGrab = true;
						showLayerStartVelocity(pLayer, ev);
					}
					else if ( ( ev->x() < x2 + 5 ) && ( ev->x() > x2 - 5 ) ){
						setCursor( QCursor( Qt::SizeHorCursor ) );
						m_bGrabLeft = false;
						m_bMouseGrab = true;
						showLayerEndVelocity(pLayer, ev);
					}
					else {
						setCursor( QCursor( Qt::ArrowCursor ) );
					}
				}
			}
		}
	}
}

void LayerPreview::mouseMoveEvent( QMouseEvent *ev )
{
	const auto pInstrument = m_pInstrumentEditorPanel->getInstrument();
	const int nSelectedComponent =
		m_pInstrumentEditorPanel->getSelectedComponent();
	const int nSelectedLayer = m_pInstrumentEditorPanel->getSelectedLayer();
	if ( pInstrument == nullptr ) {
		return;
	}
	auto pComponent = pInstrument->getComponent( nSelectedComponent );
	if ( pComponent == nullptr ) {
		return;
	}

	const int x = ev->pos().x();
	const int y = ev->pos().y();

	if ( y < 20 ) {
		setCursor( QCursor( m_speakerPixmap ) );
		return;
	}

	float fVel = (float)x / (float)width();
	if (fVel < 0 ) {
		fVel = 0;
	}
	else  if (fVel > 1) {
		fVel = 1;
	}
	if ( m_bMouseGrab ) {
		auto pLayer = pComponent->getLayer( nSelectedLayer );
		if ( pLayer != nullptr ) {
			bool bChanged = false;
			if ( m_bGrabLeft ) {
				if ( fVel < pLayer->getEndVelocity()) {
					pLayer->setStartVelocity( fVel );
					bChanged = true;
					showLayerStartVelocity( pLayer, ev );
				}
			}
			else {
				if ( fVel > pLayer->getStartVelocity()) {
					pLayer->setEndVelocity( fVel );
					bChanged = true;
					showLayerEndVelocity( pLayer, ev );
				}
			}

			if ( bChanged ) {
				update();
				Hydrogen::get_instance()->setIsModified( true );
			}
		}
	}
	else {
		int nHoveredLayer = ( ev->y() - 20 ) / LayerPreview::m_nLayerHeight;
		if ( nHoveredLayer < InstrumentComponent::getMaxLayers() &&
			 nHoveredLayer >= 0 ) {

			auto pHoveredLayer = pComponent->getLayer( nHoveredLayer );
			if ( pHoveredLayer != nullptr ) {
				int x1 = (int)( pHoveredLayer->getStartVelocity() * width() );
				int x2 = (int)( pHoveredLayer->getEndVelocity() * width() );
					
				if ( ( x < x1  + 5 ) && ( x > x1 - 5 ) ){
					setCursor( QCursor( Qt::SizeHorCursor ) );
					showLayerStartVelocity(pHoveredLayer, ev);
				}
				else if ( ( x < x2 + 5 ) && ( x > x2 - 5 ) ){
					setCursor( QCursor( Qt::SizeHorCursor ) );
					showLayerEndVelocity(pHoveredLayer, ev);
				}
				else {
					setCursor( QCursor( Qt::ArrowCursor ) );
					QToolTip::hideText();
				}
			}
			else {
				setCursor( QCursor( Qt::ArrowCursor ) );
				QToolTip::hideText();
			}
		}
		else {
			setCursor( QCursor( Qt::ArrowCursor ) );
			QToolTip::hideText();
		}
	}
}

int LayerPreview::getMidiVelocityFromRaw( const float raw )
{
	return static_cast<int> (raw * 127);
}

void LayerPreview::showLayerStartVelocity( const std::shared_ptr<InstrumentLayer> pLayer, const QMouseEvent* pEvent )
{
	const float fVelo = pLayer->getStartVelocity();

	QToolTip::showText( pEvent->globalPos(),
			tr( "Dec. = %1\nMIDI = %2" )
				.arg( QString::number( fVelo, 'f', 2) )
				.arg( getMidiVelocityFromRaw( fVelo ) +1 ),
			this);
}

void LayerPreview::showLayerEndVelocity( const std::shared_ptr<InstrumentLayer> pLayer, const QMouseEvent* pEvent )
{
	const float fVelo = pLayer->getEndVelocity();

	QToolTip::showText( pEvent->globalPos(),
			tr( "Dec. = %1\nMIDI = %2" )
				.arg( QString::number( fVelo, 'f', 2) )
				.arg( getMidiVelocityFromRaw( fVelo ) +1 ),
			this);
}

int LayerPreview::getPointSizeButton() const
{
	auto pPref = H2Core::Preferences::get_instance();
	
	int nPointSize;
	
	switch( pPref->getTheme().m_font.m_fontSize ) {
	case H2Core::FontTheme::FontSize::Small:
		nPointSize = 6;
		break;
	case H2Core::FontTheme::FontSize::Medium:
		nPointSize = 8;
		break;
	case H2Core::FontTheme::FontSize::Large:
		nPointSize = 12;
		break;
	}

	return nPointSize;
}
