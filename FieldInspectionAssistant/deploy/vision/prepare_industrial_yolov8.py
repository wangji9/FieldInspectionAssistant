#!/usr/bin/env python3
"""Extract the six YOLOv8 detection heads for RK1828 and host-side DFL decoding."""

import argparse
import ast
from pathlib import Path

import onnx
from onnx import shape_inference
from onnx.utils import Extractor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    model = shape_inference.infer_shapes(onnx.load(args.source))
    metadata = {entry.key: entry.value for entry in model.metadata_props}
    classes = len(ast.literal_eval(metadata["names"]))
    graph = model.graph
    shapes = {entry.name: tuple(dim.dim_value for dim in entry.type.tensor_type.shape.dim)
              for entry in (*graph.value_info, *graph.input, *graph.output)}
    producers = {name: node for node in graph.node for name in node.output}
    heads = {}
    for node in graph.node:
        if node.op_type != "Concat" or len(node.input) != 2:
            continue
        box, score = node.input
        box_shape, score_shape = shapes.get(box, ()), shapes.get(score, ())
        if len(box_shape) != 4 or box_shape[:2] != (1, 64):
            continue
        height, width = box_shape[2:]
        if width not in (80, 40, 20) or width != height or score_shape != (1, classes, height, width):
            continue
        if producers[box].op_type != "Conv" or producers[score].op_type != "Conv" or width in heads:
            raise ValueError("Ambiguous YOLOv8 detection head")
        heads[width] = (box, score)
    if set(heads) != {80, 40, 20}:
        raise ValueError("Expected 80, 40 and 20 pixel YOLOv8 heads")
    outputs = [name for size in (80, 40, 20) for name in heads[size]]
    result = Extractor(model).extract_model([graph.input[0].name], outputs)
    onnx.checker.check_model(result)
    onnx.save(result, args.output)
    print(f"{classes} classes; output order: {outputs}")


if __name__ == "__main__":
    main()
