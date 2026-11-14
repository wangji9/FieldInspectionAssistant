#include "NativeCameraPipeline.h"
#include <QImageReader>
#include <QTransform>
#include <QSaveFile>
#include <QStorageInfo>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <time.h>

namespace {
struct Mapping {void *data=nullptr;size_t size=0;};
class Device {
public:
    int fd=-1;QVector<Mapping> buffers;bool streaming=false;
    ~Device(){if(fd>=0){if(streaming){v4l2_buf_type type=V4L2_BUF_TYPE_VIDEO_CAPTURE;ioctl(fd,VIDIOC_STREAMOFF,&type);}for(auto b:buffers)if(b.data&&b.data!=MAP_FAILED)munmap(b.data,b.size);::close(fd);}}
};
QImage transformed(QImage image,int rotation,bool mirrored){if(mirrored)image=image.mirrored(true,false);if(rotation)image=image.transformed(QTransform().rotate(rotation));return image;}

qint64 monotonicMs()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return qint64(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

qint64 captureTimestamp(const v4l2_buffer &buffer, qint64 epochOffset)
{
    const auto received = QDateTime::currentMSecsSinceEpoch();
    if ((buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
        const auto captured = epochOffset + qint64(buffer.timestamp.tv_sec) * 1000 + buffer.timestamp.tv_usec / 1000;
        if (captured > 0 && captured <= received + 5 && received - captured < 5000)
            return captured;
    }
    return received;
}
}
QByteArray cameraJpeg(const std::shared_ptr<CameraFrameStore>&store,qint64 *capturedMs){
    if(!store)return {};
    QMutexLocker lock(&store->mutex);
    if(capturedMs)*capturedMs=store->jpegCapturedMs;
    return store->jpeg;
}
bool saveCameraPhoto(const QByteArray&jpeg,const QString&path,const QByteArray&format,int quality,int rotation,bool mirrored){
    auto image=QImage::fromData(jpeg,"JPG");if(image.isNull())return false;image=transformed(image,rotation,mirrored);
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)&&image.save(&file,format.constData(),qBound(1,quality,100))&&file.commit();
}
NativeCameraCapture::NativeCameraCapture(const QString&device,int width,int height,int fps,QObject*p):QThread(p),device_(device.section('(',0,0).trimmed()),store_(std::make_shared<CameraFrameStore>()){
    store_->width=width;store_->height=height;store_->fps=fps;
}
NativeCameraCapture::~NativeCameraCapture(){requestInterruption();wait();}
void NativeCameraCapture::run(){
    auto fail=[this](const QString&e){QMutexLocker lock(&store_->mutex);store_->error=e;};Device device;
    device.fd=::open(device_.toLocal8Bit().constData(),O_RDWR|O_NONBLOCK);
    if(device.fd<0){fail(QStringLiteral("无法打开摄像头：")+QString::fromLocal8Bit(strerror(errno)));return;}
    v4l2_capability cap{};if(ioctl(device.fd,VIDIOC_QUERYCAP,&cap)<0){fail("设备不支持 V4L2");return;}
    auto caps=cap.device_caps?cap.device_caps:cap.capabilities;
    if(!(caps&V4L2_CAP_VIDEO_CAPTURE)||!(caps&V4L2_CAP_STREAMING)){fail("请选择支持视频采集的摄像头接口");return;}
    v4l2_format format{};format.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;format.fmt.pix.width=store_->width;format.fmt.pix.height=store_->height;format.fmt.pix.pixelformat=V4L2_PIX_FMT_MJPEG;format.fmt.pix.field=V4L2_FIELD_NONE;
    if(ioctl(device.fd,VIDIOC_S_FMT,&format)<0||format.fmt.pix.pixelformat!=V4L2_PIX_FMT_MJPEG){fail("所选设备不支持 MJPEG，请选择 USB 摄像头");return;}
    v4l2_streamparm parm{};parm.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;parm.parm.capture.timeperframe.numerator=1;parm.parm.capture.timeperframe.denominator=store_->fps;
    ioctl(device.fd,VIDIOC_S_PARM,&parm);
    {QMutexLocker lock(&store_->mutex);store_->width=format.fmt.pix.width;store_->height=format.fmt.pix.height;if(parm.parm.capture.timeperframe.numerator)store_->fps=qMax(1,int(parm.parm.capture.timeperframe.denominator/parm.parm.capture.timeperframe.numerator));}
    v4l2_requestbuffers request{};request.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;request.memory=V4L2_MEMORY_MMAP;request.count=4;
    if(ioctl(device.fd,VIDIOC_REQBUFS,&request)<0||request.count<2){fail("申请摄像头缓冲区失败");return;}
    for(unsigned i=0;i<request.count;i++){
        v4l2_buffer b{};b.type=request.type;b.memory=request.memory;b.index=i;
        if(ioctl(device.fd,VIDIOC_QUERYBUF,&b)<0){fail("查询摄像头缓冲区失败");return;}
        auto data=mmap(nullptr,b.length,PROT_READ|PROT_WRITE,MAP_SHARED,device.fd,b.m.offset);device.buffers.append({data,b.length});
        if(data==MAP_FAILED||ioctl(device.fd,VIDIOC_QBUF,&b)<0){fail("摄像头缓冲区映射或入队失败");return;}
    }
    v4l2_buf_type type=V4L2_BUF_TYPE_VIDEO_CAPTURE;if(ioctl(device.fd,VIDIOC_STREAMON,&type)<0){fail("摄像头启动失败");return;}device.streaming=true;
    {QMutexLocker lock(&store_->mutex);store_->opened=true;}
    QByteArray pendingJpeg;
    qint64 pendingTimestamp = 0;
    bool decoderStopping = false;
    QWaitCondition pendingReady;
    // The capture thread never waits for JPEG decoding. The pending slot keeps
    // only the most recent compressed frame if the decoder is still busy.
    std::thread decoder([&] {
        QElapsedTimer rateClock;
        rateClock.start();
        quint64 rateFrames = 0;
        while (true) {
            QByteArray jpeg;
            qint64 capturedMs;
            QSize limit;
            int rotation;
            bool mirrored;
            {
                QMutexLocker lock(&store_->mutex);
                while (pendingJpeg.isEmpty() && !decoderStopping)
                    pendingReady.wait(&store_->mutex);
                if (decoderStopping)
                    break;
                jpeg.swap(pendingJpeg);
                capturedMs = pendingTimestamp;
                limit = store_->previewLimit;
                rotation = store_->rotation;
                mirrored = store_->mirrored;
            }
            QElapsedTimer cost;
            cost.start();
            QBuffer input(&jpeg);
            input.open(QIODevice::ReadOnly);
            QImageReader reader(&input, "JPG");
            if (limit.isValid()) {
                const QSize source = reader.size();
                if (rotation == 90 || rotation == 270)
                    limit.transpose();
                if (source.isValid()) {
                    const auto target = source.scaled(limit, Qt::KeepAspectRatio);
                    if (target.width() < source.width())
                        reader.setScaledSize(target);
                }
            }
            auto image = reader.read();
            if (image.isNull()) {
                QMutexLocker lock(&store_->mutex);
                ++store_->invalid;
                continue;
            }
            image = transformed(std::move(image), rotation, mirrored).convertToFormat(QImage::Format_RGB32);
            ++rateFrames;
            bool notify;
            {
                QMutexLocker lock(&store_->mutex);
                if (decoderStopping)
                    break;
                store_->preview = std::move(image);
                store_->previewCapturedMs = capturedMs;
                store_->previewRotation = rotation;
                store_->previewMirrored = mirrored;
                ++store_->decoded;
                ++store_->sequence;
                store_->decodeMs = cost.nsecsElapsed() / 1e6;
                if (rateClock.elapsed() >= 1000) {
                    store_->actualFps = rateFrames * 1000. / rateClock.elapsed();
                    rateFrames = 0;
                    rateClock.restart();
                }
                notify = store_->notifyPreview && !store_->previewNotificationPending;
                if (notify)
                    store_->previewNotificationPending = true;
            }
            if (notify)
                emit previewAvailable();
        }
    });
    const auto epochOffset = QDateTime::currentMSecsSinceEpoch() - monotonicMs();
    int missing = 0;
    while(!isInterruptionRequested()){
        pollfd fd{device.fd,POLLIN,0};int ready=poll(&fd,1,100);
        if(ready<0){if(errno==EINTR)continue;fail("等待摄像头画面失败");break;}
        if(!ready){if(++missing>=50){fail("摄像头超过 5 秒未返回画面，请重新打开");break;}continue;}missing=0;
        if(fd.revents&(POLLERR|POLLHUP|POLLNVAL)){fail("摄像头已断开，请重新连接");break;}
        v4l2_buffer latest{};
        int drained = 0, invalid = 0;
        auto requeue = [&](v4l2_buffer &buffer) {
            if (ioctl(device.fd, VIDIOC_QBUF, &buffer) == 0)
                return true;
            fail("摄像头帧缓冲区回收失败");
            requestInterruption();
            return false;
        };
        for(unsigned i=0;i<request.count;i++){
            v4l2_buffer b{};b.type=type;b.memory=V4L2_MEMORY_MMAP;
            if(ioctl(device.fd,VIDIOC_DQBUF,&b)<0){if(errno==EAGAIN)break;if(errno==EINTR)continue;fail("读取摄像头帧失败");requestInterruption();break;}
            bool valid=b.index<unsigned(device.buffers.size())&&b.bytesused>0&&b.bytesused<=device.buffers[b.index].size&&!(b.flags&V4L2_BUF_FLAG_ERROR);
            if (valid) {
                if (drained && !requeue(latest))
                    break;
                latest = b;
                ++drained;
            } else {
                ++invalid;
                if (!requeue(b))
                    break;
            }
        }
        if (isInterruptionRequested())
            break;
        QByteArray jpeg;
        qint64 timestamp = 0;
        if (drained) {
            jpeg = QByteArray(static_cast<const char *>(device.buffers[latest.index].data), int(latest.bytesused));
            timestamp = captureTimestamp(latest, epochOffset);
            if (!requeue(latest))
                break;
        }
        QMutexLocker lock(&store_->mutex);
        store_->invalid += invalid;
        if (jpeg.isEmpty())
            continue;
        store_->jpeg = jpeg;
        store_->jpegCapturedMs = timestamp;
        store_->captured += drained;
        store_->stale += drained - 1 + !pendingJpeg.isEmpty();
        pendingJpeg = std::move(jpeg);
        pendingTimestamp = timestamp;
        pendingReady.wakeOne();
    }
    {
        QMutexLocker lock(&store_->mutex);
        store_->opened = false;
        decoderStopping = true;
        pendingReady.wakeOne();
    }
    decoder.join();
}

NativeCameraRecorder::NativeCameraRecorder(std::shared_ptr<CameraFrameStore>store,const QString&target,int fps,int seconds,int quality,int rotation,bool mirrored,QObject*p)
 :QThread(p),store_(std::move(store)),target_(target),fps_(fps),seconds_(seconds),quality_(quality),rotation_(rotation),mirrored_(mirrored){}
NativeCameraRecorder::~NativeCameraRecorder(){requestInterruption();wait();}
QVariantMap NativeCameraRecorder::result()const{QMutexLocker lock(&mutex_);return result_;}
void NativeCameraRecorder::run(){
    QString temp=target_+".partial.mp4",error;int written=0;qint64 firstFrameMs=0;QProcess encoder;
    QStringList args{"-hide_banner","-loglevel","error","-nostdin","-n","-threads","2","-f","image2pipe","-vcodec","mjpeg","-framerate",QString::number(fps_),"-i","pipe:0","-an"};
    QStringList filters;if(mirrored_)filters<<"hflip";if(rotation_==90)filters<<"transpose=clock";else if(rotation_==180)filters<<"hflip"<<"vflip";else if(rotation_==270)filters<<"transpose=cclock";
    if(!filters.isEmpty())args<<"-vf"<<filters.join(',');args<<"-c:v"<<"mpeg4"<<"-threads"<<"2"<<"-q:v"<<QString::number(quality_)<<"-pix_fmt"<<"yuv420p"<<"-movflags"<<"+faststart"<<temp;
    encoder.start("ffmpeg",args);if(!encoder.waitForStarted(4000)){QMutexLocker lock(&mutex_);result_={{"ok",false},{"error","无法启动录像编码器"}};return;}
    QElapsedTimer clock,storage;clock.start();storage.start();
    while(!isInterruptionRequested()&&clock.elapsed()<seconds_*1000){
        if(encoder.state()!=QProcess::Running){error=QString::fromUtf8(encoder.readAllStandardError()).right(1000);break;}
        auto jpeg=cameraJpeg(store_,written ? nullptr : &firstFrameMs);if(jpeg.isEmpty()){msleep(10);continue;}
        if(encoder.write(jpeg)<0){error="写入录像编码器失败";break;}
        QElapsedTimer drain;drain.start();while(encoder.bytesToWrite()>0){if(!encoder.waitForBytesWritten(200)&&encoder.state()!=QProcess::Running)break;if(drain.elapsed()>1500){error="编码速度不足，已停止并保存当前录像";break;}}
        if(!error.isEmpty())break;written++;
        {QMutexLocker lock(&mutex_);result_={{"frames",written},{"duration",clock.elapsed()/1000.},{"fps",fps_}};}
        if(storage.elapsed()>=2000){storage.restart();if(QStorageInfo(QFileInfo(target_).absolutePath()).bytesAvailable()<20*1024*1024){error="存储空间不足，录像已停止";break;}}
        auto next=qint64(written)*1000/fps_;if(next>clock.elapsed())msleep(next-clock.elapsed());
    }
    encoder.closeWriteChannel();if(!encoder.waitForFinished(15000)){encoder.kill();encoder.waitForFinished(2000);error="录像编码保存超时";}
    bool ok=encoder.exitStatus()==QProcess::NormalExit&&encoder.exitCode()==0&&written>0&&QFileInfo(temp).size()>128&&QFile::rename(temp,target_);
    if(!ok){if(error.isEmpty())error=QString::fromUtf8(encoder.readAllStandardError()).right(1000);QFile::remove(temp);}
    QMutexLocker lock(&mutex_);result_={{"ok",ok},{"error",error},{"path",target_},{"frames",written},
        {"duration",double(written)/fps_},{"fps",fps_},{"captured_ms",firstFrameMs}};
}
