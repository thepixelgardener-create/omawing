#include <QGuiApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cstdio>
#include "scene.h"
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QSurfaceFormat format; format.setSamples(4);
    QOpenGLContext context; context.setFormat(format);
    if (!context.create()) return 2;
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    if (!context.makeCurrent(&surface)) return 3;
    for (const QSize size : {QSize(1920,1080), QSize(3840,2160)}) {
        QOpenGLFramebufferObjectFormat fbFormat; fbFormat.setSamples(4);
        fbFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject fbo(size, fbFormat); fbo.bind();
        QOpenGLPaintDevice device(size);
        Scene scene;
        Flight flight;
        Scene::Hud hud{"YOU HAVE THE CONTROLS", "FREE FLIGHT · 02 / 24", "THRUST", "ALT 04.2", "Flying into the future", "HULL", "SCORE", "001350", "BEST 001350"};
        std::vector<double> times;
        for(int act=0; act<Flight::Acts; ++act) {
            flight.scene(act);
            for(int i=0;i<45;++i) {
                flight.advance(1.0/60);
                QElapsedTimer timer;timer.start();
                { QPainter p(&device); scene.paint(p,flight,size,hud); }
                context.functions()->glFinish();
                if(i>=5) times.push_back(timer.nsecsElapsed()/1e6);
            }
        }
        double sum=0;for(double t:times)sum+=t;
        std::sort(times.begin(),times.end());
        std::printf("%dx%d: mean %.3f ms, median %.3f ms, p95 %.3f ms\n",size.width(),size.height(),sum/times.size(),times[times.size()/2],times[times.size()*95/100]);
    }
}
