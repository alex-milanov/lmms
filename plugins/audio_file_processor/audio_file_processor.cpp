/*
 * audio_file_processor.cpp - instrument for using audio-files
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * 
 * This file is part of Linux MultiMedia Studio - http://lmms.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include <QtGui/QPainter>
#include <QtGui/QBitmap>
#include <QtXml/QDomDocument>
#include <QtCore/QFileInfo>
#include <QtGui/QDropEvent>


#include "audio_file_processor.h"
#include "engine.h"
#include "song.h"
#include "InstrumentTrack.h"
#include "note_play_handle.h"
#include "interpolation.h"
#include "gui_templates.h"
#include "tooltip.h"
#include "string_pair_drag.h"
#include "mmp.h"

#include "embed.cpp"


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT audiofileprocessor_plugin_descriptor =
{
	STRINGIFY( PLUGIN_NAME ),
	"AudioFileProcessor",
	QT_TRANSLATE_NOOP( "pluginBrowser",
				"simple sampler with various settings for "
				"using samples (e.g. drums) in an "
				"instrument-track" ),
	"Tobias Doerffel <tobydox/at/users.sf.net>",
	0x0100,
	Plugin::Instrument,
	new PluginPixmapLoader( "logo" ),
	"wav,ogg,ds,spx,au,voc,aif,aiff,flac,raw",
	NULL
} ;

}




audioFileProcessor::audioFileProcessor( InstrumentTrack * _instrument_track ) :
	Instrument( _instrument_track, &audiofileprocessor_plugin_descriptor ),
	m_sampleBuffer(),
	m_ampModel( 100, 0, 500, 1, this, tr( "Amplify" ) ),
	m_startPointModel( 0, 0, 1, 0.0000001f, this, tr( "Start of sample") ),
	m_endPointModel( 1, 0, 1, 0.0000001f, this, tr( "End of sample" ) ),
	m_reverseModel( false, this, tr( "Reverse sample" ) ),
	m_loopModel( false, this, tr( "Loop") )
{
	connect( &m_reverseModel, SIGNAL( dataChanged() ),
				this, SLOT( reverseModelChanged() ) );
	connect( &m_ampModel, SIGNAL( dataChanged() ),
				this, SLOT( ampModelChanged() ) );
	connect( &m_startPointModel, SIGNAL( dataChanged() ),
				this, SLOT( loopPointChanged() ) );
	connect( &m_endPointModel, SIGNAL( dataChanged() ),
				this, SLOT( loopPointChanged() ) );
}




audioFileProcessor::~audioFileProcessor()
{
}




void audioFileProcessor::playNote( notePlayHandle * _n,
						sampleFrame * _working_buffer )
{
	const fpp_t frames = _n->framesLeftForCurrentPeriod();

	if( !_n->m_pluginData )
	{
		_n->m_pluginData = new handleState( _n->hasDetuningInfo() );
	}

	if( m_sampleBuffer.play( _working_buffer,
					(handleState *)_n->m_pluginData,
					frames, _n->frequency(),
						m_loopModel.value() ) == TRUE )
	{
		applyRelease( _working_buffer, _n );
		instrumentTrack()->processAudioBuffer( _working_buffer,
								frames,_n );
		emit isPlaying( _n->totalFramesPlayed() * _n->frequency() / m_sampleBuffer.frequency() );
	}
	else
	{
		emit isPlaying( 0 );
	}
}




void audioFileProcessor::deleteNotePluginData( notePlayHandle * _n )
{
	delete (handleState *)_n->m_pluginData;
}




void audioFileProcessor::saveSettings( QDomDocument & _doc,
							QDomElement & _this )
{
	_this.setAttribute( "src", m_sampleBuffer.audioFile() );
	if( m_sampleBuffer.audioFile() == "" )
	{
		QString s;
		_this.setAttribute( "sampledata",
						m_sampleBuffer.toBase64( s ) );
	}
	m_reverseModel.saveSettings( _doc, _this, "reversed" );
	m_loopModel.saveSettings( _doc, _this, "looped" );
	m_ampModel.saveSettings( _doc, _this, "amp" );
	m_startPointModel.saveSettings( _doc, _this, "sframe" );
	m_endPointModel.saveSettings( _doc, _this, "eframe" );
}




void audioFileProcessor::loadSettings( const QDomElement & _this )
{
	if( _this.attribute( "src" ) != "" )
	{
		setAudioFile( _this.attribute( "src" ), FALSE );
	}
	else if( _this.attribute( "sampledata" ) != "" )
	{
		m_sampleBuffer.loadFromBase64( _this.attribute( "srcdata" ) );
	}
	m_reverseModel.loadSettings( _this, "reversed" );
	m_loopModel.loadSettings( _this, "looped" );
	m_ampModel.loadSettings( _this, "amp" );
	m_startPointModel.loadSettings( _this, "sframe" );
	m_endPointModel.loadSettings( _this, "eframe" );

	loopPointChanged();
}




void audioFileProcessor::loadFile( const QString & _file )
{
	setAudioFile( _file );
}




QString audioFileProcessor::nodeName( void ) const
{
	return( audiofileprocessor_plugin_descriptor.name );
}




Uint32 audioFileProcessor::getBeatLen( notePlayHandle * _n ) const
{
	const float freq_factor = BaseFreq / _n->frequency() *
			engine::mixer()->processingSampleRate() /
					engine::mixer()->baseSampleRate();

	return( static_cast<Uint32>( floorf( ( m_sampleBuffer.endFrame() -
						m_sampleBuffer.startFrame() ) *
							freq_factor ) ) );
}




PluginView * audioFileProcessor::instantiateView( QWidget * _parent )
{
	return( new AudioFileProcessorView( this, _parent ) );
}




void audioFileProcessor::setAudioFile( const QString & _audio_file,
													bool _rename )
{
	// is current channel-name equal to previous-filename??
	if( _rename &&
		( instrumentTrack()->name() ==
			QFileInfo( m_sampleBuffer.audioFile() ).fileName() ||
				m_sampleBuffer.audioFile().isEmpty() ) )
	{
		// then set it to new one
		instrumentTrack()->setName( QFileInfo( _audio_file).fileName() );
	}
	// else we don't touch the track-name, because the user named it self

	m_sampleBuffer.setAudioFile( _audio_file );
	loopPointChanged();
}




void audioFileProcessor::reverseModelChanged( void )
{
	m_sampleBuffer.setReversed( m_reverseModel.value() );
}




void audioFileProcessor::ampModelChanged( void )
{
	m_sampleBuffer.setAmplification( m_ampModel.value() / 100.0f );
}




void audioFileProcessor::loopPointChanged( void )
{
	const f_cnt_t f1 = static_cast<f_cnt_t>( m_startPointModel.value() *
						( m_sampleBuffer.frames()-1 ) );
	const f_cnt_t f2 = static_cast<f_cnt_t>( m_endPointModel.value() *
						( m_sampleBuffer.frames()-1 ) );
	m_sampleBuffer.setStartFrame( qMin<f_cnt_t>( f1, f2 ) );
	m_sampleBuffer.setEndFrame( qMax<f_cnt_t>( f1, f2 ) );
	emit dataChanged();
}




QPixmap * AudioFileProcessorView::s_artwork = NULL;


AudioFileProcessorView::AudioFileProcessorView( Instrument * _instrument,
							QWidget * _parent ) :
	InstrumentView( _instrument, _parent )
{
	if( s_artwork == NULL )
	{
		s_artwork = new QPixmap( PLUGIN_NAME::getIconPixmap(
								"artwork" ) );
	}

	m_openAudioFileButton = new pixmapButton( this );
	m_openAudioFileButton->setCursor( QCursor( Qt::PointingHandCursor ) );
	m_openAudioFileButton->move( 227, 72 );
	m_openAudioFileButton->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"select_file" ) );
	m_openAudioFileButton->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"select_file" ) );
	connect( m_openAudioFileButton, SIGNAL( clicked() ),
					this, SLOT( openAudioFile() ) );
	toolTip::add( m_openAudioFileButton, tr( "Open other sample" ) );

	m_openAudioFileButton->setWhatsThis(
		tr( "Click here, if you want to open another audio-file. "
			"A dialog will appear where you can select your file. "
			"Settings like looping-mode, start and end-points, " 
			"amplify-value, and so on are not reset. So, it may not "
			"sound like the original sample.") );

	m_reverseButton = new pixmapButton( this );
	m_reverseButton->setCheckable( TRUE );
	m_reverseButton->move( 184, 124 );
	m_reverseButton->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"reverse_on" ) );
	m_reverseButton->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"reverse_off" ) );
	toolTip::add( m_reverseButton, tr( "Reverse sample" ) );
	m_reverseButton->setWhatsThis(
		tr( "If you enable this button, the whole sample is reversed. "
			"This is useful for cool effects, e.g. a reversed "
			"crash." ) );

	m_loopButton = new pixmapButton( this );
	m_loopButton->setCheckable( TRUE );
	m_loopButton->move( 220, 124 );
	m_loopButton->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
								"loop_on" ) );
	m_loopButton->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
								"loop_off" ) );
	toolTip::add( m_loopButton,
				tr( "Loop sample at start- and end-point" ) );
	m_loopButton->setWhatsThis(
		tr( "Here you can set, whether looping-mode is enabled. If "
			"enabled, AudioFileProcessor loops between start and "
			"end-points of a sample until the whole note is played. "
			"This is useful for things like string and choir "
			"samples." ) );

	m_ampKnob = new knob( knobStyled, this );
	m_ampKnob->setVolumeKnob( TRUE );
	m_ampKnob->move( 17, 108 );
	m_ampKnob->setFixedSize( 37, 47 );
	m_ampKnob->setHintText( tr( "Amplify:" )+" ", "%" );
	m_ampKnob->setWhatsThis(
		tr( "With this knob you can set the amplify ratio. When you "
			"set a value of 100% your sample isn't changed. "
			"Otherwise it will be amplified up or down (your "
			"actual sample-file isn't touched!)" ) );

	m_startKnob = new AudioFileProcessorWaveView::knob( this );
	m_startKnob->move( 68, 108 );
	m_startKnob->setHintText( tr( "Startpoint:" )+" ", "" );
	m_startKnob->setWhatsThis(
		tr( "With this knob you can set the point where "
			"AudioFileProcessor should begin playing your sample. "
			"If you enable looping-mode, this is the point to "
			"which AudioFileProcessor returns if a note is longer "
			"than the sample between the start and end-points." ) );

	m_endKnob = new AudioFileProcessorWaveView::knob( this );
	m_endKnob->move( 119, 108 );
	m_endKnob->setHintText( tr( "Endpoint:" )+" ", "" );
	m_endKnob->setWhatsThis(
		tr( "With this knob you can set the point where "
			"AudioFileProcessor should stop playing your sample. "
			"If you enable looping-mode, this is the point where "
			"AudioFileProcessor returns if a note is longer than "
			"the sample between the start and end-points." ) );

	m_waveView = new AudioFileProcessorWaveView( this, 245, 75, castModel<audioFileProcessor>()->m_sampleBuffer );
	m_waveView->move( 2, 172 );
	m_waveView->setKnobs(
		dynamic_cast<AudioFileProcessorWaveView::knob *>( m_startKnob ),
		dynamic_cast<AudioFileProcessorWaveView::knob *>( m_endKnob )
	);

	connect( castModel<audioFileProcessor>(), SIGNAL( isPlaying( f_cnt_t ) ),
			m_waveView, SLOT( isPlaying( f_cnt_t ) ) );

	qRegisterMetaType<f_cnt_t>( "f_cnt_t" );

	setAcceptDrops( TRUE );
}




AudioFileProcessorView::~AudioFileProcessorView()
{
}




void AudioFileProcessorView::dragEnterEvent( QDragEnterEvent * _dee )
{
	if( _dee->mimeData()->hasFormat( stringPairDrag::mimeType() ) )
	{
		QString txt = _dee->mimeData()->data(
						stringPairDrag::mimeType() );
		if( txt.section( ':', 0, 0 ) == QString( "tco_%1" ).arg(
							track::SampleTrack ) )
		{
			_dee->acceptProposedAction();
		}
		else if( txt.section( ':', 0, 0 ) == "samplefile" )
		{
			_dee->acceptProposedAction();
		}
		else
		{
			_dee->ignore();
		}
	}
	else
	{
		_dee->ignore();
	}
}




void AudioFileProcessorView::dropEvent( QDropEvent * _de )
{
	QString type = stringPairDrag::decodeKey( _de );
	QString value = stringPairDrag::decodeValue( _de );
	if( type == "samplefile" )
	{
		castModel<audioFileProcessor>()->setAudioFile( value );
		_de->accept();
		return;
	}
	else if( type == QString( "tco_%1" ).arg( track::SampleTrack ) )
	{
		multimediaProject mmp( value.toUtf8() );
		castModel<audioFileProcessor>()->setAudioFile( mmp.content().
				firstChild().toElement().attribute( "src" ) );
		_de->accept();
		return;
	}

	_de->ignore();
}




void AudioFileProcessorView::paintEvent( QPaintEvent * )
{
	QPainter p( this );

	p.drawPixmap( 0, 0, *s_artwork );

	audioFileProcessor * a = castModel<audioFileProcessor>();

 	QString file_name = "";
	int idx = a->m_sampleBuffer.audioFile().length();

	p.setFont( pointSize<8>( font() ) );

	QFontMetrics fm( p.font() );

	// simple algorithm for creating a text from the filename that
	// matches in the white rectangle
	while( idx > 0 &&
		fm.size( Qt::TextSingleLine, file_name + "..." ).width() < 210 )
	{
		file_name = a->m_sampleBuffer.audioFile()[--idx] + file_name;
	}

	if( idx > 0 )
	{
		file_name = "..." + file_name;
	}

	p.setPen( QColor( 255, 255, 255 ) );
	p.drawText( 8, 99, file_name );
}




void AudioFileProcessorView::sampleUpdated( void )
{
	m_waveView->update();
	update();
}





void AudioFileProcessorView::openAudioFile( void )
{
	QString af = castModel<audioFileProcessor>()->m_sampleBuffer.
							openAudioFile();
	if( af != "" )
	{
		castModel<audioFileProcessor>()->setAudioFile( af );
		engine::getSong()->setModified();
	}
}




void AudioFileProcessorView::modelChanged( void )
{
	audioFileProcessor * a = castModel<audioFileProcessor>();
	connect( &a->m_sampleBuffer, SIGNAL( sampleUpdated() ),
					this, SLOT( sampleUpdated() ) );
	m_ampKnob->setModel( &a->m_ampModel );
	m_startKnob->setModel( &a->m_startPointModel );
	m_endKnob->setModel( &a->m_endPointModel );
	m_reverseButton->setModel( &a->m_reverseModel );
	m_loopButton->setModel( &a->m_loopModel );
	sampleUpdated();
}




AudioFileProcessorWaveView::AudioFileProcessorWaveView( QWidget * _parent, int _w, int _h, sampleBuffer & _buf ) :
	QWidget( _parent ),
	m_sampleBuffer( _buf ),
	m_graph( QPixmap( _w - 2 * s_padding, _h - 2 * s_padding ) ),
	m_from( 0 ),
	m_to( m_sampleBuffer.frames() ),
	m_last_from( 0 ),
	m_last_to( 0 ),
	m_startKnob( 0 ),
	m_endKnob( 0 ),
	m_isDragging( false ),
	m_reversed( false ),
	m_framesPlayed( 0 ),
	m_animation(configManager::inst()->value("ui", "animateafp").toInt())
{
	setFixedSize( _w, _h );
	setMouseTracking( true );

	if( m_sampleBuffer.frames() > 1 )
	{
		const f_cnt_t marging = ( m_sampleBuffer.endFrame() - m_sampleBuffer.startFrame() ) * 0.1;
		m_from = qMax( 0, m_sampleBuffer.startFrame() - marging );
		m_to = qMin( m_sampleBuffer.endFrame() + marging, m_sampleBuffer.frames() );
	}

	update();
}




void AudioFileProcessorWaveView::isPlaying( f_cnt_t _frames_played )
{
	const f_cnt_t nb_frames = m_sampleBuffer.endFrame() - m_sampleBuffer.startFrame();
	if( nb_frames < 1 )
	{
		m_framesPlayed = 0;
	}
	else
	{
		m_framesPlayed = _frames_played % nb_frames;
	}
	update();
}




void AudioFileProcessorWaveView::enterEvent( QEvent * _e )
{
	QApplication::setOverrideCursor( Qt::OpenHandCursor );
}




void AudioFileProcessorWaveView::leaveEvent( QEvent * _e )
{
	while( QApplication::overrideCursor() )
	{
		QApplication::restoreOverrideCursor();
	}
}




void AudioFileProcessorWaveView::mousePressEvent( QMouseEvent * _me )
{
	m_isDragging = true;
	m_draggingLastPoint = _me->pos();

	if( isCloseTo( _me->x(), m_startFrameX ) )
	{
		m_draggingType = sample_start;
	}
	else if( isCloseTo( _me->x(), m_endFrameX ) )
	{
		m_draggingType = sample_end;
	}
	else
	{
		m_draggingType = wave;
		QApplication::setOverrideCursor( Qt::ClosedHandCursor );
	}
}




void AudioFileProcessorWaveView::mouseReleaseEvent( QMouseEvent * _me )
{
	m_isDragging = false;
	if( m_draggingType == wave )
	{
		QApplication::restoreOverrideCursor();
	}
}




void AudioFileProcessorWaveView::mouseMoveEvent( QMouseEvent * _me )
{
	if( ! m_isDragging )
	{
		const bool is_size_cursor =
			QApplication::overrideCursor()->shape() == Qt::SizeHorCursor;

		if( isCloseTo( _me->x(), m_startFrameX ) ||
			isCloseTo( _me->x(), m_endFrameX ) )
		{
			if( ! is_size_cursor )
			{
				QApplication::setOverrideCursor( Qt::SizeHorCursor );
			}
		}
		else if( is_size_cursor )
		{
			QApplication::restoreOverrideCursor();
		}
		return;
	}

	const int step = _me->x() - m_draggingLastPoint.x();
	switch( m_draggingType )
	{
		case sample_start:
			slideSamplePointByPx( start, step );
			break;
		case sample_end:
			slideSamplePointByPx( end, step );
			break;
		default:
			if( qAbs( _me->y() - m_draggingLastPoint.y() )
				< 2 * qAbs( _me->x() - m_draggingLastPoint.x() ) )
			{
				slide( step );
			}
			else
			{
				zoom( _me->y() < m_draggingLastPoint.y() );
			}
	}

	m_draggingLastPoint = _me->pos();
	update();
}




void AudioFileProcessorWaveView::wheelEvent( QWheelEvent * _we )
{
	zoom( _we->delta() > 0 );
	update();
}




void AudioFileProcessorWaveView::paintEvent( QPaintEvent * _pe )
{
	QPainter p( this );

	p.drawPixmap( s_padding, s_padding, m_graph );

	p.setPen( QColor( 0xFF, 0xFF, 0x00 ) );
	const QRect graph_rect( s_padding, s_padding, width() - 2 * s_padding, height() - 2 * s_padding );
	const f_cnt_t frames = m_to - m_from;
	m_startFrameX = graph_rect.x() + ( m_sampleBuffer.startFrame() - m_from ) *
						double( graph_rect.width() ) / frames;
	m_endFrameX = graph_rect.x() + ( m_sampleBuffer.endFrame() - m_from ) *
						double( graph_rect.width() ) / frames;

	p.drawLine( m_startFrameX, graph_rect.y(),
					m_startFrameX,
					graph_rect.height() + graph_rect.y() );
	p.drawLine( m_endFrameX, graph_rect.y(),
					m_endFrameX,
					graph_rect.height() + graph_rect.y() );

	if( m_endFrameX - m_startFrameX > 2 )
	{
		p.fillRect(
			m_startFrameX + 1,
			graph_rect.y(),
			m_endFrameX - m_startFrameX - 1,
			graph_rect.height() + graph_rect.y(),
			QColor( 255, 255, 0, 70 )
		);

		if( m_framesPlayed && m_animation)
		{
			const int played_width_px = m_framesPlayed
				/ double( m_sampleBuffer.endFrame() - m_sampleBuffer.startFrame() )
				* ( m_endFrameX - m_startFrameX );
			QLinearGradient g( m_startFrameX + 1, 0, m_startFrameX + 1 + played_width_px, 0 );
			const QColor c( 0, 120, 255, 180 );
			g.setColorAt( 0, Qt::transparent );
			g.setColorAt( 0.8, c );
			g.setColorAt( 1,  c );
			p.fillRect(
				m_startFrameX + 1,
				graph_rect.y(),
				played_width_px,
				graph_rect.height() + graph_rect.y(),
				g
			);
			p.setPen( QColor( 0, 255, 255 ) );
			p.drawLine(
				m_startFrameX + 1 + played_width_px,
				graph_rect.y(),
				m_startFrameX + 1 + played_width_px,
				graph_rect.height() + graph_rect.y()
			);
			m_framesPlayed = 0;
		}
	}

	QLinearGradient g( 0, 0, width() * 0.7, 0 );
	const QColor c( 0, 0, 150, 180 );
	g.setColorAt( 0, c );
	g.setColorAt( 0.4, c );
	g.setColorAt( 1,  Qt::transparent );
	p.fillRect( s_padding, s_padding, m_graph.width(), 14, g );

	p.setPen( QColor( 255, 255, 20 ) );
	p.setFont( pointSize<8>( font() ) );

	QString length_text;
	const int length = m_sampleBuffer.sampleLength();

	if( length > 20000 )
	{
		length_text = QString::number( length / 1000 ) + "s";
	}
	else if( length > 2000 )
	{
		length_text = QString::number( ( length / 100 ) / 10.0 ) + "s";
	}
	else
	{
		length_text = QString::number( length ) + "ms";
	}

	p.drawText(
		s_padding + 2,
		s_padding + 10,
		tr( "Sample length:" ) + " " + length_text
	);
}




void AudioFileProcessorWaveView::updateGraph()
{
	if( m_to == 1 )
	{
		m_to = m_sampleBuffer.frames() * 0.7;
		slideSamplePointToFrames( end, m_to * 0.7 );
	}

	if( m_from > m_sampleBuffer.startFrame() )
	{
		m_from = m_sampleBuffer.startFrame();
	}

	if( m_to < m_sampleBuffer.endFrame() )
	{
		m_to = m_sampleBuffer.endFrame();
	}

	if( m_sampleBuffer.reversed() != m_reversed )
	{
		reverse();
	}
	else if( m_last_from == m_from && m_last_to == m_to )
	{
		return;
	}

	m_last_from = m_from;
	m_last_to = m_to;

	m_graph.fill( Qt::transparent );
	QPainter p( &m_graph );
	p.setPen( QColor( 64, 255, 160 ) );
	m_sampleBuffer.visualize(
		p,
		QRect( 0, 0, m_graph.width(), m_graph.height() ),
		m_from, m_to
	);
}




void AudioFileProcessorWaveView::zoom( const bool _out )
{
	const f_cnt_t start = m_sampleBuffer.startFrame();
	const f_cnt_t end = m_sampleBuffer.endFrame();
	const f_cnt_t frames = m_sampleBuffer.frames();
	const f_cnt_t d_from = start - m_from;
	const f_cnt_t d_to = m_to - end;

	const f_cnt_t step = qMax( 1, qMax( d_from, d_to ) / 10 );
	const f_cnt_t step_from = ( _out ? - step : step );
	const f_cnt_t step_to = ( _out ? step : - step );

	const double comp_ratio = double( qMin( d_from, d_to ) )
								/ qMax( 1, qMax( d_from, d_to ) );

	f_cnt_t new_from;
	f_cnt_t new_to;

	if( ( _out && d_from < d_to ) || ( ! _out && d_to < d_from ) )
	{
		new_from = qBound( 0, m_from + step_from, start );
		new_to = qBound(
			end,
			m_to + f_cnt_t( step_to * ( new_from == m_from ? 1 : comp_ratio ) ),
			frames
		);
	}
	else
	{
		new_to = qBound( end, m_to + step_to, frames );
		new_from = qBound(
			0,
			m_from + f_cnt_t( step_from * ( new_to == m_to ? 1 : comp_ratio ) ),
			start
		);
	}

	if( double( new_to - new_from ) / m_sampleBuffer.sampleRate() > 0.05  )
	{
		m_from = new_from;
		m_to = new_to;
	}
}




void AudioFileProcessorWaveView::slide( int _px )
{
	const double fact = qAbs( double( _px ) / width() );
	f_cnt_t step = ( m_to - m_from ) * fact;
	if( _px > 0 )
	{
		step = -step;
	}

	f_cnt_t step_from = qBound( 0, m_from + step, m_sampleBuffer.frames() ) - m_from;
	f_cnt_t step_to = qBound( m_from + 1, m_to + step, m_sampleBuffer.frames() ) - m_to;

	step = qAbs( step_from ) < qAbs( step_to ) ? step_from : step_to;

	m_from += step;
	m_to += step;
	slideSampleByFrames( step );
}




void AudioFileProcessorWaveView::setKnobs( knob * _start, knob * _end )
{
	m_startKnob = _start;
	m_endKnob = _end;

	m_startKnob->setWaveView( this );
	m_startKnob->setRelatedKnob( m_endKnob );

	m_endKnob->setWaveView( this );
	m_endKnob->setRelatedKnob( m_startKnob );
}




void AudioFileProcessorWaveView::slideSamplePointByPx( knobType _point, int _px )
{
	slideSamplePointByFrames(
		_point,
		f_cnt_t( ( double( _px ) / width() ) * ( m_to - m_from ) )
	);
}




void AudioFileProcessorWaveView::slideSamplePointByFrames( knobType _point, f_cnt_t _frames, bool _slide_to )
{
	knob * knob = _point == start ? m_startKnob : m_endKnob;
	if( ! knob )
	{
		return;
	}

	const double v = double( _frames ) / m_sampleBuffer.frames();
	if( _slide_to )
	{
		knob->slideTo( v );
	}
	else
	{
		knob->slideBy( v );
	}
}




void AudioFileProcessorWaveView::slideSampleByFrames( f_cnt_t _frames )
{
	if( m_sampleBuffer.frames() <= 1 )
	{
		return;
	}
	const double v = double( _frames ) / m_sampleBuffer.frames();
	if(m_startKnob) {
		m_startKnob->slideBy( v, false );
	}
	if(m_endKnob) {
		m_endKnob->slideBy( v, false );
	}
}




void AudioFileProcessorWaveView::reverse()
{
	slideSampleByFrames(
		m_sampleBuffer.frames()
			- m_sampleBuffer.endFrame()
			- m_sampleBuffer.startFrame()
	);

	const f_cnt_t from = m_from;
	m_from = m_sampleBuffer.frames() - m_to;
	m_to = m_sampleBuffer.frames() - from;

	m_reversed = ! m_reversed;
}




void AudioFileProcessorWaveView::knob::slideTo( double _v, bool _check_bound )
{
	if( _check_bound && ! checkBound( _v ) )
	{
		return;
	}
	model()->setValue( _v );
	emit sliderMoved( model()->value() );
}




float AudioFileProcessorWaveView::knob::getValue( const QPoint & _p )
{
	const double dec_fact = ! m_waveView ? 1 :
		double( m_waveView->m_to - m_waveView->m_from )
			/ m_waveView->m_sampleBuffer.frames();
	const float inc = ::knob::getValue( _p ) * dec_fact;
	const float next = model()->value() - inc;

	if( ! checkBound( next ) )
	{
		return 0;
	}
	return inc;
}




bool AudioFileProcessorWaveView::knob::checkBound( double _v ) const
{
	if( ! m_relatedKnob || ! m_waveView )
	{
		return true;
	}

	if( ( m_relatedKnob->model()->value() - _v > 0 ) !=
		( m_relatedKnob->model()->value() - model()->value() > 0 ) )
		return false;

	const double d1 = qAbs( m_relatedKnob->model()->value() - model()->value() )
		* ( m_waveView->m_sampleBuffer.frames() )
		/ m_waveView->m_sampleBuffer.sampleRate();

	const double d2 = qAbs( m_relatedKnob->model()->value() - _v )
		* ( m_waveView->m_sampleBuffer.frames() )
		/ m_waveView->m_sampleBuffer.sampleRate();

	return d1 < d2 || d2 > 0.02;
}






extern "C"
{

// necessary for getting instance out of shared lib
Plugin * PLUGIN_EXPORT lmms_plugin_main( Model *, void * _data )
{
	return new audioFileProcessor(
				static_cast<InstrumentTrack *>( _data ) );
}


}


#include "moc_audio_file_processor.cxx"

