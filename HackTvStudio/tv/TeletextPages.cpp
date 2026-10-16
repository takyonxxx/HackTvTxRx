#include "TeletextPages.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

/* Teletext spacing attributes are written in .tti files as ESC + (code | 0x40). */
QByteArray ctl(char code) { return QByteArray(1, '\x1b') + static_cast<char>(code | 0x40); }
const QByteArray RED     = ctl(0x01);
const QByteArray GREEN   = ctl(0x02);
const QByteArray YELLOW  = ctl(0x03);
const QByteArray BLUE    = ctl(0x04);
const QByteArray CYAN    = ctl(0x06);
const QByteArray WHITE   = ctl(0x07);
const QByteArray DBL     = ctl(0x0D);   // double height
const QByteArray NEWBG   = ctl(0x1D);   // new background (current colour)

/* Teletext G0 Latin has no Turkish letters: fold to ASCII. */
QByteArray ascii(QString s)
{
    static const QString from = QStringLiteral("çÇğĞıİöÖşŞüÜ");
    static const char*   to   = "cCgGiIoOsSuU";
    for (int i = 0; i < from.size(); ++i) s.replace(from[i], QChar::fromLatin1(to[i]));
    QByteArray out;
    for (QChar c : s) out += (c.unicode() >= 0x20 && c.unicode() < 0x7f) ? static_cast<char>(c.unicode()) : '?';
    return out;
}

class Page {
public:
    Page(int number, const QString& description) {
        m_text += "DE," + ascii(description) + "\r\n";
        m_text += QString("PN,%1%2\r\n").arg(number, 3, 16, QChar('0')).arg("00").toLatin1();
        m_text += "SC,0000\r\n";
        m_text += "PS,8000\r\n";
    }
    /* One row; cells beyond 40 are dropped (control codes take one cell each). */
    void row(const QByteArray& content) {
        if (m_row > 23) return;
        QByteArray line;
        int cells = 0;
        for (int i = 0; i < content.size() && cells < 40; ++i) {
            if (content[i] == '\x1b' && i + 1 < content.size()) { line += content[i]; line += content[++i]; }
            else line += content[i];
            ++cells;
        }
        m_text += "OL," + QByteArray::number(m_row++) + "," + line + "\r\n";
    }
    void blank() { ++m_row; }
    void title(const QString& section, int number) {
        row(BLUE + NEWBG + WHITE + DBL + "HackTvStudio " + ascii(section));
        blank();                                   // double height uses two rows
        row(CYAN + QString("Page %1").arg(number).toLatin1());
        blank();
    }
    void field(const QString& name, const QString& value, const QByteArray& colour = YELLOW) {
        row(WHITE + ascii(name.leftJustified(10)) + colour + ascii(value));
    }
    void links(int red, int green, int yellow, int cyan) {
        m_text += QString("FL,%1,%2,%3,%4,100,100\r\n").arg(red).arg(green).arg(yellow).arg(cyan).toLatin1();
    }
    QByteArray text() const { return m_text; }
private:
    QByteArray m_text;
    int m_row = 1;
};

bool save(const QString& path, const QByteArray& data, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(data);
    return true;
}

} // namespace

QString TeletextPages::defaultDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/HackTvStudio";
    return QDir::cleanPath(base + "/teletext");
}

bool TeletextPages::write(const QString& dir, const TeletextInfo& info, QString* error)
{
    if (!QDir().mkpath(dir)) {
        if (error) *error = "Cannot create " + dir;
        return false;
    }
    const QString now = QDateTime::currentDateTime().toString("dd.MM.yyyy HH:mm:ss");

    // ---- 100: index
    Page idx(100, "HackTvStudio index");
    idx.title("TELETEXT", 100);
    idx.row(GREEN + "101" + WHITE + " Transmission parameters");
    idx.row(GREEN + "102" + WHITE + " Video / sound source");
    idx.row(GREEN + "103" + WHITE + " Encoder options");
    idx.blank();
    idx.field("Started", now);
    idx.field("Clock", "live, top row (header)", CYAN);
    idx.blank();
    idx.row(RED + "Analogue TV from a HackRF One");
    idx.row(WHITE + "github.com/takyonxxx/HackTvTxRx");
    idx.links(101, 102, 103, 100);

    // ---- 101: transmission
    Page tx(101, "Transmission parameters");
    tx.title("TRANSMISSION", 101);
    tx.field("Frequency", QString("%1 MHz").arg(info.frequencyHz / 1e6, 0, 'f', 3));
    if (!info.channel.isEmpty()) tx.field("Channel", info.channel);
    tx.field("Standard", info.modeId.toUpper() + "  " + info.modeDescription.section(',', 0, 0));
    tx.field("System", info.modeDescription.section(',', 1).trimmed(), CYAN);
    tx.field("Sample rt", QString("%1 MS/s").arg(info.sampleRate / 1e6, 0, 'f', 1));
    tx.field("Picture", QString("%1 x %2, %3 fps").arg(info.width).arg(info.height).arg(info.fps));
    tx.field("Sound", info.sound);
    tx.field("WSS", info.wss);
    tx.field("Level", QString::number(info.level, 'f', 2));
    tx.blank();
    tx.field("Started", now);
    tx.field("Clock", "live, top row (header)", CYAN);
    tx.links(102, 103, 100, 100);

    // ---- 102: source
    Page src(102, "Video / sound source");
    src.title("SOURCE", 102);
    src.field("Source", info.sourceKind);
    src.field("Name", info.sourceName.isEmpty() ? "-" : info.sourceName);
    src.field("Sound", info.sourceSound.isEmpty() ? "-" : info.sourceSound);
    src.blank();
    src.field("Started", now);
    src.links(103, 100, 101, 100);

    // ---- 103: options
    Page opt(103, "Encoder options");
    opt.title("OPTIONS", 103);
    if (info.options.isEmpty()) opt.row(WHITE + "(none)");
    for (const QString& o : info.options) opt.row(GREEN + "+ " + WHITE + ascii(o));
    opt.blank();
    opt.field("Started", now);
    opt.links(100, 101, 102, 100);

    return save(dir + "/P100.tti", idx.text(), error)
        && save(dir + "/P101.tti", tx.text(), error)
        && save(dir + "/P102.tti", src.text(), error)
        && save(dir + "/P103.tti", opt.text(), error);
}
