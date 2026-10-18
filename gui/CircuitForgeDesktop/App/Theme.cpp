#include "Theme.h"
#include <QFontDatabase>
#include <QPalette>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
namespace Desktop {
void ApplyNativeFrame(QWidget* Window,bool Dark){
#ifdef Q_OS_WIN
    HWND Handle=reinterpret_cast<HWND>(Window->winId());BOOL Enabled=Dark;
    // Windows 10/11 immersive frame. Unsupported attributes fall back to the native frame.
    DwmSetWindowAttribute(Handle,20,&Enabled,sizeof(Enabled));
    DWORD Rounded=2;DwmSetWindowAttribute(Handle,33,&Rounded,sizeof(Rounded));
#else
    Q_UNUSED(Window);Q_UNUSED(Dark);
#endif
}
void ApplyTheme(QApplication &App, bool Dark) {
    const auto Families = QFontDatabase::families();
    QFont Font(Families.contains("Helvetica") ? "Helvetica" : "Arial");
    Font.setPointSize(10);
    App.setFont(Font);
    App.setStyle("Fusion");
    const QString Background = Dark ? "#0B0D12" : "#EEF0F6";
    const QString Surface = Dark ? "#151922" : "#FFFFFF";
    const QString Raised = Dark ? "#1C2230" : "#E4E8F2";
    const QString Text = Dark ? "#F1F3F7" : "#202638";
    const QString Secondary = Dark ? "#9299AA" : "#566077";
    const QString Border = Dark ? "#2A3040" : "#CCD2DF";
    QPalette Palette;
    Palette.setColor(QPalette::Window, QColor(Background));
    Palette.setColor(QPalette::WindowText, QColor(Text));
    Palette.setColor(QPalette::Base, QColor(Surface));
    Palette.setColor(QPalette::AlternateBase, QColor(Raised));
    Palette.setColor(QPalette::HighlightedText, QColor("#F1F3F7"));
    Palette.setColor(QPalette::Text, QColor(Text));
    Palette.setColor(QPalette::Button, QColor(Raised));
    Palette.setColor(QPalette::ButtonText, QColor(Text));
    Palette.setColor(QPalette::Highlight, QColor("#7C8CFF"));
    App.setPalette(Palette);
    App.setStyleSheet(QString(R"(
QMainWindow, QWidget#Workspace { background:%1; color:%4; }
QWidget { color:%4; font-size:12px; }
QFrame#FloatingPanel { background:%2; border:1px solid %6; border-radius:16px; }
QWidget#PanelHeader { background:transparent; }
QLabel#PanelTitle { font-size:13px; font-weight:600; }
QLabel#Brand { font-size:21px; font-weight:600; }
QLabel#Muted { color:%5; font-size:11px; }
QLabel#Chip { background:%3; border-radius:8px; padding:6px 10px; color:#7C8CFF; }
QPushButton, QToolButton { background:%3; border:1px solid %6; border-radius:10px; padding:8px 13px; font-weight:600; }
QPushButton:hover, QToolButton:hover { background:#303A50; border-color:#68749A; color:#F1F3F7; }
QPushButton:pressed, QToolButton:pressed { background:#1B2332; color:#F1F3F7; }
QPushButton:disabled, QToolButton:disabled { color:#626A78; background:%2; }
QPushButton[variant="primary"] { background:#7C8CFF; color:#0B0D12; border-color:#7C8CFF; }
QPushButton[variant="primary"]:hover { background:#9BA7FF; }
QPushButton[variant="primary"]:pressed { background:#6575E1; }
QPushButton[variant="danger"] { color:#F07178; }
QToolButton { padding:3px; min-width:22px; min-height:22px; border-radius:8px; }
QLineEdit, QSpinBox, QComboBox { background:%3; border:1px solid %6; border-radius:9px; padding:7px; selection-background-color:#7C8CFF; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color:#7C8CFF; }
QComboBox QAbstractItemView { background:%2; color:%4; selection-background-color:#5261B0; }
QPlainTextEdit, QTextEdit, QTableView, QListWidget { background:%2; border:1px solid %6; border-radius:9px; selection-background-color:#43518B; }
QHeaderView::section { background:%3; color:%5; border:0; padding:7px; }
QMenuBar, QMenu { background:%2; color:%4; }
QMenu::item:selected { background:#43518B; border-radius:5px; }
QStatusBar { background:%2; color:%5; }
QToolTip { background:%3; color:%4; border:1px solid %6; border-radius:8px; padding:7px; }
QScrollBar:vertical { background:%2; width:9px; }
QScrollBar::handle:vertical { background:#424B63; border-radius:4px; min-height:24px; }
QScrollBar:horizontal { background:%2; height:9px; }
QScrollBar::handle:horizontal { background:#424B63; border-radius:4px; min-width:24px; }
QScrollBar::add-line, QScrollBar::sub-line { width:0; height:0; }
QCheckBox { spacing:7px; }
QCheckBox::indicator { width:15px; height:15px; border:1px solid %6; border-radius:4px; background:%3; }
QCheckBox::indicator:checked { background:#7C8CFF; border-color:#7C8CFF; }
QTableView { gridline-color:%6; alternate-background-color:%3; }
QTabWidget::pane { border:0; }
QTabBar::tab { background:%3; padding:8px; border-radius:7px; margin-right:3px; }
QTabBar::tab:selected { color:#7C8CFF; }
)")
                          .arg(Background, Surface, Raised, Text, Secondary, Border));
}
} // namespace Desktop
