#include "selfdrive/ui/qt/qt_window.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include <QDialog>
#include <QEventLoop>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>

#include "common/util.h"

namespace {

float uiFitScale() {
  // Set by initApp (auto from availableGeometry, or SCALE= override). Fallback: fit now.
  if (const char *env = std::getenv("UI_FIT_SCALE")) {
    return std::strtof(env, nullptr);
  }
  if (QScreen *screen = QGuiApplication::primaryScreen()) {
    const QRect avail = screen->availableGeometry();
    return std::min({avail.width() / float(DEVICE_SCREEN_SIZE.width()),
                     avail.height() / float(DEVICE_SCREEN_SIZE.height()),
                     1.0f});
  }
  return 1.0f;
}

QSize fittedWindowSize() {
  const float fit = uiFitScale();
  QSize size(qRound(DEVICE_SCREEN_SIZE.width() * fit),
             qRound(DEVICE_SCREEN_SIZE.height() * fit));

  // Keep inside available geometry (title bar / taskbar), preserving 2:1 aspect.
  if (QScreen *screen = QGuiApplication::primaryScreen()) {
    const QRect avail = screen->availableGeometry();
    constexpr int kFramePad = 64;
    const QSize max_size(avail.width(), std::max(360, avail.height() - kFramePad));
    if (size.width() > max_size.width() || size.height() > max_size.height()) {
      size = DEVICE_SCREEN_SIZE.scaled(max_size, Qt::KeepAspectRatio);
    }
  }
  return size;
}

// Letterbox-scales a fixed 2160x1080 UI into a resizable window.
// QT_SCALE_FACTOR (initApp) is Mac DPR cancel only; this host owns screen/window fit
// so menus stay proportional without clipping the settings right panel.
class ScaleHost : public QGraphicsView {
public:
  explicit ScaleHost(QWidget *content, bool is_dialog = false) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(Qt::black);
    setAlignment(Qt::AlignCenter);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    auto *scene = new QGraphicsScene(this);
    scene->setBackgroundBrush(Qt::black);
    setScene(scene);

    content->setFixedSize(DEVICE_SCREEN_SIZE);
    content->setParent(nullptr);
    proxy_ = scene->addWidget(content);
    // Clip any child paint that exceeds the design canvas (avoids right-edge bleed).
    proxy_->setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);
    scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(DEVICE_SCREEN_SIZE)));

    setWindowTitle(content->windowTitle().isEmpty() ? QStringLiteral("ui") : content->windowTitle());
    setMinimumSize(640, 360);

    if (is_dialog && g_main != nullptr) {
      resize(g_main->size());
    } else {
      resize(fittedWindowSize());
    }

    if (!is_dialog) {
      g_main = this;
    }
  }

  ~ScaleHost() override {
    if (g_main == this) {
      g_main = nullptr;
    }
  }

  // QGraphicsProxyWidget owns the embedded widget; release before destroying host
  // so stack-allocated QDialogs are not deleted.
  void releaseContent() {
    if (proxy_ != nullptr && proxy_->widget() != nullptr) {
      proxy_->setWidget(nullptr);
    }
  }

  static ScaleHost *mainHost() { return g_main; }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QGraphicsView::resizeEvent(event);
    refit();
  }

  void showEvent(QShowEvent *event) override {
    QGraphicsView::showEvent(event);
    refit();
  }

private:
  void refit() {
    if (proxy_ == nullptr || scene() == nullptr) {
      return;
    }
    // Fit the fixed design rect (not proxy boundingRect alone) so the full 2160x1080
    // canvas stays visible when the window is smaller than design size.
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
  }

  QGraphicsProxyWidget *proxy_ = nullptr;
  static ScaleHost *g_main;
};

ScaleHost *ScaleHost::g_main = nullptr;

void showDialogHost(ScaleHost *host, QDialog *dialog) {
  if (ScaleHost *main = ScaleHost::mainHost()) {
    host->setGeometry(main->geometry());
  }
  host->setWindowModality(Qt::ApplicationModal);
  QObject::connect(dialog, &QDialog::finished, host, [host]() {
    host->releaseContent();
    host->close();
  });
  host->show();
}

int execDialogHost(QDialog *dialog) {
  ScaleHost host(dialog, true);
  if (ScaleHost *main = ScaleHost::mainHost()) {
    host.setGeometry(main->geometry());
  }
  host.setWindowModality(Qt::ApplicationModal);
  host.show();

  QEventLoop loop;
  QObject::connect(dialog, &QDialog::finished, &loop, &QEventLoop::quit);
  loop.exec();

  const int result = dialog->result();
  host.releaseContent();
  return result;
}

}  // namespace

void setMainWindow(QWidget *w) {
  if (Hardware::PC()) {
    // Main window and dialogs both use ScaleHost so shrink/resize keeps full UI
    // (including InputDialog keyboard) visible via fitInView.
    if (auto *dialog = qobject_cast<QDialog *>(w)) {
      auto *host = new ScaleHost(w, true);
      host->setAttribute(Qt::WA_DeleteOnClose);
      showDialogHost(host, dialog);
      return;
    }
    auto *host = new ScaleHost(w, false);
    host->setAttribute(Qt::WA_DeleteOnClose);
    host->setAttribute(Qt::WA_QuitOnClose);
    host->show();
    return;
  }

  // Device / non-PC: fixed design resolution (optional numeric SCALE env for legacy)
  float scale = 1.0f;
  if (const char *scale_env = std::getenv("SCALE")) {
    if (std::string(scale_env) != "auto") {
      scale = util::getenv("SCALE", 1.0f);
    }
  }
  w->setFixedSize(DEVICE_SCREEN_SIZE * scale);
  w->show();

#ifdef QCOM2
  QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
  wl_surface *s = reinterpret_cast<wl_surface*>(native->nativeResourceForWindow("surface", w->windowHandle()));
  wl_surface_set_buffer_transform(s, WL_OUTPUT_TRANSFORM_270);
  wl_surface_commit(s);

  w->setWindowState(Qt::WindowFullScreen);
  w->setVisible(true);

  // ensure we have a valid eglDisplay, otherwise the ui will silently fail
  void *egl = native->nativeResourceForWindow("egldisplay", w->windowHandle());
  assert(egl != nullptr);
#endif
}

int execPcDialog(QDialog *dialog) {
  return execDialogHost(dialog);
}


extern "C" {
  void set_main_window(void *w) {
    setMainWindow((QWidget*)w);
  }
}
