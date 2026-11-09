# NFSU2 x87 window adaptation

The unmodified `0014-recomp_types-RECOMP_X87_WINDOW-sliding-window-FPU-st.patch`
was preserved from the project's existing local homm5 dependency snapshot.
Upstream: codepdbh/nfsu2-recompiled, revision
`62a1d3a17fc7619b9d32df182b4b47d1a4f2e959`.
Patch author: sistemas3cecasem; original commit
`486a9a1898f4720b62436b6edc475be513f34419`.
Patch SHA-256: `740560cd6d8eadd132ddc180c963388b92a4316040e2023b4c0ee4ee4c2db518`.

The imported attribution identifies the upstream authored runtime/tooling
as MIT; the underlying pcrecomp MIT license is retained in
`reference/pcrecomp/LICENSE`.

This port adapts the algorithm in `app/native/runtime/recomp_types.h`.
Array aliases retain eight-slot snapshot semantics, integer shadows follow
the floating-point window, and existing host snapshot/callback handling is
preserved. Native builds define `RECOMP_X87_WINDOW` for generated and host code.
The patch is retained for attribution and reference, not applied during builds.
