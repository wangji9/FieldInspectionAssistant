#!/usr/bin/env python3
"""Build single-core RKNN3 .rknn/.weight bundles for the RK1828 coprocessor."""

import argparse
import os
import tempfile
from pathlib import Path

import onnx
from onnx import TensorProto, helper
from rknn.api import RKNN


HYBRID = {
    "yolov5": [[
        ["/model.24/Sigmoid_2_output_0", "/model.24/Sigmoid_1_output_0", "/model.24/Sigmoid_output_0"],
        ["output0"], "w16a16",
    ]],
    "yolov6": [[
        [f"/detect/Reshape_{i}_output_0_rs" for i in (5, 4, 3)]
        + [f"/detect/Reshape_{i}_output_0" for i in (11, 10, 9)],
        ["outputs"], "w16a16",
    ]],
}
POSE_HYBRID = [[
    [f"/model.22/cv4.{level}/cv4.{level}.0/act/Mul_output_0" for level in range(3)],
    ["/model.22/Concat_6_output_0"], "w16a16",
]]


def check(status):
    if status not in (None, 0):
        raise RuntimeError(f"RKNN3 conversion failed ({status})")


def fix_palm_resize(model):
    # The OpenCV Zoo palm graph uses fixed 2× spatial upsampling. Express
    # that factor directly so RKNN3 can move these operators across layouts.
    for node in model.graph.node:
        if node.op_type == "Resize" and node.name in ("Resize__235", "Resize__264") and len(node.input) == 4:
            name = node.name + "_fieldops_scales"
            model.graph.initializer.append(helper.make_tensor(name, TensorProto.FLOAT, [4], [1, 1, 2, 2]))
            del node.input[3:]
            node.input[2] = name
    onnx.checker.check_model(model)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("yolov5", "yolov6", "pose", "segment", "face", "emotion", "palm", "hand",
                                           "industrial", "face_gaze", "face_pose", "face_mesh", "face_identity"))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--calibration", type=Path)
    args = parser.parse_args()
    model = onnx.load(args.source, load_external_data=False)
    graph = model.graph
    initializers = {item.name for item in graph.initializer}
    inputs = [item.name for item in graph.input if item.name not in initializers]
    if len(inputs) != 1:
        raise ValueError("Expected one image input")
    mean, std = {
        "face": ([104, 117, 123], [1] * 3),
        "emotion": ([127.5] * 3, [127.5] * 3),
        "face_identity": ([0] * 3, [1] * 3),
    }.get(args.kind, ([0] * 3, [255] * 3))
    if args.kind in ("face_gaze", "face_pose"):
        mean = [255 * value for value in (.485, .456, .406)]
        std = [255 * value for value in (.229, .224, .225)]
    config = dict(target_platform="rk1820", core_num=1, rknn_build_parallelism=2,
                  mean_values=[mean], std_values=[std], quantized_dtype="w8a8",
                  quantized_algorithm="normal", quantized_method="channel",
                  input_attrs={inputs[0]: {"dtype": "uint8", "layout": "NHWC"}})
    if args.kind in HYBRID:
        config["subgraph_dtype_target"] = HYBRID[args.kind]
    else:
        # Keep the ONNX output order and layout for the host's model-specific C++ decoders.
        config["output_attrs"] = {
            item.name: {
                "dtype": "float16",
                "layout": "NCHW" if len(item.type.tensor_type.shape.dim) == 4 else "UNDEFINED",
            } for item in graph.output
        }
        if args.kind == "pose" and args.calibration:
            config["subgraph_dtype_target"] = POSE_HYBRID
    converter = RKNN(verbose=False)
    source = args.source.resolve()
    output = args.output.resolve()
    calibration = args.calibration.resolve() if args.calibration else None
    with tempfile.TemporaryDirectory() as scratch:
        if args.kind == "palm":
            fix_palm_resize(model)
            source = Path(scratch) / "palm.onnx"
            onnx.save(model, source)
        if args.kind == "face_mesh":
            from onnxsim import simplify

            model, valid = simplify(model, overwrite_input_shapes={inputs[0]: [1, 3, 256, 256]})
            if not valid:
                raise ValueError("Could not fix the face mesh to batch size one")
            source = Path(scratch) / "mesh.onnx"
            onnx.save(model, source)
        if args.kind in ("face_gaze", "face_pose"):
            from onnx.version_converter import convert_version

            source = Path(scratch) / "opset19.onnx"
            onnx.save(convert_version(model, 19), source)
        cwd = Path.cwd()
        try:
            os.chdir(scratch)
            check(converter.config(**config))
            check(converter.load_onnx(model=str(source)))
            check(converter.build(do_quantization=calibration is not None,
                                  dataset=str(calibration) if calibration else None))
            check(converter.export_rknn(str(output)))
        finally:
            converter.release()
            os.chdir(cwd)


if __name__ == "__main__":
    main()
