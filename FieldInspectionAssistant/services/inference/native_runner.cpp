#include <QCoreApplication>
#include <QJsonDocument>
#include <QStringDecoder>
#include <QJsonObject>
#include <QFileInfo>
#include <QDateTime>
#include <csignal>
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>
#include "rkllm.h"
#include "image_enc.h"

using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }
static void event(QJsonObject o) { auto b=QJsonDocument(o).toJson(QJsonDocument::Compact); std::cout << "\n@@JSON@@" << b.constData() << std::endl; }
static volatile std::sig_atomic_t cancelled=0;
static void cancelSignal(int) { cancelled=1; }
struct State { Clock::time_point started; double ttft=-1; RKLLMPerfStat perf{}; bool failed=false; int callbacks=0; QStringDecoder decoder{QStringDecoder::Utf8}; };
static int result(RKLLMResult *r,void *data,LLMCallState s) {
    auto &st=*static_cast<State*>(data);
    if (s==RKLLM_RUN_ERROR) st.failed=true;
    if (r && s==RKLLM_RUN_FINISH) st.perf=r->perf;
    if (r && s==RKLLM_RUN_NORMAL) {
        ++st.callbacks;
        if (st.ttft<0) st.ttft=ms(st.started);
        if (r->text && *r->text) {QString decoded=st.decoder(QByteArrayView(r->text));if(!decoded.isEmpty())event({{"event","delta"},{"text",decoded}});}
    }
    return cancelled ? 1 : 0;
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    std::signal(SIGUSR1,cancelSignal);
    const QString dir="/userdata/models/";
    LLMHandle llm=nullptr;
    QString loaded;
    rknn_app_context_t vision{};
    bool visionLoaded=false;
    QString cachedImage;
    QString llmImageKey;
    std::vector<float> cachedEmbedding;
    std::string line;
    while (std::getline(std::cin,line)) {
        cancelled=0;
        QJsonParseError error;
        auto req=QJsonDocument::fromJson(QByteArray::fromStdString(line),&error).object();
        if(error.error!=QJsonParseError::NoError) { event({{"event","error"},{"error","Invalid JSON"}}); continue; }
        if(req["mode"]=="unload") {
            if(llm) rkllm_destroy(llm); llm=nullptr; loaded.clear();
            if(visionLoaded) release_imgenc(&vision); vision={}; visionLoaded=false;
            cachedImage.clear(); cachedEmbedding.clear();
            event({{"event","done"},{"text","Models unloaded"},{"metrics",QJsonObject{}}}); continue;
        }
        try {
            auto began=Clock::now();
            bool image=req["mode"]=="vision" || req["mode"]=="encoder";
            bool encoderOnly=req["mode"]=="encoder";
            double modelLoad=0,encoderLoad=0,encoderMs=0;
            bool cacheHit=false;
            if(!encoderOnly) {
                QString name=image?"qwen3-vl-4b-instruct_w8a8_rk3588.rkllm":"Qwen3-4B-rk3588-w8a8.rkllm";
                if(loaded!=name) {
                    if(llm) rkllm_destroy(llm); llm=nullptr; loaded.clear();
                    if(!QFileInfo::exists(dir+name)) throw std::runtime_error("Model file is not ready");
                    event({{"event","status"},{"text","正在加载 "+name}});
                    auto t=Clock::now();
                    auto p=rkllm_createDefaultParam(); auto path=(dir+name).toUtf8(); p.model_path=path.constData();
                    p.max_context_len=4096; p.max_new_tokens=4096; p.top_k=1; p.top_p=0.9; p.temperature=0.7;
                    p.skip_special_token=true; p.is_async=false;
                    // RKNN vision uses domain 0; reserve separate domains for RKLLM.
                    p.extend_param.base_domain_id=1; p.extend_param.enabled_cpus_num=4; p.extend_param.enabled_cpus_mask=0xf0;
                    RKLLMCallback cb{}; cb.result_callback=result;
                    if(rkllm_init(&llm,&p,&cb)!=0) { llm=nullptr; throw std::runtime_error("rkllm_init failed; inspect backend log"); }
                    loaded=name; llmImageKey.clear(); modelLoad=ms(t);
                }
            }
            std::vector<float> embeddings;
            if(image) {
                if(!visionLoaded) {
                    auto t=Clock::now();
                    auto path=(dir+"qwen3-vl-4b_vision_rk3588.rknn").toUtf8();
                    if(init_imgenc(path.constData(),&vision,3)!=0) throw std::runtime_error("Vision encoder initialization failed");
                    visionLoaded=true; encoderLoad=ms(t);
                }
                QFileInfo imageFile(req["path"].toString());
                QString imageKey=imageFile.canonicalFilePath()+":"+QString::number(imageFile.size())+":"+QString::number(imageFile.lastModified().toMSecsSinceEpoch());
                if(imageKey==cachedImage && !cachedEmbedding.empty()) {
                    embeddings=cachedEmbedding; cacheHit=true;
                } else {
                event({{"event","status"},{"text","正在处理图片"}});
                auto img=cv::imread(req["path"].toString().toStdString());
                if(img.empty()) throw std::runtime_error("Unable to read image");
                cv::cvtColor(img,img,cv::COLOR_BGR2RGB);
                int size=std::max(img.rows,img.cols);
                cv::Mat square(size,size,img.type(),cv::Scalar(127,127,127));
                img.copyTo(square(cv::Rect((size-img.cols)/2,(size-img.rows)/2,img.cols,img.rows)));
                cv::Mat resized; cv::resize(square,resized,cv::Size(vision.model_width,vision.model_height));
                embeddings.resize(vision.model_image_token*vision.model_embed_size*vision.io_num.n_output);
                auto t=Clock::now();
                if(run_imgenc(&vision,resized.data,embeddings.data())!=0) throw std::runtime_error("Image inference failed");
                encoderMs=ms(t);
                cachedImage=imageKey; cachedEmbedding=embeddings;
                }
            }
            if(encoderOnly) {
                event({{"event","done"},{"text",QString("图像编码成功：%1 tokens × %2 维 × %3 输出").arg(vision.model_image_token).arg(vision.model_embed_size).arg(vision.io_num.n_output)},
                    {"metrics",QJsonObject{{"model_load_ms",encoderLoad},{"encoder_ms",encoderMs},{"encoder_cache_hit",cacheHit},{"encoder_fps",encoderMs>0?1000.0/encoderMs:0.0},{"image_tokens",vision.model_image_token},{"total_ms",ms(began)}}}});
                continue;
            }
            // keep_history=0 lets RKLLM match and reuse the identical prompt prefix.
            // Each request remains independent; old answers are never appended to the prompt.
            if(image && llmImageKey!=cachedImage) { rkllm_clear_kv_cache(llm,0,nullptr,nullptr); llmImageKey=cachedImage; }
            auto prompt=req["question"].toString("请用中文简短回答。");
            if(!req["benchmark_raw"].toBool(false)) prompt="直接给出答案，省略思考过程和开场说明。用中文段落或简短列表，必要时用 Markdown。\n"+prompt;
            if(image) prompt="<image>"+prompt;
            auto utf8=prompt.toUtf8();
            RKLLMInput input{}; input.role="user"; input.enable_thinking=false;
            if(image) {
                input.input_type=RKLLM_INPUT_MULTIMODAL;
                auto &v=input.multimodal_input; v.prompt=utf8.data();
                v.image.image_embed=embeddings.data(); v.image.n_image_tokens=vision.model_image_token; v.image.n_image=1;
                v.image.image_start="<|vision_start|>"; v.image.image_end="<|vision_end|>"; v.image.image_content="<|image_pad|>";
                v.image.image_width=vision.model_width; v.image.image_height=vision.model_height;
            } else { input.input_type=RKLLM_INPUT_PROMPT; input.prompt_input=utf8.constData(); }
            RKLLMInferParam infer{}; infer.mode=RKLLM_INFER_GENERATE; infer.keep_history=0;
            infer.max_new_tokens=std::max(8,std::min(4096,req["max_tokens"].toInt(256)));
            State state{}; state.started=Clock::now();
            event({{"event","status"},{"text","正在推理"}});
            int ret=rkllm_run(llm,&input,&infer,&state);
            double inferenceMs=ms(state.started);
            if((ret || state.failed) && !cancelled) throw std::runtime_error("RKLLM inference failed");
            auto &p=state.perf;
            QJsonObject metrics{{"model_load_ms",modelLoad+encoderLoad},{"encoder_ms",encoderMs},{"llm_ttft_ms",state.ttft},
                {"ttft_ms",state.ttft<0?-1:ms(began)-inferenceMs+state.ttft},{"encoder_cache_hit",cacheHit},{"prefill_tokens",p.prefill_tokens},{"prefill_ms",p.prefill_time_ms},
                {"decode_tokens",p.generate_tokens},{"decode_ms",p.generate_time_ms},{"inference_ms",inferenceMs},
                {"total_ms",ms(began)},{"memory_mb",p.memory_usage_mb},
                {"prefill_tps",p.prefill_time_ms>0?1000.0*p.prefill_tokens/p.prefill_time_ms:0.0},
                {"decode_tps",p.generate_time_ms>0?1000.0*p.generate_tokens/p.generate_time_ms:0.0}};
            if(cancelled) for(const auto &key:{"decode_ms","decode_tokens","decode_tps","prefill_ms","prefill_tokens","prefill_tps","memory_mb"}) metrics.remove(key);
            event({{"event","done"},{"cancelled",bool(cancelled)},{"metrics",metrics}});
            if(cancelled) rkllm_clear_kv_cache(llm,0,nullptr,nullptr);
        } catch(const std::exception &e) { event({{"event","error"},{"error",QString::fromUtf8(e.what())}}); }
    }
    if(llm) rkllm_destroy(llm);
    if(visionLoaded) release_imgenc(&vision);
}
