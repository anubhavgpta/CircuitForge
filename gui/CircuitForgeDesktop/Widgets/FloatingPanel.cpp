#include "FloatingPanel.h"
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSizeGrip>
#include <QToolButton>
namespace Desktop {
FloatingPanel::FloatingPanel(const QString &Title, QWidget *Parent) : QFrame(Parent) {
    setObjectName("FloatingPanel");
    setMinimumSize(220, 100);
    auto *Shadow = new QGraphicsDropShadowEffect(this);
    Shadow->setBlurRadius(28);
    Shadow->setOffset(0, 8);
    Shadow->setColor(QColor(0, 0, 0, 95));
    setGraphicsEffect(Shadow);
    auto *Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(12, 8, 12, 6);
    Layout->setSpacing(8);
    Header = new QWidget(this);
    Header->setObjectName("PanelHeader");
    Header->setCursor(Qt::SizeAllCursor);
    Header->installEventFilter(this);
    auto *Row = new QHBoxLayout(Header);
    Row->setContentsMargins(2, 0, 0, 0);
    auto *Label = new QLabel(Title);
    Label->setObjectName("PanelTitle");
    Label->setAttribute(Qt::WA_TransparentForMouseEvents);
    Row->addWidget(Label, 1);
    auto *Collapse = new QToolButton;
    Collapse->setText("−");
    Collapse->setToolTip("Collapse / expand panel");
    Collapse->setAccessibleName("Collapse " + Title);
    auto *Close = new QToolButton;
    Close->setText("×");
    Close->setToolTip("Hide panel · reopen from View menu");
    Close->setAccessibleName("Hide " + Title);
    Row->addWidget(Collapse);
    Row->addWidget(Close);
    Layout->addWidget(Header);
    Content = new QWidget(this);
    Layout->addWidget(Content, 1);
    Grip = new QLabel("◢", this);
    Grip->setObjectName("PanelResizeGrip");
    Grip->setFixedSize(18, 16);
    Grip->setCursor(Qt::SizeFDiagCursor);
    Grip->setToolTip("Resize panel");
    Grip->installEventFilter(this);
    Layout->addWidget(Grip, 0, Qt::AlignRight);
    connect(Collapse, &QToolButton::clicked, this, [this] { SetCollapsed(!Collapsed); });
    connect(Close, &QToolButton::clicked, this, [this] {
        hide();
        emit VisibilityChanged(false);
    });
}
void FloatingPanel::SetCollapsed(bool Value) {
    if (Collapsed == Value)
        return;
    Collapsed = Value;
    if (Value) {
        ExpandedHeight = height();
        Content->hide();
        setMinimumHeight(62);
        resize(width(), 62);
    } else {
        Content->show();
        setMinimumHeight(100);
        resize(width(), ExpandedHeight);
    }
    KeepInWorkspace();
}
void FloatingPanel::KeepInWorkspace() {
    if (!parentWidget())
        return;
    resize(qMin(width(), qMax(minimumWidth(), parentWidget()->width() - 16)),
           qMin(height(), qMax(minimumHeight(), parentWidget()->height() - 16)));
    move(qBound(0, x(), qMax(0, parentWidget()->width() - width())),
         qBound(0, y(), qMax(0, parentWidget()->height() - height())));
}
bool FloatingPanel::eventFilter(QObject *Object, QEvent *Event) {
    if (Object == Grip) {
        if (Event->type() == QEvent::MouseButtonPress) {
            auto *Mouse = static_cast<QMouseEvent *>(Event);
            if (Mouse->button() == Qt::LeftButton) {
                Resizing = true;
                ResizeOrigin = Mouse->globalPosition().toPoint();
                ResizeStart = size();
                raise();
                return true;
            }
        }
        if (Event->type() == QEvent::MouseMove && Resizing) {
            auto *Mouse = static_cast<QMouseEvent *>(Event);
            QPoint Delta = Mouse->globalPosition().toPoint() - ResizeOrigin;
            resize(qMax(minimumWidth(), ResizeStart.width() + Delta.x()),
                   qMax(minimumHeight(), ResizeStart.height() + Delta.y()));
            KeepInWorkspace();
            return true;
        }
        if (Event->type() == QEvent::MouseButtonRelease) {
            Resizing = false;
            return true;
        }
    }
    if (Object == Header) {
        if (Event->type() == QEvent::MouseButtonPress) {
            auto *Mouse = static_cast<QMouseEvent *>(Event);
            if (Mouse->button() == Qt::LeftButton) {
                Dragging = true;
                DragOffset = Mouse->globalPosition().toPoint() - mapToGlobal(QPoint());
                raise();
                return true;
            }
        }
        if (Event->type() == QEvent::MouseMove && Dragging) {
            auto *Mouse = static_cast<QMouseEvent *>(Event);
            move(parentWidget()->mapFromGlobal(Mouse->globalPosition().toPoint() - DragOffset));
            KeepInWorkspace();
            return true;
        }
        if (Event->type() == QEvent::MouseButtonRelease) {
            Dragging = false;
            return true;
        }
        if (Event->type() == QEvent::MouseButtonDblClick) {
            SetCollapsed(!Collapsed);
            return true;
        }
    }
    return QFrame::eventFilter(Object, Event);
}
} // namespace Desktop
