"""Add structured token/metrics output and output limits to pinned vendor demos."""
from pathlib import Path

SDK = Path('/userdata/ai-workbench/vendor/rknn3-model-zoo')
HELPER = r'''
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>
static int64_t fieldops_started = 0;
static int64_t fieldops_loaded = 0;
static QStringDecoder fieldops_decoder(QStringDecoder::Utf8);
static void fieldops_delta(const std::string &piece) {
    QString decoded=fieldops_decoder(QByteArrayView(piece.data(),piece.size()));
    if(decoded.isEmpty())return;
    QJsonObject o{{"event", "delta"}, {"text", decoded}};
    auto b = QJsonDocument(o).toJson(QJsonDocument::Compact);
    printf("\n@@JSON@@%s\n", b.constData()); fflush(stdout);
}
static void fieldops_metrics(rknn_perf_metrics_t *p, int64_t first, bool has_first, bool vision) {
    double prefill = has_first ? (first-p->llm_start_time)/1000.0 : 0;
    double decode = has_first ? (p->llm_end_time-first)/1000.0 : 0;
    double enc = vision ? p->vision_latency/1000.0 : 0;
    QJsonObject o{{"model_load_ms", (fieldops_loaded-fieldops_started)/1000.0},
        {"llm_ttft_ms", prefill}, {"ttft_ms", prefill+enc},
        {"prefill_ms", prefill}, {"decode_ms", decode},
        {"prefill_tokens", p->n_prefill_tokens}, {"decode_tokens", p->n_decode_tokens},
        {"prefill_tps", prefill>0 ? p->n_prefill_tokens*1000.0/prefill : 0},
        {"decode_tps", decode>0 ? p->n_decode_tokens*1000.0/decode : 0},
        {"encoder_ms", enc}, {"inference_ms", (p->llm_end_time-p->llm_start_time)/1000.0+enc}};
    auto b = QJsonDocument(o).toJson(QJsonDocument::Compact);
    printf("\n@@METRICS@@%s\n", b.constData()); fflush(stdout);
}
'''


def original(path):
    backup = Path(str(path)+'.rk1828.orig')
    if not backup.exists():
        backup.write_bytes(path.read_bytes())
    return backup.read_text()


for name, vision in [('Qwen3_5', False), ('Qwen2_5_VL', True)]:
    folder = SDK/'examples'/name/'cpp'
    path = folder/'main.cc'
    s = original(path)
    s = s.replace('int64_t first_token;', HELPER+'\nint64_t first_token;')
    s = s.replace('printf("%s", piece.c_str());', 'fieldops_delta(piece);')
    s = s.replace('int main(int argc, char **argv)\n{',
                  'int main(int argc, char **argv)\n{\n    fieldops_started = getCurrentTimeUs();')
    mark = '    // Image Embed\n' if vision else '    // LLM Input\n'
    assert mark in s, name
    before,after=s.rsplit(mark,1)
    s=before+'    fieldops_loaded = getCurrentTimeUs();\n'+mark+after
    s = s.replace('    printf_perf(&perf);',
                  '    fieldops_metrics(&perf, first_token, !first_decode, '+str(vision).lower()+');\n    printf_perf(&perf);')
    if vision:
        s = s.replace('    int ret;\n', '    int ret;\n    int fieldops_exit = -1;\n', 1)
        s = s.replace('out:\n', 'out:\n    fieldops_exit = ret;\n', 1)
        s = s.replace('    return ret;\n}', '    return fieldops_exit;\n}')
    path.write_text(s)
    cmake = folder/'CMakeLists.txt'
    s = original(cmake)
    s += '\nfind_package(Qt6 REQUIRED COMPONENTS Core)\ntarget_link_libraries(${PROJECT_NAME} Qt6::Core)\n'
    # Qt 6 requires C++17, matching the existing Qt application toolchain.
    s = s.replace('set(CMAKE_CXX_STANDARD 11)', 'set(CMAKE_CXX_STANDARD 17)')
    cmake.write_text(s)
    core = folder/('llm/rknn_qwen2_5_vl_llm.cc' if vision else 'rknn_qwen3_5_llm.cc')
    s = original(core)
    needle = 'llm_infer_param.max_new_tokens = MAX_NEW_TOKENS;'
    assert needle in s
    s = s.replace(needle, 'llm_infer_param.max_new_tokens = getenv("BOARD_AI_MAX_TOKENS") ? atoi(getenv("BOARD_AI_MAX_TOKENS")) : MAX_NEW_TOKENS;')
    core.write_text(s)
    if vision:
        core=folder/'qwen2_5_vl.cc'
        s=original(core)
        assert 'int start_us = getCurrentTimeUs();' in s
        s=s.replace('int start_us = getCurrentTimeUs();','int64_t start_us = getCurrentTimeUs();')
        core.write_text(s)
    print('Patched', name)
