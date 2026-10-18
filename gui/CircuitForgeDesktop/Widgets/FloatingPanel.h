#pragma once
#include <QFrame>
#include <QPoint>
class QVBoxLayout;
namespace Desktop {
class FloatingPanel : public QFrame {
    Q_OBJECT
  public:
    FloatingPanel(const QString &Title, QWidget *Parent);
    QWidget *Body() const {
        return Content;
    }
    void SetCollapsed(bool Value);
    bool IsCollapsed() const {
        return Collapsed;
    }
    QRect WorkspaceGeometry() const {
        QRect Result = geometry();
        if (Collapsed)
            Result.setHeight(ExpandedHeight);
        return Result;
    }
    void KeepInWorkspace();
  signals:
    void VisibilityChanged(bool Visible);

  protected:
    bool eventFilter(QObject *Object, QEvent *Event) override;

  private:
    QWidget *Header;
    QWidget *Grip;
    bool Resizing = false;
    QPoint ResizeOrigin;
    QSize ResizeStart;
    QWidget *Content;
    QPoint DragOffset;
    bool Dragging = false, Collapsed = false;
    int ExpandedHeight = 300;
};
} // namespace Desktop
