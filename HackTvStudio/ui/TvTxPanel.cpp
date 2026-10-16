#include "TvTxPanel.h"
#include "VideoView.h"
#include "tv/QtAvSource.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QStandardPaths>
#include <QVBoxLayout>

TvTxPanel::TvTxPanel(QWidget* parent)
    : ModePanel(parent)
    , m_enc(new TvTxEncoder(this))
{
    buildUi();

    connect(m_enc->videoSource(), &QtAvSource::previewFrame, this,
            [this](const QImage& img) { m_preview->setFrame(img); });
    connect(m_enc, &TvTxEncoder::sourceError, this,
            [this](const QString& msg) { m_statusLabel->setText("Source error: " + msg); });

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &TvTxPanel::updateStatus);
    m_statusTimer->start(500);
}

void TvTxPanel::buildUi()
{
    //   [ source preview            ] [ TX monitor spectrum (docked by StudioWindow) ]
    //   [ TV standard / video source ] [ options                                      ]
    QGridLayout* grid = new QGridLayout(this);
    grid->setSpacing(8);
    grid->setContentsMargins(8, 8, 8, 8);

    // ---- Preview (top left)
    QGroupBox* prevGroup = new QGroupBox("Source preview", this);
    QVBoxLayout* pl = new QVBoxLayout(prevGroup);
    pl->setContentsMargins(6, 6, 6, 6);
    m_preview = new VideoView(prevGroup);
    m_preview->setWideAspect(false);
    pl->addWidget(m_preview);
    m_statusLabel = new QLabel("Press START to transmit", prevGroup);
    m_statusLabel->setStyleSheet("QLabel { color: #708090; font-size: 8pt; }");
    m_statusLabel->setWordWrap(true);
    pl->addWidget(m_statusLabel);
    grid->addWidget(prevGroup, 0, 0);

    // ---- Spectrum slot (top right, touching the preview)
    QGroupBox* specGroup = new QGroupBox("TX monitor", this);
    m_specSlot = new QVBoxLayout(specGroup);
    m_specSlot->setContentsMargins(6, 6, 6, 6);
    grid->addWidget(specGroup, 0, 1);

    // ---- Bottom left: standard + source
    QWidget* left = new QWidget(this);
    QVBoxLayout* rl = new QVBoxLayout(left);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);
    QWidget* right = left;   // parent for the groups below

    // Standard
    QGroupBox* stdGroup = new QGroupBox("TV standard", right);
    QVBoxLayout* sl = new QVBoxLayout(stdGroup);
    m_modeCombo = new QComboBox(stdGroup);
    for (const auto& m : TvTxEncoder::modes())
        m_modeCombo->addItem(QString("%1  —  %2").arg(QString::fromStdString(m.first).toUpper(), QString::fromStdString(m.second)),
                             QString::fromStdString(m.first));
    m_modeCombo->setCurrentIndex(m_modeCombo->findData("b"));
    sl->addWidget(m_modeCombo);
    QLabel* hint = new QLabel("<i>B/G = Türkiye & Avrupa (PAL, 5.5 MHz ses). Receiver tab decodes PAL-B/G.</i>", stdGroup);
    hint->setStyleSheet("QLabel { color: #708090; font-size: 8pt; }");
    hint->setWordWrap(true);
    sl->addWidget(hint);
    rl->addWidget(stdGroup);

    // Source
    QGroupBox* srcGroup = new QGroupBox("Video source", right);
    QVBoxLayout* vl = new QVBoxLayout(srcGroup);
    m_testRadio = new QRadioButton("Test pattern (colour bars + tone)", srcGroup);
    m_fileRadio = new QRadioButton("Video file", srcGroup);
    m_testRadio->setChecked(true);
    vl->addWidget(m_testRadio);
    vl->addWidget(m_fileRadio);
    QHBoxLayout* fr = new QHBoxLayout();
    m_fileEdit = new QLineEdit(srcGroup);
    m_fileEdit->setPlaceholderText("mp4 / mkv / avi / mov ...");
    m_browseButton = new QPushButton("Browse", srcGroup);
    connect(m_browseButton, &QPushButton::clicked, this, &TvTxPanel::browseFile);
    fr->addWidget(m_fileEdit, 1);
    fr->addWidget(m_browseButton);
    vl->addLayout(fr);
    m_loopCheck = new QCheckBox("Loop", srcGroup);
    m_loopCheck->setChecked(true);
    vl->addWidget(m_loopCheck);
    rl->addWidget(srcGroup);
    rl->addStretch();
    grid->addWidget(left, 1, 0);

    // ---- Bottom right: options (three checkbox columns, two form columns)
    QGroupBox* optGroup = new QGroupBox("Options", this);
    QGridLayout* gl = new QGridLayout(optGroup);
    gl->setHorizontalSpacing(12);
    gl->setVerticalSpacing(4);
    m_filterCheck = new QCheckBox("Output filter (VSB)", optGroup); m_filterCheck->setChecked(true);
    m_colourCheck = new QCheckBox("Colour", optGroup);              m_colourCheck->setChecked(true);
    m_audioCheck  = new QCheckBox("Audio", optGroup);               m_audioCheck->setChecked(true);
    m_a2Check     = new QCheckBox("A2 stereo", optGroup);
    m_nicamCheck  = new QCheckBox("NICAM", optGroup);               m_nicamCheck->setChecked(true);
    m_acpCheck    = new QCheckBox("ACP (Macrovision)", optGroup);
    m_vitsCheck   = new QCheckBox("VITS test lines", optGroup);
    m_vitcCheck   = new QCheckBox("VITC timecode", optGroup);
    m_invertCheck = new QCheckBox("Invert video", optGroup);
    gl->addWidget(m_filterCheck, 0, 0); gl->addWidget(m_colourCheck, 0, 1); gl->addWidget(m_audioCheck,  0, 2, 1, 2);
    gl->addWidget(m_nicamCheck,  1, 0); gl->addWidget(m_a2Check,     1, 1); gl->addWidget(m_acpCheck,    1, 2, 1, 2);
    gl->addWidget(m_vitsCheck,   2, 0); gl->addWidget(m_vitcCheck,   2, 1); gl->addWidget(m_invertCheck, 2, 2, 1, 2);

    gl->addWidget(new QLabel("WSS:", optGroup), 3, 0);
    m_wssCombo = new QComboBox(optGroup);
    m_wssCombo->addItem("Off", "");
    m_wssCombo->addItem("4:3", "4:3");
    m_wssCombo->addItem("16:9", "16:9");
    m_wssCombo->addItem("14:9 letterbox", "14:9-letterbox");
    m_wssCombo->addItem("14:9 top", "14:9-top");
    m_wssCombo->addItem("16:9 letterbox", "16:9-letterbox");
    m_wssCombo->addItem("16:9 top", "16:9-top");
    m_wssCombo->addItem("16:9+ letterbox", "16:9+-letterbox");
    m_wssCombo->addItem("14:9 window", "14:9-window");
    m_wssCombo->addItem("Auto (from source aspect)", "auto");
    gl->addWidget(m_wssCombo, 3, 1);

    gl->addWidget(new QLabel("Scrambling:", optGroup), 3, 2, Qt::AlignRight);
    m_scrambleCombo = new QComboBox(optGroup);
    m_scrambleCombo->addItem("None", "");
    m_scrambleCombo->addItem("Videocrypt I (free access)", "vc1");
    m_scrambleCombo->addItem("Videocrypt II (free access)", "vc2");
    m_scrambleCombo->addItem("Videocrypt S", "vcs");
    m_scrambleCombo->addItem("Nagravision Syster", "syster");
    gl->addWidget(m_scrambleCombo, 3, 3);

    gl->addWidget(new QLabel("Teletext:", optGroup), 4, 0);
    m_teletextEdit = new QLineEdit(optGroup);
    m_teletextEdit->setPlaceholderText("folder of .tti pages (optional)");
    gl->addWidget(m_teletextEdit, 4, 1);

    gl->addWidget(new QLabel("Level:", optGroup), 4, 2, Qt::AlignRight);
    m_levelSpin = new QDoubleSpinBox(optGroup);
    m_levelSpin->setRange(0.1, 1.0);
    m_levelSpin->setSingleStep(0.05);
    m_levelSpin->setValue(1.0);
    gl->addWidget(m_levelSpin, 4, 3);

    QLabel* legal = new QLabel("<i>Transmitting on TV frequencies needs a licence. Test into a dummy load.</i>", optGroup);
    legal->setWordWrap(true);
    legal->setStyleSheet("QLabel { color: #ff8800; font-size: 8pt; }");
    gl->addWidget(legal, 5, 0, 1, 4);
    gl->setRowStretch(6, 1);
    gl->setColumnStretch(1, 1);
    gl->setColumnStretch(3, 1);
    grid->addWidget(optGroup, 1, 1);

    grid->setRowStretch(0, 1);          // preview and spectrum take the height
    grid->setRowStretch(1, 0);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 2);
}

void TvTxPanel::browseFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString f = QFileDialog::getOpenFileName(this, "Video file", dir,
                                                   "Video (*.mp4 *.mkv *.avi *.mov *.webm *.mpg *.ts *.flv);;All files (*)");
    if (f.isEmpty()) return;
    m_fileEdit->setText(f);
    m_fileRadio->setChecked(true);
}

TvTxConfig TvTxPanel::collectConfig() const
{
    TvTxConfig c;
    c.mode     = m_modeCombo->currentData().toString().toStdString();
    c.filter   = m_filterCheck->isChecked();
    c.colour   = m_colourCheck->isChecked();
    c.audio    = m_audioCheck->isChecked();
    c.a2stereo = m_a2Check->isChecked();
    c.nicam    = m_nicamCheck->isChecked();
    c.acp      = m_acpCheck->isChecked();
    c.vits     = m_vitsCheck->isChecked();
    c.vitc     = m_vitcCheck->isChecked();
    c.invert   = m_invertCheck->isChecked();
    c.wss      = m_wssCombo->currentData().toString().toStdString();
    c.scramble = m_scrambleCombo->currentData().toString().toStdString();
    c.teletext = m_teletextEdit->text().trimmed().toStdString();
    c.level    = static_cast<float>(m_levelSpin->value());
    return c;
}

ITxSource* TvTxPanel::txSource()
{
    // Called right before the TX engine starts: hand over the current settings
    m_enc->setConfig(collectConfig());
    m_enc->setSource(m_fileRadio->isChecked() ? TvTxEncoder::Source::VideoFile : TvTxEncoder::Source::TestPattern,
                     m_fileEdit->text(), m_loopCheck->isChecked());
    return m_enc;
}

void TvTxPanel::onDeviceStarted()
{
    m_running = true;
    if (m_testRadio->isChecked()) {
        QImage bars(702, 576, QImage::Format_RGB32);
        const QColor cols[8] = { Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue, Qt::black };
        for (int x = 0; x < bars.width(); ++x) {
            const QColor c = cols[x * 8 / bars.width()];
            for (int y = 0; y < bars.height(); ++y) bars.setPixel(x, y, c.rgb());
        }
        m_preview->setFrame(bars);
    }
}

void TvTxPanel::onDeviceStopped()
{
    m_running = false;
}

void TvTxPanel::updateStatus()
{
    if (!m_running) {
        emit statusText("TV TX idle");
        if (m_enc->lastError().isEmpty())
            m_statusLabel->setText(m_fileRadio->isChecked() && m_fileEdit->text().isEmpty()
                                       ? "Choose a video file or use the test pattern" : "Press START to transmit");
        else
            m_statusLabel->setText("Last error: " + m_enc->lastError());
        return;
    }
    const vid_t* v = m_enc->vid();
    QString s = QString("TV TX | %1 | %2").arg(m_modeCombo->currentData().toString().toUpper(),
                                                m_fileRadio->isChecked() ? QFileInfo(m_fileEdit->text()).fileName() : "test pattern");
    if (v) s += QString(" | %1x%2 @ %3 MS/s").arg(v->av.width).arg(v->av.height).arg(v->sample_rate / 1e6, 0, 'f', 1);
    emit statusText(s);
    m_statusLabel->setText("Transmitting");
}

void TvTxPanel::saveSettings(QSettings& s)
{
    s.beginGroup("tvTx");
    s.setValue("mode", m_modeCombo->currentData().toString());
    s.setValue("source", m_fileRadio->isChecked() ? 1 : 0);
    s.setValue("file", m_fileEdit->text());
    s.setValue("loop", m_loopCheck->isChecked());
    s.setValue("filter", m_filterCheck->isChecked());
    s.setValue("colour", m_colourCheck->isChecked());
    s.setValue("audio", m_audioCheck->isChecked());
    s.setValue("a2", m_a2Check->isChecked());
    s.setValue("nicam", m_nicamCheck->isChecked());
    s.setValue("acp", m_acpCheck->isChecked());
    s.setValue("vits", m_vitsCheck->isChecked());
    s.setValue("vitc", m_vitcCheck->isChecked());
    s.setValue("invert", m_invertCheck->isChecked());
    s.setValue("wss", m_wssCombo->currentIndex());
    s.setValue("scramble", m_scrambleCombo->currentIndex());
    s.setValue("teletext", m_teletextEdit->text());
    s.setValue("level", m_levelSpin->value());
    s.endGroup();
}

void TvTxPanel::loadSettings(QSettings& s)
{
    s.beginGroup("tvTx");
    const int mi = m_modeCombo->findData(s.value("mode", "b").toString());
    if (mi >= 0) m_modeCombo->setCurrentIndex(mi);
    (s.value("source", 0).toInt() == 1 ? m_fileRadio : m_testRadio)->setChecked(true);
    m_fileEdit->setText(s.value("file").toString());
    m_loopCheck->setChecked(s.value("loop", true).toBool());
    m_filterCheck->setChecked(s.value("filter", true).toBool());
    m_colourCheck->setChecked(s.value("colour", true).toBool());
    m_audioCheck->setChecked(s.value("audio", true).toBool());
    m_a2Check->setChecked(s.value("a2", false).toBool());
    m_nicamCheck->setChecked(s.value("nicam", true).toBool());
    m_acpCheck->setChecked(s.value("acp", false).toBool());
    m_vitsCheck->setChecked(s.value("vits", false).toBool());
    m_vitcCheck->setChecked(s.value("vitc", false).toBool());
    m_invertCheck->setChecked(s.value("invert", false).toBool());
    m_wssCombo->setCurrentIndex(s.value("wss", 0).toInt());
    m_scrambleCombo->setCurrentIndex(s.value("scramble", 0).toInt());
    m_teletextEdit->setText(s.value("teletext").toString());
    m_levelSpin->setValue(s.value("level", 1.0).toDouble());
    s.endGroup();
}
