#ifndef MYTHOPENGLPERF_H
#define MYTHOPENGLPERF_H

// Qt
#include <QVector>
#include <QOpenGLTimeMonitor>

// QOpenGLTimeMonitor is not available with Qt GLES2 builds
#if defined(QT_OPENGL_ES_2)

#ifndef GLuint64
typedef uint64_t GLuint64;
#endif

class QOpenGLTimeMonitor
{
  public:
    QOpenGLTimeMonitor() {};
    void setSampleCount(int) {};
    int  sampleCount() const { return 0; };
    bool create() { return false; };
    bool isCreated() const { return false; };
    int  recordSample() { return 0; };
    bool isResultAvailable() const { return false; };
    QVector<GLuint64> waitForIntervals() const { return QVector<GLuint64>(); };
    void reset() {};
};
#endif

// MythTV
#include "mythuiexp.h"

class MUI_PUBLIC MythOpenGLPerf : public QOpenGLTimeMonitor
{
  public:
    MythOpenGLPerf(const QString &Name, QVector<QString> Names, int SampleCount = 30);
    void RecordSample    (void);
    void LogSamples      (void);
    int  GetTimersRunning(void);

  private:
    QString m_name                 { };
    int  m_sampleCount             { 0 };
    int  m_totalSamples            { 30 };
    bool m_timersReady             { true };
    int  m_timersRunning           { 0 };
    QVector<GLuint64> m_timerData  { 0 };
    QVector<QString>  m_timerNames { };
};

#endif // MYTHOPENGLPERF_H
