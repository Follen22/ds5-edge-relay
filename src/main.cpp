#include "mainwindow.hpp"
#include <QApplication>
#include <QPalette>
#include <QColor>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("ds5-edge-relay");
    app.setOrganizationName("ds5-edge-relay");
    app.setApplicationDisplayName("DS5 Edge Relay");
    app.setQuitOnLastWindowClosed(false);

    // ── Тёмная палитра ────────────────────────────────────────────────────────
    QPalette p;
    p.setColor(QPalette::Window,          QColor(0x12, 0x12, 0x1f));
    p.setColor(QPalette::WindowText,      QColor(0xe0, 0xe0, 0xf0));
    p.setColor(QPalette::Base,            QColor(0x19, 0x19, 0x28));
    p.setColor(QPalette::AlternateBase,   QColor(0x1c, 0x1c, 0x2e));
    p.setColor(QPalette::ToolTipBase,     QColor(0x1c, 0x1c, 0x2e));
    p.setColor(QPalette::ToolTipText,     QColor(0xe0, 0xe0, 0xf0));
    p.setColor(QPalette::Text,            QColor(0xe0, 0xe0, 0xf0));
    p.setColor(QPalette::Button,          QColor(0x1c, 0x1c, 0x2e));
    p.setColor(QPalette::ButtonText,      QColor(0xe0, 0xe0, 0xf0));
    p.setColor(QPalette::BrightText,      QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Link,            QColor(0x00, 0xc9, 0xa7));
    p.setColor(QPalette::Highlight,       QColor(0x00, 0xc9, 0xa7));
    p.setColor(QPalette::HighlightedText, QColor(0x0a, 0x0a, 0x1a));
    p.setColor(QPalette::Mid,             QColor(0x2a, 0x2a, 0x45));
    p.setColor(QPalette::Dark,            QColor(0x1c, 0x1c, 0x2e));
    p.setColor(QPalette::Shadow,          QColor(0x0a, 0x0a, 0x15));
    app.setPalette(p);

    // ── Глобальный QSS (компоненты, которые не берут стиль из палитры) ────────
    app.setStyleSheet(R"(
QGroupBox {
    background-color: #1c1c2e;
    border: 1px solid #2a2a45;
    border-radius: 10px;
    margin-top: 20px;
    padding: 14px 10px 10px 10px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 14px;
    padding: 2px 8px;
    color: #00c9a7;
    font-size: 10px;
    font-weight: bold;
    letter-spacing: 1px;
}
QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    border: 1.5px solid #3a3a6a;
    border-radius: 4px;
    background: #191928;
}
QCheckBox::indicator:checked {
    background-color: #00c9a7;
    border-color: #00c9a7;
}
QCheckBox::indicator:hover { border-color: #00c9a7; }
QScrollBar:horizontal {
    height: 6px;
    background: #191928;
    border-radius: 3px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #3a3a6a;
    border-radius: 3px;
    min-width: 20px;
}
QScrollBar::handle:horizontal:hover { background: #00c9a7; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QMenu {
    background-color: #1c1c2e;
    border: 1px solid #2a2a45;
    border-radius: 6px;
    padding: 4px;
}
QMenu::item { padding: 6px 20px; border-radius: 4px; }
QMenu::item:selected { background: #2a2a45; color: #00c9a7; }
QToolTip {
    background-color: #1c1c2e;
    color: #e0e0f0;
    border: 1px solid #2a2a45;
    border-radius: 4px;
    padding: 4px 8px;
}
)");

    MainWindow window;
    window.show();

    return app.exec();
}
