#!/usr/bin/env python3
"""Convert the original ONNX checkpoints for the RK3588 stream workers."""

import argparse
import os
import tempfile
from pathlib import Path

from rknn.api import RKNN

POSE_KEYPOINT_BRANCHES = [
    [f"/model.22/cv4.{level}/cv4.{level}.0/act/Mul_output_0", "/model.22/Concat_6_output_0"]
    for level in range(3)
]


def nchw_input(source, output):
    import onnx

    model = onnx.load(source)
    image = model.graph.input[0]
    previous_name = image.name
    image.name = "fieldops_rgb"
    dimensions = image.type.tensor_type.shape.dim
    n, h, w, c = (dimension.dim_value for dimension in dimensions)
    if n != 1 or c != 3 or min(h, w) <= 0:
        raise ValueError("Expected a fixed NHWC image input")
    for dimension, size in zip(dimensions, (n, c, h, w)):
        dimension.dim_value = size
    transpose = onnx.helper.make_node("Transpose", [image.name], [previous_name], perm=[0, 2, 3, 1])
    model.graph.node.insert(0, transpose)
    onnx.checker.check_model(model)
    onnx.save(model, output)


def check(result):
    if result not in (0, None):
        raise RuntimeError(f"RKNN conversion failed with status {result}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("pose", "face", "emotion", "palm", "hand"))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--calibration", type=Path, help="COCO calibration list for hybrid INT8/FP16 pose")
    args = parser.parse_args()
    if args.calibration and args.kind != "pose":
        parser.error("--calibration applies to pose")
    mean, std = {
        "face": ([104, 117, 123], [1] * 3),
        "emotion": ([127.5] * 3, [127.5] * 3),
    }.get(args.kind, ([0] * 3, [255] * 3))

    with tempfile.TemporaryDirectory() as temporary:
        source = args.source.resolve()
        output = args.output.resolve()
        calibration = args.calibration.resolve() if args.calibration else None
        if args.kind in ("palm", "hand"):
            source = Path(temporary) / "nchw.onnx"
            nchw_input(args.source, source)
        model = RKNN()
        try:
            check(model.config(mean_values=[mean], std_values=[std], target_platform="rk3588"))
            check(model.load_onnx(model=str(source)))
            if calibration:
                # Follow Rockchip's pose example and retain FP16 for keypoint coordinates.
                cwd = Path.cwd()
                try:
                    os.chdir(temporary)
                    check(model.hybrid_quantization_step1(
                        dataset=str(calibration), proposal=False, custom_hybrid=POSE_KEYPOINT_BRANCHES
                    ))
                finally:
                    os.chdir(cwd)
            else:
                check(model.build(do_quantization=False))
                check(model.export_rknn(str(output)))
        finally:
            model.release()
        if calibration:
            prefix = Path(temporary) / source.stem
            model = RKNN()
            try:
                check(model.config(mean_values=[mean], std_values=[std], target_platform="rk3588"))
                check(model.hybrid_quantization_step2(
                    model_input=f"{prefix}.model", data_input=f"{prefix}.data",
                    model_quantization_cfg=f"{prefix}.quantization.cfg"
                ))
                check(model.export_rknn(str(output)))
            finally:
                model.release()


if __name__ == "__main__":
    main()
