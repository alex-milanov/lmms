/*
 * audio_file_processor.h - declaration of class audioFileProcessor
 *                          (instrument-plugin for using audio-files)
 *
 * Copyright (c) 2004-2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#ifndef _AUDIO_FILE_PROCESSOR_H
#define _AUDIO_FILE_PROCESSOR_H

#include <QtGui/QPixmap>

#include "Instrument.h"
#include "InstrumentView.h"
#include "sample_buffer.h"
#include "knob.h"
#include "pixmap_button.h"



class audioFileProcessor : public Instrument
{
	Q_OBJECT
public:
	audioFileProcessor( InstrumentTrack * _instrument_track );
	virtual ~audioFileProcessor();

	virtual void playNote( notePlayHandle * _n, 
						sampleFrame * _working_buffer );
	virtual void deleteNotePluginData( notePlayHandle * _n );

	virtual void saveSettings( QDomDocument & _doc,
						QDomElement & _parent );
	virtual void loadSettings( const QDomElement & _this );

	virtual void loadFile( const QString & _file );

	virtual QString nodeName() const;

	virtual Uint32 getBeatLen( notePlayHandle * _n ) const;

	virtual f_cnt_t desiredReleaseFrames() const
	{
		return( 128 );
	}

	virtual PluginView * instantiateView( QWidget * _parent );


public slots:
	void setAudioFile( const QString & _audio_file, bool _rename = true );


private slots:
	void reverseModelChanged();
	void ampModelChanged();
	void loopPointChanged();


signals:
	void isPlaying( f_cnt_t _frames_played );


private:
	typedef sampleBuffer::handleState handleState;

	sampleBuffer m_sampleBuffer;
	
	FloatModel m_ampModel;
	FloatModel m_startPointModel;
	FloatModel m_endPointModel;
	BoolModel m_reverseModel;
	BoolModel m_loopModel;

	friend class AudioFileProcessorView;

} ;



class AudioFileProcessorWaveView;


class AudioFileProcessorView : public InstrumentView
{
	Q_OBJECT
public:
	AudioFileProcessorView( Instrument * _instrument, QWidget * _parent );
	virtual ~AudioFileProcessorView();


protected slots:
	void sampleUpdated();
	void openAudioFile();


protected:
	virtual void dragEnterEvent( QDragEnterEvent * _dee );
	virtual void dropEvent( QDropEvent * _de );
	virtual void paintEvent( QPaintEvent * );


private:
	virtual void modelChanged();

	static QPixmap * s_artwork;

	AudioFileProcessorWaveView * m_waveView;
	knob * m_ampKnob;
	knob * m_startKnob;
	knob * m_endKnob;
	pixmapButton * m_openAudioFileButton;
	pixmapButton * m_reverseButton;
	pixmapButton * m_loopButton;

} ;



class AudioFileProcessorWaveView : public QWidget
{
	Q_OBJECT
protected:
	virtual void enterEvent( QEvent * _e );
	virtual void leaveEvent( QEvent * _e );
	virtual void mousePressEvent( QMouseEvent * _me );
	virtual void mouseReleaseEvent( QMouseEvent * _me );
	virtual void mouseMoveEvent( QMouseEvent * _me );
	virtual void wheelEvent( QWheelEvent * _we );
	virtual void paintEvent( QPaintEvent * _pe );


public:
	enum knobType
	{
		start,
		end,
	} ;

	class knob : public ::knob
	{
		const AudioFileProcessorWaveView * m_waveView;
		const knob * m_relatedKnob;


	public:
		knob( QWidget * _parent ) :
			::knob( knobStyled, _parent ),
			m_waveView( 0 ),
			m_relatedKnob( 0 )
		{
			setFixedSize( 37, 47 );
		}

		void setWaveView( const AudioFileProcessorWaveView * _wv )
		{
			m_waveView = _wv;
		}

		void setRelatedKnob( const knob * _knob )
		{
			m_relatedKnob = _knob;
		}

		void slideBy( double _v, bool _check_bound = true )
		{
			slideTo( model()->value() + _v, _check_bound );
		}

		void slideTo( double _v, bool _check_bound = true );


	protected:
		float getValue( const QPoint & _p );


	private:
		bool checkBound( double _v ) const;

	} ;


public slots:
	void update()
	{
		updateGraph();
		QWidget::update();
	}

	void isPlaying( f_cnt_t _frames_played );


private:
	static const int s_padding = 2;

	enum draggingType
	{
		wave,
		sample_start,
		sample_end,
	} ;

	sampleBuffer & m_sampleBuffer;
	QPixmap m_graph;
	f_cnt_t m_from;
	f_cnt_t m_to;
	f_cnt_t m_last_from;
	f_cnt_t m_last_to;
	knob * m_startKnob;
	knob * m_endKnob;
	f_cnt_t m_startFrameX;
	f_cnt_t m_endFrameX;
	bool m_isDragging;
	QPoint m_draggingLastPoint;
	draggingType m_draggingType;
	bool m_reversed;
	f_cnt_t m_framesPlayed;
	bool m_animation;

public:
	AudioFileProcessorWaveView( QWidget * _parent, int _w, int _h, sampleBuffer & _buf );
	void setKnobs( knob * _start, knob * _end );


private:
	void zoom( const bool _out = false );
	void slide( int _px );
	void slideSamplePointByPx( knobType _point, int _px );
	void slideSamplePointByFrames( knobType _point, f_cnt_t _frames, bool _slide_to = false );
	void slideSampleByFrames( f_cnt_t _frames );

	void slideSamplePointToFrames( knobType _point, f_cnt_t _frames )
	{
		slideSamplePointByFrames( _point, _frames, true );
	}

	void updateGraph();
	void reverse();

	static bool isCloseTo( int _a, int _b )
	{
		return qAbs( _a - _b ) < 3;
	}

} ;




#endif
