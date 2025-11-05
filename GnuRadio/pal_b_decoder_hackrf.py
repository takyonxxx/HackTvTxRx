#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: PAL-B/G TV Decoder
# Author: Maren Robotics
# Description: PAL-B/G TV Decoder with HackRF
# GNU Radio version: 3.10.12.0

from PyQt5 import Qt
from gnuradio import qtgui
from PyQt5 import QtCore
from PyQt5.QtCore import QObject, pyqtSlot
from gnuradio import analog
import math
from gnuradio import audio
from gnuradio import blocks
from gnuradio import filter
from gnuradio.filter import firdes
from gnuradio import gr
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
import osmosdr
import time
import sip
import threading



class pal_b_decoder_hackrf(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "PAL-B/G TV Decoder", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("PAL-B/G TV Decoder")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except BaseException as exc:
            print(f"Qt GUI: Could not set Icon: {str(exc)}", file=sys.stderr)
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

        self.settings = Qt.QSettings("gnuradio/flowgraphs", "pal_b_decoder_hackrf")

        try:
            geometry = self.settings.value("geometry")
            if geometry:
                self.restoreGeometry(geometry)
        except BaseException as exc:
            print(f"Qt GUI: Could not restore geometry: {str(exc)}", file=sys.stderr)
        self.flowgraph_started = threading.Event()

        ##################################################
        # Variables
        ##################################################
        self.video_samp_rate = video_samp_rate = 6e6
        self.video_offset = video_offset = 0.0
        self.video_gain = video_gain = 1.5
        self.vga_gain = vga_gain = 20
        self.samp_rate = samp_rate = 16e6
        self.lna_gain = lna_gain = 40
        self.demod_mode = demod_mode = 0
        self.center_freq = center_freq = 478e6
        self.audio_rate = audio_rate = 48000
        self.audio_gain = audio_gain = 1.0

        ##################################################
        # Blocks
        ##################################################

        self._video_offset_range = qtgui.Range(-1.0, 1.0, 0.01, 0.0, 200)
        self._video_offset_win = qtgui.RangeWidget(self._video_offset_range, self.set_video_offset, "Video Offset", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._video_offset_win, 0, 1, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._video_gain_range = qtgui.Range(0.1, 10.0, 0.1, 1.5, 200)
        self._video_gain_win = qtgui.RangeWidget(self._video_gain_range, self.set_video_gain, "Video Gain", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._video_gain_win, 0, 0, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._vga_gain_range = qtgui.Range(0, 62, 2, 20, 200)
        self._vga_gain_win = qtgui.RangeWidget(self._vga_gain_range, self.set_vga_gain, "VGA Gain", "counter_slider", int, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._vga_gain_win, 1, 1, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(1, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._lna_gain_range = qtgui.Range(0, 40, 8, 40, 200)
        self._lna_gain_win = qtgui.RangeWidget(self._lna_gain_range, self.set_lna_gain, "LNA Gain", "counter_slider", int, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._lna_gain_win, 1, 0, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 1):
            self.top_grid_layout.setColumnStretch(c, 1)
        # Create the options list
        self._demod_mode_options = [0, 1]
        # Create the labels list
        self._demod_mode_labels = ['AM', 'FM']
        # Create the combo box
        # Create the radio buttons
        self._demod_mode_group_box = Qt.QGroupBox("Demod Mode" + ": ")
        self._demod_mode_box = Qt.QHBoxLayout()
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
        self.top_grid_layout.addWidget(self._demod_mode_group_box, 0, 2, 1, 1)
        for r in range(0, 1):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._center_freq_range = qtgui.Range(88e6, 900e6, 100, 478e6, 400)
        self._center_freq_win = qtgui.RangeWidget(self._center_freq_range, self.set_center_freq, "Center Frequency (Hz)", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._center_freq_win, 2, 0, 1, 2)
        for r in range(2, 3):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self._audio_gain_range = qtgui.Range(0.0, 10.0, 0.1, 1.0, 200)
        self._audio_gain_win = qtgui.RangeWidget(self._audio_gain_range, self.set_audio_gain, "Audio Gain", "counter_slider", float, QtCore.Qt.Horizontal)
        self.top_grid_layout.addWidget(self._audio_gain_win, 1, 2, 1, 1)
        for r in range(1, 2):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(2, 3):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.rational_resampler_xxx_1 = filter.rational_resampler_fff(
                interpolation=3,
                decimation=1000,
                taps=[],
                fractional_bw=0)
        self.rational_resampler_xxx_0 = filter.rational_resampler_fff(
                interpolation=1,
                decimation=3,
                taps=[],
                fractional_bw=0)
        self.qtgui_waterfall_sink_x_0 = qtgui.waterfall_sink_c(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            center_freq, #fc
            samp_rate, #bw
            "Waterfall", #name
            1, #number of inputs
            None # parent
        )
        self.qtgui_waterfall_sink_x_0.set_update_time(0.10)
        self.qtgui_waterfall_sink_x_0.enable_grid(False)
        self.qtgui_waterfall_sink_x_0.enable_axis_labels(True)



        labels = ['', '', '', '', '',
                  '', '', '', '', '']
        colors = [0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
                  1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_waterfall_sink_x_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_waterfall_sink_x_0.set_line_label(i, labels[i])
            self.qtgui_waterfall_sink_x_0.set_color_map(i, colors[i])
            self.qtgui_waterfall_sink_x_0.set_line_alpha(i, alphas[i])

        self.qtgui_waterfall_sink_x_0.set_intensity_range(-140, 10)

        self._qtgui_waterfall_sink_x_0_win = sip.wrapinstance(self.qtgui_waterfall_sink_x_0.qwidget(), Qt.QWidget)

        self.top_grid_layout.addWidget(self._qtgui_waterfall_sink_x_0_win, 4, 0, 2, 2)
        for r in range(4, 6):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_time_sink_x_0 = qtgui.time_sink_f(
            1024, #size
            video_samp_rate, #samp_rate
            "Video Signal", #name
            1, #number of inputs
            None # parent
        )
        self.qtgui_time_sink_x_0.set_update_time(0.10)
        self.qtgui_time_sink_x_0.set_y_axis(-1, 1)

        self.qtgui_time_sink_x_0.set_y_label('Amplitude', "")

        self.qtgui_time_sink_x_0.enable_tags(True)
        self.qtgui_time_sink_x_0.set_trigger_mode(qtgui.TRIG_MODE_FREE, qtgui.TRIG_SLOPE_POS, 0.0, 0, 0, "")
        self.qtgui_time_sink_x_0.enable_autoscale(False)
        self.qtgui_time_sink_x_0.enable_grid(False)
        self.qtgui_time_sink_x_0.enable_axis_labels(True)
        self.qtgui_time_sink_x_0.enable_control_panel(False)
        self.qtgui_time_sink_x_0.enable_stem_plot(False)


        labels = ['Video', '', '', '', '',
            '', '', '', '', '']
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
                self.qtgui_time_sink_x_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_time_sink_x_0.set_line_label(i, labels[i])
            self.qtgui_time_sink_x_0.set_line_width(i, widths[i])
            self.qtgui_time_sink_x_0.set_line_color(i, colors[i])
            self.qtgui_time_sink_x_0.set_line_style(i, styles[i])
            self.qtgui_time_sink_x_0.set_line_marker(i, markers[i])
            self.qtgui_time_sink_x_0.set_line_alpha(i, alphas[i])

        self._qtgui_time_sink_x_0_win = sip.wrapinstance(self.qtgui_time_sink_x_0.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_time_sink_x_0_win, 6, 0, 1, 2)
        for r in range(6, 7):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.qtgui_freq_sink_x_0 = qtgui.freq_sink_c(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            center_freq, #fc
            samp_rate, #bw
            "Frequency Spectrum", #name
            1,
            None # parent
        )
        self.qtgui_freq_sink_x_0.set_update_time(0.10)
        self.qtgui_freq_sink_x_0.set_y_axis((-140), 10)
        self.qtgui_freq_sink_x_0.set_y_label('Relative Gain', 'dB')
        self.qtgui_freq_sink_x_0.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.qtgui_freq_sink_x_0.enable_autoscale(False)
        self.qtgui_freq_sink_x_0.enable_grid(False)
        self.qtgui_freq_sink_x_0.set_fft_average(1.0)
        self.qtgui_freq_sink_x_0.enable_axis_labels(True)
        self.qtgui_freq_sink_x_0.enable_control_panel(True)
        self.qtgui_freq_sink_x_0.set_fft_window_normalized(False)



        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_freq_sink_x_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_freq_sink_x_0.set_line_label(i, labels[i])
            self.qtgui_freq_sink_x_0.set_line_width(i, widths[i])
            self.qtgui_freq_sink_x_0.set_line_color(i, colors[i])
            self.qtgui_freq_sink_x_0.set_line_alpha(i, alphas[i])

        self._qtgui_freq_sink_x_0_win = sip.wrapinstance(self.qtgui_freq_sink_x_0.qwidget(), Qt.QWidget)
        self.top_grid_layout.addWidget(self._qtgui_freq_sink_x_0_win, 3, 0, 1, 2)
        for r in range(3, 4):
            self.top_grid_layout.setRowStretch(r, 1)
        for c in range(0, 2):
            self.top_grid_layout.setColumnStretch(c, 1)
        self.osmosdr_source_0 = osmosdr.source(
            args="numchan=" + str(1) + " " + 'hackrf=0'
        )
        self.osmosdr_source_0.set_time_unknown_pps(osmosdr.time_spec_t())
        self.osmosdr_source_0.set_sample_rate(samp_rate)
        self.osmosdr_source_0.set_center_freq(center_freq, 0)
        self.osmosdr_source_0.set_freq_corr(0, 0)
        self.osmosdr_source_0.set_dc_offset_mode(0, 0)
        self.osmosdr_source_0.set_iq_balance_mode(0, 0)
        self.osmosdr_source_0.set_gain_mode(False, 0)
        self.osmosdr_source_0.set_gain(lna_gain, 0)
        self.osmosdr_source_0.set_if_gain(20, 0)
        self.osmosdr_source_0.set_bb_gain(vga_gain, 0)
        self.osmosdr_source_0.set_antenna('', 0)
        self.osmosdr_source_0.set_bandwidth(0, 0)
        self.low_pass_filter_2 = filter.fir_filter_fff(
            1,
            firdes.low_pass(
                1,
                samp_rate,
                15000,
                1000,
                window.WIN_HAMMING,
                6.76))
        self.low_pass_filter_1 = filter.fir_filter_fff(
            1,
            firdes.low_pass(
                1,
                video_samp_rate,
                3e6,
                500e3,
                window.WIN_HAMMING,
                6.76))
        self.low_pass_filter_0 = filter.fir_filter_fff(
            1,
            firdes.low_pass(
                1,
                samp_rate,
                5e6,
                1e6,
                window.WIN_HAMMING,
                6.76))
        self.dc_blocker_xx_0 = filter.dc_blocker_ff(32, True)
        self.blocks_selector_0 = blocks.selector(gr.sizeof_float*1,demod_mode,0)
        self.blocks_selector_0.set_enabled(True)
        self.blocks_multiply_const_vxx_1 = blocks.multiply_const_ff(audio_gain)
        self.blocks_multiply_const_vxx_0 = blocks.multiply_const_ff(video_gain)
        self.blocks_complex_to_mag_0 = blocks.complex_to_mag(1)
        self.blocks_add_const_vxx_0 = blocks.add_const_ff(video_offset)
        self.audio_sink_0 = audio.sink(audio_rate, '', True)
        self.analog_quadrature_demod_cf_1 = analog.quadrature_demod_cf((samp_rate/(2*3.14159*5e3)))
        self.analog_quadrature_demod_cf_0 = analog.quadrature_demod_cf((samp_rate/(2*3.14159*75e3)))
        self.analog_agc_xx_0 = analog.agc_ff((1e-4), 1.0, 1.0, 65536)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.analog_agc_xx_0, 0), (self.blocks_multiply_const_vxx_0, 0))
        self.connect((self.analog_quadrature_demod_cf_0, 0), (self.blocks_selector_0, 1))
        self.connect((self.analog_quadrature_demod_cf_1, 0), (self.low_pass_filter_2, 0))
        self.connect((self.blocks_add_const_vxx_0, 0), (self.qtgui_time_sink_x_0, 0))
        self.connect((self.blocks_complex_to_mag_0, 0), (self.blocks_selector_0, 0))
        self.connect((self.blocks_multiply_const_vxx_0, 0), (self.blocks_add_const_vxx_0, 0))
        self.connect((self.blocks_multiply_const_vxx_1, 0), (self.audio_sink_0, 0))
        self.connect((self.blocks_selector_0, 0), (self.low_pass_filter_0, 0))
        self.connect((self.dc_blocker_xx_0, 0), (self.rational_resampler_xxx_0, 0))
        self.connect((self.low_pass_filter_0, 0), (self.dc_blocker_xx_0, 0))
        self.connect((self.low_pass_filter_1, 0), (self.analog_agc_xx_0, 0))
        self.connect((self.low_pass_filter_2, 0), (self.rational_resampler_xxx_1, 0))
        self.connect((self.osmosdr_source_0, 0), (self.analog_quadrature_demod_cf_0, 0))
        self.connect((self.osmosdr_source_0, 0), (self.analog_quadrature_demod_cf_1, 0))
        self.connect((self.osmosdr_source_0, 0), (self.blocks_complex_to_mag_0, 0))
        self.connect((self.osmosdr_source_0, 0), (self.qtgui_freq_sink_x_0, 0))
        self.connect((self.osmosdr_source_0, 0), (self.qtgui_waterfall_sink_x_0, 0))
        self.connect((self.rational_resampler_xxx_0, 0), (self.low_pass_filter_1, 0))
        self.connect((self.rational_resampler_xxx_1, 0), (self.blocks_multiply_const_vxx_1, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("gnuradio/flowgraphs", "pal_b_decoder_hackrf")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_video_samp_rate(self):
        return self.video_samp_rate

    def set_video_samp_rate(self, video_samp_rate):
        self.video_samp_rate = video_samp_rate
        self.low_pass_filter_1.set_taps(firdes.low_pass(1, self.video_samp_rate, 3e6, 500e3, window.WIN_HAMMING, 6.76))
        self.qtgui_time_sink_x_0.set_samp_rate(self.video_samp_rate)

    def get_video_offset(self):
        return self.video_offset

    def set_video_offset(self, video_offset):
        self.video_offset = video_offset
        self.blocks_add_const_vxx_0.set_k(self.video_offset)

    def get_video_gain(self):
        return self.video_gain

    def set_video_gain(self, video_gain):
        self.video_gain = video_gain
        self.blocks_multiply_const_vxx_0.set_k(self.video_gain)

    def get_vga_gain(self):
        return self.vga_gain

    def set_vga_gain(self, vga_gain):
        self.vga_gain = vga_gain
        self.osmosdr_source_0.set_bb_gain(self.vga_gain, 0)

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.analog_quadrature_demod_cf_0.set_gain((self.samp_rate/(2*3.14159*75e3)))
        self.analog_quadrature_demod_cf_1.set_gain((self.samp_rate/(2*3.14159*5e3)))
        self.low_pass_filter_0.set_taps(firdes.low_pass(1, self.samp_rate, 5e6, 1e6, window.WIN_HAMMING, 6.76))
        self.low_pass_filter_2.set_taps(firdes.low_pass(1, self.samp_rate, 15000, 1000, window.WIN_HAMMING, 6.76))
        self.osmosdr_source_0.set_sample_rate(self.samp_rate)
        self.qtgui_freq_sink_x_0.set_frequency_range(self.center_freq, self.samp_rate)
        self.qtgui_waterfall_sink_x_0.set_frequency_range(self.center_freq, self.samp_rate)

    def get_lna_gain(self):
        return self.lna_gain

    def set_lna_gain(self, lna_gain):
        self.lna_gain = lna_gain
        self.osmosdr_source_0.set_gain(self.lna_gain, 0)

    def get_demod_mode(self):
        return self.demod_mode

    def set_demod_mode(self, demod_mode):
        self.demod_mode = demod_mode
        self._demod_mode_callback(self.demod_mode)
        self.blocks_selector_0.set_input_index(self.demod_mode)

    def get_center_freq(self):
        return self.center_freq

    def set_center_freq(self, center_freq):
        self.center_freq = center_freq
        self.osmosdr_source_0.set_center_freq(self.center_freq, 0)
        self.qtgui_freq_sink_x_0.set_frequency_range(self.center_freq, self.samp_rate)
        self.qtgui_waterfall_sink_x_0.set_frequency_range(self.center_freq, self.samp_rate)

    def get_audio_rate(self):
        return self.audio_rate

    def set_audio_rate(self, audio_rate):
        self.audio_rate = audio_rate

    def get_audio_gain(self):
        return self.audio_gain

    def set_audio_gain(self, audio_gain):
        self.audio_gain = audio_gain
        self.blocks_multiply_const_vxx_1.set_k(self.audio_gain)




def main(top_block_cls=pal_b_decoder_hackrf, options=None):

    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()
    tb.flowgraph_started.set()

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
