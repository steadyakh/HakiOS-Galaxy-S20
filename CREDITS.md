# Credits

- HakiOS: [oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel), the upstream port and build tools; see LICENSE and THIRD_PARTY_LICENSES.md.
- Panfrost buffer retention: [N1709/z3sios](https://github.com/N1709/z3sios/blob/1323aa51d4c16816fa8fd95309f864f7e3888793/kernel/tools/mesa/panfrost-retain.patch), copyright (c) 2026 the y2sios contributors, MIT. The full notice is retained inside patches/10-mesa-retain/10-mesa-retain.patch and included in the build artifact.
- E1 Metal library and plug-in (opt-in `metal=2`): [N1709/z3sios](https://github.com/N1709/z3sios/tree/1323aa51d4c16816fa8fd95309f864f7e3888793), copyright (c) 2026 the y2sios contributors, MIT. The AIR frontend and `redwoodmetal.m` sources are vendored under `metal-e1/`, adapted to the HakiosGPU DRM shim; the full notice is `metal-e1/LICENSE.z3sios` and is included in the `x1s-metal-e1` artifact. Sources only, not built or measured on this phone yet.
