#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: PAL-B/G Enhanced Decoder
# Description: Enhanced PAL-B/G Video Decoder for HackRF
# GNU Radio version: v3.9.2.0-85-g08bb05c1

from distutils.version import StrictVersion

if __name__ == '__main__':
    import ctypes
    import sys
    if sys.platform.startswith('linux'):
        try:
            x11 = ctypes.cdll.LoadLibrary('libX11.so')
            x11.XInitThreads()
        except:
            print("Warning: failed to XInitThreads()")

from PyQt5 import Qt
from PyQt5.QtCore import QObject, pyqtSlot
from gnuradio import qtgui
from gnuradio.filter import firdes
import sip
from gnuradio import analog
import math
from gnuradio import audio
from gnuradio import blocks
from gnuradio import filter
from gnuradio import gr
from gnuradio.fft import window
import sys
import signal
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
from gnuradio.qtgui import Range, RangeWidget
from PyQt5 import QtCore
import osmosdr
import time



from gnuradio import qtgui

class palb_decoder(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "PAL-B/G Enhanced Decoder", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("PAL-B/G Enhanced Decoder")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except:
            pass
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("GNU Radio", "palb_decoder")

        try:
            if StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
                self.restoreGeometry(self.settings.value("geometry").toByteArray())
            else:
                self.restoreGeometry(self.settings.value("geometry"))
        except:
            pass

        ##################################################
        # Variables
        ##################################################
        self.video_samp_rate = video_samp_rate = 6e6
        self.sound_carrier = sound_carrier = 5.5e6
        self.samp_rate = samp_rate = 16e6
        self.rf_gain = rf_gain = 20
        self.if_gain = if_gain = 20
        self.freq = freq = 486e6
        self.demod_mode = demod_mode = 0
        self.color_carrier = color_carrier = 4.43361875e6
        self.bb_gain = bb_gain = 20
        self.audio_volume = audio_volume = 1
        self.audio_samp_rate = audio_samp_rate = 48e3

        ##################################################
        # Blocks
        ##################################################
        self._rf_gain_range = Range(0, 40, 1, 20, 200)
        self._rf_gain_win = RangeWidget(self._rf_gain_range, self.set_rf_gain, 'RF Gain', "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._rf_gain_win, 0, 0, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._if_gain_range = Range(0, 40, 1, 20, 200)
        self._if_gain_win = RangeWidget(self._if_gain_range, self.set_if_gain, 'IF Gain', "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._if_gain_win, 0, 1, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        # Create the options list
        self._demod_mode_options = [0, 1]
        # Create the labels list
        self._demod_mode_labels = ['AM (VSB)', 'FM']
        # Create the combo box
        # Create the radio buttons
        self._demod_mode_group_box = Qt.QGroupBox('Video Demod' + ": ")
        self._demod_mode_box = Qt.QVBoxLayout()
        class variable_chooser_button_group(Qt.QButtonGroup):
            def __init__(self, parent=None):
                Qt.QButtonGroup.__init__(self, parent)
            @pyqtSlot(int)
            def updateButtonChecked(self, button_id):
                self.button(button_id).setChecked(True)
        self._demod_mode_button_group = variable_chooser_button_group()
        self._demod_mode_group_box.setLayout(self._demod_mode_box)
        for i, _label in enumerate(self._demod_mode_labels):
            radio_button = Qt.QRadioButton(_label)
            self._demod_mode_box.addWidget(radio_button)
            self._demod_mode_button_group.addButton(radio_button, i)
        self._demod_mode_callback = lambda i: Qt.QMetaObject.invokeMethod(self._demod_mode_button_group, "updateButtonChecked", Qt.Q_ARG("int", self._demod_mode_options.index(i)))
        self._demod_mode_callback(self.demod_mode)
        self._demod_mode_button_group.buttonClicked[int].connect(
            lambda i: self.set_demod_mode(self._demod_mode_options[i]))
        self.top_grid_layout.addWidget(self._demod_mode_group_box, 0, 3, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(3, 4):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._bb_gain_range = Range(0, 40, 1, 20, 200)
        self._bb_gain_win = RangeWidget(self._bb_gain_range, self.set_bb_gain, 'BB Gain', "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._bb_gain_win, 0, 2, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._audio_volume_range = Range(0, 10, 0.1, 1, 200)
        self._audio_volume_win = RangeWidget(self._audio_volume_range, self.set_audio_volume, 'Audio Volume', "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._audio_volume_win, 0, 4, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(4, 5):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.rational_resampler_video = filter.rational_resampler_fff(
                interpolation=1,
                decimation=int(samp_rate/video_samp_rate),
                taps=[],
                fractional_bw=0)
        self.rational_resampler_audio = filter.rational_resampler_fff(
                interpolation=1,
                decimation=int(samp_rate/audio_samp_rate),
                taps=[],
                fractional_bw=0)
        self.qtgui_time_sink_video = qtgui.time_sink_f(
            int(video_samp_rate/15625*2), #size
            video_samp_rate, #samp_rate
            "Demodulated Video", #name
            1, #number of inputs
            None # parent
        )
        self.qtgui_time_sink_video.set_update_time(0.05)
        self.qtgui_time_sink_video.set_y_axis(-0.5, 1.5)

        self.qtgui_time_sink_video.set_y_label('Amplitude', "")

        self.qtgui_time_sink_video.enable_tags(True)
        self.qtgui_time_sink_video.set_trigger_mode(qtgui.TRIG_MODE_FREE, qtgui.TRIG_SLOPE_POS, 0.0, 0, 0, "")
        self.qtgui_time_sink_video.enable_autoscale(True)
        self.qtgui_time_sink_video.enable_grid(True)
        self.qtgui_time_sink_video.enable_axis_labels(True)
        self.qtgui_time_sink_video.enable_control_panel(False)
        self.qtgui_time_sink_video.enable_stem_plot(False)


        labels = ['Video', 'Signal 2', 'Signal 3', 'Signal 4', 'Signal 5',
            'Signal 6', 'Signal 7', 'Signal 8', 'Signal 9', 'Signal 10']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ['blue', 'red', 'green', 'black', 'cyan',
            'magenta', 'yellow', 'dark red', 'dark green', 'dark blue']
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]
        styles = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        markers = [-1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1]


        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_time_sink_video.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_time_sink_video.set_line_label(i, labels[i])
            self.qtgui_time_sink_video.set_line_width(i, widths[i])
            self.qtgui_time_sink_video.set_line_color(i, colors[i])
            self.qtgui_time_sink_video.set_line_style(i, styles[i])
            self.qtgui_time_sink_video.set_line_marker(i, markers[i])
            self.qtgui_time_sink_video.set_line_alpha(i, alphas[i])

        self._qtgui_time_sink_video_win = sip.wrapinstance(self.qtgui_time_sink_video.pyqwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_time_sink_video_win, 1, 2, 2, 2)
        for r in range(1, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 4):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_time_sink_color = qtgui.time_sink_f(
            1024, #size
            samp_rate, #samp_rate
            "Color Subcarrier (4.43 MHz)", #name
            1, #number of inputs
            None # parent
        )
        self.qtgui_time_sink_color.set_update_time(0.10)
        self.qtgui_time_sink_color.set_y_axis(0, 1)

        self.qtgui_time_sink_color.set_y_label('Amplitude', "")

        self.qtgui_time_sink_color.enable_tags(True)
        self.qtgui_time_sink_color.set_trigger_mode(qtgui.TRIG_MODE_FREE, qtgui.TRIG_SLOPE_POS, 0.0, 0, 0, "")
        self.qtgui_time_sink_color.enable_autoscale(True)
        self.qtgui_time_sink_color.enable_grid(True)
        self.qtgui_time_sink_color.enable_axis_labels(True)
        self.qtgui_time_sink_color.enable_control_panel(False)
        self.qtgui_time_sink_color.enable_stem_plot(False)


        labels = ['Color @ 4.43MHz', 'Signal 2', 'Signal 3', 'Signal 4', 'Signal 5',
            'Signal 6', 'Signal 7', 'Signal 8', 'Signal 9', 'Signal 10']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ['magenta', 'red', 'green', 'black', 'cyan',
            'magenta', 'yellow', 'dark red', 'dark green', 'dark blue']
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]
        styles = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        markers = [-1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1]


        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_time_sink_color.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_time_sink_color.set_line_label(i, labels[i])
            self.qtgui_time_sink_color.set_line_width(i, widths[i])
            self.qtgui_time_sink_color.set_line_color(i, colors[i])
            self.qtgui_time_sink_color.set_line_style(i, styles[i])
            self.qtgui_time_sink_color.set_line_marker(i, markers[i])
            self.qtgui_time_sink_color.set_line_alpha(i, alphas[i])

        self._qtgui_time_sink_color_win = sip.wrapinstance(self.qtgui_time_sink_color.pyqwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_time_sink_color_win, 3, 2, 1, 2)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 4):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_freq_sink_rf = qtgui.freq_sink_c(
            2048, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            0, #fc
            samp_rate, #bw
            "RF Spectrum", #name
            1,
            None # parent
        )
        self.qtgui_freq_sink_rf.set_update_time(0.10)
        self.qtgui_freq_sink_rf.set_y_axis(-100, 0)
        self.qtgui_freq_sink_rf.set_y_label('Relative Gain', 'dB')
        self.qtgui_freq_sink_rf.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.qtgui_freq_sink_rf.enable_autoscale(False)
        self.qtgui_freq_sink_rf.enable_grid(True)
        self.qtgui_freq_sink_rf.set_fft_average(0.2)
        self.qtgui_freq_sink_rf.enable_axis_labels(True)
        self.qtgui_freq_sink_rf.enable_control_panel(False)
        self.qtgui_freq_sink_rf.set_fft_window_normalized(False)



        labels = ['RF Spectrum', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_freq_sink_rf.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_freq_sink_rf.set_line_label(i, labels[i])
            self.qtgui_freq_sink_rf.set_line_width(i, widths[i])
            self.qtgui_freq_sink_rf.set_line_color(i, colors[i])
            self.qtgui_freq_sink_rf.set_line_alpha(i, alphas[i])

        self._qtgui_freq_sink_rf_win = sip.wrapinstance(self.qtgui_freq_sink_rf.pyqwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_freq_sink_rf_win, 1, 0, 2, 2)
        for r in range(1, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_freq_sink_audio = qtgui.freq_sink_f(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            0, #fc
            audio_samp_rate, #bw
            "Audio Spectrum (5.5 MHz)", #name
            1,
            None # parent
        )
        self.qtgui_freq_sink_audio.set_update_time(0.10)
        self.qtgui_freq_sink_audio.set_y_axis(-80, 0)
        self.qtgui_freq_sink_audio.set_y_label('Relative Gain', 'dB')
        self.qtgui_freq_sink_audio.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.qtgui_freq_sink_audio.enable_autoscale(False)
        self.qtgui_freq_sink_audio.enable_grid(True)
        self.qtgui_freq_sink_audio.set_fft_average(0.2)
        self.qtgui_freq_sink_audio.enable_axis_labels(True)
        self.qtgui_freq_sink_audio.enable_control_panel(False)
        self.qtgui_freq_sink_audio.set_fft_window_normalized(False)


        self.qtgui_freq_sink_audio.set_plot_pos_half(not True)

        labels = ['Audio', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["green", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_freq_sink_audio.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_freq_sink_audio.set_line_label(i, labels[i])
            self.qtgui_freq_sink_audio.set_line_width(i, widths[i])
            self.qtgui_freq_sink_audio.set_line_color(i, colors[i])
            self.qtgui_freq_sink_audio.set_line_alpha(i, alphas[i])

        self._qtgui_freq_sink_audio_win = sip.wrapinstance(self.qtgui_freq_sink_audio.pyqwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_freq_sink_audio_win, 3, 0, 1, 2)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.osmosdr_source_0 = osmosdr.source(
            args="numchan=" + str(1) + " " + 'hackrf=0'
        )
        self.osmosdr_source_0.set_time_unknown_pps(osmosdr.time_spec_t())
        self.osmosdr_source_0.set_sample_rate(samp_rate)
        self.osmosdr_source_0.set_center_freq(freq, 0)
        self.osmosdr_source_0.set_freq_corr(0, 0)
        self.osmosdr_source_0.set_dc_offset_mode(0, 0)
        self.osmosdr_source_0.set_iq_balance_mode(0, 0)
        self.osmosdr_source_0.set_gain_mode(False, 0)
        self.osmosdr_source_0.set_gain(rf_gain, 0)
        self.osmosdr_source_0.set_if_gain(if_gain, 0)
        self.osmosdr_source_0.set_bb_gain(bb_gain, 0)
        self.osmosdr_source_0.set_antenna('', 0)
        self.osmosdr_source_0.set_bandwidth(0, 0)
        self.low_pass_filter_video = filter.fir_filter_ccf(
            1,
            firdes.low_pass(
                1,
                samp_rate,
                5e6,
                0.5e6,
                window.WIN_HAMMING,
                6.76))
        self.freq_xlating_fir_filter_audio = filter.freq_xlating_fir_filter_ccc(1, firdes.low_pass(1, samp_rate, 100e3, 50e3), sound_carrier, samp_rate)
        self.dc_blocker_xx_0 = filter.dc_blocker_ff(1024, True)
        self.blocks_selector_0 = blocks.selector(gr.sizeof_float*1,demod_mode,0)
        self.blocks_selector_0.set_enabled(True)
        self.blocks_multiply_const_audio = blocks.multiply_const_ff(audio_volume)
        self.blocks_complex_to_mag_color = blocks.complex_to_mag(1)
        self.blocks_complex_to_mag_0 = blocks.complex_to_mag(1)
        self.band_pass_filter_color = filter.fir_filter_ccf(
            1,
            firdes.band_pass(
                1,
                samp_rate,
                color_carrier-500e3,
                color_carrier+500e3,
                200e3,
                window.WIN_HAMMING,
                6.76))
        self.band_pass_filter_audio = filter.fir_filter_ccf(
            1,
            firdes.band_pass(
                1,
                samp_rate,
                sound_carrier-100e3,
                sound_carrier+100e3,
                50e3,
                window.WIN_HAMMING,
                6.76))
        self.audio_sink_0 = audio.sink(int(audio_samp_rate), '', True)
        self.analog_quadrature_demod_cf_0 = analog.quadrature_demod_cf(samp_rate/(2*math.pi*5e6))
        self.analog_quadrature_demod_audio = analog.quadrature_demod_cf(samp_rate/(2*math.pi*50e3))



        ##################################################
        # Connections
        ##################################################
        self.connect((self.analog_quadrature_demod_audio, 0), (self.rational_resampler_audio, 0))
        self.connect((self.analog_quadrature_demod_cf_0, 0), (self.blocks_selector_0, 1))
        self.connect((self.band_pass_filter_audio, 0), (self.freq_xlating_fir_filter_audio, 0))
        self.connect((self.band_pass_filter_color, 0), (self.blocks_complex_to_mag_color, 0))
        self.connect((self.blocks_complex_to_mag_0, 0), (self.blocks_selector_0, 0))
        self.connect((self.blocks_complex_to_mag_color, 0), (self.qtgui_time_sink_color, 0))
        self.connect((self.blocks_multiply_const_audio, 0), (self.audio_sink_0, 0))
        self.connect((self.blocks_multiply_const_audio, 0), (self.qtgui_freq_sink_audio, 0))
        self.connect((self.blocks_selector_0, 0), (self.dc_blocker_xx_0, 0))
        self.connect((self.dc_blocker_xx_0, 0), (self.rational_resampler_video, 0))
        self.connect((self.freq_xlating_fir_filter_audio, 0), (self.analog_quadrature_demod_audio, 0))
        self.connect((self.low_pass_filter_video, 0), (self.analog_quadrature_demod_cf_0, 0))
        self.connect((self.low_pass_filter_video, 0), (self.blocks_complex_to_mag_0, 0))
        self.connect((self.osmosdr_source_0, 0), (self.band_pass_filter_audio, 0))
        self.connect((self.osmosdr_source_0, 0), (self.band_pass_filter_color, 0))
        self.connect((self.osmosdr_source_0, 0), (self.low_pass_filter_video, 0))
        self.connect((self.osmosdr_source_0, 0), (self.qtgui_freq_sink_rf, 0))
        self.connect((self.rational_resampler_audio, 0), (self.blocks_multiply_const_audio, 0))
        self.connect((self.rational_resampler_video, 0), (self.qtgui_time_sink_video, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("GNU Radio", "palb_decoder")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_video_samp_rate(self):
        return self.video_samp_rate

    def set_video_samp_rate(self, video_samp_rate):
        self.video_samp_rate = video_samp_rate
        self.qtgui_time_sink_video.set_samp_rate(self.video_samp_rate)

    def get_sound_carrier(self):
        return self.sound_carrier

    def set_sound_carrier(self, sound_carrier):
        self.sound_carrier = sound_carrier
        self.band_pass_filter_audio.set_taps(firdes.band_pass(1, self.samp_rate, self.sound_carrier-100e3, self.sound_carrier+100e3, 50e3, window.WIN_HAMMING, 6.76))
        self.freq_xlating_fir_filter_audio.set_center_freq(self.sound_carrier)

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.analog_quadrature_demod_audio.set_gain(self.samp_rate/(2*math.pi*50e3))
        self.analog_quadrature_demod_cf_0.set_gain(self.samp_rate/(2*math.pi*5e6))
        self.band_pass_filter_audio.set_taps(firdes.band_pass(1, self.samp_rate, self.sound_carrier-100e3, self.sound_carrier+100e3, 50e3, window.WIN_HAMMING, 6.76))
        self.band_pass_filter_color.set_taps(firdes.band_pass(1, self.samp_rate, self.color_carrier-500e3, self.color_carrier+500e3, 200e3, window.WIN_HAMMING, 6.76))
        self.freq_xlating_fir_filter_audio.set_taps(firdes.low_pass(1, self.samp_rate, 100e3, 50e3))
        self.low_pass_filter_video.set_taps(firdes.low_pass(1, self.samp_rate, 5e6, 0.5e6, window.WIN_HAMMING, 6.76))
        self.osmosdr_source_0.set_sample_rate(self.samp_rate)
        self.qtgui_freq_sink_rf.set_frequency_range(0, self.samp_rate)
        self.qtgui_time_sink_color.set_samp_rate(self.samp_rate)

    def get_rf_gain(self):
        return self.rf_gain

    def set_rf_gain(self, rf_gain):
        self.rf_gain = rf_gain
        self.osmosdr_source_0.set_gain(self.rf_gain, 0)

    def get_if_gain(self):
        return self.if_gain

    def set_if_gain(self, if_gain):
        self.if_gain = if_gain
        self.osmosdr_source_0.set_if_gain(self.if_gain, 0)

    def get_freq(self):
        return self.freq

    def set_freq(self, freq):
        self.freq = freq
        self.osmosdr_source_0.set_center_freq(self.freq, 0)

    def get_demod_mode(self):
        return self.demod_mode

    def set_demod_mode(self, demod_mode):
        self.demod_mode = demod_mode
        self._demod_mode_callback(self.demod_mode)
        self.blocks_selector_0.set_input_index(self.demod_mode)

    def get_color_carrier(self):
        return self.color_carrier

    def set_color_carrier(self, color_carrier):
        self.color_carrier = color_carrier
        self.band_pass_filter_color.set_taps(firdes.band_pass(1, self.samp_rate, self.color_carrier-500e3, self.color_carrier+500e3, 200e3, window.WIN_HAMMING, 6.76))

    def get_bb_gain(self):
        return self.bb_gain

    def set_bb_gain(self, bb_gain):
        self.bb_gain = bb_gain
        self.osmosdr_source_0.set_bb_gain(self.bb_gain, 0)

    def get_audio_volume(self):
        return self.audio_volume

    def set_audio_volume(self, audio_volume):
        self.audio_volume = audio_volume
        self.blocks_multiply_const_audio.set_k(self.audio_volume)

    def get_audio_samp_rate(self):
        return self.audio_samp_rate

    def set_audio_samp_rate(self, audio_samp_rate):
        self.audio_samp_rate = audio_samp_rate
        self.qtgui_freq_sink_audio.set_frequency_range(0, self.audio_samp_rate)




def main(top_block_cls=palb_decoder, options=None):

    if StrictVersion("4.5.0") <= StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
        style = gr.prefs().get_string('qtgui', 'style', 'raster')
        Qt.QApplication.setGraphicsSystem(style)
    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()

    tb.show()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    qapp.exec_()

if __name__ == '__main__':
    main()
