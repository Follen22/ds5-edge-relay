#include "gamepadwidget.hpp"
#include <QPainter>
#include <QMouseEvent>

namespace {
QVector<GamepadWidget::Zone> make_zones()
{
    // Exact coordinates measured from 1.xcf red zones.
    // rect = {cx - w/2, cy - h/2, w, h}  (normalized 0..1, canvas 1494x1128)
    return {
        { ds5::BTN_L2,         "L2",        {0.178, 0.031, 0.091, 0.126} },
        { ds5::BTN_R2,         "R2",        {0.730, 0.026, 0.101, 0.129} },
        { ds5::BTN_L1,         "L1",        {0.164, 0.162, 0.117, 0.062} },
        { ds5::BTN_R1,         "R1",        {0.710, 0.166, 0.125, 0.060} },
        { ds5::BTN_DPAD_UP,    "DPadUp",    {0.196, 0.299, 0.057, 0.066} },
        { ds5::BTN_CREATE,     "Create",    {0.275, 0.240, 0.031, 0.061} },
        { ds5::BTN_OPTIONS,    "Options",   {0.690, 0.240, 0.035, 0.066} },
        { ds5::BTN_TRIANGLE,   "Triangle",  {0.740, 0.267, 0.066, 0.090} },
        { ds5::BTN_DPAD_LEFT,  "DPadLeft",  {0.146, 0.360, 0.068, 0.074} },
        { ds5::BTN_DPAD_RIGHT, "DPadRight", {0.230, 0.359, 0.066, 0.079} },
        { ds5::BTN_SQUARE,     "Square",    {0.683, 0.355, 0.053, 0.078} },
        { ds5::BTN_CIRCLE,     "Circle",    {0.804, 0.355, 0.061, 0.082} },
        { ds5::BTN_DPAD_DOWN,  "DPadDown",  {0.199, 0.419, 0.050, 0.074} },
        { ds5::BTN_CROSS,      "Cross",     {0.743, 0.445, 0.059, 0.079} },
        { ds5::BTN_L3,         "L3",        {0.306, 0.493, 0.093, 0.128} },
        { ds5::BTN_R3,         "R3",        {0.594, 0.494, 0.094, 0.121} },
        { ds5::BTN_LFN,        "LFN",       {0.320, 0.671, 0.068, 0.060} },
        { ds5::BTN_RFN,        "RFN",       {0.603, 0.683, 0.073, 0.048} },
        { ds5::BTN_LB,         "LB",        {0.275, 0.826, 0.098, 0.134} },
        { ds5::BTN_RB,         "RB",        {0.624, 0.829, 0.088, 0.122} },
    };
}
} // namespace

GamepadWidget::GamepadWidget(QWidget* parent)
    : QWidget(parent)
    , zones_(make_zones())
{
    pixmap_.load(":/1.png");
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize GamepadWidget::sizeHint() const
{
    if (!pixmap_.isNull()) {
        const int w = 460;
        const int h = qRound(static_cast<double>(pixmap_.height()) * w / pixmap_.width());
        return {w, h};
    }
    return {460, 260};
}

QSize GamepadWidget::minimumSizeHint() const
{
    return {200, 110};
}

QRectF GamepadWidget::imageRect() const
{
    if (pixmap_.isNull()) return QRectF(rect());
    const QSizeF s = QSizeF(pixmap_.size()).scaled(size(), Qt::KeepAspectRatio);
    const double x = (width()  - s.width())  / 2.0;
    const double y = (height() - s.height()) / 2.0;
    return {x, y, s.width(), s.height()};
}

int GamepadWidget::zoneAt(const QPointF& pos) const
{
    const QRectF ir = imageRect();
    if (!ir.contains(pos)) return -1;
    const double nx = (pos.x() - ir.left())  / ir.width();
    const double ny = (pos.y() - ir.top())   / ir.height();
    // Iterate in reverse so later-defined zones (face buttons) win over earlier (touchpad)
    for (int i = zones_.size() - 1; i >= 0; --i) {
        if (zones_[i].rect.contains(nx, ny)) return i;
    }
    return -1;
}

void GamepadWidget::setMode(Mode m)
{
    mode_ = m;
    update();
}

void GamepadWidget::clearSelection()
{
    trigger_idx_ = -1;
    action_idxs_.clear();
    trigger_btn_ = {0, 0};
    action_btns_.clear();
    update();
}

void GamepadWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF ir = imageRect();

    if (pixmap_.isNull()) {
        p.fillRect(rect(), palette().color(QPalette::Window));
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, "1.png not found");
    } else {
        // Fill image area with GUI background, then overlay the pixmap using
        // Screen blend: black pixels disappear into the background, light lines
        // remain — the gamepad looks drawn in the app's own color scheme.
        p.fillRect(ir.toRect(), palette().color(QPalette::Window));
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        p.drawPixmap(ir.toRect(), pixmap_);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    for (int i = 0; i < zones_.size(); ++i) {
        const Zone& z         = zones_[i];
        const bool  is_trigger = (i == trigger_idx_);
        const bool  is_action  = action_idxs_.contains(i);
        const bool  is_hover   = (i == hover_idx_);

        QColor col;
        if (is_trigger)
            col = QColor(231, 76, 60, is_hover ? 130 : 100);
        else if (is_action)
            col = QColor(52, 152, 219, is_hover ? 130 : 100);
        else if (is_hover && mode_ != Mode::IDLE)
            col = QColor(200, 200, 200, 80);
        else
            continue;

        const QRectF pr = {
            ir.left() + z.rect.x()      * ir.width(),
            ir.top()  + z.rect.y()      * ir.height(),
            z.rect.width()              * ir.width(),
            z.rect.height()             * ir.height()
        };

        p.setBrush(col);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(pr, 4, 4);

#ifdef QT_DEBUG
        p.setPen(QPen(col.darker(130), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(pr);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::NoBrush);
#endif
    }
}

void GamepadWidget::mouseMoveEvent(QMouseEvent* event)
{
    const int idx = zoneAt(event->position());
    if (idx != hover_idx_) {
        hover_idx_ = idx;
        update();
    }
}

void GamepadWidget::leaveEvent(QEvent*)
{
    hover_idx_ = -1;
    update();
}

void GamepadWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (mode_ == Mode::IDLE) return;

    const int idx = zoneAt(event->position());
    if (idx < 0) return;

    const Zone& z = zones_[idx];

    if (mode_ == Mode::SELECT_TRIGGER) {
        trigger_idx_ = idx;
        trigger_btn_ = z.btn;
        emit triggerSelected(z.btn, z.name);
        update();
    } else { // SELECT_ACTIONS
        const int pos = action_idxs_.indexOf(idx);
        if (pos >= 0) {
            action_idxs_.removeAt(pos);
            action_btns_.removeAt(pos);
            emit actionToggled(z.btn, z.name, false);
        } else {
            action_idxs_.append(idx);
            action_btns_.append(z.btn);
            emit actionToggled(z.btn, z.name, true);
        }
        update();
    }
}
