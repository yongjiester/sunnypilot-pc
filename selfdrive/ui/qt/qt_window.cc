#include "selfdrive/ui/qt/qt_window.h"

#include <cstdlib>
#include <string>

#include <QDialog>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>

#include "common/util.h"

namespace {

// Resizable host that letterbox-scales a fixed 2160x1080 UI into the window.
// QT_SCALE_FACTOR (set in initApp) already includes screen fit + Mac DPR cancel;
// at the default DEVICE-sized window, fitInView scale is ~1.0. Resizing the window
// scales the design uniformly so menus/fonts stay proportional.
class ScaleHost : public QGraphicsView {
public:
  explicit ScaleHost(QWidget *content) {
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
    scene->setSceneRect(proxy_->boundingRect());

    // Default size matches design; QT_SCALE_FACTOR already fitted it to the screen.
    // Allow free resize so the user can grow/shrink; fitInView keeps aspect ratio.
    setWindowTitle(content->windowTitle().isEmpty() ? QStringLiteral("ui") : content->windowTitle());
    setMinimumSize(640, 360);
    resize(DEVICE_SCREEN_SIZE);
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QGraphicsView::resizeEvent(event);
    if (proxy_ != nullptr) {
      fitInView(proxy_, Qt::KeepAspectRatio);
    }
  }

  void showEvent(QShowEvent *event) override {
    QGraphicsView::showEvent(event);
    if (proxy_ != nullptr) {
      fitInView(proxy_, Qt::KeepAspectRatio);
    }
  }

private:
  QGraphicsProxyWidget *proxy_ = nullptr;
};

}  // namespace

void setMainWindow(QWidget *w) {
  if (Hardware::PC()) {
    // Dialogs stay top-level for QDialog::exec(); scaling comes from QT_SCALE_FACTOR.
    // Main window gets ScaleHost so the user can resize and keep uniform UI scale.
    if (qobject_cast<QDialog *>(w) == nullptr) {
      auto *host = new ScaleHost(w);
      host->setAttribute(Qt::WA_DeleteOnClose);
      host->setAttribute(Qt::WA_QuitOnClose);
      host->show();
      return;
    }
    w->setFixedSize(DEVICE_SCREEN_SIZE);
    w->show();
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


extern "C" {
  void set_main_window(void *w) {
    setMainWindow((QWidget*)w);
  }
}
