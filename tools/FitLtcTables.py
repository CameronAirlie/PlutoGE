"""Fits the linearly transformed cosine (LTC) tables rectangle lights are shaded with (LtcAreaLights.slang) and writes
engine/render/src/LtcTables.inl.

Heitz, Dupuy, Hill and Neubelt, "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines" (2016): for each
perceptual roughness r and view angle theta, an isotropic LTC is fitted to the GGX BRDF times the cosine (height-
uncorrelated Smith masking, no Fresnel). The table is indexed as the shader samples it: x = r, y = sqrt(1 - cos theta),
LtcTableSize texels each, texel centres at the ends of the ranges.

Each texel stores:
  - the inverse transform, normalised so its middle element is 1: (a, b, c, d) with
        Minv = [[a, 0, b], [0, 1, 0], [c, 0, d]]   (applied as Minv * v, in the frame x = view's tangent, z = normal)
  - (norm, fresnel): the BRDF's directional albedo, and the same integral weighted by (1 - v.h)^5, so a surface with
    specular colour F0 scales the polygon integral by F0 * norm + (1 - F0) * fresnel (Schlick's approximation).

Run from the repository root; needs numpy. Takes a few minutes:
    python tools/FitLtcTables.py [--size 64] [--output engine/render/src/LtcTables.inl]
"""
import argparse
import math
import sys
import time

import numpy as np

MIN_ALPHA = 0.00001
SAMPLES = 32  # per axis, for the fit error and the albedo integrals


def ggx_lambda(alpha, cos_theta):
    cos_theta = np.clip(cos_theta, 1e-7, 1.0)
    tan_theta = np.sqrt(np.maximum(1.0 - cos_theta * cos_theta, 0.0)) / cos_theta
    with np.errstate(divide='ignore'):
        a = np.where(tan_theta > 0.0, 1.0 / (alpha * np.maximum(tan_theta, 1e-12)), np.inf)
    return np.where(np.isinf(a), 0.0, 0.5 * (-1.0 + np.sqrt(1.0 + 1.0 / (a * a))))


def ggx_eval(v, l, alpha):
    """GGX BRDF times n.l (no Fresnel) and the pdf of ggx_sample, for directions `l` (N x 3)."""
    lz = l[:, 2]
    h = v[None, :] + l
    h /= np.maximum(np.linalg.norm(h, axis=1, keepdims=True), 1e-12)
    hz = np.maximum(h[:, 2], 1e-7)
    slope2 = (h[:, 0] ** 2 + h[:, 1] ** 2) / (hz * hz)
    d = 1.0 / (1.0 + slope2 / (alpha * alpha))
    d = d * d / (math.pi * alpha * alpha * hz ** 4)
    vdoth = np.abs(h @ v)
    pdf = np.abs(d * hz / (4.0 * np.maximum(vdoth, 1e-7)))
    lambda_v = ggx_lambda(alpha, v[2])
    g2 = np.where(lz > 0.0, 1.0 / (1.0 + lambda_v + ggx_lambda(alpha, lz)), 0.0)
    value = d * g2 / (4.0 * v[2])
    return np.where(lz > 0.0, value, 0.0), pdf


def ggx_sample(v, alpha, u1, u2):
    phi = 2.0 * math.pi * u1
    r = alpha * np.sqrt(u2 / (1.0 - u2))
    n = np.stack([r * np.cos(phi), r * np.sin(phi), np.ones_like(r)], axis=1)
    n /= np.linalg.norm(n, axis=1, keepdims=True)
    return -v[None, :] + 2.0 * n * (n @ v)[:, None]


U1, U2 = np.meshgrid((np.arange(SAMPLES) + 0.5) / SAMPLES, (np.arange(SAMPLES) + 0.5) / SAMPLES)
U1, U2 = U1.ravel(), U2.ravel()


class Ltc:
    def __init__(self):
        self.m11, self.m22, self.m13 = 1.0, 1.0, 0.0
        self.x = np.array([1.0, 0.0, 0.0])
        self.y = np.array([0.0, 1.0, 0.0])
        self.z = np.array([0.0, 0.0, 1.0])
        self.amplitude = 1.0
        self.update()

    def update(self):
        frame = np.stack([self.x, self.y, self.z], axis=1)
        self.m = frame @ np.array([[self.m11, 0.0, self.m13], [0.0, self.m22, 0.0], [0.0, 0.0, 1.0]])
        self.inverse = np.linalg.inv(self.m)
        self.determinant = abs(np.linalg.det(self.m))

    def eval(self, l):
        original = l @ self.inverse.T
        length = np.linalg.norm(original, axis=1)
        original /= length[:, None]
        d = np.maximum(original[:, 2], 0.0) / math.pi
        jacobian = 1.0 / (self.determinant * length ** 3)
        return self.amplitude * d * jacobian

    def sample(self, u1, u2):
        theta = np.arccos(np.sqrt(u1))
        phi = 2.0 * math.pi * u2
        original = np.stack([np.sin(theta) * np.cos(phi), np.sin(theta) * np.sin(phi), np.cos(theta)], axis=1)
        l = original @ self.m.T
        return l / np.linalg.norm(l, axis=1, keepdims=True)


def fit_error(ltc, v, alpha):
    """Heitz's error: cubed differences, multiple-importance sampled from both distributions."""
    error = 0.0
    for l in (ltc.sample(U1, U2), ggx_sample(v, alpha, U1, U2)):
        brdf, brdf_pdf = ggx_eval(v, l, alpha)
        value = ltc.eval(l)
        pdf = value / ltc.amplitude
        denominator = pdf + brdf_pdf
        mask = denominator > 0.0
        error += np.sum(np.abs(brdf[mask] - value[mask]) ** 3 / denominator[mask])
    return error / (SAMPLES * SAMPLES)


def albedo_terms(v, alpha):
    l = ggx_sample(v, alpha, U1, U2)
    value, pdf = ggx_eval(v, l, alpha)
    mask = pdf > 0.0
    weight = np.where(mask, value / np.where(mask, pdf, 1.0), 0.0)
    h = v[None, :] + l
    h /= np.maximum(np.linalg.norm(h, axis=1, keepdims=True), 1e-12)
    fresnel_weight = (1.0 - np.clip(h @ v, 0.0, 1.0)) ** 5
    direction = (weight[:, None] * l).sum(axis=0)
    direction[1] = 0.0
    direction /= max(np.linalg.norm(direction), 1e-12)
    count = SAMPLES * SAMPLES
    return weight.sum() / count, (weight * fresnel_weight).sum() / count, direction


def nelder_mead(function, start, delta=0.05, tolerance=1e-5, max_iterations=100):
    dimension = len(start)
    points = [np.array(start, dtype=float)]
    for i in range(dimension):
        point = np.array(start, dtype=float)
        point[i] += delta
        points.append(point)
    values = [function(p) for p in points]
    for _ in range(max_iterations):
        order = np.argsort(values)
        points = [points[i] for i in order]
        values = [values[i] for i in order]
        if abs(values[-1] - values[0]) <= tolerance * max(abs(values[0]), 1e-12):
            break
        centroid = np.mean(points[:-1], axis=0)
        reflected = centroid + (centroid - points[-1])
        reflected_value = function(reflected)
        if reflected_value < values[0]:
            expanded = centroid + 2.0 * (centroid - points[-1])
            expanded_value = function(expanded)
            points[-1], values[-1] = (expanded, expanded_value) if expanded_value < reflected_value else (reflected, reflected_value)
        elif reflected_value < values[-2]:
            points[-1], values[-1] = reflected, reflected_value
        else:
            contracted = centroid + 0.5 * (points[-1] - centroid)
            contracted_value = function(contracted)
            if contracted_value < values[-1]:
                points[-1], values[-1] = contracted, contracted_value
            else:
                for i in range(1, len(points)):
                    points[i] = points[0] + 0.5 * (points[i] - points[0])
                    values[i] = function(points[i])
    best = int(np.argmin(values))
    return points[best]


def fit(ltc, v, alpha, isotropic):
    def objective(parameters):
        if isotropic:
            ltc.m11 = ltc.m22 = max(parameters[0], MIN_ALPHA)
            ltc.m13 = 0.0
        else:
            ltc.m11 = max(parameters[0], MIN_ALPHA)
            ltc.m22 = max(parameters[1], MIN_ALPHA)
            ltc.m13 = parameters[2]
        ltc.update()
        return fit_error(ltc, v, alpha)

    start = [ltc.m11] if isotropic else [ltc.m11, ltc.m22, ltc.m13]
    objective(nelder_mead(objective, start))


def fit_tables(size):
    matrices = np.zeros((size, size, 3, 3))
    terms = np.zeros((size, size, 2))
    ltc = Ltc()
    begin = time.time()
    # From rough to smooth, each view angle starting from its neighbour's fit.
    for a in range(size - 1, -1, -1):
        for t in range(size):
            x = t / (size - 1)
            cos_theta = 1.0 - x * x
            theta = min(1.57, math.acos(cos_theta))
            v = np.array([math.sin(theta), 0.0, math.cos(theta)])
            roughness = a / (size - 1)
            alpha = max(roughness * roughness, MIN_ALPHA)
            norm, fresnel, direction = albedo_terms(v, alpha)
            ltc.amplitude = norm
            if t == 0:
                ltc.x, ltc.y, ltc.z = np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), np.array([0, 0, 1.0])
                if a == size - 1:
                    ltc.m11, ltc.m22, ltc.m13 = 1.0, 1.0, 0.0
                else:
                    previous = matrices[a + 1, t]
                    ltc.m11, ltc.m22, ltc.m13 = previous[0, 0], previous[1, 1], 0.0
                isotropic = True
            else:
                ltc.z = direction
                ltc.x = np.array([direction[2], 0.0, -direction[0]])
                ltc.y = np.array([0.0, 1.0, 0.0])
                isotropic = False
            ltc.update()
            fit(ltc, v, alpha, isotropic)
            matrices[a, t] = ltc.m
            terms[a, t] = (norm, fresnel)
        print(f"roughness {a / (size - 1):.3f} fitted ({time.time() - begin:.0f} s)", file=sys.stderr)
    return matrices, terms


def literal(value):
    """A C++ float literal: always with a decimal point or an exponent."""
    text = f'{value:.7g}'
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'f'


def write(path, size, matrices, terms):
    with open(path, 'w', newline='\n') as out:
        out.write('// Generated by tools/FitLtcTables.py; do not edit. Linearly transformed cosine fits of the GGX BRDF for\n')
        out.write('// rectangle lights (LtcAreaLights.slang): texel (x = perceptual roughness, y = sqrt(1 - n.v)) holds the\n')
        out.write('// inverse transform (a, b, c, d), Minv = [[a, 0, b], [0, 1, 0], [c, 0, d]], and (norm, fresnel, 0, 0).\n')
        out.write(f'constexpr int LtcTableSize = {size};\n')
        out.write(f'constexpr float LtcMatrixTable[{size * size * 4}] = {{\n')
        for t in range(size):
            for a in range(size):
                inverse = np.linalg.inv(matrices[a, t])
                inverse /= inverse[1, 1]
                values = (inverse[0, 0], inverse[0, 2], inverse[2, 0], inverse[2, 2])
                out.write('    ' + ', '.join(literal(value) for value in values) + ',\n')
        out.write('};\n')
        out.write(f'constexpr float LtcAmplitudeTable[{size * size * 4}] = {{\n')
        for t in range(size):
            for a in range(size):
                norm, fresnel = terms[a, t]
                out.write(f'    {literal(norm)}, {literal(fresnel)}, 0.0f, 0.0f,\n')
        out.write('};\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--size', type=int, default=64)
    parser.add_argument('--output', default='engine/render/src/LtcTables.inl')
    arguments = parser.parse_args()
    matrices, terms = fit_tables(arguments.size)
    write(arguments.output, arguments.size, matrices, terms)
    print(f"wrote {arguments.output}", file=sys.stderr)


if __name__ == '__main__':
    main()
