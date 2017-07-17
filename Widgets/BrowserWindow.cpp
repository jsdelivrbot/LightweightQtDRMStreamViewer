#include "BrowserWindow.hpp"

#include <QApplication>
#include <QDesktopWidget>

#include <Core/ConfigManager.hpp>
#include <Core/StreamingProviderStore.hpp>

BrowserWindow::BrowserWindow(QWidget *parent)
    : QWidget(parent)
{
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::BypassGraphicsProxyWidget |
                         Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    QRect desktopSize = QApplication::desktop()->screenGeometry();
    this->resize(desktopSize.width() / 1.2, desktopSize.height() / 1.2);
    this->move(desktopSize.width() / 2 - this->size().width() / 2, desktopSize.height() / 2 - this->size().height() / 2);

    this->installEventFilter(this);
    this->setMouseTracking(true);

    this->m_layout = new QVBoxLayout();
    this->m_layout->setContentsMargins(0, 0, 0, 0);
    this->m_layout->setMargin(0);
    this->m_layout->setSizeConstraint(QLayout::SetMaximumSize);

    this->createTitleBar();

    this->webView = new QWebEngineView();
    this->webView->setContextMenuPolicy(Qt::NoContextMenu);
    this->m_layout->addWidget(this->webView);
    this->setLayout(this->m_layout);

    QObject::connect(this->webView, &QWebEngineView::titleChanged, this, &BrowserWindow::setWindowTitle);
    QObject::connect(this->webView, &QWebEngineView::loadFinished, this, &BrowserWindow::hideScrollBars);

    new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_F), this, SLOT(toggleFullScreen()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q), this, SLOT(close()));

    // temporary workaround until i figured out how to implement a player-like behavior
    // note: the Maxdome player is buggy and slow, it also has major cursor visibility toggle issues,
    //       that's why i want to implement such a feature client-side in this app
    new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_H), this, SLOT(toggleCursorVisibility()));

    // Inject app name and version into the default Qt Web Engine user agent
    // the default looks like this:
    //     Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) QtWebEngine/5.9.1 Chrome/56.0.2924.122 Safari/537.36
    QString UserAgent = this->webView->page()->profile()->httpUserAgent();
    UserAgent.replace("QtWebEngine", qApp->applicationName() + '/' + qApp->applicationVersion() + " QtWebEngine");
    this->webView->page()->profile()->setHttpUserAgent(UserAgent);

    // allow PepperFlash to toggle fullscreen (Netflix, Amazon Video, etc.)
    this->webView->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    QObject::connect(this->webView->page(), &QWebEnginePage::fullScreenRequested, this, &BrowserWindow::acceptFullScreen);

    // other WebEngine settings
    this->webView->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    this->webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    this->webView->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);

    // load scripts for injection
    this->loadEmbeddedScript(this->mJs_hideScrollBars, "hide-scrollbars");
}

void BrowserWindow::createTitleBar()
{
    this->m_titleBar = new TitleBar(this);
    this->m_titleBar->setFixedHeight(25);
    this->m_titleBar->setBackgroundRole(QPalette::Background);

    QPalette titleBarScheme;
    titleBarScheme.setColor(QPalette::All, QPalette::Background, QColor( 50,  50,  50, 255));
    titleBarScheme.setColor(QPalette::All, QPalette::Text,       QColor(255, 255, 255, 255));
    titleBarScheme.setColor(QPalette::All, QPalette::WindowText, QColor(255, 255, 255, 255));

    this->m_titleBar->setPalette(titleBarScheme);
    this->m_titleBar->setVisible(false);
    this->m_layout->addWidget(this->m_titleBar);
}

void BrowserWindow::loadEmbeddedScript(QString &target, const QString &filename)
{
    QFile file(":/" + filename + ".js");
    if (file.open(QFile::ReadOnly | QFile::Text))
    {
        target = file.readAll();
        file.close();
        qDebug() << "Loaded embedded script" << file.fileName();
    }
    else
    {
        qDebug() << "Error loading embedded script" << file.fileName();
    }
}

BrowserWindow::~BrowserWindow()
{
    this->m_baseTitle.clear();
    this->m_cookieStoreId.clear();
    this->m_engineProfilePath.clear();
    this->m_cookies.clear();

    this->mJs_hideScrollBars.clear();
}

void BrowserWindow::showNormal()
{
    if (this->m_titleBar)
        this->m_titleBar->setVisible(true);
    QWidget::showNormal();
}

void BrowserWindow::showFullScreen()
{
    if (this->m_titleBar)
        this->m_titleBar->setVisible(false);
    QWidget::showFullScreen();
}

void BrowserWindow::setWindowTitle(const QString &title)
{
    QWidget::setWindowTitle(title + QString::fromUtf8(" ─ ") + this->m_baseTitle);
    this->m_titleBar->setTitle(QWidget::windowTitle());
}

void BrowserWindow::setWindowIcon(const QIcon &icon)
{
    if (icon.isNull())
    {
        QWidget::setWindowIcon(qApp->windowIcon());
        this->m_titleBar->setIcon(qApp->windowIcon().pixmap(23, 23, QIcon::Normal, QIcon::On));
    }
    else
    {
        QWidget::setWindowIcon(icon);
        this->m_titleBar->setIcon(icon.pixmap(23, 23, QIcon::Normal, QIcon::On));
    }
}

void BrowserWindow::setTitleBarVisibility(bool visible)
{
    this->m_titleBar->setVisible(visible);
}

void BrowserWindow::setTitleBarColor(const QColor &color, const QColor &textColor)
{
    QPalette titleBarScheme = this->m_titleBar->palette();
    titleBarScheme.setColor(QPalette::All, QPalette::Background, color);
    titleBarScheme.setColor(QPalette::All, QPalette::Text,       textColor);
    titleBarScheme.setColor(QPalette::All, QPalette::WindowText, textColor);
    this->m_titleBar->setPalette(titleBarScheme);
    this->setPalette(titleBarScheme);
}

void BrowserWindow::setBaseTitle(const QString &title)
{
    this->m_baseTitle = title;
}

void BrowserWindow::setUrl(const QUrl &url)
{
    this->webView->setUrl(url);
}

void BrowserWindow::setProfile(const QString &id)
{
    this->m_engineProfilePath = Config()->webEngineProfiles() + '/' + id;

    this->m_cookieStoreId = id;
    this->webView->page()->profile()->setCachePath(this->m_engineProfilePath);
    this->webView->page()->profile()->setPersistentStoragePath(this->m_engineProfilePath + '/' + "Storage");
    this->webView->page()->profile()->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    this->m_cookieStore = this->webView->page()->profile()->cookieStore();
    this->m_cookieStore->loadAllCookies();
}

void BrowserWindow::reset()
{
    this->m_engineProfilePath = Config()->webEngineProfiles() + '/' + "Default";

    this->m_baseTitle.clear();
    QWidget::setWindowTitle(QLatin1String("BrowserWindow"));
    this->webView->setUrl(QUrl("about:blank"));
    this->webView->stop();

    this->m_cookieStoreId.clear();
    this->webView->page()->profile()->setCachePath(this->m_engineProfilePath);
    this->webView->page()->profile()->setPersistentStoragePath(this->m_engineProfilePath + '/' + "Storage");
    this->webView->page()->profile()->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    this->m_cookieStore = this->webView->page()->profile()->cookieStore();

    this->close();
}

bool BrowserWindow::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::MouseMove)
    {
         this->mouseMoveEvent(static_cast<QMouseEvent*>(event));
    }

    return false;
}

void BrowserWindow::toggleFullScreen()
{
    this->isFullScreen() ? this->showNormal() : this->showFullScreen();
}

void BrowserWindow::hideScrollBars()
{
    this->webView->page()->runJavaScript(this->mJs_hideScrollBars);
}

void BrowserWindow::acceptFullScreen(QWebEngineFullScreenRequest req)
{
    // accept fullscreen request, does nothing to the window or widgets
    req.accept();

    // actually toggle fullscreen mode
    this->toggleFullScreen();
}

void BrowserWindow::toggleCursorVisibility()
{
    // buggy as fuck and doesn't work most of the time
    static bool hidden = false; // assume cursor is visible
    this->unsetCursor();
    hidden ? this->showCursor() : this->hideCursor();
    hidden = !hidden;
}

void BrowserWindow::showCursor()
{
    this->setCursor(QCursor(Qt::ArrowCursor));
}

void BrowserWindow::hideCursor()
{
    this->setCursor(QCursor(Qt::BlankCursor));
}
