#include "selfdrive/ui/qt/util.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayoutItem>
#include <QScreen>
#include <QSize>
#include <QStyleOption>
#include <QPainterPath>
#include <QTextStream>
#include <QtXml/QDomDocument>

#include "common/swaglog.h"
#include "common/util.h"
#include "system/hardware/hw.h"

// Keep in sync with DEVICE_SCREEN_SIZE in qt_window.h (avoid include cycle).
static const QSize kDeviceScreenSize = {2160, 1080};

QString getVersion() {
  static QString version =  QString::fromStdString(Params().get("Version"));
  return version;
}

QString getBrand() {
  return QObject::tr("sunnypilot");
}

QString getUserAgent() {
  return "openpilot-" + getVersion();
}

std::optional<QString> getDongleId() {
  std::string id = Params().get("DongleId");

  if (!id.empty() && (id != "UnregisteredDevice")) {
    return QString::fromStdString(id);
  } else {
    return {};
  }
}

QMap<QString, QString> getSupportedLanguages() {
  QFile f(":/languages.json");
  f.open(QIODevice::ReadOnly | QIODevice::Text);
  QString val = f.readAll();

  QJsonObject obj = QJsonDocument::fromJson(val.toUtf8()).object();
  QMap<QString, QString> map;
  for (auto key : obj.keys()) {
    map[key] = obj[key].toString();
  }
  return map;
}

QString timeAgo(const QDateTime &date) {
  if (!util::system_time_valid()) {
    return date.date().toString();
  }

  int diff = date.secsTo(QDateTime::currentDateTimeUtc());

  QString s;
  if (diff < 60) {
    s = QObject::tr("now");
  } else if (diff < 60 * 60) {
    int minutes = diff / 60;
    s = QObject::tr("%n minute(s) ago", "", minutes);
  } else if (diff < 60 * 60 * 24) {
    int hours = diff / (60 * 60);
    s = QObject::tr("%n hour(s) ago", "", hours);
  } else if (diff < 3600 * 24 * 7) {
    int days = diff / (60 * 60 * 24);
    s = QObject::tr("%n day(s) ago", "", days);
  } else {
    s = date.date().toString();
  }

  return s;
}

void setQtSurfaceFormat() {
  QSurfaceFormat fmt;
#ifdef __APPLE__
  fmt.setVersion(3, 2);
  fmt.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
#else
  fmt.setRenderableType(QSurfaceFormat::OpenGLES);
#endif
  fmt.setSamples(16);
  fmt.setStencilBufferSize(1);
  QSurfaceFormat::setDefaultFormat(fmt);
}

void sigTermHandler(int s) {
  std::signal(s, SIG_DFL);
  qApp->quit();
}

namespace {

// Fit 2160x1080 design into available screen. Uses physical pixels when HiDPI will be cancelled.
float computeUiFitScale(QScreen *screen, qreal device_pixel_ratio) {
  if (screen == nullptr) {
    return 1.0f;
  }
  const QRect avail = screen->availableGeometry();
  const float avail_w = avail.width() * static_cast<float>(device_pixel_ratio);
  const float avail_h = avail.height() * static_cast<float>(device_pixel_ratio);
  return std::min({avail_w / kDeviceScreenSize.width(),
                   avail_h / kDeviceScreenSize.height(),
                   1.0f});
}

float resolveUserScaleOrFit(float auto_fit) {
  // SCALE unset or "auto" -> auto-fit; SCALE=<number> overrides (e.g. 0.75, 1)
  const char *scale_env = std::getenv("SCALE");
  if (scale_env == nullptr || std::string(scale_env) == "auto") {
    return auto_fit;
  }
  return util::getenv("SCALE", auto_fit);
}

}  // namespace

void initApp(int argc, char *argv[], bool disable_hidpi) {
  Hardware::set_display_power(true);
  Hardware::set_brightness(65);

  // setup signal handlers to exit gracefully
  std::signal(SIGINT, sigTermHandler);
  std::signal(SIGTERM, sigTermHandler);

  QString app_dir;
  float dpr_inv = 1.0f;
  float fit = 1.0f;

#ifdef __APPLE__
  // Temporary QApplication to read DPR / screen before the real app starts
  QApplication tmp(argc, argv);
  app_dir = QCoreApplication::applicationDirPath();
  const qreal dpr = tmp.devicePixelRatio();
  if (disable_hidpi) {
    dpr_inv = static_cast<float>(1.0 / dpr);
  }
  if (Hardware::PC()) {
    // Pass physical pixels when HiDPI will be cancelled via QT_SCALE_FACTOR.
    fit = resolveUserScaleOrFit(computeUiFitScale(tmp.primaryScreen(), disable_hidpi ? dpr : 1.0));
  }
#else
  app_dir = QFileInfo(util::readlink("/proc/self/exe").c_str()).path();
  if (Hardware::PC()) {
    // Temporary app to query availableGeometry before QT_SCALE_FACTOR is applied
    QGuiApplication tmp(argc, argv);
    fit = resolveUserScaleOrFit(computeUiFitScale(tmp.primaryScreen(), 1.0));
  }
#endif

  // UI_FIT_SCALE: initial ScaleHost size for main window and dialogs (SCALE= override).
  // Must NOT go into QT_SCALE_FACTOR — combining global Qt scale with fitInView clips the
  // right edge of settings (stylesheet px + proxy size disagree). Dialogs (e.g. keyboard)
  // also use ScaleHost so they track the resized main window.
  if (Hardware::PC()) {
    qputenv("UI_FIT_SCALE", QString::number(static_cast<double>(fit), 'g', 8).toLocal8Bit());
  }

  // QT_SCALE_FACTOR: Mac HiDPI cancel only. Screen fit is ScaleHost::fitInView.
  if (disable_hidpi && dpr_inv != 1.0f) {
    qputenv("QT_SCALE_FACTOR", QString::number(static_cast<double>(dpr_inv), 'g', 8).toLocal8Bit());
  }

  qputenv("QT_DBL_CLICK_DIST", QByteArray::number(150));
  // ensure the current dir matches the exectuable's directory
  QDir::setCurrent(app_dir);

  setQtSurfaceFormat();
}

void swagLogMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
  static std::map<QtMsgType, int> levels = {
    {QtMsgType::QtDebugMsg, CLOUDLOG_DEBUG},
    {QtMsgType::QtInfoMsg, CLOUDLOG_INFO},
    {QtMsgType::QtWarningMsg, CLOUDLOG_WARNING},
    {QtMsgType::QtCriticalMsg, CLOUDLOG_ERROR},
    {QtMsgType::QtSystemMsg, CLOUDLOG_ERROR},
    {QtMsgType::QtFatalMsg, CLOUDLOG_CRITICAL},
  };

  std::string file, function;
  if (context.file != nullptr) file = context.file;
  if (context.function != nullptr) function = context.function;

  auto bts = msg.toUtf8();
  cloudlog_e(levels[type], file.c_str(), context.line, function.c_str(), "%s", bts.constData());
}


QWidget* topWidget(QWidget* widget) {
  while (widget->parentWidget() != nullptr) widget=widget->parentWidget();
  return widget;
}

QPixmap loadPixmap(const QString &fileName, const QSize &size, Qt::AspectRatioMode aspectRatioMode) {
  if (size.isEmpty()) {
    return QPixmap(fileName);
  } else {
    return QPixmap(fileName).scaled(size, aspectRatioMode, Qt::SmoothTransformation);
  }
}

static QHash<QString, QByteArray> load_bootstrap_icons() {
  QHash<QString, QByteArray> icons;

  QFile f(":/bootstrap-icons.svg");
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QDomDocument xml;
    xml.setContent(&f);
    QDomNode n = xml.documentElement().firstChild();
    while (!n.isNull()) {
      QDomElement e = n.toElement();
      if (!e.isNull() && e.hasAttribute("id")) {
        QString svg_str;
        QTextStream stream(&svg_str);
        n.save(stream, 0);
        svg_str.replace("<symbol", "<svg");
        svg_str.replace("</symbol>", "</svg>");
        icons[e.attribute("id")] = svg_str.toUtf8();
      }
      n = n.nextSibling();
    }
  }
  return icons;
}

QPixmap bootstrapPixmap(const QString &id) {
  static QHash<QString, QByteArray> icons = load_bootstrap_icons();

  QPixmap pixmap;
  if (auto it = icons.find(id); it != icons.end()) {
    pixmap.loadFromData(it.value(), "svg");
  }
  return pixmap;
}

bool hasLongitudinalControl(const cereal::CarParams::Reader &car_params) {
  // Using the experimental longitudinal toggle, returns whether longitudinal control
  // will be active without needing a restart of openpilot
  return car_params.getAlphaLongitudinalAvailable()
             ? Params().getBool("AlphaLongitudinalEnabled")
             : car_params.getOpenpilotLongitudinalControl();
}

// ParamWatcher

ParamWatcher::ParamWatcher(QObject *parent) : QObject(parent) {
  watcher = new QFileSystemWatcher(this);
  QObject::connect(watcher, &QFileSystemWatcher::fileChanged, this, &ParamWatcher::fileChanged);
}

void ParamWatcher::fileChanged(const QString &path) {
  auto param_name = QFileInfo(path).fileName();
  auto param_value = QString::fromStdString(params.get(param_name.toStdString()));

  auto it = params_hash.find(param_name);
  bool content_changed = (it == params_hash.end()) || (it.value() != param_value);
  params_hash[param_name] = param_value;
  // emit signal when the content changes.
  if (content_changed) {
    emit paramChanged(param_name, param_value);
  }
}

void ParamWatcher::addParam(const QString &param_name) {
  watcher->addPath(QString::fromStdString(params.getParamPath(param_name.toStdString())));
}
