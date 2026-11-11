"""Build additional resident targets from instrumented, pinned upstream demos.

Run after patch_rk1828_demos.py and patch_asr_metrics.py. Original CLI targets
remain available for baseline comparisons and do not implement model caching.
"""
from pathlib import Path
import shutil

ROOT=Path('/userdata/ai-workbench')
SDK=ROOT/'vendor/rknn3-model-zoo/examples'
for name,online in [('Qwen3_5',False),('Qwen2_5_VL',False),('Qwen3_ASR',False),('Qwen3_ASR',True)]:
    folder=SDK/name/'cpp'
    source=folder/('online_main.cc' if online else 'main.cc')
    s=source.read_text()
    # Decode across token boundaries: a Chinese UTF-8 character may span tokens.
    if name in ('Qwen3_5','Qwen2_5_VL') and 'QStringDecoder fieldops_decoder' not in s:
        s=s.replace('#include <QJsonObject>','#include <QJsonObject>\n#include <QStringDecoder>')
        s=s.replace('static void fieldops_delta(', 'static QStringDecoder fieldops_decoder(QStringDecoder::Utf8);\nstatic void fieldops_delta(')
        s=s.replace('    QJsonObject o{{"event", "delta"}, {"text", QString::fromUtf8(piece.data(), piece.size())}};', '    QString decoded=fieldops_decoder(QByteArrayView(piece.data(),piece.size()));\n    if(decoded.isEmpty())return;\n    QJsonObject o{{"event", "delta"}, {"text", decoded}};')
    # The header needs the SDK definitions included by each model header.
    pos=s.index('\n#define ') if name=='Qwen3_ASR' else s.index('\nint64_t first_token;')
    s=s[:pos]+'\n#include "resident_protocol.h"\n'+s[pos:]
    # Initialize error-path resources in the original ASR/text entry points.
    s=s.replace('Tokenizer *tokenizer;', 'Tokenizer *tokenizer = nullptr;')
    s=s.replace('Tokenizer* tokenizer;', 'Tokenizer* tokenizer = nullptr;')
    if name=='Qwen3_ASR' and not online:
        s=s.replace('memset(&embedding_info, 0x00, sizeof(embedding_info));',
                    'memset(&embedding_info, 0x00, sizeof(embedding_info));\n    embedding_info.fd=-1;')
    # On callback errors, propagate failure instead of reporting a successful run.
    s=s.replace('printf("\\n\\nError occurred during inference\\n");',
                'printf("\\n\\nError occurred during inference\\n");')
    if name=='Qwen3_5':
        marker='    // LLM Input\n'
        setup='''
            QByteArray prompt_data=input["question"].toString().toUtf8();
            fieldops_decoder.resetState();prompt=prompt_data.constData();first_decode=true;first_token=0;
            memset(&perf,0,sizeof(perf));
'''
        body=s[s.rindex(marker):s.index('\nout:')]
        body=body.replace('goto out;', 'return infer_ret;')
        session='rknn_app_ctx.rknn_sess'
        body=setup+body+'\n            return infer_ret;\n'
        load='(fieldops_loaded-fieldops_started)/1000.0'
        before=s[:s.rindex(marker)]
        reset='fieldops_started=fieldops_loaded;'
    elif name=='Qwen2_5_VL':
        marker='    // Read Image\n'
        setup='''
            QByteArray question_data=input["question"].toString().toUtf8();
            QByteArray path_data=input["path"].toString().toUtf8();
            fieldops_decoder.resetState();prompt=question_data.constData();img_path=path_data.constData();
            first_decode=true;first_token=0;memset(&perf,0,sizeof(perf));
            memset(&src_image,0,sizeof(src_image));
            ResidentCleanup cleanup{[&]{free(src_image.virt_addr);memset(&src_image,0,sizeof(src_image));}};
'''
        body=s[s.index(marker):s.index('\nout:')].replace('goto out;','return ret;')
        body=setup+body+'\n            return ret;\n'
        before=s[:s.index(marker)];session='rknn_app_ctx.llm.rknn_sess'
        load='(fieldops_loaded-fieldops_started)/1000.0';reset='fieldops_started=fieldops_loaded;'
    else:
        marker='    ret = read_audio(audio_path, &full_audio);' if online else '    // Read Audio\n'
        buffer='full_audio' if online else 'src_audio'
        setup='''
            QByteArray path_data=input["path"].toString().toUtf8();audio_path=path_data.constData();
            ret=0;first_decode=true;first_token=0;
'''
        setup+=f'            memset(&{buffer},0,sizeof({buffer}));\n'
        cleanup=f'free({buffer}.data);memset(&{buffer},0,sizeof({buffer}));'
        if online:
            setup+='''
            online_ctx.round_token_ids.clear();online_ctx.text_started=false;
            audio_embed_history.clear();committed_token_ids.clear();llm_input_embeds.clear();
            committed_output_started=false;frames_per_chunk=0;
            remaining_rollback_rounds=ONLINE_ROLLBACK_ROUNDS;round_idx=0;
'''
        else:
            setup+='            language_flag=true;memset(&perf,0,sizeof(perf));audio_embeds=nullptr;\n'
            cleanup+='free(audio_embeds);audio_embeds=nullptr;'
        setup+='            ResidentCleanup cleanup{[&]{'+cleanup+'}};\n'
        body=s[s.index(marker):s.index('\nout:')].replace('goto out;', 'return ret;')
        if online:
            needle='    for (int frame_offset = 0; frame_offset < full_audio.num_frames; frame_offset += frames_per_chunk)\n    {'
            assert needle in body
            body=body.replace(needle,needle+'\n        if (fieldops_cancelled.load()) return 0;')
        body=setup+body+'\n            return ret;\n'
        before=s[:s.index(marker)];session='rknn_app_ctx.llm.rknn_sess'
        # Existing ASR output retains parser-compatible performance markers.
        load='(getCurrentTimeUs()-fieldops_model_begin)/1000.0';reset=''
    loop='''
    {
        double model_load_ms=LOAD;
        RESET
        resident_loop(model_load_ms,SESSION,CONTEXT,[&](const QJsonObject &input)->int {
BODY
        });
    }
'''.replace('LOAD',load).replace('RESET',reset).replace('SESSION',session).replace('CONTEXT',session.replace('rknn_sess','rknn_ctx')).replace('BODY',body)
    s=before+loop+s[s.index('\nout:'):]
    outfile='fieldops_online_resident.cc' if online else 'fieldops_resident.cc'
    (folder/outfile).write_text(s)
    protocol_header = Path(__file__).resolve().parents[2] / 'services/inference/resident_protocol.h'
    shutil.copyfile(protocol_header, folder/'resident_protocol.h')
    cmake=folder/'CMakeLists.txt';c=cmake.read_text()
    target='${PROJECT_NAME}'+('_online' if online else '')
    resident=target+'_resident'
    label='# FIELDOPS_RESIDENT_'+str(online)
    if label not in c:
        c+='\n'+label+f'''
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core)
get_target_property(resident_sources {target} SOURCES)
list(REMOVE_ITEM resident_sources {'online_main.cc' if online else 'main.cc'})
add_executable({resident} {outfile} ${{resident_sources}})
get_target_property(resident_links {target} LINK_LIBRARIES)
get_target_property(resident_includes {target} INCLUDE_DIRECTORIES)
get_target_property(resident_definitions {target} COMPILE_DEFINITIONS)
target_link_libraries({resident} PRIVATE ${{resident_links}} Qt6::Core)
target_include_directories({resident} PRIVATE ${{resident_includes}})
if(resident_definitions)
    target_compile_definitions({resident} PRIVATE ${{resident_definitions}})
endif()
install(TARGETS {resident} DESTINATION .)
'''
        cmake.write_text(c)
    print('Generated',folder/outfile)
