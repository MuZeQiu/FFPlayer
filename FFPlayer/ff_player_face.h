#ifndef FF_PLAYER_FACE_H
#define FF_PLAYER_FACE_H

#include <iostream>
#include <QApplication>
#include <QDesktopWidget>
#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include "ff_player_base.h"

namespace FFPlayer {
class ff_player;
class ff_player_face: private QLabel {
public:
    friend ff_player;
    explicit ff_player_face(ff_player_base *fpb):
        QLabel(Q_NULLPTR, Qt::Window),
        fpb_(fpb),
        app_(fpb->get_app()),
        screen_width_(app_.desktop()->availableGeometry().width()),
        screen_height_(app_.desktop()->availableGeometry().height()),
        dest_width_(fpb->get_dest_width()),
        dest_height_(fpb->get_dest_height()+100),
        content_face_(this, 0, 0, dest_width_, dest_height_-100),
        player_but_(this, (dest_width_-60)/2, dest_height_-70, 60, 60),
        player_slider_(this, 0, dest_height_-100, dest_width_, 20),
        player_next_but_(this, (dest_width_-60)/2+90, dest_height_-70, 60, 60),
        player_last_but_(this, (dest_width_-60)/2-90, dest_height_-70, 60, 60) {
        assert(dest_width_ != screen_width_);
        assert(dest_height_ != screen_height_);
        resize(dest_width_, dest_height_);
        set_action();
        show();
    }

    ~ff_player_face() {}

    void set_action() {
        content_face_.dbl_click_cb_ = [this](QWidget*){
            if (player_but_.text() == "Start") {
                fpb_->player_start();
                player_but_.setText("Pause");
            } else {
                fpb_->player_pause();
                player_but_.setText("Start");
            }
        };
        player_but_.press_cb_ = [this](QWidget*){
            if (player_but_.text() == "Start") {
                fpb_->player_start();
                player_but_.setText("Pause");
            } else {
                fpb_->player_pause();
                player_but_.setText("Start");
            }
        };
        player_but_.double_press_cb_ = [this](QWidget*){
            if (player_but_.text() == "Start") {
                fpb_->player_start();
                player_but_.setText("Pause");
            } else {
                fpb_->player_pause();
                player_but_.setText("Start");
            }
        };
        player_slider_.value_change_cb_ = [this](QWidget*, int pos){
            fpb_->player_slide(pos);
        };
        player_slider_.slider_release_cb_ = [this](QWidget*) {
            fpb_->slider_release();
        };
        player_next_but_.press_cb_ = [this](QWidget*){
            fpb_->player_next("");
        };
        player_next_but_.double_press_cb_ = [this](QWidget*){
            fpb_->player_next("");
        };
        player_last_but_.press_cb_ = [this](QWidget*){
            fpb_->player_last("");
        };
        player_last_but_.double_press_cb_ = [this](QWidget*){
            fpb_->player_last("");
        };
    }

protected:
      bool event(QEvent *e) override {
        if (e->type() == QEvent::Type::Resize) {
            fpb_->player_resize();
            dest_width_ = width();
            dest_height_ = height();
            content_face_.setGeometry(0, 0, dest_width_, dest_height_-100);
            player_but_.setGeometry((dest_width_-60)/2, dest_height_-70, 60, 60);
            player_slider_.setGeometry(0, dest_height_-100, dest_width_, 20);
            player_next_but_.setGeometry((dest_width_-60)/2+90, dest_height_-70, 60, 60);
            player_last_but_.setGeometry((dest_width_-60)/2-90, dest_height_-70, 60, 60);
        }
        if (e->type() == QEvent::Type::Close) {
            fpb_->player_close();
            exit(0);
        }
        return QLabel::event(e);
    }

private:
    class ff_player_content_face: public QLabel {
    public:
        friend ff_player_face;
        ff_player_content_face(QWidget *parent,
                               unsigned int x,
                               unsigned int y,
                               unsigned int width,
                               unsigned int height):
            QLabel(parent) {
            assert(parent);
            setGeometry(x, y, width, height);
            setScaledContents(true);
        }
        ~ff_player_content_face(){};
        void set_style_sheet(const unsigned int width,\
                             const std::tuple<unsigned int, unsigned int, unsigned int>& rgb,\
                             char* border_style = "solid") {
            assert(std::get<0>(rgb)<256&&std::get<1>(rgb)<256&&std::get<2>(rgb)<256);
            QString style_sheet = "border-width:";
            style_sheet += QString::number(width);
            style_sheet += QString("px;");
            style_sheet += QString("border-color:rgb");
            style_sheet += QString("(")+QString::number(std::get<0>(rgb))+QString(",")+\
                    QString::number(std::get<1>(rgb))+QString(",")+\
                    QString::number(std::get<2>(rgb))+QString(");");
            style_sheet += QString("border-style:") + QString(border_style) + QString(";");
            this->setStyleSheet(style_sheet);
        }
    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(dbl_click_cb_);
                dbl_click_cb_(this);
            }
            return QLabel::event(e);
        }
    private:
        std::function<void (QWidget *)> dbl_click_cb_;
    };
    class ff_player_but: public QPushButton {
    public:
        friend ff_player_face;
        ff_player_but(QWidget *parent,
                               unsigned int x,
                               unsigned int y,
                               unsigned int width,
                               unsigned int height):QPushButton(parent) {
            assert(parent);
            this->setGeometry(170, 330, 60, 60);
            this->setText("Pause");
        }
        ~ff_player_but(){};

    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonPress) {
                assert(press_cb_);
                press_cb_(this);
            }
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(double_press_cb_);
                double_press_cb_(this);
            }
            return QPushButton::event(e);
        }
    private:
        std::function<void (QWidget *)> press_cb_;
        std::function<void (QWidget *)> double_press_cb_;
    };
    class ff_player_next_but: public QPushButton {
    public:
        friend ff_player_face;
        ff_player_next_but(QWidget *parent,
                           unsigned int x,
                           unsigned int y,
                           unsigned int width,
                           unsigned int height):QPushButton(parent) {
            assert(parent);
            this->setGeometry(x, y, width, height);
            this->setText("Next");
        }
        ~ff_player_next_but(){};

    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonPress) {
                assert(press_cb_);
                press_cb_(this);
            }
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(double_press_cb_);
                double_press_cb_(this);
            }
            return QPushButton::event(e);
        }
    private:
        std::function<void (QWidget *)> press_cb_;
        std::function<void (QWidget *)> double_press_cb_;
    };
    class ff_player_last_but: public QPushButton {
    public:
        friend ff_player_face;
        ff_player_last_but(QWidget *parent,
                           unsigned int x,
                           unsigned int y,
                           unsigned int width,
                           unsigned int height):QPushButton(parent) {
            assert(parent);
            setGeometry(x, y, width, height);
            setText("Last");

        }
        ~ff_player_last_but(){};

    protected:
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Type::MouseButtonPress) {
                assert(press_cb_);
                press_cb_(this);
            }
            if (e->type() == QEvent::Type::MouseButtonDblClick) {
                assert(double_press_cb_);
                double_press_cb_(this);
            }
            return QPushButton::event(e);
        }
    private:
        std::function<void (QWidget *)> press_cb_;
        std::function<void (QWidget *)> double_press_cb_;
    };
    class ff_player_slider: public QSlider {
        friend ff_player_face;

    public:
        ff_player_slider(QWidget *parent,
                         unsigned int x,
                         unsigned int y,
                         unsigned int width,
                         unsigned int height):
            QSlider(Qt::Orientation::Horizontal, parent){
            assert(parent);
            setGeometry(x, y, width, height);
            setMinimum(1);
            setMaximum(100);
            setSliderPosition(1);
        }
        ~ff_player_slider(){}

    protected:
        bool event(QEvent *e) {
            if (e->type() == QEvent::MouseButtonRelease) {
                if (slider_release_cb_) slider_release_cb_(this);
            }
            return QSlider::event(e);
        }

        virtual void sliderChange(SliderChange change) override {
            if (change == SliderChange::SliderValueChange) {
                if (isSliderDown()) {
                    if (value_change_cb_) value_change_cb_(this, sliderPosition());
                }
            }
            QSlider::sliderChange(change);
        }
    private:
        std::function<void (QWidget *, int)> value_change_cb_;
        std::function<void (QWidget *)> slider_release_cb_;
    };
    void get_screen_size() {
        QDesktopWidget* desktop_widget = QApplication::desktop();
        QRect desk_rect = desktop_widget->availableGeometry();
        screen_width_ = desk_rect.width();
        screen_height_ = desk_rect.height();
    }

    ff_player_base *fpb_;
    QApplication& app_;
    ff_player_content_face content_face_;
    ff_player_but player_but_;
    ff_player_next_but player_next_but_;
    ff_player_last_but player_last_but_;
    ff_player_slider player_slider_;
    unsigned int screen_width_;
    unsigned int screen_height_;
    unsigned int dest_width_;
    unsigned int dest_height_;
};
}

#endif // FF_PLAYER_FACE_H
