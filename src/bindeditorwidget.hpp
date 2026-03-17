#pragma once
#include <QWidget>
#include <QList>
#include "ds5_report.hpp"
#include "bindstorage.hpp"

class GamepadWidget;
class QCheckBox;
class QPushButton;
class QLabel;
class QHBoxLayout;
class QPropertyAnimation;

class BindEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit BindEditorWidget(QWidget* parent = nullptr);

    std::vector<ds5::ButtonBinding> activeBindings() const;
    void setRunning(bool running);
    void retranslate(bool ru);

signals:
    void bindingsChanged(); // emitted whenever the active binding set changes

private slots:
    void on_use_bindings_toggled(bool checked);
    void on_set_bind_clicked();
    void on_apply_clicked();
    void on_trigger_selected(ds5::ButtonBit btn, const QString& name);
    void on_action_toggled(ds5::ButtonBit btn, const QString& name, bool added);

private:
    enum class EditorState { IDLE, SELECT_TRIGGER, SELECT_ACTIONS };

    void rebuild_list();
    void set_editor_state(EditorState state);
    void save_binds();
    void update_status_text();

    QCheckBox*          use_bindings_cb_ = nullptr;
    QWidget*            content_         = nullptr;
    GamepadWidget*      gamepad_         = nullptr;
    QLabel*             status_label_    = nullptr;
    QPushButton*        set_bind_btn_    = nullptr;
    QPushButton*        apply_btn_       = nullptr;
    QWidget*            list_area_       = nullptr;
    QHBoxLayout*        list_layout_     = nullptr;
    QPropertyAnimation* anim_            = nullptr;

    EditorState                    editor_state_ = EditorState::IDLE;
    bool                           running_      = false;
    bool                           lang_ru_      = true;
    QList<BindStorage::BindRecord> binds_;

    // Pending bind being constructed
    QString     pending_trigger_name_;
    ds5::ButtonBit  pending_trigger_btn_ = {0, 0};
    QStringList pending_action_names_;
};
