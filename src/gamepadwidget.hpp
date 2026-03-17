#pragma once
#include <QWidget>
#include <QPixmap>
#include <QVector>
#include "ds5_report.hpp"

class GamepadWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { IDLE, SELECT_TRIGGER, SELECT_ACTIONS };

    struct Zone {
        ds5::ButtonBit btn;
        QString        name;
        QRectF         rect; // normalized (x, y, w, h), top-left origin
    };

    explicit GamepadWidget(QWidget* parent = nullptr);

    void setMode(Mode m);
    void clearSelection();

    ds5::ButtonBit          triggerBtn() const { return trigger_btn_; }
    QVector<ds5::ButtonBit> actionBtns()  const { return action_btns_; }

    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;

signals:
    void triggerSelected(ds5::ButtonBit btn, const QString& name);
    void actionToggled(ds5::ButtonBit btn, const QString& name, bool added);

protected:
    void paintEvent(QPaintEvent* event)       override;
    void mousePressEvent(QMouseEvent* event)  override;
    void mouseMoveEvent(QMouseEvent* event)   override;
    void leaveEvent(QEvent* event)            override;

private:
    QPixmap                  pixmap_;
    QVector<Zone>            zones_;
    Mode                     mode_        = Mode::IDLE;
    int                      hover_idx_   = -1;
    int                      trigger_idx_ = -1;
    QVector<int>             action_idxs_;
    ds5::ButtonBit           trigger_btn_ = {0, 0};
    QVector<ds5::ButtonBit>  action_btns_;

    QRectF imageRect() const;
    int    zoneAt(const QPointF& pos) const;
};
