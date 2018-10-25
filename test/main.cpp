#include <QApplication>
#include <iostream>
#include "ff_player.h"

using namespace FFPlayer;

#define Enable_Stack_Trace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ff_player player(app, "file.mp4");
    player.play();
    return app.exec();
}
