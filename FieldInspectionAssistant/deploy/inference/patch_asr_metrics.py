"""Add stage timers to official ASR examples, preserving the upstream source."""
from pathlib import Path
ROOT=Path('/userdata/ai-workbench/vendor/rknn3-model-zoo/examples/Qwen3_ASR/cpp')
for name in ('main.cc','online_main.cc'):
    path=ROOT/name; original=path.with_suffix('.cc.fieldops.orig')
    if not original.exists(): original.write_bytes(path.read_bytes())
    text=original.read_text()
    if name=='main.cc':
        text=text.replace('    // Load Tokenizer', '    int64_t fieldops_model_begin = getCurrentTimeUs();\n\n    // Load Tokenizer',1)
        text=text.replace('    // Read Audio', '    printf(" Model load = %.2f ms\\n", (getCurrentTimeUs() - fieldops_model_begin) / 1000.0);\n\n    // Read Audio',1)
    else:
        text=text.replace('    tokenizer = new Tokenizer', '    int64_t fieldops_model_begin = getCurrentTimeUs();\n    int64_t fieldops_infer_begin = 0;\n\n    tokenizer = new Tokenizer',1)
        text=text.replace('    ret = read_audio(audio_path, &full_audio);', '    printf(" Model load = %.2f ms\\n", (getCurrentTimeUs() - fieldops_model_begin) / 1000.0);\n\n    ret = read_audio(audio_path, &full_audio);',1)
        text=text.replace('    for (int frame_offset = 0;', '    fieldops_infer_begin = getCurrentTimeUs();\n    for (int frame_offset = 0;',1)
        marker='    {\n        decoded_asr_text final_committed_text'
        replacement='''    printf(" Total Inference = %.2f ms\\n", (getCurrentTimeUs() - fieldops_infer_begin) / 1000.0);
    printf(" Audio Duration = %.4f s\\n", (double)full_audio.num_frames / full_audio.sample_rate);

    {
        decoded_asr_text final_committed_text'''
        assert marker in text
        text=text.replace(marker,replacement,1)
        stream_marker='        committed_output_started = commit_add_text.text_started;'
        assert stream_marker in text
        text=text.replace(stream_marker,stream_marker+'''
        printf("\\nFIELDOPS_ASR_PARTIAL\\n%s%s\\nFIELDOPS_ASR_END\\n", committed_text.text.c_str(), unfix_text.text.c_str());
        fflush(stdout);
''',1)
    assert ' Model load = ' in text
    path.write_text(text)
    print('Instrumented',path)
