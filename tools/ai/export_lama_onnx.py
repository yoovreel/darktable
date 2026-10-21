#!/usr/bin/env python3
# Export LaMa (big-lama, Apache-2.0, https://github.com/advimman/lama) from
# its TorchScript release to ONNX for darktable's AI remove tool.
#
#   python3 export_lama_onnx.py big-lama.pt model.onnx check.png
#
# ONNX has no complex tensors and the torchscript exporter has no rule for
# torch.fft, so LaMa's Fourier units are mapped onto ONNX DFT (opset 17)
# below, carrying complex values as a trailing [real, imag] axis. The script
# checks the ONNX result against torch on a test pattern.
import sys, torch, warnings, numpy as np
warnings.filterwarnings("ignore")
from torch.onnx import register_custom_op_symbolic
import torch.onnx.symbolic_helper as sh

OPSET = 17

def _const(g, v):
    return g.op("Constant", value_t=torch.tensor(v))

def _hw_scale(g, x, axes, inverse):
    # sqrt(prod(shape[axes])) or its reciprocal, computed from the runtime shape
    shape = g.op("Shape", x)
    dims = g.op("Gather", shape, _const(g, axes), axis_i=0)
    n = g.op("ReduceProd", dims, keepdims_i=0)
    n = g.op("Cast", n, to_i=1)
    r = g.op("Sqrt", n)
    return r if inverse else g.op("Reciprocal", r)

# rfftn over the last two dims (rank-4 input), norm="ortho"; returns [..., 2]
def rfftn(g, x, s, dim, norm):
    x1 = g.op("Unsqueeze", x, _const(g, [-1]))
    y = g.op("DFT", x1, axis_i=3, onesided_i=1)   # along W (onesided)
    y = g.op("DFT", y, axis_i=2, onesided_i=0)    # along H
    return g.op("Mul", y, _hw_scale(g, x, [2, 3], False))

def view_as_real(g, x):
    return x

def view_as_complex(g, x):
    return x

# irfftn over H and W, norm="ortho"; input [B, C, H, Wr, 2], W = 2 * (Wr - 1)
def irfftn(g, y, s, dim, norm):
    z = g.op("DFT", y, axis_i=2, inverse_i=1, onesided_i=0)       # undo H, ONNX divides by H
    # bins Wr-2 .. 1 of each row, conjugated, complete the W spectrum
    mid = g.op("Slice", z, _const(g, [-2]), _const(g, [0]), _const(g, [3]), _const(g, [-1]))
    conj = g.op("Mul", mid, g.op("Constant", value_t=torch.tensor([1.0, -1.0], dtype=torch.float32)))
    full = g.op("Concat", z, conj, axis_i=3)
    r = g.op("DFT", full, axis_i=3, inverse_i=1, onesided_i=0)     # undo W, ONNX divides by W
    real = g.op("Slice", r, _const(g, [0]), _const(g, [1]), _const(g, [4]))
    real = g.op("Squeeze", real, _const(g, [-1]))
    # ONNX already divided by H*W; ortho wants 1/sqrt(H*W)
    return g.op("Mul", real, _hw_scale(g, real, [2, 3], True))

def real(g, x):
    return g.op("Squeeze", g.op("Slice", x, _const(g, [0]), _const(g, [1]), _const(g, [-1])), _const(g, [-1]))

def imag(g, x):
    return g.op("Squeeze", g.op("Slice", x, _const(g, [1]), _const(g, [2]), _const(g, [-1])), _const(g, [-1]))

def complex_(g, re_, im_):
    return g.op("Concat", g.op("Unsqueeze", re_, _const(g, [-1])), g.op("Unsqueeze", im_, _const(g, [-1])), axis_i=-1)

register_custom_op_symbolic("aten::real", real, OPSET)
register_custom_op_symbolic("aten::imag", imag, OPSET)
register_custom_op_symbolic("aten::complex", complex_, OPSET)
register_custom_op_symbolic("aten::fft_rfftn", rfftn, OPSET)
register_custom_op_symbolic("aten::fft_irfftn", irfftn, OPSET)
register_custom_op_symbolic("aten::view_as_real", view_as_real, OPSET)
register_custom_op_symbolic("aten::view_as_complex", view_as_complex, OPSET)

m = torch.jit.load(sys.argv[1], map_location="cpu").eval()
H = W = 512
xs = np.linspace(0, 1, W, dtype=np.float32)
img = np.stack([np.tile(xs, (H, 1)), np.tile(xs[::-1], (H, 1)), np.full((H, W), 0.4, np.float32)])
img[:, :, ::32] = 0.9
hole = np.zeros((1, H, W), np.float32); hole[:, 180:330, 180:330] = 1
ti = torch.from_numpy(img)[None]; tm = torch.from_numpy(hole)[None]
with torch.no_grad():
    ref = m(ti, tm)
torch.onnx.export(m, (ti, tm), sys.argv[2], input_names=["image", "mask"], output_names=["output"], opset_version=OPSET, dynamo=False)
import onnxruntime as ort
s = ort.InferenceSession(sys.argv[2], providers=["CPUExecutionProvider"])
o = s.run(None, {"image": ti.numpy(), "mask": tm.numpy()})[0]
diff = float(np.abs(o - ref.numpy()).max())
print("max abs diff onnx vs torch:", diff)
assert diff < 1e-3, "ONNX export does not match torch"
print("hole err torch %.3f onnx %.3f" % (np.abs(ref.numpy()[0]-img)[:,180:330,180:330].mean(), np.abs(o[0]-img)[:,180:330,180:330].mean()))
from PIL import Image
Image.fromarray((np.concatenate([img, ref.numpy()[0], o[0]], 2).clip(0,1).transpose(1, 2, 0) * 255).astype(np.uint8)).save(sys.argv[3])
