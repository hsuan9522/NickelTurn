#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

class NickelTurnChapterButtonRoute : public QObject {
    Q_OBJECT

public:
    NickelTurnChapterButtonRoute(void *controller, QWidget *reading_view, QObject *parent);

private slots:
    void leftTapped(bool checked);
    void rightTapped(bool checked);

private:
    void dispatch(bool from_left);
    void *m_controller;
    QPointer<QWidget> m_reading_view;
};
