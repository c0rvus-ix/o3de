/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/RHI/Factory.h>
#include <AtomToolsFramework/PerformanceMonitor/PerformanceMonitorRequestBus.h>
#include <AtomToolsFramework/Util/Util.h>
#include <AtomToolsFramework/Window/AtomToolsMainWindow.h>
#include <AtomToolsFramework/Window/AtomToolsMainWindowNotificationBus.h>
#include <AzCore/Name/Name.h>
#include <AzCore/Utils/Utils.h>
#include <AzToolsFramework/API/EditorPythonRunnerRequestsBus.h>
#include <AzToolsFramework/PythonTerminal/ScriptTermDialog.h>

#include <QCloseEvent>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>

namespace AtomToolsFramework
{
    AtomToolsMainWindow::AtomToolsMainWindow(const AZ::Crc32& toolId, QWidget* parent)
        : Base(parent)
        , m_toolId(toolId)
        , m_advancedDockManager(new AzQtComponents::FancyDocking(this))
        , m_mainWindowWrapper(new AzQtComponents::WindowDecorationWrapper(AzQtComponents::WindowDecorationWrapper::OptionAutoTitleBarButtons))
    {
        setObjectName(QApplication::applicationName().append(" MainWindow"));

        setDockNestingEnabled(true);
        setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
        setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
        setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
        setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

        AddCommonMenus();

        m_statusMessage = new QLabel(statusBar());
        statusBar()->addPermanentWidget(m_statusMessage, 1);

        auto centralWidget = new QWidget(this);
        auto centralWidgetLayout = new QVBoxLayout(centralWidget);
        centralWidgetLayout->setMargin(0);
        centralWidgetLayout->setContentsMargins(0, 0, 0, 0);
        centralWidget->setLayout(centralWidgetLayout);
        setCentralWidget(centralWidget);

        m_assetBrowser = new AtomToolsAssetBrowser(this);
        AddDockWidget("Asset Browser", m_assetBrowser, Qt::BottomDockWidgetArea, Qt::Horizontal);
        AddDockWidget("Python Terminal", new AzToolsFramework::CScriptTermDialog, Qt::BottomDockWidgetArea, Qt::Horizontal);
        SetDockWidgetVisible("Python Terminal", false);

        SetupMetrics();
        UpdateWindowTitle();

        resize(1280, 1024);

        // Manage saving window geometry, restoring state window is shown for the first time
        m_mainWindowWrapper->setGuest(this);
        m_mainWindowWrapper->enableSaveRestoreGeometry(
            QApplication::organizationName(), QApplication::applicationName(), "mainWindowGeometry");

        AtomToolsMainWindowRequestBus::Handler::BusConnect(m_toolId);
    }

    AtomToolsMainWindow::~AtomToolsMainWindow()
    {
        PerformanceMonitorRequestBus::Broadcast(&PerformanceMonitorRequestBus::Handler::SetProfilerEnabled, false);
        AtomToolsMainWindowRequestBus::Handler::BusDisconnect();
    }

    void AtomToolsMainWindow::showEvent(QShowEvent* showEvent)
    {
        if (!m_shownBefore)
        {
            m_shownBefore = true;
            m_defaultWindowState = m_advancedDockManager->saveState();
            m_mainWindowWrapper->showFromSettings();
            const AZStd::string windowState =
                AtomToolsFramework::GetSettingsObject("/O3DE/AtomToolsFramework/MainWindow/WindowState", AZStd::string());
            m_advancedDockManager->restoreState(QByteArray(windowState.data(), aznumeric_cast<int>(windowState.size())));
        }

        Base::showEvent(showEvent);
    }

    void AtomToolsMainWindow::closeEvent(QCloseEvent* closeEvent)
    {
        if (closeEvent->isAccepted())
        {
            const QByteArray windowState = m_advancedDockManager->saveState();
            AtomToolsFramework::SetSettingsObject(
                "/O3DE/AtomToolsFramework/MainWindow/WindowState", AZStd::string(windowState.begin(), windowState.end()));
            AtomToolsMainWindowNotificationBus::Event(m_toolId, &AtomToolsMainWindowNotifications::OnMainWindowClosing);
        }

        Base::closeEvent(closeEvent);
    }

    void AtomToolsMainWindow::ActivateWindow()
    {
        show();
        raise();
        activateWindow();
    }

    bool AtomToolsMainWindow::AddDockWidget(const AZStd::string& name, QWidget* widget, uint32_t area, uint32_t orientation)
    {
        auto dockWidgetItr = m_dockWidgets.find(name);
        if (dockWidgetItr != m_dockWidgets.end() || !widget)
        {
            return false;
        }

        auto dockWidget = new AzQtComponents::StyledDockWidget(name.c_str(), this);
        dockWidget->setObjectName(QString("%1_DockWidget").arg(name.c_str()));
        dockWidget->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
        widget->setObjectName(QString("%1_Widget").arg(name.c_str()));
        widget->setWindowTitle(name.c_str());
        widget->setParent(dockWidget);
        widget->setMinimumSize(QSize(300, 300));
        dockWidget->setWidget(widget);
        addDockWidget(aznumeric_cast<Qt::DockWidgetArea>(area), dockWidget);
        resizeDocks({ dockWidget }, { 400 }, aznumeric_cast<Qt::Orientation>(orientation));
        m_dockWidgets[name] = dockWidget;

        m_dockActions[name] = m_menuView->addAction(name.c_str(), [this, name](){
            SetDockWidgetVisible(name, !IsDockWidgetVisible(name));
        });
        return true;
    }

    void AtomToolsMainWindow::RemoveDockWidget(const AZStd::string& name)
    {
        auto dockWidgetItr = m_dockWidgets.find(name);
        if (dockWidgetItr != m_dockWidgets.end())
        {
            delete dockWidgetItr->second;
            m_dockWidgets.erase(dockWidgetItr);
        }
        auto dockActionItr = m_dockActions.find(name);
        if (dockActionItr != m_dockActions.end())
        {
            delete dockActionItr->second;
            m_dockActions.erase(dockActionItr);
        }
    }

    void AtomToolsMainWindow::SetDockWidgetVisible(const AZStd::string& name, bool visible)
    {
        auto dockWidgetItr = m_dockWidgets.find(name);
        if (dockWidgetItr != m_dockWidgets.end())
        {
            dockWidgetItr->second->setVisible(visible);
        }
    }

    bool AtomToolsMainWindow::IsDockWidgetVisible(const AZStd::string& name) const
    {
        auto dockWidgetItr = m_dockWidgets.find(name);
        if (dockWidgetItr != m_dockWidgets.end())
        {
            return dockWidgetItr->second->isVisible();
        }
        return false;
    }

    AZStd::vector<AZStd::string> AtomToolsMainWindow::GetDockWidgetNames() const
    {
        AZStd::vector<AZStd::string> names;
        names.reserve(m_dockWidgets.size());
        for (const auto& dockWidgetPair : m_dockWidgets)
        {
            names.push_back(dockWidgetPair.first);
        }
        return names;
    }

    void AtomToolsMainWindow::SetStatusMessage(const QString& message)
    {
        m_statusMessage->setText(QString("<font color=\"White\">%1</font>").arg(message));
    }

    void AtomToolsMainWindow::SetStatusWarning(const QString& message)
    {
        m_statusMessage->setText(QString("<font color=\"Yellow\">%1</font>").arg(message));
    }

    void AtomToolsMainWindow::SetStatusError(const QString& message)
    {
        m_statusMessage->setText(QString("<font color=\"Red\">%1</font>").arg(message));
    }

    void AtomToolsMainWindow::AddCommonMenus()
    {
        m_menuFile = menuBar()->addMenu("&File");
        m_menuEdit = menuBar()->addMenu("&Edit");
        m_menuView = menuBar()->addMenu("&View");
        m_menuHelp = menuBar()->addMenu("&Help");

        m_menuFile->addAction("Run &Python...", [this]() {
            const QString script = QFileDialog::getOpenFileName(
                this, QObject::tr("Run Script"), QString(AZ::Utils::GetProjectPath().c_str()), QString("*.py"));
            if (!script.isEmpty())
            {
                AzToolsFramework::EditorPythonRunnerRequestBus::Broadcast(
                    &AzToolsFramework::EditorPythonRunnerRequestBus::Events::ExecuteByFilename, script.toUtf8().constData());
            }
        });

        m_menuFile->addSeparator();

        m_menuFile->addAction("E&xit", [this]() {
            close();
        }, QKeySequence::Quit);

        m_menuEdit->addAction("&Settings...", [this]() {
            OpenSettings();
        }, QKeySequence::Preferences);

        m_menuView->addAction("Default Layout", [this]() {
            m_advancedDockManager->restoreState(m_defaultWindowState);
        });
        m_menuView->addSeparator();

        m_menuHelp->addAction("&Help...", [this]() {
            OpenHelp();
        });

        m_menuHelp->addAction("&About...", [this]() {
            OpenAbout();
        });
    }

    void AtomToolsMainWindow::OpenSettings()
    {
    }

    void AtomToolsMainWindow::OpenHelp()
    {
    }

    void AtomToolsMainWindow::OpenAbout()
    {
        QMessageBox::about(this, windowTitle(), QApplication::applicationName());
    }

    void AtomToolsMainWindow::SetupMetrics()
    {
        m_statusBarCpuTime = new QLabel(this);
        statusBar()->addPermanentWidget(m_statusBarCpuTime);
        m_statusBarGpuTime = new QLabel(this);
        statusBar()->addPermanentWidget(m_statusBarGpuTime);
        m_statusBarFps = new QLabel(this);
        statusBar()->addPermanentWidget(m_statusBarFps);

        static constexpr int UpdateIntervalMs = 1000;
        m_metricsTimer.setInterval(UpdateIntervalMs);
        m_metricsTimer.start();
        connect(&m_metricsTimer, &QTimer::timeout, this, &AtomToolsMainWindow::UpdateMetrics);

        PerformanceMonitorRequestBus::Broadcast(&PerformanceMonitorRequestBus::Handler::SetProfilerEnabled, true);

        UpdateMetrics();
    }

    void AtomToolsMainWindow::UpdateMetrics()
    {
        PerformanceMetrics metrics = {};
        PerformanceMonitorRequestBus::BroadcastResult(metrics, &PerformanceMonitorRequestBus::Handler::GetMetrics);

        m_statusBarCpuTime->setText(tr("CPU Time %1 ms").arg(QString::number(metrics.m_cpuFrameTimeMs, 'f', 2)));
        m_statusBarGpuTime->setText(tr("GPU Time %1 ms").arg(QString::number(metrics.m_gpuFrameTimeMs, 'f', 2)));
        int frameRate = metrics.m_cpuFrameTimeMs > 0 ? aznumeric_cast<int>(1000 / metrics.m_cpuFrameTimeMs) : 0;
        m_statusBarFps->setText(tr("FPS %1").arg(QString::number(frameRate)));
    }

    void AtomToolsMainWindow::UpdateWindowTitle()
    {
        AZ::Name apiName = AZ::RHI::Factory::Get().GetName();
        if (!apiName.IsEmpty())
        {
            QString title = QString{ "%1 (%2)" }.arg(QApplication::applicationName()).arg(apiName.GetCStr());
            setWindowTitle(title);
        }
        else
        {
            AZ_Assert(false, "Render API name not found");
            setWindowTitle(QApplication::applicationName());
        }
    }
} // namespace AtomToolsFramework
